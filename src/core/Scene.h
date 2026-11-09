#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace Studio {

// A scene with particles draws at most this many flat quads, background and every item included.
// The PixelFireworks demo held about 119 FPS with 16,000 quads in AnyPS5, so that is the budget.
inline constexpr int MaximumSceneBlocks = 16000;

struct SceneItem {
    QString id;
    QString type; // "rectangle", "text", "image" or "particles"
    QString name;
    double x = 0, y = 0, width = 0, height = 0;
    QString color = "#FFD23F";
    int scale = 8;
    QString text;
    QString source; // image items: project-relative path under assets/
    bool controllable = false;
    // Particles items only. width and height are the area the effect happens in. The ranges are in
    // docs/scene-format.md.
    QString preset = "fireworks";
    QString palette = "color";
    int amount = 400;
    int spread = 50;
    int speed = 100;
    int gravity = 100;
    int pixelSize = 8;
    bool looping = true;
};

struct Scene {
    QString background = "#151C3A";
    QList<SceneItem> items;
};

// One decoded image for the generated code. Row by row, top row first, no padding, 4 bytes per
// pixel: red, green, blue, alpha, with the color already multiplied by alpha.
struct SceneImageData {
    QString source;
    int width = 0;
    int height = 0;
    QByteArray pixels;
};

struct SceneLoadResult {
    std::optional<Scene> scene;
    QString error;
};

SceneLoadResult loadScene(const QString& path);
bool saveScene(const QString& path, const Scene& scene, QString& error);
// How many flat quads the game draws for this scene: the background, every item and every particle.
int sceneQuadCount(const Scene& scene);
QStringList particlePresetNames();
QStringList particlePaletteNames();
// Gives `item` the preset's beginner settings and its usual area, keeping its name, id and position.
void applyParticlePreset(SceneItem& item, const QString& preset);
QString validateScene(const Scene& scene); // empty string when valid
QStringList sceneImageSources(const Scene& scene); // each image's source once, in the order first used
// Empty when the scene is invalid or `images` does not hold exactly the scene's sources, in that order.
QByteArray generateSceneHeader(const Scene& scene, const QList<SceneImageData>& images = {});
QByteArray generateImageSource(const QList<SceneImageData>& images); // src/GeneratedImages.cpp
QString imageSizeProblem(int width, int height, qint64 bytesSoFar); // empty when the image fits the limits
QString imageFileProblem(const QString& projectRoot, const QString& source); // empty when it is a regular file we may read
bool writeGeneratedScene(const QString& projectRoot, const Scene& scene, const QList<SceneImageData>& images, QString& error);
bool installSceneRuntime(const QString& runtimeDirectory, const QString& projectRoot, const QString& projectName, QString& error);

}
