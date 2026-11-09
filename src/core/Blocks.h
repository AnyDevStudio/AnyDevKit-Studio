#pragma once

#include "Scene.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

// Jigsaw style game logic. A program is a tree of typed blocks: events at the top level, actions
// inside them, and more actions inside "If score" blocks. Every block has an id that stays the same
// across saves, so an editor can select, reorder or draw a block as a graph node. The format is
// described in docs/blocks-format.md.
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
    // Actions: inside an event or an IfScore.
    MoveBy,
    SetPosition,
    Show,
    Hide,
    SetColor,
    AddScore,
    SetScore,
    IfScore, // also holds actions
};

enum class ControllerButton { Cross, Circle, Square, Triangle, DpadUp, DpadDown, DpadLeft, DpadRight };

enum class ScoreComparison { Equal, NotEqual, Less, LessOrEqual, Greater, GreaterOrEqual };

// Only the fields the type uses matter; blockInfo says which those are. The others are not saved.
struct Block {
    QString id;
    BlockType type = BlockType::EveryFrame;

    ControllerButton button = ControllerButton::Cross; // WhileButtonHeld
    QString item;                                      // scene item id; WhenItemsTouch's first item
    QString otherItem;                                 // WhenItemsTouch's second item
    double x = 0, y = 0;                               // MoveBy (distance), SetPosition (place)
    QString color = "#FFD23F";                         // SetColor
    int value = 0;                                     // AddScore, SetScore, IfScore's number
    ScoreComparison comparison = ScoreComparison::Equal; // IfScore

    QList<Block> children; // the actions inside an event or an IfScore, run in order
};

struct LogicProgram {
    QList<Block> events; // run from first to last
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
};

struct ButtonInfo {
    ControllerButton button;
    QString jsonName;
    QString title;
};

struct ComparisonInfo {
    ScoreComparison comparison;
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
inline constexpr int MaximumLogicScore = 1000000000;
inline constexpr qint64 MaximumLogicFileBytes = 4 * 1024 * 1024;

// One thing to fix. blockId is empty when the problem is the program as a whole.
struct LogicProblem {
    QString blockId;
    QString message;
};

struct LogicLoadResult {
    std::optional<LogicProgram> program;
    QString error;
};

// Reads logic.json. Missing or unchosen scene items are not an error here, so an unfinished program
// can still be opened and repaired; validateLogic reports them.
LogicLoadResult loadLogic(const QString& path);

// Replaces the file only after the whole new contents are written. Fails when the structure is
// invalid (validateLogicStructure), and leaves any existing file untouched.
bool saveLogic(const QString& path, const LogicProgram& program, QString& error);

// Rules that need no scene: ids, nesting, limits, number ranges and colors.
QList<LogicProblem> validateLogicStructure(const LogicProgram& program);

// Everything a program must satisfy before it can be built, including that each item exists.
QList<LogicProblem> validateLogic(const LogicProgram& program, const Scene& scene);

// A block id no block in the program uses yet.
QString newBlockId(const LogicProgram& program);

// The contents of src/GeneratedLogic.hpp. Empty when the program or the scene is invalid.
QByteArray generateLogicHeader(const LogicProgram& program, const Scene& scene);

// The header for a project with no logic. It depends on nothing else in the project.
QByteArray generateEmptyLogicHeader();

// Writes src/GeneratedLogic.hpp for the project at projectRoot from its logic.json, or the empty
// header when there is no logic.json. Fails with a message for the user when the logic has a
// problem, and then keeps the previous header.
bool exportLogicHeader(const QString& projectRoot, const Scene& scene, QString& error);

}
