#pragma once

#include "Scene.h"
#include "Sounds.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

// Jigsaw style game logic. A program is a list of named variables and a tree of typed blocks: events
// at the top level, actions inside them, and more actions inside "If variable" blocks. Every block has
// an id that stays the same across saves, so an editor can select, reorder or draw a block as a graph
// node. The format is described in docs/blocks-format.md.
//
// How an editor uses this file:
//   1. loadLogic(path) to read logic.json, or start from an empty LogicProgram.
//   2. blockCatalog() for the palette. newBlockId() for the id of a block being added.
//   3. validateLogic(program, scene) after each change, and show each problem on its block.
//   4. saveLogic(path, program, error). A program may be saved while unfinished: a block with no
//      item chosen, or an item that has left the scene, is reported by validateLogic only.
//   5. exportLogicHeader(projectRoot, scene, error) before a build. Nothing else is needed to run.
namespace Studio {

enum class BlockType {
    // Events: top level only, and each holds actions.
    WhenGameStarts,
    WhileButtonHeld,
    EveryFrame,
    WhenItemsTouch,
    // Actions: inside an event or an IfVariable.
    MoveBy,
    SetPosition,
    Show,
    Hide,
    SetColor,
    SetVariable,
    ChangeVariable,
    IfVariable, // also holds actions
    BurstParticles, // the item must be a particles item
    PlaySound,
    PlayMusic,
    StopMusic,
    SetVolume,
    SaveVariables, // keeps every variable on the console until LoadVariables
    LoadVariables,
    PlayAnimation,  // the item must be a sprite sheet
    FlipItem,       // the item must be an image
    CameraFollow,   // the item must not be screen-fixed
    SetCameraPosition,
    // Physics events: top level only, and each holds actions. The item must be a moving body.
    WhenItemLands,
    WhenItemHitsSolid,
    WhenItemLeavesWorld, // falling into a pit, past the bottom of the world
    // Physics and scene actions.
    SetVelocity, // units per second; the item must be a moving body
    JumpItem,    // as the jump button does; the item must be a moving body
    IfOnGround,  // also holds actions; the item must be a moving body
    GoToScene,
    RestartScene,
};

enum class ControllerButton { Cross, Circle, Square, Triangle, DpadUp, DpadDown, DpadLeft, DpadRight };

enum class VariableComparison { Equal, NotEqual, Less, LessOrEqual, Greater, GreaterOrEqual };

// Only the fields the type uses matter; blockInfo says which those are. The others are not saved.
struct Block {
    QString id;
    BlockType type = BlockType::EveryFrame;

    ControllerButton button = ControllerButton::Cross; // WhileButtonHeld
    QString item;                                      // scene item id; WhenItemsTouch's first item
    QString otherItem;                                 // WhenItemsTouch's second item
    double x = 0, y = 0;                               // MoveBy (distance), SetPosition (place)
    QString color = "#FFD23F";                         // SetColor
    QString variable;                                  // SetVariable, ChangeVariable, IfVariable: a variable's name
    QString sound;                                     // PlaySound, PlayMusic: a sound's name
    int value = 0;                                     // SetVariable's value, ChangeVariable's amount, IfVariable's number, SetVolume's percent
    VariableComparison comparison = VariableComparison::Equal; // IfVariable
    QString animation;                                 // PlayAnimation: an animation's name
    bool flipped = true;                               // FlipItem: true faces left, false faces right
    QString scene;                                     // GoToScene: a scene's id

    QList<Block> children; // the actions inside an event or an IfVariable, run in order
};

// Logic belongs per scene. An editor keeps the scene it shows in `events`, and the others here by id.
struct SceneLogic {
    QString scene;
    QList<Block> events;
};

struct LogicProgram {
    QList<LogicVariable> variables; // in the order they are listed in the Variables area
    QList<ProjectSound> sounds;     // in the order they are listed in the Sounds area
    QList<Block> events; // run from first to last
    QString scene; // the scene `events` belongs to; empty means the project's first scene, as in older files
    QList<SceneLogic> otherScenes; // every other scene's logic; ids are unique across the whole program
};

// What the palette and the validation messages need to know about a block type.
struct BlockInfo {
    BlockType type;
    QString jsonName; // the "type" value in logic.json
    QString title;    // shown to the user, for example "Move item"
    bool isEvent;
    bool holdsActions;
    bool usesButton;
    bool usesItem;
    bool usesOtherItem;
    bool usesPosition;
    bool usesColor;
    bool usesValue;
    bool usesComparison;
    bool usesVariable;
    bool usesSound;
    bool usesAnimation = false;
    bool usesFlipped = false;
    bool usesScene = false;
};

struct ButtonInfo {
    ControllerButton button;
    QString jsonName;
    QString title;
};

struct ComparisonInfo {
    VariableComparison comparison;
    QString jsonName;
    QString title;
};

const QList<BlockInfo>& blockCatalog(); // every block type, in palette order
const BlockInfo& blockInfo(BlockType type);
const QList<ButtonInfo>& buttonCatalog();
const QList<ComparisonInfo>& comparisonCatalog();

inline constexpr int MaximumLogicEvents = 128;
inline constexpr int MaximumLogicBlocks = 2048; // events and actions together
inline constexpr int MaximumLogicDepth = 8;     // an event is level 1, its actions level 2
inline constexpr int MaximumBlockIdLength = 64;
inline constexpr double MaximumLogicCoordinate = 1000000; // both for positions and for moves
inline constexpr int MaximumVariableValue = 1000000000; // the largest a block or an initial value can say
inline constexpr int MaximumLogicVariables = 64;
inline constexpr int MaximumVolumePercent = 100;
inline constexpr qint64 MaximumLogicFileBytes = 64 * 1024 * 1024; // every scene together

// One thing to fix. blockId is empty when the problem is the program as a whole.
struct LogicProblem {
    QString blockId;
    QString message;
};

struct LogicLoadResult {
    std::optional<LogicProgram> program;
    QString error;
};

// Reads logic.json. Missing or unchosen scene items and variables are not an error here, so an
// unfinished program can still be opened and repaired; validateLogic reports them. A schema 1 file's
// score blocks are read as variable blocks using a variable named score.
LogicLoadResult loadLogic(const QString& path);

// Replaces the file only after the whole new contents are written. Fails when the structure is
// invalid (validateLogicStructure), and leaves any existing file untouched.
bool saveLogic(const QString& path, const LogicProgram& program, QString& error);

// Rules that need no scene: variables, ids, nesting, limits, number ranges and colors.
QList<LogicProblem> validateLogicStructure(const LogicProgram& program);

// Everything a program must satisfy before it can be built, including that each item exists. The
// program's `events` are checked against `scene`; a Go to scene block is checked against `sceneIds` when given.
QList<LogicProblem> validateLogic(const LogicProgram& program, const Scene& scene, const QStringList& sceneIds = {});

// The events of one scene of the program, and whether a scene is the project's first one. Older files have
// events with no scene named, and they belong to the first scene.
QList<Block> eventsOfScene(const LogicProgram& program, const QString& sceneId, bool isFirstScene);

// The whole game's logic: every scene's checks and generated code. A problem names the scene when there are several.
QList<LogicProblem> validateGameLogic(const LogicProgram& program, const SceneSet& set);

// That every {name} in the scene's text is a variable of the program. One problem per missing name
// per item, in scene order, with an empty blockId. Separate from validateLogic because a text item
// is not a block: the editor shows these at build, naming the item.
QList<LogicProblem> validateTextBindings(const Scene& scene, const LogicProgram& program);

// A block id no block in the program uses yet.
QString newBlockId(const LogicProgram& program);

// The contents of src/GeneratedLogic.hpp. Empty when the program or the scene is invalid.
QByteArray generateLogicHeader(const LogicProgram& program, const Scene& scene);
QByteArray generateLogicHeader(const LogicProgram& program, const SceneSet& set);

// The header for a project with no logic. It depends on nothing else in the project.
QByteArray generateEmptyLogicHeader();

// The contents of src/GeneratedVariables.hpp: the variables' initial values, their name hashes for save
// files, whether the program saves or loads, and how each text item that shows variables is built. Empty when the program, the scene or a text binding is invalid.
QByteArray generateVariablesHeader(const LogicProgram& program, const Scene& scene);
QByteArray generateVariablesHeader(const LogicProgram& program, const SceneSet& set);

// Writes src/GeneratedLogic.hpp, src/GeneratedVariables.hpp and the sound files for the project at
// projectRoot from its logic.json, or the empty headers and an empty sound table when there is no
// logic.json. Fails with a message for the user when the logic, a text binding or a sound has a
// problem, and then keeps the previous headers.
bool exportLogicHeader(const QString& projectRoot, const Scene& scene, QString& error);
bool exportLogicHeader(const QString& projectRoot, const SceneSet& set, QString& error);

// True when a program uses a block that needs game code from this version of the runtime.
bool programUsesWorldBlocks(const LogicProgram& program);

// True when a program uses a physics or scene block.
bool programUsesPhysicsBlocks(const LogicProgram& program);

}
