#include "pobwindow.hpp"

#include <QColor>
#include <QDateTime>
#include <QKeyEvent>
#include <QtGui/QGuiApplication>
#include <QImageReader>
#include <memory>
#include <stdexcept>

#include "lua.h"
#include "lua_utils.hpp"
#include "qevent.h"
#include "qnamespace.h"

extern lua_State *L;

int dscount;

POBWindow *pobwindow;


namespace
{

void pushMouseString(QMouseEvent *event) {
    switch (event->button()) {
    case Qt::LeftButton:
        lua_pushstring(L, "LEFTBUTTON");
        break;
    case Qt::RightButton:
        lua_pushstring(L, "RIGHTBUTTON");
        break;
    case Qt::MiddleButton:
        lua_pushstring(L, "MIDDLEBUTTON");
        break;
    case Qt::ForwardButton:
        lua_pushstring(L, "MOUSE5");
        break;
    case Qt::BackButton:
        lua_pushstring(L, "MOUSE4");
        break;
    default:
        std::cout << "MOUSE STRING? " << event->button() << std::endl;
    }
}

bool pushKeyString(int keycode) {
    switch (keycode) {
    case Qt::Key_Escape:
        lua_pushstring(L, "ESCAPE");
        break;
    case Qt::Key_Tab:
        lua_pushstring(L, "TAB");
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        lua_pushstring(L, "RETURN");
        break;
    case Qt::Key_Backspace:
        lua_pushstring(L, "BACK");
        break;
    case Qt::Key_Delete:
        lua_pushstring(L, "DELETE");
        break;
    case Qt::Key_Home:
        lua_pushstring(L, "HOME");
        break;
    case Qt::Key_End:
        lua_pushstring(L, "END");
        break;
    case Qt::Key_Up:
        lua_pushstring(L, "UP");
        break;
    case Qt::Key_Down:
        lua_pushstring(L, "DOWN");
        break;
    case Qt::Key_Left:
        lua_pushstring(L, "LEFT");
        break;
    case Qt::Key_Right:
        lua_pushstring(L, "RIGHT");
        break;
    case Qt::Key_PageUp:
        lua_pushstring(L, "PAGEUP");
        break;
    case Qt::Key_PageDown:
        lua_pushstring(L, "PAGEDOWN");
        break;
    default:
        return false;
    }
    return true;
}

}

POBWindow::~POBWindow()
{
    textureLoader.stop();
    textureLoader.wait();
}

void POBWindow::initializeGL() {
    QImage wimg{1, 1, QImage::Format_Mono};
    wimg.fill(1);
    white.reset(new QOpenGLTexture(wimg));
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glEnable(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
//    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_ALPHA_TEST);
//    glAlphaFunc(GL_GREATER, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void POBWindow::resizeGL(int w, int h) {
    width = w;
    height = h;
}

void POBWindow::paintGL() {
    //exit(1);
    isDrawing = true;
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glColor4f(0, 0, 0, 0);

    repaintTimer.start(100);

    for (auto& layer: layers) {
      layer.second.clear();
    }

    uniqueTextureDrawn.clear();
    dscount = 0;

    currentLayer = &layers[{0, 0}];
    curLayer = 0;
    curSubLayer = 0;

    pushCallback("OnFrame");
    int result = lua_pcall(L, 1, 0, 0);
    if (result != 0) {
        lua_error(L);
    }

    if (uniqueTextureDrawn.size() > textureCache.maxCost()) {
        textureCache.setMaxCost(static_cast<int>(1.2f * uniqueTextureDrawn.size()));
    }

    if (RetrieveLoadedTextures()) {
        repaintTimer.start(10);
    }

    for (auto& layer : layers) {
        for (auto& cmd : layer.second) {
            cmd->execute();
        }
    }
    isDrawing = false;
}

void POBWindow::subScriptFinished() {
    bool clean = true;
    for (int i = 0;i < subScriptList.size();i++) {
        if (subScriptList[i].get()) {
            clean = false;
            if (subScriptList[i]->isFinished()) {
                subScriptList[i]->onSubFinished(L, i);
                subScriptList[i].reset();
            }
        }
    }
    if (clean) {
        subScriptList.clear();
    }

    update();
}

void POBWindow::mouseMoveEvent(QMouseEvent *event) {
    update();
}

void POBWindow::mousePressEvent(QMouseEvent *event) {
    pushCallback("OnKeyDown");
    pushMouseString(event);
    lua_pushboolean(L, false);
    int result = lua_pcall(L, 3, 0, 0);
    if (result != 0) {
        lua_error(L);
    }
    update();
}

void POBWindow::mouseReleaseEvent(QMouseEvent *event) {
    pushCallback("OnKeyUp");
    pushMouseString(event);
    int result = lua_pcall(L, 2, 0, 0);
    if (result != 0) {
        lua_error(L);
    }
    update();
}

void POBWindow::mouseDoubleClickEvent(QMouseEvent *event) {
    pushCallback("OnKeyDown");
    pushMouseString(event);
    lua_pushboolean(L, true);
    int result = lua_pcall(L, 3, 0, 0);
    if (result != 0) {
        lua_error(L);
    }
    update();
}

void POBWindow::wheelEvent(QWheelEvent *event) {
    pushCallback("OnKeyUp");
    if (event->angleDelta().y() > 0) {
        lua_pushstring(L, "WHEELUP");
    } else if (event->angleDelta().y() < 0) {
        lua_pushstring(L, "WHEELDOWN");
    } else {
        return;
    }
    lua_pushboolean(L, false);
    int result = lua_pcall(L, 3, 0, 0);
    if (result != 0) {
        lua_error(L);
    }
    update();
}

void POBWindow::keyPressEvent(QKeyEvent *event) {
    pushCallback("OnKeyDown");
    if (!pushKeyString(event->key())) {
        if (event->key() >= ' ' && event->key() <= '~') {
            char s[2];
            if (event->key() >= 'A' && event->key() <= 'Z' && !(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)) {
                s[0] = event->key() + 32;
            } else {
                s[0] = event->key();
            }
            s[1] = 0;
            if (!(QGuiApplication::keyboardModifiers() & Qt::ControlModifier)) {
                lua_pop(L, 2);
                pushCallback("OnChar");
            }
            lua_pushstring(L, s);
        } else {
            lua_pushstring(L, "ASDF");
            //std::cout << "UNHANDLED KEYDOWN" << std::endl;
        }
    }
    lua_pushboolean(L, false);
    int result = lua_pcall(L, 3, 0, 0);
    if (result != 0) {
        lua_error(L);
    }
    update();
}

void POBWindow::keyReleaseEvent(QKeyEvent *event) {
    pushCallback("OnKeyUp");
    if (!pushKeyString(event->key())) {
        lua_pushstring(L, "ASDF");
        //std::cout << "UNHANDLED KEYUP" << std::endl;
    }
    int result = lua_pcall(L, 2, 0, 0);
    if (result != 0) {
        lua_error(L);
    }
    update();
}

LazyLoadedTexture& POBWindow::GetLazyLoadedTexture(const QString& path)
{
    auto iter = textureIndexByPath.find(path);
    if (iter != textureIndexByPath.end()) {
        return lazyLoadedTexture[iter->GetIndex()];
    }

    QImageReader reader(path);
    QSize size = reader.size();
    if (not size.isValid() || size.isEmpty()) {
        // invalid image
        return lazyLoadedTexture[0];
    }
    TextureIndex new_tex_idx = lazyLoadedTexture.size();
    lazyLoadedTexture.append({
            .index = new_tex_idx,
            .path = path,
            .size = size,
            .state = LoadState::NotLoaded,
            });
    textureIndexByPath[path] = new_tex_idx;
    return lazyLoadedTexture[new_tex_idx.GetIndex()];
}

LazyLoadedTexture& POBWindow::GetLazyLoadedTexture(TextureIndex index)
{
    if (index.GetIndex() >= static_cast<size_t>(lazyLoadedTexture.size())) {
        return lazyLoadedTexture[0];
    }
    return lazyLoadedTexture[index.GetIndex()];
}

QOpenGLTexture& POBWindow::GetTexture(TextureIndex index)
{
    uniqueTextureDrawn.insert(index.GetIndex());

    auto* tex = textureCache[index.GetIndex()];
    if (tex != nullptr) {
        return *tex;
    }

    auto& llt = lazyLoadedTexture[index.GetIndex()];
    if (llt.state == LoadState::NotLoaded || llt.state == LoadState::Loaded) {
        llt.state = LoadState::Loading;
        textureLoader.request_load(llt);
    }

    if (white == nullptr) {
        throw std::runtime_error("white is null");
    }
    return *white;
}

bool POBWindow::RetrieveLoadedTextures()
{
   textureLoader.collect_loaded_textures(tmpLoadedTextures); 
   if (tmpLoadedTextures.empty()) {
       return false;
   }

   for (auto& [idx, img] : tmpLoadedTextures) {
       LoadState ls = LoadState::LoadFailed;
       if (img) {
           auto tex = std::make_unique<QOpenGLTexture>(*img);
           if (tex->isCreated()) {
               ls = LoadState::Loaded;
               textureCache.insert(idx.GetIndex(), tex.release());
           }
       }
       lazyLoadedTexture[idx.GetIndex()].state = ls;
   }
   tmpLoadedTextures.clear();
   return true;
}


int POBWindow::IsUserData(lua_State* L, int index, const char* metaName)
{
    if (lua_type(L, index) != LUA_TUSERDATA || lua_getmetatable(L, index) == 0) {
        return 0;
    }
    lua_getfield(L, LUA_REGISTRYINDEX, metaName);
    int ret = lua_rawequal(L, -2, -1);
    lua_pop(L, 2);
    return ret;
}

void POBWindow::SetDrawLayer(int layer) {
    SetDrawLayer(layer, 0);
}

void POBWindow::SetDrawLayer(int layer, int subLayer) {
    if (layer == curLayer && subLayer == curSubLayer) {
        return;
    }

    curLayer = layer;
    curSubLayer = subLayer;
    QPair<int, int> key{layer, subLayer};
    currentLayer = &layers[key];
}


void POBWindow::AppendCmd(std::unique_ptr<Cmd> cmd) {
    currentLayer->emplace_back(std::move(cmd));
}

void POBWindow::DrawColor(const float col[4]) {
    if (col) {
        drawColor[0] = col[0];
        drawColor[1] = col[1];
        drawColor[2] = col[2];
        drawColor[3] = col[3];
    } else {
        drawColor[0] = 1.0f;
        drawColor[1] = 1.0f;
        drawColor[2] = 1.0f;
        drawColor[3] = 1.0f;
    }
    AppendCmd(std::make_unique<ColorCmd>(drawColor));
}

void POBWindow::DrawColor(uint32_t col) {
    drawColor[0] = ((col >> 16) & 0xFF) / 255.0f;
    drawColor[1] = ((col >> 8) & 0xFF) / 255.0f;
    drawColor[2] = (col & 0xFF) / 255.0f;
    drawColor[3] = (col >> 24) / 255.0f;
}

void POBWindow::closeEvent(QCloseEvent *event) {
    pushCallback("CanExit");
    int result = lua_pcall(L, 1, 1, 0);
    if (result != 0) {
        lua_error(L);
        return;
    }

    int n = lua_gettop(L);
    int canExit = lua_toboolean(L, n);
    lua_pop(L, 1);

    if (canExit) {
        pushCallback("OnExit");
        result = lua_pcall(L, 1, 0, 0);
        if (result != 0) {
            lua_error(L);
        }
    } else {
        event->ignore();
    }
}
