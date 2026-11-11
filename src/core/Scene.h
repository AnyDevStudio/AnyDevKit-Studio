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
// The screen the game draws is always this big. A world can be larger, never smaller.
inline constexpr int ScreenWidthUnits = 1920;
inline constexpr int ScreenHeightUnits = 1080;
// World coordinates stay well inside what a float can count exactly.
inline constexpr int MaximumWorldSide = 32768;
// A scene with tilemaps or sprites uses at most this many draw calls; each one rebinds a texture.
inline constexpr int MaximumSceneDrawCalls = 64;
inline constexpr int TiltedPictureQuads = 270; // World2D::TiltStrips: a flat picture in a tilted scene is drawn as thin rows
inline constexpr int MaximumSceneTilt = 70; // beyond this the top of the screen sees too far to look like ground
// Tile limits. Every tile on the screen is one quad, counted against MaximumSceneBlocks.
inline constexpr int MinimumTileDimension = 4;
inline constexpr int MaximumTileDimension = 256;
inline constexpr int MaximumTileScale = 16;
inline constexpr int MaximumTilemapSide = 512;
inline constexpr int MaximumSceneTileCells = 1048576;
inline constexpr int MaximumTileIndex = 65535;
// Scene limits. Budgets apply to each scene on its own.
inline constexpr int MaximumScenes = 32;
inline constexpr int MaximumSceneNameLength = 40;
// Physics limits. The game checks every moving body against the solids of its scene, so a scene is held to this.
inline constexpr int MaximumSceneBodies = 128;
inline constexpr int MaximumSceneSolidItems = 256;
inline constexpr int MaximumBodySide = 2048;
inline constexpr int MaximumGravityScale = 300;
inline constexpr int MinimumMoveSpeed = 60;
inline constexpr int MaximumMoveSpeed = 3000;
inline constexpr int MinimumJumpHeight = 16;
inline constexpr int MaximumJumpHeight = 2000;
inline constexpr int DefaultGravityScale = 100; // percent; a beginner's character feels right here
inline constexpr int DefaultMoveSpeed = 600;    // units per second
inline constexpr int DefaultJumpHeight = 260;   // units
// Sprite sheet limits.
inline constexpr int MaximumAnimationsPerSprite = 16;
inline constexpr int MaximumFramesPerAnimation = 256;
inline constexpr int MaximumFramesPerSecond = 60;
inline constexpr int MaximumLooks = 64;
// A variable name is short and simple, so it reads well inside text and blocks.
inline constexpr int MaximumVariableNameLength = 24;
// The widest a whole number can be: the sign and all ten digits of -2147483648.
inline constexpr int MaximumVariableCharacters = 11;
// A scene holds at most this many items, every copy of a template counted.
inline constexpr int MaximumSceneItems = 1024;
// How many copies one template can make, and how many players a game can have.
inline constexpr int MaximumCopies = 256;
inline constexpr int MaximumPlayers = 4;

// A named run of frames on a sprite sheet.
struct SceneAnimation {
    QString name;
    QList<int> frames; // frame numbers on the sheet, left to right then top to bottom, starting at 0
    int fps = 8;
    bool operator==(const SceneAnimation&) const = default;
};

struct SceneItem {
    QString id;
    QString type; // "rectangle", "text", "image", "particles" or "tilemap"
    QString name;
    double x = 0, y = 0, width = 0, height = 0;
    QString color = "#FFD23F";
    int scale = 8;
    QString text;
    QString source; // image and tilemap items: project-relative path under assets/
    bool controllable = false;
    bool screenFixed = false; // true: x and y are on the screen and the camera does not move it
    // Image items only. pixelated draws with nearest-neighbour sampling; a sheet is a sprite.
    bool pixelated = false;
    bool flipX = false;
    int frameWidth = 0; // 0 means a plain picture, not a sheet
    int frameHeight = 0;
    QList<SceneAnimation> animations;
    QString animation; // the one that plays from the start; empty means the first
    int looks = 1;     // a sheet can hold several looks of the same frames, each under the last; Set look picks one
    // Tilemap items only. width and height are derived; see updateTilemapSize.
    int tileWidth = 16, tileHeight = 16;
    int tileScale = 4; // drawn size of a tile is tileWidth * tileScale world units
    int columns = 0, rows = 0;
    QList<int> tiles; // columns * rows numbers, row by row, top row first; -1 is empty
    QList<int> solidTiles; // tile numbers that block moving bodies, sorted and unique
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
    // Physics: empty for none, "solid" for a wall or platform, "body" for something that moves and collides.
    // Rectangles and images only. The numbers below only matter for a body.
    QString physics;
    int gravityScale = DefaultGravityScale; // percent of normal gravity
    bool platformer = false;                // moves with the stick or D-pad, jumps on Cross
    bool topDown = false;                   // walks in every direction with the stick or D-pad, with no gravity
    int player = 1;                         // whose pad drives a body's controls: 1 to MaximumPlayers
    bool hiddenWhileEditing = false;        // starts hidden on the editing stage; the game still shows it
    bool upright = false;                   // stands up from the ground in a tilted scene, in depth order
    double drawWidth = 0, drawHeight = 0;   // an upright item's picture size; 0 means its width or height
    int copies = 0;                         // 0: an ordinary item; 1 or more: a template the game makes that many copies of, all hidden until a block spawns one
    QString follows;                        // an item this one stays on, at followX and followY from its top left, shown only while it is
    double followX = 0, followY = 0;
    int copyNumber = -1;                    // set by expandCopies on each copy, 0 for the first; never saved
    int moveSpeed = DefaultMoveSpeed;       // units per second
    int jumpHeight = DefaultJumpHeight;     // units
};

// One scene: its own items and world. A project is a list of scenes.
struct Scene {
    QString id = "scene-1";
    QString name = "Scene 1";
    QString background = "#151C3A";
    int worldWidth = ScreenWidthUnits;  // the area the camera can show; older scenes are stage-sized
    int worldHeight = ScreenHeightUnits;
    double cameraX = 0; // where the camera starts, its top left corner in the world
    double cameraY = 0;
    int tilt = 0;                           // percent, 0 is the flat 2D camera
    QList<SceneItem> items;
};

// Every scene of a project, in the order the Scene tab lists them. Logic belongs per scene; variables are
// shared by all of them (see Blocks.h).
struct SceneSet {
    QList<Scene> scenes;
    QString startScene; // id of the scene the game starts in
};

// A named whole number. The list lives in logic.json (see Blocks.h); it is declared here because a
// text item can show one, and the scene needs nothing else from the logic.
// Game variables have one value. Player variables have one value for each player, and blocks use the current
// player's. Copy variables have one value for each copy of a template, and blocks inside a copy's event use that copy's.
// A screen variable is a game variable with one value for each screen; online, each friend has their own screen.
enum class VariableScope { Game, Player, Copy, Screen };
// Screen variables are kept, numbered and saved with the game variables.
constexpr VariableScope storedScope(VariableScope scope) { return scope == VariableScope::Screen ? VariableScope::Game : scope; }

struct LogicVariable {
    QString name;
    int initial = 0;
    VariableScope scope = VariableScope::Game;
};

// A text can show a player variable for one player with {name@2}; plain {name} shows the first player's.

// Letters, digits and _, not starting with a digit, at most MaximumVariableNameLength long.
bool isVariableName(const QString& name);

// A text item's `text` is plain characters, {{ and }} for literal braces, and {name} to show a variable.
struct TextPart {
    QString text;     // plain text with no escapes left, when variable is empty
    QString variable; // the name, when this part shows a variable
    int player = 0;   // {name@2} shows player 2's value of a player variable; 0 when no player is named
    bool letter = false; // {name:letter} shows 1 to 26 as A to Z, 27 to 36 as 0 to 9, and anything else as a space
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

struct SceneSetLoadResult {
    std::optional<SceneSet> set;
    QString error;
};

// Reads scene.json. A file from before several scenes loads as one scene, so older projects keep working.
SceneSetLoadResult loadSceneSet(const QString& path);
// The scene the game starts in, from a file with one or several scenes.
SceneLoadResult loadScene(const QString& path);
bool saveSceneSet(const QString& path, const SceneSet& set, QString& error);
bool saveScene(const QString& path, const Scene& scene, QString& error);
QString validateSceneSet(const SceneSet& set); // empty string when valid
// How many flat quads the game draws for this scene: the background, every item and every particle.
int sceneQuadCount(const Scene& scene);
QStringList particlePresetNames();
QStringList particlePaletteNames();
// Gives `item` the preset's beginner settings and its usual area, keeping its name, id and position.
void applyParticlePreset(SceneItem& item, const QString& preset);
bool isSpriteSheet(const SceneItem& item); // an image item cut into frames
// Sets a tilemap's width and height from its cells, tile size and scale.
void updateTilemapSize(SceneItem& item);
// Changes a tilemap's size in cells, keeping every tile that still fits and leaving new cells empty.
void resizeTilemap(SceneItem& item, int columns, int rows);
// How many tile quads a tilemap can ever put on the screen: what the screen fits, at most the whole map.
int tilemapReservedQuads(const SceneItem& item);
// How many tiles all the scene's tilemaps hold; each one goes into the game as data.
int sceneTileCells(const Scene& scene);
// How many draw calls the game makes for this scene. Runs of items that share a texture and sampling are one draw.
int sceneDrawCalls(const Scene& scene);
// A row of a tilemap as text: "-1*3,5*2,2", value or value*count, comma separated.
QString encodeTileRow(const QList<int>& tiles, int first, int count);
bool decodeTileRow(const QString& text, int columns, QList<int>& out);
// The tiles the runtime reads: columns * rows little-endian 32-bit cells.
QByteArray tilemapCellBytes(const SceneItem& item);
// Empty when every sheet frame and tile fits its image. Needs decoded images, so it runs at export.
QString sceneImagesProblem(const Scene& scene, const QList<SceneImageData>& images);
QString validateScene(const Scene& scene); // empty string when valid
// The set with each template replaced by its copies, in place: the first keeps the template's id, the others
// are named id~2, id~3 and so on. A copy that follows a template with copies follows the copy with its number.
SceneSet expandCopies(const SceneSet& set);
Scene starterScene(); // a 2D game: a player the controller moves and a title
Scene appStarterScene(); // an app: a header title and labeled buttons, nothing moves
QStringList sceneImageSources(const Scene& scene); // each image's source once, in the order first used
QStringList sceneImageSources(const SceneSet& set); // the same across every scene, scenes in list order
// How much of `set` a scene owns in the generated tables: items and generated tilemaps are numbered across all scenes.
int sceneItemCount(const SceneSet& set);
// Empty when the scene is invalid or `images` does not hold exactly the scene's sources, in that order.
QByteArray generateSceneHeader(const Scene& scene, const QList<SceneImageData>& images = {});
QByteArray generateSceneHeader(const SceneSet& set, const QList<SceneImageData>& images = {});
QByteArray textLiteral(const QString& text); // the text as the contents of a C++ string literal
// src/GeneratedImages.cpp, which also pulls in tilemapCount tile files named tilemap-N.tiles.
QByteArray generateImageSource(const QList<SceneImageData>& images, int tilemapCount = 0);
QString imageSizeProblem(int width, int height, qint64 bytesSoFar); // empty when the image fits the limits
QString imageFileProblem(const QString& projectRoot, const QString& source); // empty when it is a regular file we may read
bool writeGeneratedScene(const QString& projectRoot, const Scene& scene, const QList<SceneImageData>& images, QString& error);
bool writeGeneratedScene(const QString& projectRoot, const SceneSet& set, const QList<SceneImageData>& images, QString& error);
// A project that already has a scene.json keeps it, so `starter` is only written when there is none.
bool installSceneRuntime(const QString& runtimeDirectory, const QString& projectRoot, const QString& projectName, QString& error, const Scene& starter = starterScene());
// True when the project has game code from a Studio that predates variables. It builds fine until a
// project starts using them, so a project without variables is left alone.
bool sceneRuntimeNeedsUpdate(const QString& projectRoot);
// True when the project has a runtime from before the camera, sprites and tilemaps.
bool sceneRuntimeLacksWorld(const QString& projectRoot);
// The same, for a scene that uses a tilemap, a sprite sheet, a camera or anything else added with them.
bool sceneRuntimeNeedsUpdate(const QString& projectRoot, const Scene& scene);
// True when the project has game code from before physics and several scenes.
bool sceneRuntimeLacksPhysics(const QString& projectRoot);
bool sceneSetUsesPhysics(const SceneSet& set); // any solid or body, or more than one scene

}
