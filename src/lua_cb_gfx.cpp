#include "lua_cb_gfx.hpp"

#include <QOpenGLTexture>

#include <memory>

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include "pobwindow.hpp"
#include "lua_utils.hpp"
#include "utils.hpp"

QRegularExpression colourCodes{R"((\^x.{6})|(\^\d))"};

static const char* fontMap[8] = { "FIXED", "VAR", "VAR BOLD", "FONTIN SC", "FONTIN SC ITALIC", "FONTIN", "FONTIN ITALIC", nullptr};

static const char* fonts[7] = {
    "Bitstream Vera Sans Mono",
    "Liberation Sans",
    "Liberation Sans Bold",
    "Fontin SmallCaps",
    "Fontin SmallCaps Italic",
    "Fontin",
    "Fontin Italic",
};

// =============
// Image Handles
// =============

struct imgHandle_s {
    TextureIndex tex_idx = 0;
};

int l_NewImageHandle(lua_State* L)
{
    // Creates an image handle referencing LazyLoadedTexture 0
    auto imgHandle = static_cast<imgHandle_s*>(lua_newuserdata(L, sizeof(imgHandle_s)));
    std::ranges::construct_at(imgHandle);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_setmetatable(L, -2);
    return 1;
}

imgHandle_s* GetImgHandle(lua_State* L, const char* method)
{
    LAssert(L, pobwindow->IsUserData(L, 1, "uiimghandlemeta"), "imgHandle:%s() must be used on an image handle", method);
    auto imgHandle = (imgHandle_s*)lua_touserdata(L, 1);
    lua_remove(L, 1);
    return imgHandle;
}

int l_imgHandleGC(lua_State* L)
{
    imgHandle_s* imgHandle = GetImgHandle(L, "__gc");
    imgHandle->~imgHandle_s();
    return 0;
}

int l_imgHandleLoad(lua_State* L) 
{
    imgHandle_s* imgHandle = GetImgHandle(L, "Load");
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: imgHandle:Load(fileName[, flag1[, flag2...]])");
    LAssert(L, lua_isstring(L, 1), "imgHandle:Load() argument 1: expected string, got %t", 1);

    QString fileName = lua_tostring(L, 1);
    QString fullFileName;
    if (fileName.contains(':') || pobwindow->scriptWorkDir.isEmpty()) {
        fullFileName = fileName;
    } else {
        fullFileName = pobwindow->scriptWorkDir + QDir::separator() + fileName;
    }

    auto& img = pobwindow->GetLazyLoadedTexture(fullFileName);
    imgHandle->tex_idx = img.index;

#if 0
    int flags = TF_NOMIPMAP;
    for (int f = 2; f <= n; f++) {
        if ( !lua_isstring(L, f) ) {
            continue;
        }
        const char* flag = lua_tostring(L, f);
        if ( !strcmp(flag, "ASYNC") ) {
            flags|= TF_ASYNC;
        } else if ( !strcmp(flag, "CLAMP") ) {
            flags|= TF_CLAMP;
        } else if ( !strcmp(flag, "MIPMAP") ) {
            flags&= ~TF_NOMIPMAP;
        } else {
            LAssert(L, 0, "imgHandle:Load(): unrecognised flag '%s'", flag);
        }
    }
#endif
    return 0;
}

int l_imgHandleUnload(lua_State* L)
{
  // NOTE: not used in scripts
    return 0;
}

int l_imgHandleIsValid(lua_State* L)
{
    imgHandle_s* imgHandle = GetImgHandle(L, "IsValid");
    lua_pushboolean(L, imgHandle->tex_idx.IsValid());
    return 1;
}

int l_imgHandleIsLoading(lua_State* L)
{
  // NOTE: not used in scripts
    lua_pushboolean(L, false);
    return 1;
}

int l_imgHandleSetLoadingPriority(lua_State* L)
{
  // NOTE: not used in scripts
    return 0;
}

int l_imgHandleImageSize(lua_State* L)
{
    imgHandle_s* imgHandle = GetImgHandle(L, "ImageSize");
    auto& img = pobwindow->GetLazyLoadedTexture(imgHandle->tex_idx);
    lua_pushinteger(L, img.size.width());
    lua_pushinteger(L, img.size.height());
    return 2;
}

// =========
// Rendering
// =========

int l_RenderInit(lua_State* L)
{
    return 0;
}

int l_GetScreenSize(lua_State* L)
{
    lua_pushinteger(L, pobwindow->width);
    lua_pushinteger(L, pobwindow->height);
    return 2;
}

int l_SetClearColor(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 3, "Usage: SetClearColor(red, green, blue[, alpha])");
    float color[4];
    for (int i = 1; i <= 3; i++) {
        LAssert(L, lua_isnumber(L, i), "SetClearColor() argument %d: expected number, got %t", i, i);
        color[i-1] = (float)lua_tonumber(L, i);
    }
    if (n >= 4 && !lua_isnil(L, 4)) {
        LAssert(L, lua_isnumber(L, 4), "SetClearColor() argument 4: expected number or nil, got %t", 4);
        color[3] = (float)lua_tonumber(L, 4);
    } else {
        color[3] = 1.0;
    }
    glClearColor(color[0], color[1], color[2], color[3]);
    return 0;
}

int l_SetDrawLayer(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: SetDrawLayer({layer|nil}[, subLayer])");
    LAssert(L, lua_isnumber(L, 1) || lua_isnil(L, 1), "SetDrawLayer() argument 1: expected number or nil, got %t", 1);
    if (n >= 2) {
        LAssert(L, lua_isnumber(L, 2), "SetDrawLayer() argument 2: expected number, got %t", 2);
    }
    if (lua_isnil(L, 1)) {
        LAssert(L, n >= 2, "SetDrawLayer(): must provide subLayer if layer is nil");
        pobwindow->SetDrawSubLayer(lua_tointeger(L, 2));
    } else if (n >= 2) {
        pobwindow->SetDrawLayer(lua_tointeger(L, 1), lua_tointeger(L, 2));
    } else {
        pobwindow->SetDrawLayer(lua_tointeger(L, 1));
    }
    return 0;
}

void ViewportCmd::execute() {
    float ratio = pobwindow->devicePixelRatio();
    int new_x = x * ratio;
    int new_y = (pobwindow->height - h - y) * ratio;
    int new_w = w * ratio;
    int new_h = h * ratio;
    glViewport(new_x, new_y, new_w, new_h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, (float)w, (float)h, 0, -9999, 9999);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

int l_SetViewport(lua_State* L)
{
    int n = lua_gettop(L);
    if (n) {
        LAssert(L, n >= 4, "Usage: SetViewport([x, y, width, height])");
        for (int i = 1; i <= 4; i++) {
            LAssert(L, lua_isnumber(L, i), "SetViewport() argument %d: expected number, got %t", i, i);
        }
        pobwindow->AppendCmd(std::make_unique<ViewportCmd>((int)lua_tointeger(L, 1), (int)lua_tointeger(L, 2), (int)lua_tointeger(L, 3), (int)lua_tointeger(L, 4)));
    } else {
        pobwindow->AppendCmd(std::make_unique<ViewportCmd>(0, 0, pobwindow->width, pobwindow->height));
    }
    return 0;
}

int l_SetDrawColor(lua_State* L)
{
    LAssert(L, pobwindow->isDrawing, "SetDrawColor() called outside of OnFrame");
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: SetDrawColor(red, green, blue[, alpha]) or SetDrawColor(escapeStr)");
    float color[4];
    if (lua_type(L, 1) == LUA_TSTRING) {
        LAssert(L, IsColorEscape(lua_tostring(L, 1)), "SetDrawColor() argument 1: invalid color escape sequence");
        ReadColorEscape(lua_tostring(L, 1), color);
        color[3] = 1.0;
    } else {
        LAssert(L, n >= 3, "Usage: SetDrawColor(red, green, blue[, alpha]) or SetDrawColor(escapeStr)");
        for (int i = 1; i <= 3; i++) {
            LAssert(L, lua_isnumber(L, i), "SetDrawColor() argument %d: expected number, got %t", i, i);
            color[i-1] = (float)lua_tonumber(L, i);
        }
        if (n >= 4 && !lua_isnil(L, 4)) {
            LAssert(L, lua_isnumber(L, 4), "SetDrawColor() argument 4: expected number or nil, got %t", 4);
            color[3] = (float)lua_tonumber(L, 4);
        } else {
            color[3] = 1.0;
        }
    }
    pobwindow->DrawColor(color);
    return 0;
}

int l_DrawImage(lua_State* L)
{
    LAssert(L, pobwindow->isDrawing, "DrawImage() called outside of OnFrame");
    int n = lua_gettop(L);
    LAssert(L, n >= 5, "Usage: DrawImage({imgHandle|nil}, left, top, width, height[, tcLeft, tcTop, tcRight, tcBottom])");
    LAssert(L, lua_isnil(L, 1) || pobwindow->IsUserData(L, 1, "uiimghandlemeta"), "DrawImage() argument 1: expected image handle or nil, got %t", 1);
    TextureIndex tex_idx = 0;
    if ( !lua_isnil(L, 1) ) {
        auto imgHandle = (imgHandle_s*)lua_touserdata(L, 1);
        tex_idx = imgHandle->tex_idx;
        // issue load request
        pobwindow->GetTexture(tex_idx);
    }
    float arg[8];
    if (n > 5) {
        LAssert(L, n >= 9, "DrawImage(): incomplete set of texture coordinates provided");
        for (int i = 2; i <= 9; i++) {
            LAssert(L, lua_isnumber(L, i), "DrawImage() argument %d: expected number, got %t", i, i);
            arg[i-2] = (float)lua_tonumber(L, i);
        }
        pobwindow->AppendCmd(std::make_unique<DrawImageCmd>(tex_idx, arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7]));
    } else {
        for (int i = 2; i <= 5; i++) {
            LAssert(L, lua_isnumber(L, i), "DrawImage() argument %d: expected number, got %t", i, i);
            arg[i-2] = (float)lua_tonumber(L, i);
        }
        pobwindow->AppendCmd(std::make_unique<DrawImageCmd>(tex_idx, arg[0], arg[1], arg[2], arg[3]));
    }
    return 0;
}

void DrawTextureCmd::execute() {
    this->BindTexture();
    glBegin(GL_TRIANGLE_FAN);
    for (int v = 0; v < 4; v++) {
        glTexCoord2d(s[v], t[v]);
        glVertex2d(x[v], y[v]);
    }
    glEnd();
}

void DrawImageQuadCmd::BindTexture() {
    pobwindow->GetTexture(tex).bind();
}

int l_DrawImageQuad(lua_State* L)
{
    LAssert(L, pobwindow->isDrawing, "DrawImageQuad() called outside of OnFrame");
    int n = lua_gettop(L);
    LAssert(L, n >= 9, "Usage: DrawImageQuad({imgHandle|nil}, x1, y1, x2, y2, x3, y3, x4, y4[, s1, t1, s2, t2, s3, t3, s4, t4])");
    LAssert(L, lua_isnil(L, 1) || pobwindow->IsUserData(L, 1, "uiimghandlemeta"), "DrawImageQuad() argument 1: expected image handle or nil, got %t", 1);
    TextureIndex tex_idx = 0;
    if ( !lua_isnil(L, 1) ) {
        auto imgHandle = (imgHandle_s*)lua_touserdata(L, 1);
        tex_idx = imgHandle->tex_idx;
        pobwindow->GetTexture(tex_idx);
    }
    float arg[16];
    if (n > 9) {
        LAssert(L, n >= 17, "DrawImageQuad(): incomplete set of texture coordinates provided");
        for (int i = 2; i <= 17; i++) {
            LAssert(L, lua_isnumber(L, i), "DrawImageQuad() argument %d: expected number, got %t", i, i);
            arg[i-2] = (float)lua_tonumber(L, i);
        }
        pobwindow->AppendCmd(std::make_unique<DrawImageQuadCmd>(tex_idx, arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11], arg[12], arg[13], arg[14], arg[15]));
    } else {
        for (int i = 2; i <= 9; i++) {
            LAssert(L, lua_isnumber(L, i), "DrawImageQuad() argument %d: expected number, got %t", i, i);
            arg[i-2] = (float)lua_tonumber(L, i);
        }
        pobwindow->AppendCmd(std::make_unique<DrawImageQuadCmd>(tex_idx, arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7]));
    }
    return 0;
}

DrawStringCmd::DrawStringCmd(float X, float Y, int Align, int Size, int fontKey, const char *Text) : text(Text) {
    dscount++;
    if (Text[0] == '^' && Text[1] != '\0') {
        switch(Text[1]) {
        case '0':
            setCol(0.0f, 0.0f, 0.0f);
            break;
        case '1':
            setCol(1.0f, 0.0f, 0.0f);
            break;
        case '2':
            setCol(0.0f, 1.0f, 0.0f);
            break;
        case '3':
            setCol(0.0f, 0.0f, 1.0f);
            break;
        case '4':
            setCol(1.0f, 1.0f, 0.0f);
            break;
        case '5':
            setCol(1.0f, 0.0f, 1.0f);
            break;
        case '6':
            setCol(0.0f, 1.0f, 1.0f);
            break;
        case '7':
            setCol(1.0f, 1.0f, 1.0f);
            break;
        case '8':
            setCol(0.7f, 0.7f, 0.7f);
            break;
        case '9':
            setCol(0.4f, 0.4f, 0.4f);
            break;
        case 'x':
            int xr, xg, xb;
            sscanf(text.toStdString().c_str() + 2, "%2x%2x%2x", &xr, &xg, &xb);
            setCol(xr / 255.0f, xg / 255.0f, xb / 255.0f);
            break;
        default:
            break;
        }
    } else {
        col[3] = 0;
    }
    int count = 0;
    for (auto i = colourCodes.globalMatch(text);i.hasNext();i.next()) {
        count += 1;
    }
    if (count > 1) {
        //std::cout << text.toStdString().c_str() << " " << count << std::endl;
    }
    text.remove(colourCodes);

    QString cacheKey = (QString::number(fontKey) + "_" + QString::number(Size) + "_" + text);
    if (pobwindow->stringCache.contains(cacheKey)) {
        tex = pobwindow->stringCache[cacheKey];
    } else {
        QString fontName = fonts[fontKey];

        QFont font(fontName);
        font.setPixelSize(Size + pobwindow->fontFudge);
        QFontMetrics fm(font);
        QSize size = fm.size(0, text);

        QImage brush(size, QImage::Format_ARGB32);
        brush.fill(QColor(255, 255, 255, 0));
        tex = nullptr;
        if (brush.width() && brush.height()) {
            QPainter p(&brush);
            p.setPen(QColor(255, 255, 255, 255));
            p.setFont(font);
            p.setCompositionMode(QPainter::CompositionMode_Plus);
            p.drawText(0, 0, size.width(), size.height(), 0, text);
            p.end();
            tex.reset(new QOpenGLTexture(brush));
        }
        pobwindow->stringCache.emplace(cacheKey, tex);
    }
    int width = 0;
    int height = 0;
    if (tex) {
        width = tex->width();
        height = tex->height();
    }

    switch (Align) {
    case F_CENTRE:
        X = floor((pobwindow->width - width) / 2.0f + X);
        break;
    case F_RIGHT:
        X = floor(pobwindow->width - width - X);
        break;
    case F_CENTRE_X:
        X = floor(X - width / 2.0f);
        break;
    case F_RIGHT_X:
        X = floor(X - width) + 5;
        break;
    }
    x[0] = X;
    y[0] = Y;
    x[1] = X + width;
    y[1] = Y;
    x[2] = X + width;
    y[2] = Y + height;
    x[3] = X;
    y[3] = Y + height;

    s[0] = 0;
    t[0] = 0;
    s[1] = 1;
    t[1] = 0;
    s[2] = 1;
    t[2] = 1;
    s[3] = 0;
    t[3] = 1;
}

void DrawStringCmd::BindTexture()
{
    if (tex == nullptr) {
        //throw std::runtime_error("str tex is null");
        pobwindow->white->bind();
    } else {
        tex->bind();
    }
}

int l_DrawString(lua_State* L)
{
    LAssert(L, pobwindow->isDrawing, "DrawString() called outside of OnFrame");
    int n = lua_gettop(L);
    LAssert(L, n >= 6, "Usage: DrawString(left, top, align, height, font, text)");
    LAssert(L, lua_isnumber(L, 1), "DrawString() argument 1: expected number, got %t", 1);
    LAssert(L, lua_isnumber(L, 2), "DrawString() argument 2: expected number, got %t", 2);
    LAssert(L, lua_isstring(L, 3) || lua_isnil(L, 3), "DrawString() argument 3: expected string or nil, got %t", 3);
    LAssert(L, lua_isnumber(L, 4), "DrawString() argument 4: expected number, got %t", 4);
    LAssert(L, lua_isstring(L, 5), "DrawString() argument 5: expected string, got %t", 5);
    LAssert(L, lua_isstring(L, 6), "DrawString() argument 6: expected string, got %t", 6);
    static const char* alignMap[6] = { "LEFT", "CENTER", "RIGHT", "CENTER_X", "RIGHT_X", nullptr };
    pobwindow->AppendCmd(std::make_unique<DrawStringCmd>(
        (float)lua_tonumber(L, 1), (float)lua_tonumber(L, 2), luaL_checkoption(L, 3, "LEFT", alignMap), 
        (int)lua_tointeger(L, 4), luaL_checkoption(L, 5, "FIXED", fontMap), lua_tostring(L, 6)
                                                  ));
    return 0;
}

int l_DrawStringWidth(lua_State* L) 
{
    int n = lua_gettop(L);
    LAssert(L, n >= 3, "Usage: DrawStringWidth(height, font, text)");
    LAssert(L, lua_isnumber(L, 1), "DrawStringWidth() argument 1: expected number, got %t", 1);
    LAssert(L, lua_isstring(L, 2), "DrawStringWidth() argument 2: expected string, got %t", 2);
    LAssert(L, lua_isstring(L, 3), "DrawStringWidth() argument 3: expected string, got %t", 3);

    int fontsize = lua_tointeger(L, 1);
    int fontKey = luaL_checkoption(L, 2, "FIXED", fontMap);
    QString text(lua_tostring(L, 3));

    QString fontName = fonts[fontKey];

    text.remove(colourCodes);

    QString cacheKey = (QString::number(fontKey) + "_" + QString::number(fontsize) + "_" + text);
    if (pobwindow->stringCache.contains(cacheKey) && pobwindow->stringCache[cacheKey]) {
        lua_pushinteger(L, pobwindow->stringCache[cacheKey]->width());
        return 1;
    }

    QFont font(fontName);
    font.setPixelSize(fontsize + pobwindow->fontFudge);
    QFontMetrics fm(font);
    lua_pushinteger(L, fm.size(0, text).width());
    return 1;
}

int l_DrawStringCursorIndex(lua_State* L) 
{
    int n = lua_gettop(L);
    LAssert(L, n >= 5, "Usage: DrawStringCursorIndex(height, font, text, cursorX, cursorY)");
    LAssert(L, lua_isnumber(L, 1), "DrawStringCursorIndex() argument 1: expected number, got %t", 1);
    LAssert(L, lua_isstring(L, 2), "DrawStringCursorIndex() argument 2: expected string, got %t", 2);
    LAssert(L, lua_isstring(L, 3), "DrawStringCursorIndex() argument 3: expected string, got %t", 3);
    LAssert(L, lua_isnumber(L, 4), "DrawStringCursorIndex() argument 4: expected number, got %t", 4);
    LAssert(L, lua_isnumber(L, 5), "DrawStringCursorIndex() argument 5: expected number, got %t", 5);

    int fontsize = lua_tointeger(L, 1);
    int fontKey = luaL_checkoption(L, 2, "FIXED", fontMap);
    QString text(lua_tostring(L, 3));

    QString fontName = fonts[fontKey];

    text.remove(colourCodes);

    QStringList texts = text.split("\n");
    QFont font(fontName);
    font.setPixelSize(fontsize + pobwindow->fontFudge);
    QFontMetrics fm(font);
    int curX = lua_tointeger(L, 4);
    int curY = lua_tointeger(L, 5);
    int yidx = std::max(0, std::min((int)texts.size() - 1, curY / fm.lineSpacing()));
    text = texts[yidx];
    int i = 0;
    for (;i <= text.size();i++) {
        if (fm.size(0, text.left(i)).width() > curX) {
            break;
        }
    }
    for (int y = 0;y < yidx;y++) {
        i += texts[y].size() + 1;
    }
    lua_pushinteger(L, i);
    return 1;
}


