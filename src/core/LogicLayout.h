#pragma once

#include "Blocks.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

// Pure geometry and palette rules for the Logic tab. Nothing here touches a file or a window, so it
// is tested without either. The numbers are in interface pixels and are read back by the QML.
namespace Studio {

namespace LogicMetrics {
inline constexpr double BlockWidth = 420;
inline constexpr double BlockPadding = 18;
inline constexpr double CanvasPadding = 32;
inline constexpr double ColumnGap = 48;
inline constexpr double EventGap = 52;
inline constexpr double RowHeight = 64;
inline constexpr double SecondRowHeight = 50;
inline constexpr double FooterHeight = 32;
inline constexpr double EmptyCavityHeight = 60;
inline constexpr double NestIndent = 38;
inline constexpr double ProblemHeight = 52; // extra room on a block that shows a problem
inline constexpr double SnapDistance = 150; // how far from a landing spot a dragged action still snaps
}

// A value edited inside a block. `kind` is one of item, variable, sound, button, comparison, number, whole, color.
struct BlockField {
    QString key;
    QString kind;
    QString label;
};

// Derived from the catalog's flags, so a new block type needs no change here.
QList<BlockField> blockFields(const BlockInfo& info);

// "events", "movement", "looks", "variables", "sound" or "saving". Every catalog entry has exactly one.
QString blockCategory(const BlockInfo& info);
QString blockCategoryTitle(const QString& category);

// True when a block's title and fields do not fit on one line of a block.
bool blockUsesTwoRows(const BlockInfo& info);

struct BlockPlacement {
    QString id;
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;       // the block's own shape, including what it wraps
    double headerHeight = 0;
    double cavityHeight = 0; // the part that wraps other blocks, 0 when it wraps none
    double footerHeight = 0;
    double problemHeight = 0; // part of headerHeight, shown at the bottom of the header
    double stackHeight = 0;  // for an event, the event and everything snapped under it
    bool twoRows = false;
    int runOrder = 0; // an event's position in the program, from 1
    int depth = 1;    // an event is 1, as in MaximumLogicDepth
};

struct LogicLayout {
    QList<BlockPlacement> placements; // in reading order
    QHash<QString, qsizetype> byId;
    double contentWidth = 0;
    double contentHeight = 0;
    int columns = 1;

    const BlockPlacement* find(const QString& id) const {
        const auto found = byId.find(id);
        return found == byId.end() ? nullptr : &placements[*found];
    }
};

// `problems` holds the ids of blocks that show a problem, which need a taller header.
LogicLayout layoutLogic(const LogicProgram& program, double canvasWidth, const QSet<QString>* problems = nullptr);

// Where a dragged block or stack could land. `parentId` is empty for the event list.
struct LogicLanding {
    QString parentId;
    int index = 0;
    double x = 0;
    double y = 0;
    double width = LogicMetrics::BlockWidth;
    bool isEvent = false;
};

// Spots in a program the dragged blocks were already taken out of. `stackDepth` is the levels the
// dragged blocks add below their landing spot; nothing that would pass MaximumLogicDepth is offered.
QList<LogicLanding> landingSpots(const LogicProgram& program, const LogicLayout& layout, bool draggingEvent, int stackDepth);

// The closest spot to a block carried with its top left corner at (x, y). Events can land anywhere;
// actions need to be within SnapDistance. Returns nullptr when nothing is close enough.
const LogicLanding* nearestLanding(const QList<LogicLanding>& spots, double x, double y);

}
