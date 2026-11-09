#include "LogicLayout.h"

#include <algorithm>
#include <cmath>

namespace Studio {

namespace {

using namespace LogicMetrics;

constexpr double CharacterWidth = 8.4; // average width of a character in a block title
constexpr double GlyphWidth = 32;      // the group mark and its gap before the title
constexpr double OrderChipWidth = 36;  // an event's run number
constexpr double FieldGap = 10;

double fieldWidth(const BlockField& field) {
    if (field.kind == "item") return field.label.isEmpty() ? 168 : 206;
    if (field.kind == "variable" || field.kind == "sound") return 168;
    if (field.kind == "button") return 148;
    if (field.kind == "comparison") return 172;
    if (field.kind == "whole") return 96;
    if (field.kind == "number") return 100;
    if (field.kind == "color") return 40;
    if (field.kind == "animation") return 140;
    if (field.kind == "flip") return 118;
    if (field.kind == "scene") return 168;
    return 120;
}

double blockWidth(const Block& block, int depth) {
    if (depth >= MaximumLogicDepth) return BlockWidth;
    const auto& info = blockInfo(block.type);
    auto contents = BlockWidth;
    for (const auto& child : block.children) contents = std::max(contents, blockWidth(child, depth + 1));
    return info.holdsActions && !info.isEvent ? contents + NestIndent : contents;
}

class Placer {
public:
    Placer(LogicLayout& out, const QSet<QString>* problems) : out(out), problems(problems) {}

    double stack(const QList<Block>& blocks, double x, double y, int depth) {
        for (const auto& block : blocks) y = place(block, x, y, depth);
        return y;
    }

    double place(const Block& block, double x, double y, int depth, int runOrder = 0) {
        const auto& info = blockInfo(block.type);
        const auto index = out.placements.size();
        out.placements.emplaceBack();
        out.byId.insert(block.id, index);

        BlockPlacement placement;
        placement.id = block.id;
        placement.x = x;
        placement.y = y;
        placement.depth = depth;
        placement.runOrder = runOrder;
        placement.twoRows = blockUsesTwoRows(info);
        placement.width = blockWidth(block, depth);
        placement.headerHeight = RowHeight + (placement.twoRows ? SecondRowHeight : 0);
        if (problems && problems->contains(block.id)) {
            placement.problemHeight = ProblemHeight;
            placement.headerHeight += ProblemHeight;
        }

        double bottom;
        const auto canDescend = depth < MaximumLogicDepth;
        if (info.holdsActions && !info.isEvent) {
            const auto top = y + placement.headerHeight;
            const auto contents = canDescend ? stack(block.children, x + NestIndent, top, depth + 1) : top;
            placement.cavityHeight = std::max(EmptyCavityHeight, contents - top);
            placement.footerHeight = FooterHeight;
            placement.height = placement.headerHeight + placement.cavityHeight + placement.footerHeight;
            bottom = y + placement.height;
        } else {
            placement.height = placement.headerHeight;
            bottom = y + placement.height;
            if (info.holdsActions && canDescend) bottom = stack(block.children, x, bottom, depth + 1);
        }
        placement.stackHeight = bottom - y;
        out.placements[index] = placement;
        return bottom;
    }

private:
    LogicLayout& out;
    const QSet<QString>* problems;
};

}

QList<BlockField> blockFields(const BlockInfo& info) {
    QList<BlockField> fields;
    if (info.usesButton) fields.append({"button", "button", ""});
    if (info.usesItem) fields.append({"item", "item", ""});
    if (info.usesScene) fields.append({"scene", "scene", ""});
    if (info.usesAnimation) fields.append({"animation", "animation", "plays"});
    if (info.usesFlipped) fields.append({"flipped", "flip", ""});
    if (info.usesOtherItem) fields.append({"otherItem", "item", "and"});
    if (info.usesVariable) fields.append({"variable", "variable", ""});
    if (info.usesSound) fields.append({"sound", "sound", ""});
    if (info.usesComparison) fields.append({"comparison", "comparison", ""});
    if (info.usesValue) fields.append({"value", "whole", ""});
    if (info.usesPosition) {
        fields.append({"x", "number", "x"});
        fields.append({"y", "number", "y"});
    }
    if (info.usesColor) fields.append({"color", "color", ""});
    return fields;
}

QString blockCategory(const BlockInfo& info) {
    if (info.isEvent) return "events";
    if (info.type == BlockType::SetVelocity || info.type == BlockType::JumpItem || info.type == BlockType::IfOnGround) return "physics";
    if (info.usesScene || info.type == BlockType::RestartScene) return "scenes";
    if (info.usesPosition) return "movement";
    if (info.usesVariable) return "variables";
    if (info.usesSound || info.type == BlockType::StopMusic || info.type == BlockType::SetVolume) return "sound";
    if (info.type == BlockType::SaveVariables || info.type == BlockType::LoadVariables) return "saving";
    return "looks";
}

QString blockCategoryTitle(const QString& category) {
    if (category == "events") return "Events";
    if (category == "movement") return "Movement";
    if (category == "looks") return "Looks";
    if (category == "variables") return "Variables";
    if (category == "sound") return "Sound";
    if (category == "saving") return "Saving";
    if (category == "physics") return "Physics";
    if (category == "scenes") return "Scenes";
    return category;
}

bool blockUsesTwoRows(const BlockInfo& info) {
    auto needed = info.title.size() * CharacterWidth + GlyphWidth + (info.isEvent ? OrderChipWidth : 0);
    const auto fields = blockFields(info);
    for (const auto& field : fields) needed += fieldWidth(field) + FieldGap;
    return needed > BlockWidth - 2 * BlockPadding;
}

LogicLayout layoutLogic(const LogicProgram& program, double canvasWidth, const QSet<QString>* problems) {
    LogicLayout out;
    Placer placer(out, problems);

    auto columnWidth = BlockWidth;
    for (const auto& event : program.events) columnWidth = std::max(columnWidth, blockWidth(event, 1));
    out.columns = std::max(1, static_cast<int>(std::floor((canvasWidth - 2 * CanvasPadding + ColumnGap) / (columnWidth + ColumnGap))));

    QList<double> bottoms(out.columns, CanvasPadding);
    for (qsizetype index = 0; index < program.events.size(); ++index) {
        const auto column = std::ranges::distance(bottoms.begin(), std::ranges::min_element(bottoms));
        const auto x = CanvasPadding + column * (columnWidth + ColumnGap);
        const auto y = bottoms[column];
        bottoms[column] = placer.place(program.events[index], x, y, 1, static_cast<int>(index) + 1) + EventGap;
    }

    out.contentWidth = 2 * CanvasPadding + out.columns * columnWidth + (out.columns - 1) * ColumnGap;
    out.contentHeight = *std::ranges::max_element(bottoms) + CanvasPadding;
    return out;
}

namespace {

void addActionSpots(const Block& parent, const LogicLayout& layout, int depth, int stackDepth, QList<LogicLanding>& spots) {
    const auto* place = layout.find(parent.id);
    if (!place) return;
    const auto wraps = blockInfo(parent.type).holdsActions;
    if (!wraps) return;
    if (depth + stackDepth <= MaximumLogicDepth) {
        const auto x = blockInfo(parent.type).isEvent ? place->x : place->x + NestIndent;
        for (qsizetype index = 0; index <= parent.children.size(); ++index) {
            double y;
            if (index == 0) {
                y = place->y + place->headerHeight;
            } else {
                const auto* previous = layout.find(parent.children[index - 1].id);
                y = previous ? previous->y + previous->stackHeight : place->y + place->height;
            }
            spots.append({parent.id, static_cast<int>(index), x, y, BlockWidth, false});
        }
    }
    if (depth < MaximumLogicDepth) {
        for (const auto& child : parent.children) addActionSpots(child, layout, depth + 1, stackDepth, spots);
    }
}

}

QList<LogicLanding> landingSpots(const LogicProgram& program, const LogicLayout& layout, bool draggingEvent, int stackDepth) {
    QList<LogicLanding> spots;
    if (draggingEvent) {
        if (program.events.isEmpty()) {
            spots.append({QString(), 0, CanvasPadding, CanvasPadding, BlockWidth, true});
            return spots;
        }
        for (qsizetype index = 0; index < program.events.size(); ++index) {
            const auto* place = layout.find(program.events[index].id);
            if (place) spots.append({QString(), static_cast<int>(index), place->x, place->y, BlockWidth, true});
        }
        const auto* last = layout.find(program.events.last().id);
        if (last) {
            spots.append({QString(), static_cast<int>(program.events.size()), last->x, last->y + last->stackHeight + EventGap, BlockWidth, true});
        }
        return spots;
    }
    for (const auto& event : program.events) addActionSpots(event, layout, 1, stackDepth, spots);
    return spots;
}

const LogicLanding* nearestLanding(const QList<LogicLanding>& spots, double x, double y) {
    const LogicLanding* best = nullptr;
    double bestDistance = 0;
    for (const auto& spot : spots) {
        // Up and down matters more than sideways: a stack's left edge rarely lines up exactly.
        const auto distance = std::hypot((spot.x - x) * 0.5, spot.y - y);
        if (!spot.isEvent && distance > SnapDistance) continue;
        if (!best || distance < bestDistance) {
            best = &spot;
            bestDistance = distance;
        }
    }
    return best;
}

}
