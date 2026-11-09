#include "Blocks.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <cmath>
#include <limits>

namespace Studio {

namespace {

constexpr int SupportedSchema = 1;
constexpr auto LogicFileName = "logic.json";
constexpr auto HeaderFolder = "src";
constexpr auto HeaderFileName = "GeneratedLogic.hpp";

QString describeBlock(const Block& block) {
    const auto& title = blockInfo(block.type).title;
    if (block.id.isEmpty()) return QString("A \"%1\" block").arg(title);
    return QString("The \"%1\" block (%2)").arg(title, block.id);
}

QString nestingMessage(const Block& block) {
    return QString("%1 is nested too deeply. Blocks can only go inside each other %2 levels deep. Move it further out.").arg(describeBlock(block)).arg(MaximumLogicDepth);
}

bool isValidBlockId(const QString& id) {
    if (id.isEmpty() || id.size() > MaximumBlockIdLength) return false;
    for (const auto character : id) {
        const auto code = character.unicode();
        const auto isLetter = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
        const auto isDigit = code >= '0' && code <= '9';
        if (!isLetter && !isDigit && code != '-' && code != '_') return false;
    }
    return true;
}

bool isHexDigit(QChar character) {
    const auto code = character.unicode();
    return (code >= '0' && code <= '9') || (code >= 'a' && code <= 'f') || (code >= 'A' && code <= 'F');
}

bool isColor(const QString& color) {
    if (color.size() != 7 || color[0] != '#') return false;
    for (qsizetype index = 1; index < color.size(); ++index) {
        if (!isHexDigit(color[index])) return false;
    }
    return true;
}

bool isUsableCoordinate(double value) {
    return std::isfinite(value) && std::abs(value) <= MaximumLogicCoordinate;
}

// Counts down to the depth limit only, so that a tree far too deep cannot exhaust the stack.
void countBlocks(const QList<Block>& blocks, int depth, qsizetype& count) {
    count += blocks.size();
    if (depth >= MaximumLogicDepth) return;
    for (const auto& block : blocks) countBlocks(block.children, depth + 1, count);
}

// One walk over the tree, so problems come out in the order the blocks appear. With no scene, only
// the rules a saved file must follow are checked.
class Validator {
public:
    explicit Validator(const Scene* scene) : scene(scene) {
        if (scene) {
            for (const auto& item : scene->items) {
                itemIds.insert(item.id);
                if (item.type == "particles") particleIds.insert(item.id);
            }
        }
    }

    QList<LogicProblem> run(const LogicProgram& program) {
        if (program.events.size() > MaximumLogicEvents) {
            return {{{}, QString("This program has %1 events, but a program can hold at most %2. Remove some events and try again.").arg(program.events.size()).arg(MaximumLogicEvents)}};
        }
        qsizetype total = 0;
        countBlocks(program.events, 1, total);
        if (total > MaximumLogicBlocks) {
            return {{{}, QString("This program has %1 blocks, but a program can hold at most %2. Remove some blocks and try again.").arg(total).arg(MaximumLogicBlocks)}};
        }
        for (const auto& block : program.events) visit(block, 1);
        return problems;
    }

private:
    void report(const Block& block, const QString& message) {
        problems.append({block.id, message});
    }

    void checkItem(const Block& block, const QString& id) {
        if (!scene) return;
        if (id.isEmpty()) {
            report(block, QString("%1 has no item chosen. Choose an item from the scene.").arg(describeBlock(block)));
        } else if (!itemIds.contains(id)) {
            report(block, QString("%1 uses the item \"%2\", which is not in the scene. Choose an item that exists, or delete this block.").arg(describeBlock(block), id));
        }
    }

    void visit(const Block& block, int depth) {
        const auto& info = blockInfo(block.type);
        const auto label = describeBlock(block);

        if (!isValidBlockId(block.id)) {
            report(block, QString("%1 has the id \"%2\". An id needs 1 to %3 letters, digits, - or _ characters.").arg(label, block.id).arg(MaximumBlockIdLength));
        } else if (usedIds.contains(block.id)) {
            report(block, QString("%1 uses the id \"%2\", which another block already has. Give each block its own id.").arg(label, block.id));
        }
        usedIds.insert(block.id);

        if (depth == 1 && !info.isEvent) report(block, QString("%1 must go inside an event, such as \"Every frame\". Drag it into one.").arg(label));
        if (depth > 1 && info.isEvent) report(block, QString("%1 is an event, so it cannot go inside another block. Move it to the top level.").arg(label));
        if (!info.holdsActions && !block.children.isEmpty()) report(block, QString("%1 cannot hold other blocks. Move them out of it.").arg(label));

        if (info.usesItem) checkItem(block, block.item);
        if (block.type == BlockType::BurstParticles && scene && itemIds.contains(block.item) && !particleIds.contains(block.item)) {
            report(block, QString("%1 uses the item \"%2\", which is not a Particles item. Choose a Particles item to burst from.").arg(label, block.item));
        }
        if (info.usesOtherItem) {
            checkItem(block, block.otherItem);
            if (scene && !block.item.isEmpty() && block.item == block.otherItem) {
                report(block, QString("%1 needs two different items, but both sides are \"%2\". Choose another item for one side.").arg(label, block.item));
            }
        }
        if (info.usesPosition) {
            for (const auto& [field, value] : {std::pair<const char*, double>{"x", block.x}, {"y", block.y}}) {
                if (!isUsableCoordinate(value)) report(block, QString("%1 needs %2 to be a number from -%3 to %3.").arg(label, field).arg(MaximumLogicCoordinate, 0, 'f', 0));
            }
        }
        if (info.usesColor && !isColor(block.color)) report(block, QString("%1 needs a color that looks like #RRGGBB, for example #FFD23F.").arg(label));
        if (info.usesValue && std::abs(static_cast<qint64>(block.value)) > MaximumLogicScore) {
            report(block, QString("%1 needs a whole number from -%2 to %2.").arg(label).arg(MaximumLogicScore));
        }

        if (block.children.isEmpty()) return;
        if (depth >= MaximumLogicDepth) {
            report(block.children.first(), nestingMessage(block.children.first()));
            return;
        }
        for (const auto& child : block.children) visit(child, depth + 1);
    }

    const Scene* scene;
    QSet<QString> itemIds;
    QSet<QString> particleIds;
    QSet<QString> usedIds;
    QList<LogicProblem> problems;
};

double numberOrInvalid(const QJsonValue& value) {
    return value.isDouble() ? value.toDouble() : std::numeric_limits<double>::quiet_NaN();
}

QString unknownChoiceMessage(const QString& label, const QString& kind, const QString& value, const QStringList& choices) {
    return QString("%1 has the %2 \"%3\", which does not exist. Choose one of: %4.").arg(label, kind, value, choices.join(", "));
}

QString parseBlock(const QJsonValue& value, qsizetype index, int depth, qsizetype& count, Block& block);

QString parseChildren(const QJsonArray& actions, int depth, qsizetype& count, Block& block) {
    for (qsizetype index = 0; index < actions.size(); ++index) {
        Block child;
        const auto problem = parseBlock(actions.at(index), index, depth + 1, count, child);
        if (!problem.isEmpty()) return problem;
        block.children.append(child);
    }
    return {};
}

// Fields of the wrong JSON type either stop here with a message or become values the validator rejects.
QString parseBlock(const QJsonValue& value, qsizetype index, int depth, qsizetype& count, Block& block) {
    if (!value.isObject()) return QString("Block number %1 must be a JSON object.").arg(index + 1);
    if (++count > MaximumLogicBlocks) return QString("This program has more than %1 blocks. Remove some blocks and try again.").arg(MaximumLogicBlocks);
    const auto object = value.toObject();
    block.id = object.value("id").toString();

    const auto typeName = object.value("type").toString();
    const BlockInfo* found = nullptr;
    for (const auto& candidate : blockCatalog()) {
        if (candidate.jsonName == typeName) found = &candidate;
    }
    if (!found) {
        const auto shown = block.id.isEmpty() ? QString("Block number %1").arg(index + 1) : QString("Block (%1)").arg(block.id);
        if (typeName.isEmpty()) return QString("%1 has no type. Delete it or choose a block from the palette.").arg(shown);
        return QString("%1 has the unknown type \"%2\". Delete it or choose a block from the palette.").arg(shown, typeName);
    }
    const auto& info = *found;
    block.type = info.type;
    const auto label = describeBlock(block);

    if (info.usesButton) {
        const auto name = object.value("button").toString();
        const ButtonInfo* button = nullptr;
        QStringList choices;
        for (const auto& candidate : buttonCatalog()) {
            choices << candidate.jsonName;
            if (candidate.jsonName == name) button = &candidate;
        }
        if (!button) return unknownChoiceMessage(label, "button", name, choices);
        block.button = button->button;
    }
    if (info.usesItem) block.item = object.value("item").toString();
    if (info.usesOtherItem) block.otherItem = object.value("otherItem").toString();
    if (info.usesPosition) {
        block.x = numberOrInvalid(object.value("x"));
        block.y = numberOrInvalid(object.value("y"));
    }
    if (info.usesColor) block.color = object.value("color").toString();
    if (info.usesValue) {
        const auto number = object.value("value");
        const auto amount = number.toDouble();
        if (!number.isDouble() || amount != std::floor(amount) || std::abs(amount) > MaximumLogicScore) {
            return QString("%1 needs a whole number from -%2 to %2.").arg(label).arg(MaximumLogicScore);
        }
        block.value = static_cast<int>(amount);
    }
    if (info.usesComparison) {
        const auto name = object.value("comparison").toString();
        const ComparisonInfo* comparison = nullptr;
        QStringList choices;
        for (const auto& candidate : comparisonCatalog()) {
            choices << candidate.jsonName;
            if (candidate.jsonName == name) comparison = &candidate;
        }
        if (!comparison) return unknownChoiceMessage(label, "comparison", name, choices);
        block.comparison = comparison->comparison;
    }

    const auto actions = object.value("actions");
    if (info.holdsActions) {
        if (!actions.isArray()) return QString("%1 needs an \"actions\" list. Add \"actions\": [] to it.").arg(label);
        const auto list = actions.toArray();
        if (!list.isEmpty() && depth >= MaximumLogicDepth) return nestingMessage(block);
        return parseChildren(list, depth, count, block);
    }
    if (!actions.isUndefined()) return QString("%1 cannot hold other blocks. Remove its \"actions\" list.").arg(label);
    return {};
}

QJsonObject blockToJson(const Block& block) {
    const auto& info = blockInfo(block.type);
    QJsonObject object{{"id", block.id}, {"type", info.jsonName}};
    if (info.usesButton) {
        for (const auto& candidate : buttonCatalog()) {
            if (candidate.button == block.button) object.insert("button", candidate.jsonName);
        }
    }
    if (info.usesItem) object.insert("item", block.item);
    if (info.usesOtherItem) object.insert("otherItem", block.otherItem);
    if (info.usesPosition) {
        object.insert("x", block.x);
        object.insert("y", block.y);
    }
    if (info.usesColor) object.insert("color", block.color);
    if (info.usesValue) object.insert("value", block.value);
    if (info.usesComparison) {
        for (const auto& candidate : comparisonCatalog()) {
            if (candidate.comparison == block.comparison) object.insert("comparison", candidate.jsonName);
        }
    }
    if (info.holdsActions) {
        QJsonArray actions;
        for (const auto& child : block.children) actions.append(blockToJson(child));
        object.insert("actions", actions);
    }
    return object;
}

QByteArray floatLiteral(double value) {
    auto text = QByteArray::number(value, 'f', 6);
    while (text.endsWith('0') && text.at(text.size() - 2) != '.') text.chop(1);
    if (text == "-0.0") text = "0.0";
    return text + 'f';
}

QByteArray colorLiterals(const QString& color) {
    QByteArrayList channels;
    for (const qsizetype start : {1, 3, 5}) channels << floatLiteral(color.mid(start, 2).toInt(nullptr, 16) / 255.0);
    return channels.join(", ");
}

QByteArray buttonConstant(ControllerButton button) {
    switch (button) {
    case ControllerButton::Cross: return "Cross";
    case ControllerButton::Circle: return "Circle";
    case ControllerButton::Square: return "Square";
    case ControllerButton::Triangle: return "Triangle";
    case ControllerButton::DpadUp: return "DpadUp";
    case ControllerButton::DpadDown: return "DpadDown";
    case ControllerButton::DpadLeft: return "DpadLeft";
    case ControllerButton::DpadRight: return "DpadRight";
    }
    return {};
}

QByteArray comparisonOperator(ScoreComparison comparison) {
    switch (comparison) {
    case ScoreComparison::Equal: return "==";
    case ScoreComparison::NotEqual: return "!=";
    case ScoreComparison::Less: return "<";
    case ScoreComparison::LessOrEqual: return "<=";
    case ScoreComparison::Greater: return ">";
    case ScoreComparison::GreaterOrEqual: return ">=";
    }
    return {};
}

QByteArray indentation(int level) {
    return QByteArray(level * 4, ' ');
}

// Block ids are limited to letters, digits, - and _, so one can safely end a line comment. Item ids
// and names are free text, which is why only indexes reach the generated code.
class Generator {
public:
    explicit Generator(const Scene& scene) {
        for (qsizetype index = 0; index < scene.items.size(); ++index) itemIndexes.insert(scene.items[index].id, index);
    }

    QByteArray index(const QString& itemId) const { return QByteArray::number(itemIndexes.value(itemId)); }

    void actions(const QList<Block>& blocks, int level, QByteArray& code) const {
        for (const auto& block : blocks) action(block, level, code);
    }

    void action(const Block& block, int level, QByteArray& code) const {
        const auto pad = indentation(level);
        const auto comment = " // " + block.id.toLatin1() + '\n';
        switch (block.type) {
        case BlockType::MoveBy:
            code += pad + "world.MoveBy(" + index(block.item) + ", " + floatLiteral(block.x) + ", " + floatLiteral(block.y) + ");" + comment;
            return;
        case BlockType::SetPosition:
            code += pad + "world.SetPosition(" + index(block.item) + ", " + floatLiteral(block.x) + ", " + floatLiteral(block.y) + ");" + comment;
            return;
        case BlockType::Show:
            code += pad + "world.SetVisible(" + index(block.item) + ", true);" + comment;
            return;
        case BlockType::Hide:
            code += pad + "world.SetVisible(" + index(block.item) + ", false);" + comment;
            return;
        case BlockType::SetColor:
            code += pad + "world.SetColor(" + index(block.item) + ", " + colorLiterals(block.color) + ");" + comment;
            return;
        case BlockType::AddScore:
            code += pad + "world.AddScore(" + QByteArray::number(block.value) + ");" + comment;
            return;
        case BlockType::SetScore:
            code += pad + "world.SetScore(" + QByteArray::number(block.value) + ");" + comment;
            return;
        case BlockType::BurstParticles:
            code += pad + "world.BurstParticles(" + index(block.item) + ");" + comment;
            return;
        case BlockType::IfScore:
            code += pad + "if (world.Score() " + comparisonOperator(block.comparison) + ' ' + QByteArray::number(block.value) + ") {" + comment;
            actions(block.children, level + 1, code);
            code += pad + "}\n";
            return;
        case BlockType::WhenGameStarts:
        case BlockType::WhileButtonHeld:
        case BlockType::EveryFrame:
        case BlockType::WhenItemsTouch:
            return; // validation keeps events out of an action list
        }
    }

    void frameEvent(const Block& block, int touchIndex, QByteArray& code) const {
        code += "    // " + block.id.toLatin1() + ": " + blockInfo(block.type).title.toLatin1() + '\n';
        if (block.type == BlockType::EveryFrame) {
            actions(block.children, 1, code);
        } else if (block.type == BlockType::WhileButtonHeld) {
            code += "    if (world.Held(Buttons::" + buttonConstant(block.button) + ")) {\n";
            actions(block.children, 2, code);
            code += "    }\n";
        } else if (block.type == BlockType::WhenItemsTouch) {
            const auto slot = "memory.touching[" + QByteArray::number(touchIndex) + "]";
            code += "    {\n";
            code += "        const bool touching = world.Touching(" + index(block.item) + ", " + index(block.otherItem) + ");\n";
            code += "        const bool started = touching && !" + slot + ";\n";
            code += "        " + slot + " = touching;\n";
            code += "        if (started) {\n";
            actions(block.children, 3, code);
            code += "        }\n";
            code += "    }\n";
        }
    }

private:
    QHash<QString, qsizetype> itemIndexes;
};

void collectBurstTargets(const QList<Block>& blocks, QSet<QString>& targets) {
    for (const auto& block : blocks) {
        if (block.type == BlockType::BurstParticles) targets.insert(block.item);
        collectBurstTargets(block.children, targets);
    }
}

bool referencesItems(const QList<Block>& blocks) {
    for (const auto& block : blocks) {
        const auto& info = blockInfo(block.type);
        if (info.usesItem || info.usesOtherItem || referencesItems(block.children)) return true;
    }
    return false;
}

QByteArray assembleHeader(bool includeScene, bool includeButtons, int itemCount, int touchEvents, const QByteArray& startCode, const QByteArray& frameCode) {
    QByteArray header = "// Generated by AnyDevKit Studio from logic.json. Do not edit.\n#pragma once\n\n";
    if (includeScene) header += "#include \"GeneratedScene.hpp\"\n";
    if (includeButtons) header += "#include \"Input/Buttons.hpp\"\n";
    if (includeScene || includeButtons) header += '\n';
    header += "#include <array>\n#include <cstddef>\n\nnamespace GeneratedLogic {\n\n";
    if (includeScene) {
        header += "static_assert(GeneratedScene::Items.size() == " + QByteArray::number(itemCount)
            + ", \"GeneratedScene.hpp and GeneratedLogic.hpp do not match. Build the project again from Studio.\");\n\n";
    }
    header += "inline constexpr std::size_t TouchEventCount = " + QByteArray::number(touchEvents) + ";\n\n";
    header += "struct Memory {\n    std::array<bool, TouchEventCount> touching{};\n};\n\n";
    header += "template <typename World>\nvoid OnStart([[maybe_unused]] World& world) {\n" + startCode + "}\n\n";
    header += "template <typename World>\nvoid OnFrame([[maybe_unused]] World& world, [[maybe_unused]] Memory& memory) {\n" + frameCode + "}\n\n";
    header += "}\n";
    return header;
}

}

const QList<BlockInfo>& blockCatalog() {
    // Keep in the order of BlockType: blockInfo indexes this list.
    //                                                                               event  holds  button item   other  pos    color  value  cmp
    static const QList<BlockInfo> catalog{
        {BlockType::WhenGameStarts, "whenGameStarts", "When the game starts",        true,  true,  false, false, false, false, false, false, false},
        {BlockType::WhileButtonHeld, "whileButtonHeld", "While a button is held",    true,  true,  true,  false, false, false, false, false, false},
        {BlockType::EveryFrame, "everyFrame", "Every frame",                         true,  true,  false, false, false, false, false, false, false},
        {BlockType::WhenItemsTouch, "whenItemsTouch", "When two items touch",        true,  true,  false, true,  true,  false, false, false, false},
        {BlockType::MoveBy, "moveBy", "Move item",                                   false, false, false, true,  false, true,  false, false, false},
        {BlockType::SetPosition, "setPosition", "Set item position",                 false, false, false, true,  false, true,  false, false, false},
        {BlockType::Show, "show", "Show item",                                       false, false, false, true,  false, false, false, false, false},
        {BlockType::Hide, "hide", "Hide item",                                       false, false, false, true,  false, false, false, false, false},
        {BlockType::SetColor, "setColor", "Set item color",                          false, false, false, true,  false, false, true,  false, false},
        {BlockType::AddScore, "addScore", "Add to score",                            false, false, false, false, false, false, false, true,  false},
        {BlockType::SetScore, "setScore", "Set score",                               false, false, false, false, false, false, false, true,  false},
        {BlockType::IfScore, "ifScore", "If score",                                  false, true,  false, false, false, false, false, true,  true},
        {BlockType::BurstParticles, "burstParticles", "Burst particles from item",   false, false, false, true,  false, false, false, false, false},
    };
    return catalog;
}

const BlockInfo& blockInfo(BlockType type) {
    return blockCatalog().at(static_cast<qsizetype>(type));
}

const QList<ButtonInfo>& buttonCatalog() {
    static const QList<ButtonInfo> catalog{
        {ControllerButton::Cross, "cross", "Cross"},
        {ControllerButton::Circle, "circle", "Circle"},
        {ControllerButton::Square, "square", "Square"},
        {ControllerButton::Triangle, "triangle", "Triangle"},
        {ControllerButton::DpadUp, "dpadUp", "D-pad up"},
        {ControllerButton::DpadDown, "dpadDown", "D-pad down"},
        {ControllerButton::DpadLeft, "dpadLeft", "D-pad left"},
        {ControllerButton::DpadRight, "dpadRight", "D-pad right"},
    };
    return catalog;
}

const QList<ComparisonInfo>& comparisonCatalog() {
    static const QList<ComparisonInfo> catalog{
        {ScoreComparison::Equal, "equal", "is equal to"},
        {ScoreComparison::NotEqual, "notEqual", "is not equal to"},
        {ScoreComparison::Less, "less", "is less than"},
        {ScoreComparison::LessOrEqual, "lessOrEqual", "is at most"},
        {ScoreComparison::Greater, "greater", "is greater than"},
        {ScoreComparison::GreaterOrEqual, "greaterOrEqual", "is at least"},
    };
    return catalog;
}

QList<LogicProblem> validateLogicStructure(const LogicProgram& program) {
    return Validator(nullptr).run(program);
}

QList<LogicProblem> validateLogic(const LogicProgram& program, const Scene& scene) {
    return Validator(&scene).run(program);
}

LogicLoadResult loadLogic(const QString& path) {
    const auto fileName = QFileInfo(path).fileName();
    const auto failure = [](const QString& message) { return LogicLoadResult{std::nullopt, message}; };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return failure(QString("%1 could not be opened: %2. Check that the file exists and try again.").arg(fileName, file.errorString()));
    if (file.size() > MaximumLogicFileBytes) return failure(QString("%1 is too large. A logic file can be at most %2 MB. Restore a backup or remove some blocks.").arg(fileName).arg(MaximumLogicFileBytes / (1024 * 1024)));

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) return failure(QString("%1 is not valid JSON: %2 at character %3. Fix the file or restore a backup.").arg(fileName, parseError.errorString(), QString::number(parseError.offset)));
    if (!document.isObject()) return failure(QString("%1 must contain a JSON object with the program in it.").arg(fileName));
    const auto root = document.object();

    const auto schema = root.value("schemaVersion");
    if (!schema.isDouble() || schema.toInt(-1) != SupportedSchema) return failure(QString("%1 needs schemaVersion %2, the only version this Studio supports.").arg(fileName).arg(SupportedSchema));
    if (!root.value("events").isArray()) return failure(QString("%1 has no events list. Add \"events\": [] to the file.").arg(fileName));

    LogicProgram program;
    qsizetype count = 0;
    const auto events = root.value("events").toArray();
    for (qsizetype index = 0; index < events.size(); ++index) {
        Block block;
        const auto problem = parseBlock(events.at(index), index, 1, count, block);
        if (!problem.isEmpty()) return failure(problem);
        program.events.append(block);
    }

    const auto problems = validateLogicStructure(program);
    if (!problems.isEmpty()) return failure(problems.first().message);
    return {program, {}};
}

bool saveLogic(const QString& path, const LogicProgram& program, QString& error) {
    const auto problems = validateLogicStructure(program);
    if (!problems.isEmpty()) {
        error = problems.first().message;
        return false;
    }

    QJsonArray events;
    for (const auto& block : program.events) events.append(blockToJson(block));
    const QJsonObject root{{"schemaVersion", SupportedSchema}, {"events", events}};
    const auto contents = QJsonDocument(root).toJson();

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        error = QString("%1 could not be saved: %2. Check that the folder exists and is writable.").arg(QFileInfo(path).fileName(), file.errorString());
        return false;
    }
    error.clear();
    return true;
}

QString newBlockId(const LogicProgram& program) {
    QSet<QString> used;
    const auto collect = [&](const auto& self, const QList<Block>& blocks, int depth) -> void {
        for (const auto& block : blocks) {
            used.insert(block.id);
            if (depth < MaximumLogicDepth) self(self, block.children, depth + 1);
        }
    };
    collect(collect, program.events, 1);

    auto candidate = used.size() + 1;
    while (used.contains(QString("block-%1").arg(candidate))) ++candidate;
    return QString("block-%1").arg(candidate);
}

QByteArray generateEmptyLogicHeader() {
    return assembleHeader(false, false, 0, 0, {}, {});
}

QByteArray generateLogicHeader(const LogicProgram& program, const Scene& scene) {
    if (!validateScene(scene).isEmpty() || !validateLogic(program, scene).isEmpty()) return {};
    if (program.events.isEmpty()) return generateEmptyLogicHeader();

    const Generator generator(scene);
    QByteArray startCode;
    // A one-shot item a block bursts plays when that block runs, not also when the game starts.
    QSet<QString> burstTargets;
    collectBurstTargets(program.events, burstTargets);
    for (const auto& item : scene.items) {
        if (item.type == "particles" && !item.looping && burstTargets.contains(item.id)) {
            startCode += "    world.AwaitBurst(" + generator.index(item.id) + ");\n";
        }
    }
    QByteArray frameCode;
    int touchEvents = 0;
    bool usesButtons = false;
    for (const auto& block : program.events) {
        if (block.type == BlockType::WhenGameStarts) {
            startCode += "    // " + block.id.toLatin1() + ": " + blockInfo(block.type).title.toLatin1() + '\n';
            generator.actions(block.children, 1, startCode);
            continue;
        }
        generator.frameEvent(block, touchEvents, frameCode);
        if (block.type == BlockType::WhenItemsTouch) ++touchEvents;
        if (block.type == BlockType::WhileButtonHeld) usesButtons = true;
    }
    return assembleHeader(referencesItems(program.events), usesButtons, static_cast<int>(scene.items.size()), touchEvents, startCode, frameCode);
}

bool exportLogicHeader(const QString& projectRoot, const Scene& scene, QString& error) {
    error.clear();
    const auto fail = [&](const QString& message) {
        error = message;
        return false;
    };

    const auto root = QDir::cleanPath(QDir(projectRoot).absolutePath());
    const auto logicPath = root + '/' + LogicFileName;
    const QFileInfo logicInfo(logicPath);

    QByteArray header;
    if (!logicInfo.exists() && !logicInfo.isSymLink()) {
        header = generateEmptyLogicHeader();
    } else {
        const auto loaded = loadLogic(logicPath);
        if (!loaded.program) return fail(loaded.error);
        const auto problems = validateLogic(*loaded.program, scene);
        if (!problems.isEmpty()) {
            auto message = problems.first().message;
            const auto others = problems.size() - 1;
            if (others == 1) message += " There is 1 more problem to fix.";
            if (others > 1) message += QString(" There are %1 more problems to fix.").arg(others);
            return fail(message);
        }
        header = generateLogicHeader(*loaded.program, scene);
        if (header.isEmpty()) return fail("The game logic could not be generated because the scene has a problem. Fix the scene and try again.");
    }

    // A link could lead outside the project, and a build must only change files inside it.
    const auto folder = root + '/' + HeaderFolder;
    const auto path = folder + '/' + HeaderFileName;
    for (const auto& candidate : {folder, path}) {
        const QFileInfo info(candidate);
        if (info.isSymLink() || info.isJunction()) return fail(QString("%1 is a link, and Studio will not write through it. Replace it with a regular file or folder and try again.").arg(QDir(root).relativeFilePath(candidate)));
    }
    if (!QDir().mkpath(folder)) return fail(QString("%1 could not be created. Check that the project folder is writable.").arg(HeaderFolder));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(header) != header.size() || !file.commit()) {
        return fail(QString("%1/%2 could not be written: %3. Check that the folder is writable.").arg(HeaderFolder, HeaderFileName, file.errorString()));
    }
    return true;
}

}
