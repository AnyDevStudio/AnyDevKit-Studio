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
// The largest an image may be on each side. The editor scales bigger pictures down to this.
inline constexpr int MaximumImageDimension = 4096;
// A variable name is short and simple, so it reads well inside text and blocks.
inline constexpr int MaximumVariableNameLength = 24;
// The widest a whole number can be: the sign and all ten digits of -2147483648.
inline constexpr int MaximumVariableCharacters = 11;

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

// A named whole number. The list lives in logic.json (see Blocks.h); it is declared here because a
// text item can show one, and the scene needs nothing else from the logic.
struct LogicVariable {
    QString name;
    int initial = 0;
};

// Letters, digits and _, not starting with a digit, at most MaximumVariableNameLength long.
bool isVariableName(const QString& name);

// A text item's `text` is plain characters, {{ and }} for literal braces, and {name} to show a variable.
struct TextPart {
    QString text;     // plain text with no escapes left, when variable is empty
    QString variable; // the name, when this part shows a variable
};

struct TextTemplate {
    QList<TextPart> parts;
    QString problem; // empty when the text is well formed
};

TextTemplate parseTextTemplate(const QString& text);
bool textUsesVariables(const QString& text);
QStringList textVariables(const QString& text); // each name it shows, once, in the order first used
// What the stage shows before the game runs: variables with their initial values and braces unescaped.
// An unknown variable stays as typed, and so does a text that is not well formed.
QString displayedText(const QString& text, const QList<LogicVariable>& variables);
QString renameVariableInText(const QString& text, const QString& name, const QString& newName);

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
Scene starterScene(); // a 2D game: a player the controller moves and a title
Scene appStarterScene(); // an app: a header title and labeled buttons, nothing moves
QStringList sceneImageSources(const Scene& scene); // each image's source once, in the order first used
// Empty when the scene is invalid or `images` does not hold exactly the scene's sources, in that order.
QByteArray generateSceneHeader(const Scene& scene, const QList<SceneImageData>& images = {});
QByteArray textLiteral(const QString& text); // the text as the contents of a C++ string literal
QByteArray generateImageSource(const QList<SceneImageData>& images); // src/GeneratedImages.cpp
QString imageSizeProblem(int width, int height, qint64 bytesSoFar); // empty when the image fits the limits
QString imageFileProblem(const QString& projectRoot, const QString& source); // empty when it is a regular file we may read
bool writeGeneratedScene(const QString& projectRoot, const Scene& scene, const QList<SceneImageData>& images, QString& error);
// A project that already has a scene.json keeps it, so `starter` is only written when there is none.
bool installSceneRuntime(const QString& runtimeDirectory, const QString& projectRoot, const QString& projectName, QString& error, const Scene& starter = starterScene());
// True when the project has game code from a Studio that predates variables. It builds fine until a
// project starts using them, so a project without variables is left alone.
bool sceneRuntimeNeedsUpdate(const QString& projectRoot);

}
