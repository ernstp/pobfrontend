#include <memory>

#include <QCache>
#include <QDir>
#include <QHash>
#include <QOpenGLWindow>
#include <QPainter>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <unordered_map>

#include "main.h"
#include "qevent.h"
#include "src/texture_loader.hpp"
#include "subscript.hpp"
#include "lazy_loaded_texture.hpp"

class POBWindow : public QOpenGLWindow {
    Q_OBJECT
public:
    POBWindow() : stringCache(), textureCache(12) {
        QString AppDataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        scriptPath = QDir::currentPath() + "/src";
        scriptWorkDir = QDir::currentPath() + "/src";
        basePath = QDir::currentPath();
        userPath = AppDataLocation;

        fontFudge = -2;

        connect(&repaintTimer, &QTimer::timeout, this, QOverload<>::of(&QOpenGLWindow::update));

        currentLayer = &layers[{0, 0}];

        textureIndexByPath.reserve(200);
        lazyLoadedTexture.append({
            .index = 0,
            .path = "<none>",
            .size = { 1, 1 },
            .state = LoadState::Loaded,
            });

        textureLoader.start();
    }

    ~POBWindow();

    void initializeGL();
    void resizeGL(int w, int h);
    void paintGL();

    void subScriptFinished();
    void mouseMoveEvent(QMouseEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void mouseDoubleClickEvent(QMouseEvent *event);
    void wheelEvent(QWheelEvent *event);
    void keyPressEvent(QKeyEvent *event);
    void keyReleaseEvent(QKeyEvent *event);
    void closeEvent(QCloseEvent *event);

    LazyLoadedTexture& GetLazyLoadedTexture(const QString& path);
    LazyLoadedTexture& GetLazyLoadedTexture(TextureIndex index);
    QOpenGLTexture& GetTexture(TextureIndex index);
    bool RetrieveLoadedTextures();

    int IsUserData(lua_State* L, int index, const char* metaName);

    void SetDrawLayer(int layer);
    void SetDrawLayer(int layer, int subLayer);
    void SetDrawSubLayer(int subLayer) {
        SetDrawLayer(curLayer, subLayer);
    }
    void AppendCmd(std::unique_ptr<Cmd> cmd);
    void DrawColor(const float col[4] = NULL);
    void DrawColor(uint32_t col);

    QString scriptPath;
    QString scriptWorkDir;
    QString basePath;
    QString userPath;
    int curLayer;
    int curSubLayer;
    int fontFudge;
    int width;
    int height;
    bool isDrawing;
    QString fontName;
    float drawColor[4];

    TextureLoader textureLoader;
    QList<std::shared_ptr<SubScript>> subScriptList;

    std::map<QPair<int, int>, std::vector<std::unique_ptr<Cmd>>> layers;
    std::vector<std::unique_ptr<Cmd>>* currentLayer = nullptr;
    std::vector<std::pair<TextureIndex, std::unique_ptr<QImage>>> tmpLoadedTextures;
    std::unique_ptr<QOpenGLTexture> white;
    QHash<QString, TextureIndex> textureIndexByPath;
    QSet<size_t> uniqueTextureDrawn;
    QList<LazyLoadedTexture> lazyLoadedTexture;
    std::unordered_map<QString, std::shared_ptr<QOpenGLTexture>> stringCache;
    QCache<size_t, QOpenGLTexture> textureCache;
    QTimer repaintTimer;
};

extern POBWindow* pobwindow;
extern int dscount;
