#include <cstddef>
#include <cstdio>
#include <iostream>
#include <memory>

#include <QClipboard>
#include <QDateTime>
#include <QFontDatabase>
#include <QtGui/QGuiApplication>

#include <vector>
#include <zlib.h>

#include "pobwindow.hpp"
#include "qnamespace.h"
#include "qobject.h"
#include "src/utils.hpp"
#include "subscript.hpp"
#include "lua_utils.hpp"
#include "lua_cb_gfx.hpp"

lua_State *L;
QGuiApplication *app;


static constexpr const char* describeLuaError(int err)
{
  switch (err) {
    case LUA_ERRERR:
      return "error in error handling";
    case LUA_ERRRUN:
      return "runtime error";
    case LUA_ERRMEM:
      return "memory allocation error";
  }
  return "<unknown>";
}

static lua_CFunction g_prevPanicHandler;

static int l_AtPanic(lua_State* L)
{
  std::cout << "lua PANIC: ";
  if (lua_isstring(L, 1)) {
    std::cout << lua_tostring(L, 1);
  }
  std::cout << std::endl;
  if (g_prevPanicHandler) {
    return g_prevPanicHandler(L);
  }
  return 0;
}

[[maybe_unused]] static void dumpLuaStack(lua_State* L, size_t limit = (size_t)-1)
{
  int top = lua_gettop(L);
  for (int i = top; i >= 1 && limit > 0; --i, --limit) {
    int t = lua_type(L, i);
    std::printf("%4i %5i ", i, i - top - 1);
    switch (t) {
      case LUA_TSTRING:
        std::cout << '"' << lua_tostring(L, i) << '"';
        break;
      case LUA_TNUMBER:
        std::cout << lua_tonumber(L, i);
        break;
      case LUA_TBOOLEAN:
        std::cout << (lua_toboolean(L, i) ? "true" : "false");
        break;
      case LUA_TNIL:
      default:
        std::cout << lua_typename(L, t) << " (" << t << ")";
        break;
    }
    std::cout << "\n";
  }
  std::cout << std::endl;
}

static void installPanicHandler(lua_State* L)
{
  g_prevPanicHandler = lua_atpanic(L, l_AtPanic);
}

static void invokeLuaDebugTraceback(lua_State* L)
{
  lua_getfield(L, LUA_GLOBALSINDEX, "debug");
  lua_getfield(L, -1, "traceback");
  lua_remove(L, -2);
  lua_call(L, 0, 1);
  if (lua_isstring(L, -1)) {
    std::cout << lua_tostring(L, -1) << "\n";
  }
  lua_remove(L, -1);
}

static int l_pcallErrorHandler(lua_State* L)
{
  invokeLuaDebugTraceback(L);
  lua_pushvalue(L, 1);
  return 1;
}

// =========
// Callbacks
// =========

static int l_SetCallback(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: SetCallback(name[, func])");
    LAssert(L, lua_isstring(L, 1), "SetCallback() argument 1: expected string, got %t", 1);
    lua_pushvalue(L, 1);
    if (n >= 2) {
        LAssert(L, lua_isfunction(L, 2) || lua_isnil(L, 2), "SetCallback() argument 2: expected function or nil, got %t", 2);
        lua_pushvalue(L, 2);
    } else {
        lua_pushnil(L);
    }
    lua_settable(L, lua_upvalueindex(1));
    return 0;
}

static int l_GetCallback(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: GetCallback(name)");
    LAssert(L, lua_isstring(L, 1), "GetCallback() argument 1: expected string, got %t", 1);
    lua_pushvalue(L, 1);
    lua_gettable(L, lua_upvalueindex(1));
    return 1;
}

static int l_SetMainObject(lua_State* L)
{
    int n = lua_gettop(L);
    lua_pushstring(L, "MainObject");
    if (n >= 1) {
        LAssert(L, lua_istable(L, 1) || lua_isnil(L, 1), "SetMainObject() argument 1: expected table or nil, got %t", 1);
        lua_pushvalue(L, 1);
    } else {
        lua_pushnil(L);
    }
    lua_settable(L, lua_upvalueindex(1));
    return 0;
}

static int l_StripEscapes(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: StripEscapes(string)");
    LAssert(L, lua_isstring(L, 1), "StripEscapes() argument 1: expected string, got %t", 1);
    const char* str = lua_tostring(L, 1);
    char* strip = new char[strlen(str) + 1];
    char* p = strip;
    while (*str) {
        int esclen = IsColorEscape(str);
        if (esclen) {
            str+= esclen;
        } else {
            *(p++) = *(str++);
        }
    }
    *p = 0;
    lua_pushstring(L, strip);
    delete[] strip;
    return 1;
}

static int l_GetAsyncCount(lua_State* L)
{
//    lua_pushinteger(L, pobwindow->GetTexAsyncCount());
    lua_pushinteger(L, 0);
    return 1;
}

// ==============
// Search Handles
// ==============

struct searchHandle_s {
    QFileInfoList *fil;
};

static int l_NewFileSearch(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: NewFileSearch(spec[, findDirectories])");
    LAssert(L, lua_isstring(L, 1), "NewFileSearch() argument 1: expected string, got %t", 1);
    QString search_string(lua_tostring(L, 1));
    QStringList split = search_string.split("/");
    QString wildcard = split.takeLast();
    QDir dir(split.join("/"));
    QStringList filters;
    filters << wildcard;
    bool dirOnly = lua_toboolean(L, 2) != 0;
    QFileInfoList fil = dir.entryInfoList(filters, QDir::NoDotAndDotDot | (dirOnly ? QDir::Dirs : QDir::Files));
    if (fil.isEmpty()) {
        return 0;
    }

    auto handle = (searchHandle_s*)lua_newuserdata(L, sizeof(searchHandle_s));
    handle->fil = new QFileInfoList(fil);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_setmetatable(L, -2);
    return 1;
}

static QFileInfoList* GetSearchHandle(lua_State* L, const char* method, bool valid)
{
    LAssert(L, pobwindow->IsUserData(L, 1, "uisearchhandlemeta"), "searchHandle:%s() must be used on a search handle", method);
    auto searchHandle = (searchHandle_s*)lua_touserdata(L, 1);
    lua_remove(L, 1);
    if (valid) {
        LAssert(L, !searchHandle->fil->isEmpty(), "searchHandle:%s(): search handle is no longer valid (ran out of files to find)", method);
    }
    return searchHandle->fil;
}

static int l_searchHandleGC(lua_State* L)
{
    QFileInfoList* searchHandle = GetSearchHandle(L, "__gc", false);
    delete searchHandle;
    return 0;
}

static int l_searchHandleNextFile(lua_State* L)
{
    QFileInfoList* searchHandle = GetSearchHandle(L, "NextFile", true);
    searchHandle->removeFirst();
    if ( searchHandle->isEmpty() ) {
        return 0;
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int l_searchHandleGetFileName(lua_State* L)
{
    QFileInfoList* searchHandle = GetSearchHandle(L, "GetFileName", true);
    lua_pushstring(L, searchHandle->first().fileName().toStdString().c_str());
    return 1;
}

static int l_searchHandleGetFileSize(lua_State* L)
{
    QFileInfoList* searchHandle = GetSearchHandle(L, "GetFileSize", true);
    lua_pushinteger(L, searchHandle->first().size());
    return 1;
}

static int l_searchHandleGetFileModifiedTime(lua_State* L)
{
    QFileInfoList* searchHandle = GetSearchHandle(L, "GetFileModifiedTime", true);
    QDateTime modified = searchHandle->first().lastModified();
    lua_pushnumber(L, modified.toMSecsSinceEpoch());
    lua_pushstring(L, modified.date().toString().toStdString().c_str());
    lua_pushstring(L, modified.time().toString().toStdString().c_str());
    return 3;
}

// =================
// General Functions
// =================

static int l_SetWindowTitle(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: SetWindowTitle(title)");
    LAssert(L, lua_isstring(L, 1), "SetWindowTitle() argument 1: expected string, got %t", 1);
    pobwindow->setTitle(lua_tostring(L, 1));
    return 0;
}

static int l_GetCursorPos(lua_State* L)
{
    QPoint pos = QCursor::pos();
    pos = pobwindow->mapFromGlobal(pos);
    lua_pushinteger(L, pos.x());
    lua_pushinteger(L, pos.y());
    return 2;
}

static int l_SetCursorPos(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 2, "Usage: SetCursorPos(x, y)");
    LAssert(L, lua_isnumber(L, 1), "SetCursorPos() argument 1: expected number, got %t", 1);
    LAssert(L, lua_isnumber(L, 2), "SetCursorPos() argument 2: expected number, got %t", 2);
    //pobwindow->sys->video->SetRelativeCursor((int)lua_tointeger(L, 1), (int)lua_tointeger(L, 2));
    return 0;
}

static int l_ShowCursor(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: ShowCursor(doShow)");
    //pobwindow->sys->ShowCursor(lua_toboolean(L, 1));
    return 0;
}

static int l_IsKeyDown(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: IsKeyDown(keyName)");
    LAssert(L, lua_isstring(L, 1), "IsKeyDown() argument 1: expected string, got %t", 1);
    size_t len;
    const char* kname = lua_tolstring(L, 1, &len);
    LAssert(L, len >= 1, "IsKeyDown() argument 1: string is empty", 1);
    QString k(kname);
    bool result = false;
    if (k == "LEFTBUTTON") {
        if (QGuiApplication::mouseButtons() & Qt::LeftButton) {
            result = true;
        }
    } else {
        int keys = QGuiApplication::keyboardModifiers();
        if (k == "CTRL") {
            result = keys & Qt::ControlModifier;
        } else if (k == "SHIFT") {
            result = keys & Qt::ShiftModifier;
        } else if (k == "ALT") {
            result = keys & Qt::AltModifier;
        } else {
            std::cout << "UNKNOWN ISKEYDOWN: " << k.toStdString() << std::endl;
        }
    }
    lua_pushboolean(L, result);
    //int key = pobwindow->KeyForName(kname);
    //LAssert(L, key, "IsKeyDown(): unrecognised key name");
    //lua_pushboolean(L, pobwindow->sys->IsKeyDown(key));
    return 1;
}

static int l_Copy(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: Copy(string)");
    LAssert(L, lua_isstring(L, 1), "Copy() argument 1: expected string, got %t", 1);
    QGuiApplication::clipboard()->setText(lua_tostring(L, 1));
    return 0;
}

static int l_Paste(lua_State* L)
{
    QString data = QGuiApplication::clipboard()->text();
    if (data.size()) {
        lua_pushstring(L, data.toStdString().c_str());
        return 1;
    } else {
        return 0;
    }
}

static int l_Deflate(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: Deflate(string)");
    LAssert(L, lua_isstring(L, 1), "Deflate() argument 1: expected string, got %t", 1);
    z_stream_s z;
    z.zalloc = NULL;
    z.zfree = NULL;
    deflateInit(&z, 9);
    size_t inLen;
    Byte* in = (Byte*)lua_tolstring(L, 1, &inLen);
    uLong outSz = deflateBound(&z, inLen);
    std::vector<Byte> out(outSz);
    z.next_in = in;
    z.avail_in = inLen;
    z.next_out = out.data();
    z.avail_out = outSz;
    int err = deflate(&z, Z_FINISH);
    deflateEnd(&z);
    if (err == Z_STREAM_END) {
        lua_pushlstring(L, (const char*)out.data(), z.total_out);
        return 1;
    } else {
        lua_pushnil(L);
        lua_pushstring(L, zError(err));
        return 2;
    }
}

static int l_Inflate(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: Inflate(string)");
    LAssert(L, lua_isstring(L, 1), "Inflate() argument 1: expected string, got %t", 1);
    size_t inLen;
    Byte* in = (Byte*)lua_tolstring(L, 1, &inLen);
    int outSz = inLen * 4;
    Byte* out = new Byte[outSz];
    z_stream_s z;
    z.next_in = in;
    z.avail_in = inLen;
    z.zalloc = NULL;
    z.zfree = NULL;
    z.next_out = out;
    z.avail_out = outSz;
    inflateInit(&z);
    int err;
    while ((err = inflate(&z, Z_NO_FLUSH)) == Z_OK) {
        if (z.avail_out == 0) {
            // Output buffer filled, embiggen it
            int newSz = outSz << 1;
            Byte *newOut = (Byte *)realloc(out, newSz);
            if (newOut) {
                out = newOut;
            } else {
                // PANIC
                delete[] out;
                return 0;
            }
            z.next_out = out + outSz;
            z.avail_out = outSz;
            outSz = newSz;
        }
      }
    inflateEnd(&z);
    if (err == Z_STREAM_END) {
        lua_pushlstring(L, (const char*)out, z.total_out);
        return 1;
    } else {
        lua_pushnil(L);
        lua_pushstring(L, zError(err));
        return 2;
    }
}

static int l_GetTime(lua_State* L)
{
    qint64 ms = QDateTime::currentDateTime().toMSecsSinceEpoch();
    lua_pushinteger(L, ms);
    return 1;
}

static int l_GetScriptPath(lua_State* L)
{
    lua_pushstring(L, pobwindow->scriptPath.toStdString().c_str());
    return 1;
}

static int l_GetRuntimePath(lua_State* L)
{
    lua_pushstring(L, pobwindow->basePath.toStdString().c_str());
    return 1;
}

static int l_GetUserPath(lua_State* L)
{
    lua_pushstring(L, pobwindow->userPath.toStdString().c_str());
    return 1;
}

static int l_MakeDir(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: MakeDir(path)");
    LAssert(L, lua_isstring(L, 1), "MakeDir() argument 1: expected string, got %t", 1);
    lua_pushboolean(L, QDir().mkpath(lua_tostring(L, 1)));
    return 1;
}

static int l_RemoveDir(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: l_RemoveDir(path)");
    LAssert(L, lua_isstring(L, 1), "l_RemoveDir() argument 1: expected string, got %t", 1);
    QDir d;
    if (!d.rmdir(lua_tostring(L, 1))) {
        lua_pushnil(L);
        return 1;
    } else {
        lua_pushboolean(L, true);
        return 1;
    }
}

static int l_SetWorkDir(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: SetWorkDir(path)");
    LAssert(L, lua_isstring(L, 1), "SetWorkDir() argument 1: expected string, got %t", 1);
    if (QDir::setCurrent(lua_tostring(L, 1))) {
        pobwindow->scriptWorkDir = lua_tostring(L, 1);
    }
    return 0;
}

static int l_GetWorkDir(lua_State* L)
{
    lua_pushstring(L, QDir::currentPath().toStdString().c_str());
    return 1;
}

static int l_LaunchSubScript(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 3, "Usage: LaunchSubScript(scriptText, funcList, subList[, ...])");
    for (int i = 1; i <= 3; i++) {
        LAssert(L, lua_isstring(L, i), "LaunchSubScript() argument %d: expected string, got %t", i, i);
    }
    for (int i = 4; i <= n; i++) {
        LAssert(L, lua_isnil(L, i) || lua_isboolean(L, i) || lua_isnumber(L, i) || lua_isstring(L, i), 
                           "LaunchSubScript() argument %d: only nil, boolean, number and string types can be passed to sub script", i);
    }
    int slot = pobwindow->subScriptList.size();
    pobwindow->subScriptList.append(std::make_shared<SubScript>(L));
    // Signal us when the subscript completes so we can trigger a repaint.
    pobwindow->connect( pobwindow->subScriptList[slot].get(), &SubScript::finished, pobwindow, &POBWindow::subScriptFinished );
    pobwindow->subScriptList[slot]->start();
    lua_pushinteger(L, slot);
    return 1;
}

static int l_AbortSubScript(lua_State* L)
{
    std::cout << "SUBSCRIPT ABORT STUB" << std::endl;
    return 0;
    /*
        int n = lua_gettop(L);
      LAssert(L, n >= 1, "Usage: AbortSubScript(ssID)");
      LAssert(L, lua_islightuserdata(L, 1), "AbortSubScript() argument 1: expected subscript ID, got %t", 1);
      notdword slot = (notdword)lua_touserdata(L, 1);
      LAssert(L, slot < pobwindow->subScriptSize && pobwindow->subScriptList[slot], "AbortSubScript() argument 1: invalid subscript ID");
      LAssert(L, pobwindow->subScriptList[slot]->IsRunning(), "AbortSubScript(): subscript isn't running");
      ui_ISubScript::FreeHandle(pobwindow->subScriptList[slot]);
      pobwindow->subScriptList[slot] = NULL;
      return 0;
    */
}

static int l_IsSubScriptRunning(lua_State* L)
{
    std::cout << "SUBSCRIPT RUNNING STUB" << std::endl;
    return 0;
    /*
        int n = lua_gettop(L);
      LAssert(L, n >= 1, "Usage: IsSubScriptRunning(ssID)");
      LAssert(L, lua_islightuserdata(L, 1), "IsSubScriptRunning() argument 1: expected subscript ID, got %t", 1);
      notdword slot = (notdword)lua_touserdata(L, 1);
      LAssert(L, slot < pobwindow->subScriptSize && pobwindow->subScriptList[slot], "IsSubScriptRunning() argument 1: invalid subscript ID");
      lua_pushboolean(L, pobwindow->subScriptList[slot]->IsRunning());
      return 1;
    */
}

static int l_LoadModule(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: LoadModule(name[, ...])");
    LAssert(L, lua_isstring(L, 1), "LoadModule() argument 1: expected string, got %t", 1);
    QString fileName(lua_tostring(L, 1));
    if (!fileName.endsWith(".lua")) {
        fileName = fileName + ".lua";
    }
    QDir::setCurrent(pobwindow->scriptPath);
    int err = luaL_loadfile(L, fileName.toStdString().c_str());
    QDir::setCurrent(pobwindow->scriptWorkDir);
    LAssert(L, err == 0, "LoadModule() error loading '%s':\n%s", fileName.toStdString().c_str(), lua_tostring(L, -1));
    lua_replace(L, 1);	// Replace module name with module main chunk
    lua_call(L, n - 1, LUA_MULTRET);
    return lua_gettop(L);
}

static int l_PLoadModule(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: PLoadModule(name[, ...])");
    LAssert(L, lua_isstring(L, 1), "PLoadModule() argument 1: expected string, got %t", 1);
    QString fileName(lua_tostring(L, 1));
    if (!fileName.endsWith(".lua")) {
        fileName = fileName + ".lua";
    }
    QDir::setCurrent(pobwindow->scriptPath);
    int err = luaL_loadfile(L, fileName.toStdString().c_str());
    QDir::setCurrent(pobwindow->scriptWorkDir);
    if (err) {
        return 1;
    }
    lua_replace(L, 1);	// Replace module name with module main chunk
    //lua_getfield(L, LUA_REGISTRYINDEX, "traceback");
    //lua_insert(L, 1); // Insert traceback function at start of stack
    err = lua_pcall(L, n - 1, LUA_MULTRET, 0);
    if (err) {
        return 1;
    }
    lua_pushnil(L);
    lua_insert(L, 1);
//    lua_replace(L, 1); // Replace traceback function with nil
    return lua_gettop(L);
}

static int l_PCall(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: PCall(func[, ...])");
    LAssert(L, lua_isfunction(L, 1), "PCall() argument 1: expected function, got %t", 1);
    lua_pushcfunction(L, l_pcallErrorHandler);
    lua_insert(L, 1);
    int err = lua_pcall(L, n - 1, LUA_MULTRET, 1);
    lua_remove(L, 1);
    if (err) {
        std::cout << "PCall error: " << describeLuaError(err) << "\n";
        if (lua_isstring(L, 1)) {
          std::cout << lua_tostring(L, 1) << "\n";
        }
        invokeLuaDebugTraceback(L);
        return 1;
    }
    // no error is represented as nil in the first member of the returned tuple
    lua_pushnil(L);
    lua_insert(L, 1);
    return lua_gettop(L);
}

static int l_ConPrintf(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: ConPrintf(fmt[, ...])");
    LAssert(L, lua_isstring(L, 1), "ConPrintf() argument 1: expected string, got %t", 1);
    lua_pushvalue(L, lua_upvalueindex(1));	// string.format
    lua_insert(L, 1);
    lua_call(L, n, 1);
    LAssert(L, lua_isstring(L, 1), "ConPrintf() error: string.format returned non-string");
    std::cout << lua_tostring(L, 1) << std::endl;
    //pobwindow->sys->con->Printf("%s\n", lua_tostring(L, 1));
    return 0;
}

static void printTableItter(lua_State* L, int index, int level, bool recurse)
{
    lua_checkstack(L, 5);
    lua_pushnil(L);
    while (lua_next(L, index)) {
        for (int t = 0; t < level; t++) std::cout << "  ";
        // Print key
        if (lua_type(L, -2) == LUA_TSTRING) {
            std::cout << "[\"" << lua_tostring(L, -2) << "\"] = ";
        } else {
            lua_pushvalue(L, 2);	// Push tostring function
            lua_pushvalue(L, -3);	// Push key
            lua_call(L, 1, 1);		// Call tostring
            std::cout << lua_tostring(L, -1) << " = ";
            lua_pop(L, 1);			// Pop result of tostring
        }
        // Print value
        if (lua_type(L, -1) == LUA_TTABLE) {
            bool expand = recurse;
            if (expand) {
                lua_pushvalue(L, -1);	// Push value
                lua_gettable(L, 3);		// Index printed tables list
                expand = lua_toboolean(L, -1) == 0;
                lua_pop(L, 1);			// Pop result of indexing
            }
            if (expand) {
                lua_pushvalue(L, -1);	// Push value
                lua_pushboolean(L, 1);
                lua_settable(L, 3);		// Add to printed tables list
                std::cout << "table: " << lua_topointer(L, -1) << " {"
                          << std::endl;
                printTableItter(L, lua_gettop(L), level + 1, true);
                for (int t = 0; t < level; t++) std::cout << "  ";
                std::cout << "}" << std::endl;
            } else {
                std::cout << "table: " << lua_topointer(L, -1) << " { ... }\n";
            }
        } else if (lua_type(L, -1) == LUA_TSTRING) {
            std::cout << "\"" << lua_tostring(L, -1) << "\"" << std::endl;
        } else {
            lua_pushvalue(L, 2);	// Push tostring function
            lua_pushvalue(L, -2);	// Push value
            lua_call(L, 1, 1);		// Call tostring
            std::cout << lua_tostring(L, -1) << std::endl;
            lua_pop(L, 1);			// Pop result of tostring
        }
        lua_pop(L, 1);	// Pop value
    }
}

static int l_ConPrintTable(lua_State* L)
{
    return 0;
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: ConPrintTable(tbl[, noRecurse])");
    LAssert(L, lua_istable(L, 1), "ConPrintTable() argument 1: expected table, got %t", 1);
    bool recurse = lua_toboolean(L, 2) == 0;
    lua_settop(L, 1);
    lua_getglobal(L, "tostring");
    lua_newtable(L);		// Printed tables list
    lua_pushvalue(L, 1);	// Push root table
    lua_pushboolean(L, 1);
    lua_settable(L, 3);		// Add root table to printed tables list
    printTableItter(L, 1, 0, recurse);
    return 0;
}

static int l_ConExecute(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: ConExecute(cmd)");
    LAssert(L, lua_isstring(L, 1), "ConExecute() argument 1: expected string, got %t", 1);
    //pobwindow->sys->con->Execute(lua_tostring(L,1)); // FIXME
    return 0;
}

static int l_ConClear(lua_State* L)
{
//    pobwindow->sys->con->Clear();
    return 0;
}

static int l_print(lua_State* L)
{
    int n = lua_gettop(L);
    lua_getglobal(L, "tostring");
    for (int i = 1; i <= n; i++) {
        lua_pushvalue(L, -1);	// Push tostring function
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);		// Call tostring
        const char* s = lua_tostring(L, -1);
        LAssert(L, s != NULL, "print() error: tostring returned non-string");
        if (i > 1) std::cout << " ";
        std::cout << s;
        lua_pop(L, 1);			// Pop result of tostring
    }
    std::cout << std::endl;
    return 0;
}

static int l_SpawnProcess(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: SpawnProcess(cmdName[, args])");
    LAssert(L, lua_isstring(L, 1), "SpawnProcess() argument 1: expected string, got %t", 1);
    // FIXME
//    pobwindow->sys->SpawnProcess(lua_tostring(L, 1), lua_tostring(L, 2));
    return 0;
}

static int l_OpenURL(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: OpenURL(url)");
    LAssert(L, lua_isstring(L, 1), "OpenURL() argument 1: expected string, got %t", 1);
    // FIXME
    //pobwindow->sys->OpenURL(lua_tostring(L, 1));
    return 0;
}

static int l_SetProfiling(lua_State* L)
{
    int n = lua_gettop(L);
    LAssert(L, n >= 1, "Usage: SetProfiling(isEnabled)");
    // FIXME
    //pobwindow->debug->SetProfiling(lua_toboolean(L, 1) == 1);
    return 0;
}

static int l_Restart(lua_State* L)
{
    // FIXME
    //pobwindow->restartFlag = true;
    return 0;
}

static int l_Exit(lua_State* L)
{
    int n = lua_gettop(L);
    const char* msg = nullptr;
    if (n >= 1 && !lua_isnil(L, 1)) {
        LAssert(L, lua_isstring(L, 1), "Exit() argument 1: expected string or nil, got %t", 1);
        msg = lua_tostring(L, 1);
    }
    (void)msg;

    app->exit();

    return 0;
}

#define ADDFUNC(n) lua_pushcclosure(L, l_##n, 0);lua_setglobal(L, #n);
#define ADDFUNCCL(n, u) lua_pushcclosure(L, l_##n, u);lua_setglobal(L, #n);

void RegisterGeneralLuaCallbacks(lua_State* L)
{
    // LUA_PATH/LUA_CPATH
    {
        lua_getfield(L, LUA_GLOBALSINDEX, "package");
        lua_getfield(L, -1, "cpath");
        lua_getfield(L, -2, "path");

        QString cpath = pobwindow->basePath;
        QString path = pobwindow->basePath;
        if (lua_isstring(L, -2)) {
            cpath = cpath + "/?.so;" + lua_tostring(L, -2);
        }
        if (lua_isstring(L, -1)) {
            path = path + "/?.lua;" + lua_tostring(L, -1);
        }
        lua_pushstring(L, cpath.toUtf8().data());
        lua_setfield(L, -4, "cpath");
        lua_pushstring(L, path.toUtf8().data());
        lua_setfield(L, -4, "path");

        lua_pop(L, 3);
    }

    // General function
    ADDFUNC(SetWindowTitle);
    ADDFUNC(GetCursorPos);
    ADDFUNC(SetCursorPos);
    ADDFUNC(ShowCursor);
    ADDFUNC(IsKeyDown);
    ADDFUNC(Copy);
    ADDFUNC(Paste);
    ADDFUNC(Deflate);
    ADDFUNC(Inflate);
    ADDFUNC(GetTime);
    ADDFUNC(GetScriptPath);
    ADDFUNC(GetRuntimePath);
    ADDFUNC(GetUserPath);
    ADDFUNC(MakeDir);
    ADDFUNC(RemoveDir);
    ADDFUNC(SetWorkDir);
    ADDFUNC(GetWorkDir);
    ADDFUNC(LaunchSubScript);
    ADDFUNC(AbortSubScript);
    ADDFUNC(IsSubScriptRunning);
    ADDFUNC(LoadModule);
    ADDFUNC(PLoadModule);
    ADDFUNC(PCall);
    lua_getglobal(L, "string");
    lua_getfield(L, -1, "format");
    ADDFUNCCL(ConPrintf, 1);
    lua_pop(L, 1);		// Pop 'string' table
    ADDFUNC(ConPrintTable);
    ADDFUNC(ConExecute);
    ADDFUNC(ConClear);
    ADDFUNC(print);
    ADDFUNC(SpawnProcess);
    ADDFUNC(OpenURL);
    ADDFUNC(SetProfiling);
    ADDFUNC(Restart);
    ADDFUNC(Exit);
    lua_getglobal(L, "os");
    lua_pushcfunction(L, l_Exit);
    lua_setfield(L, -2, "exit");
    lua_pop(L, 1);		// Pop 'os' table

}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    app = new QGuiApplication(argc, argv);

    QStringList args = app->arguments();

    pobwindow = new POBWindow;

    if (args.size() > 1) {
        bool ok;
        int ff = args[1].toInt(&ok);
        if (ok) {
            pobwindow->fontFudge = ff;
            args.removeAt(1);
        }
    }

    L = luaL_newstate();
    installPanicHandler(L);
    luaL_openlibs(L);

    // Arguments
    lua_createtable(L, args.size(), 0);
    for (size_t i = 0; const auto& arg : args) {
      lua_pushstring(L, arg.toUtf8().constData());
      lua_rawseti(L, -2, i++);
    }
    lua_setfield(L, LUA_GLOBALSINDEX, "arg");

    // Callbacks
    lua_newtable(L);		// Callbacks table
    lua_pushvalue(L, -1);	// Push callbacks table
    ADDFUNCCL(SetCallback, 1);
    lua_pushvalue(L, -1);	// Push callbacks table
    ADDFUNCCL(GetCallback, 1);
    lua_pushvalue(L, -1);	// Push callbacks table
    ADDFUNCCL(SetMainObject, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, "uicallbacks");

    // Image handles
    lua_newtable(L);		// Image handle metatable
    lua_pushvalue(L, -1);	// Push image handle metatable
    ADDFUNCCL(NewImageHandle, 1);
    lua_pushvalue(L, -1);	// Push image handle metatable
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, l_imgHandleGC);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, l_imgHandleLoad);
    lua_setfield(L, -2, "Load");
    lua_pushcfunction(L, l_imgHandleUnload);
    lua_setfield(L, -2, "Unload");
    lua_pushcfunction(L, l_imgHandleIsValid);
    lua_setfield(L, -2, "IsValid");
    lua_pushcfunction(L, l_imgHandleIsLoading);
    lua_setfield(L, -2, "IsLoading");
    lua_pushcfunction(L, l_imgHandleSetLoadingPriority);
    lua_setfield(L, -2, "SetLoadingPriority");
    lua_pushcfunction(L, l_imgHandleImageSize);
    lua_setfield(L, -2, "ImageSize");
    lua_setfield(L, LUA_REGISTRYINDEX, "uiimghandlemeta");

    // Rendering
    ADDFUNC(RenderInit);
    ADDFUNC(GetScreenSize);
    ADDFUNC(SetClearColor);
    ADDFUNC(SetDrawLayer);
    ADDFUNC(SetViewport);
    ADDFUNC(SetDrawColor);
    ADDFUNC(DrawImage);
    ADDFUNC(DrawImageQuad);
    ADDFUNC(DrawString);
    ADDFUNC(DrawStringWidth);
    ADDFUNC(DrawStringCursorIndex);
    ADDFUNC(StripEscapes);
    ADDFUNC(GetAsyncCount);

    // Search handles
    lua_newtable(L);	// Search handle metatable
    lua_pushvalue(L, -1);	// Push search handle metatable
    ADDFUNCCL(NewFileSearch, 1);
    lua_pushvalue(L, -1);	// Push search handle metatable
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, l_searchHandleGC);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, l_searchHandleNextFile);
    lua_setfield(L, -2, "NextFile");
    lua_pushcfunction(L, l_searchHandleGetFileName);
    lua_setfield(L, -2, "GetFileName");
    lua_pushcfunction(L, l_searchHandleGetFileSize);
    lua_setfield(L, -2, "GetFileSize");
    lua_pushcfunction(L, l_searchHandleGetFileModifiedTime);
    lua_setfield(L, -2, "GetFileModifiedTime");
    lua_setfield(L, LUA_REGISTRYINDEX, "uisearchhandlemeta");

    RegisterGeneralLuaCallbacks(L);

    int result = luaL_dofile(L, "src/Launch.lua");
    if (result != 0) {
        lua_error(L);
    }

    pushCallback("OnInit");
    result = lua_pcall(L, 1, 0, 0);
    if (result != 0) {
        lua_error(L);
    }
    pobwindow->resize(800, 600);
    pobwindow->show();
    QFontDatabase::addApplicationFont("VeraMono.ttf");
    QFontDatabase::addApplicationFont("LiberationSans-Regular.ttf");
    QFontDatabase::addApplicationFont("LiberationSans-Bold.ttf");
    QFontDatabase::addApplicationFont("Fontin-Bold.ttf");
    QFontDatabase::addApplicationFont("Fontin-Italic.ttf");
    QFontDatabase::addApplicationFont("Fontin-Regular.ttf");
    QFontDatabase::addApplicationFont("Fontin-SmallCaps.ttf");
    return app->exec();
}

