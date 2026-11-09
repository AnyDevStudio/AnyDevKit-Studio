#include "Blocks.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QByteArrayList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <tuple>

namespace Studio {

namespace {

// The version saved today. Older files still load: version 1's score blocks become variable blocks
// and a version 2 file simply has no sounds.
constexpr int CurrentSchema = 3;
constexpr int OldestLoadableSchema = 1;
constexpr int VariableSchema = 2;
constexpr int SoundSchema = 3; // sounds and the blocks that play them, set the volume, save or load
constexpr auto ScoreVariableName = "score";
constexpr auto LogicFileName = "logic.json";
constexpr auto HeaderFolder = "src";
constexpr auto HeaderFileName = "GeneratedLogic.hpp";
constexpr auto VariablesHeaderFileName = "GeneratedVariables.hpp";

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
    Validator(const Scene* scene, const LogicProgram& program) : scene(scene) {
        for (const auto& variable : program.variables) variableNames.insert(variable.name);
        for (const auto& sound : program.sounds) soundNames.insert(sound.name);
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
        checkVariables(program);
        if (const auto problem = validateSounds(program.sounds); !problem.isEmpty()) problems.append({{}, problem});
        for (const auto& block : program.events) visit(block, 1);
        return problems;
    }

private:
    void checkVariables(const LogicProgram& program) {
        if (program.variables.size() > MaximumLogicVariables) {
            problems.append({{}, QString("This program has %1 variables, but a program can hold at most %2. Remove some variables and try again.").arg(program.variables.size()).arg(MaximumLogicVariables)});
            return;
        }
        QSet<QString> seen;
        for (const auto& variable : program.variables) {
            if (!isVariableName(variable.name)) {
                problems.append({{}, QString("The variable name \"%1\" is not allowed. A name is 1 to %2 English letters, digits or _, and cannot start with a digit.").arg(variable.name).arg(MaximumVariableNameLength)});
            }
            const auto folded = variable.name.toCaseFolded();
            if (seen.contains(folded)) {
                problems.append({{}, QString("There is more than one variable named \"%1\". Give each variable its own name.").arg(variable.name)});
            }
            seen.insert(folded);
            if (std::abs(static_cast<qint64>(variable.initial)) > MaximumVariableValue) {
                problems.append({{}, QString("The variable \"%1\" needs a whole number from -%2 to %2 to start at.").arg(variable.name).arg(MaximumVariableValue)});
            }
        }
    }

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

    void checkVariable(const Block& block) {
        if (!scene) return;
        if (block.variable.isEmpty()) {
            report(block, QString("%1 has no variable chosen. Choose a variable, or add one in the Variables area.").arg(describeBlock(block)));
        } else if (!variableNames.contains(block.variable)) {
            report(block, QString("%1 uses the variable \"%2\", which does not exist. Choose another variable, or add one named %2 in the Variables area.").arg(describeBlock(block), block.variable));
        }
    }

    void checkSound(const Block& block) {
        if (!scene) return;
        if (block.sound.isEmpty()) {
            report(block, QString("%1 has no sound chosen. Choose a sound, or import one in the Sounds area.").arg(describeBlock(block)));
        } else if (!soundNames.contains(block.sound)) {
            report(block, QString("%1 uses the sound \"%2\", which is not in this project. Choose another sound, or import it again in the Sounds area.").arg(describeBlock(block), block.sound));
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
        if (info.usesVariable) checkVariable(block);
        if (info.usesSound) checkSound(block);
        if (block.type == BlockType::SetVolume && (block.value < 0 || block.value > MaximumVolumePercent)) {
            report(block, QString("%1 needs a volume from 0 to %2 percent.").arg(label).arg(MaximumVolumePercent));
        }
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
        if (info.usesValue && std::abs(static_cast<qint64>(block.value)) > MaximumVariableValue) {
            report(block, QString("%1 needs a whole number from -%2 to %2.").arg(label).arg(MaximumVariableValue));
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
    QSet<QString> variableNames;
    QSet<QString> soundNames;
    QSet<QString> usedIds;
    QList<LogicProblem> problems;
};

bool needsSoundSchema(const BlockInfo& info) {
    return info.usesSound || info.type == BlockType::StopMusic || info.type == BlockType::SetVolume || info.type == BlockType::SaveVariables || info.type == BlockType::LoadVariables;
}

double numberOrInvalid(const QJsonValue& value) {
    return value.isDouble() ? value.toDouble() : std::numeric_limits<double>::quiet_NaN();
}

QString unknownChoiceMessage(const QString& label, const QString& kind, const QString& value, const QStringList& choices) {
    return QString("%1 has the %2 \"%3\", which does not exist. Choose one of: %4.").arg(label, kind, value, choices.join(", "));
}

// What the blocks of a schema 1 file had to say about the score, which the new blocks say about a variable.
const QHash<QString, BlockType>& legacyScoreBlocks() {
    static const QHash<QString, BlockType> blocks{
        {"setScore", BlockType::SetVariable},
        {"addScore", BlockType::ChangeVariable},
        {"ifScore", BlockType::IfVariable},
    };
    return blocks;
}

struct Parsing {
    int schema = CurrentSchema;
    qsizetype count = 0;
    bool usesScore = false; // a schema 1 file had a score block, so it needs the score variable
};

QString parseBlock(const QJsonValue& value, qsizetype index, int depth, Parsing& parsing, Block& block);

QString parseChildren(const QJsonArray& actions, int depth, Parsing& parsing, Block& block) {
    for (qsizetype index = 0; index < actions.size(); ++index) {
        Block child;
        const auto problem = parseBlock(actions.at(index), index, depth + 1, parsing, child);
        if (!problem.isEmpty()) return problem;
        block.children.append(child);
    }
    return {};
}

// Fields of the wrong JSON type either stop here with a message or become values the validator rejects.
QString parseBlock(const QJsonValue& value, qsizetype index, int depth, Parsing& parsing, Block& block) {
    if (!value.isObject()) return QString("Block number %1 must be a JSON object.").arg(index + 1);
    if (++parsing.count > MaximumLogicBlocks) return QString("This program has more than %1 blocks. Remove some blocks and try again.").arg(MaximumLogicBlocks);
    const auto object = value.toObject();
    block.id = object.value("id").toString();

    const auto typeName = object.value("type").toString();
    const BlockInfo* found = nullptr;
    const auto legacyScore = parsing.schema < VariableSchema && legacyScoreBlocks().contains(typeName);
    if (legacyScore) {
        found = &blockInfo(legacyScoreBlocks().value(typeName));
        parsing.usesScore = true;
    }
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
    if (info.usesVariable && parsing.schema < VariableSchema && !legacyScore) {
        return QString("%1 uses a variable, which needs schemaVersion %2. Change schemaVersion to %2 and add a \"variables\" list.").arg(label).arg(VariableSchema);
    }
    if (needsSoundSchema(info) && parsing.schema < SoundSchema) {
        return QString("%1 needs schemaVersion %2, the first version with sounds and saving. Change schemaVersion to %2 and add a \"sounds\" list.").arg(label).arg(SoundSchema);
    }

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
    if (info.usesVariable) block.variable = legacyScore ? QString(ScoreVariableName) : object.value("variable").toString();
    if (info.usesSound) block.sound = object.value("sound").toString();
    if (info.usesPosition) {
        block.x = numberOrInvalid(object.value("x"));
        block.y = numberOrInvalid(object.value("y"));
    }
    if (info.usesColor) block.color = object.value("color").toString();
    if (info.usesValue) {
        const auto number = object.value("value");
        const auto amount = number.toDouble();
        if (!number.isDouble() || amount != std::floor(amount) || std::abs(amount) > MaximumVariableValue) {
            return QString("%1 needs a whole number from -%2 to %2.").arg(label).arg(MaximumVariableValue);
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
        return parseChildren(list, depth, parsing, block);
    }
    if (!actions.isUndefined()) return QString("%1 cannot hold other blocks. Remove its \"actions\" list.").arg(label);
    return {};
}

QString parseVariables(const QJsonArray& list, QList<LogicVariable>& variables) {
    for (qsizetype index = 0; index < list.size(); ++index) {
        if (!list.at(index).isObject()) return QString("Variable number %1 must be a JSON object.").arg(index + 1);
        const auto object = list.at(index).toObject();
        const auto name = object.value("name").toString();
        const auto initial = object.value("initial");
        const auto number = initial.toDouble();
        if (!initial.isDouble() || number != std::floor(number) || std::abs(number) > MaximumVariableValue) {
            return QString("The variable \"%1\" needs a whole number from -%2 to %2 to start at.").arg(name).arg(MaximumVariableValue);
        }
        variables.append({name, static_cast<int>(number)});
    }
    return {};
}

QString parseSounds(const QJsonArray& list, QList<ProjectSound>& sounds) {
    for (qsizetype index = 0; index < list.size(); ++index) {
        if (!list.at(index).isObject()) return QString("Sound number %1 must be a JSON object.").arg(index + 1);
        const auto object = list.at(index).toObject();
        sounds.append({object.value("name").toString(), object.value("source").toString()});
    }
    return {};
}

QJsonArray soundsToJson(const QList<ProjectSound>& sounds) {
    QJsonArray list;
    for (const auto& sound : sounds) list.append(QJsonObject{{"name", sound.name}, {"source", sound.source}});
    return list;
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
    if (info.usesVariable) object.insert("variable", block.variable);
    if (info.usesSound) object.insert("sound", block.sound);
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

QJsonArray variablesToJson(const QList<LogicVariable>& variables) {
    QJsonArray list;
    for (const auto& variable : variables) list.append(QJsonObject{{"name", variable.name}, {"initial", variable.initial}});
    return list;
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

QByteArray comparisonOperator(VariableComparison comparison) {
    switch (comparison) {
    case VariableComparison::Equal: return "==";
    case VariableComparison::NotEqual: return "!=";
    case VariableComparison::Less: return "<";
    case VariableComparison::LessOrEqual: return "<=";
    case VariableComparison::Greater: return ">";
    case VariableComparison::GreaterOrEqual: return ">=";
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
    Generator(const Scene& scene, const LogicProgram& program) {
        for (qsizetype index = 0; index < scene.items.size(); ++index) itemIndexes.insert(scene.items[index].id, index);
        for (qsizetype index = 0; index < program.variables.size(); ++index) variableIndexes.insert(program.variables[index].name, index);
        for (qsizetype index = 0; index < program.sounds.size(); ++index) soundIndexes.insert(program.sounds[index].name, index);
    }

    QByteArray index(const QString& itemId) const { return QByteArray::number(itemIndexes.value(itemId)); }

    QByteArray variable(const QString& name) const { return QByteArray::number(variableIndexes.value(name)); }

    QByteArray sound(const QString& name) const { return QByteArray::number(soundIndexes.value(name)); }

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
        case BlockType::SetVariable:
            code += pad + "world.SetVariable(" + variable(block.variable) + ", " + QByteArray::number(block.value) + ");" + comment;
            return;
        case BlockType::ChangeVariable:
            code += pad + "world.ChangeVariable(" + variable(block.variable) + ", " + QByteArray::number(block.value) + ");" + comment;
            return;
        case BlockType::BurstParticles:
            code += pad + "world.BurstParticles(" + index(block.item) + ");" + comment;
            return;
        case BlockType::PlaySound:
            code += pad + "world.PlaySound(" + sound(block.sound) + ");" + comment;
            return;
        case BlockType::PlayMusic:
            code += pad + "world.PlayMusic(" + sound(block.sound) + ");" + comment;
            return;
        case BlockType::StopMusic:
            code += pad + "world.StopMusic();" + comment;
            return;
        case BlockType::SetVolume:
            code += pad + "world.SetVolume(" + QByteArray::number(block.value) + ");" + comment;
            return;
        case BlockType::SaveVariables:
            code += pad + "world.SaveVariables();" + comment;
            return;
        case BlockType::LoadVariables:
            code += pad + "world.LoadVariables();" + comment;
            return;
        case BlockType::IfVariable:
            code += pad + "if (world.Variable(" + variable(block.variable) + ") " + comparisonOperator(block.comparison) + ' ' + QByteArray::number(block.value) + ") {" + comment;
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
    QHash<QString, qsizetype> variableIndexes;
    QHash<QString, qsizetype> soundIndexes;
};

void collectBurstTargets(const QList<Block>& blocks, QSet<QString>& targets) {
    for (const auto& block : blocks) {
        if (block.type == BlockType::BurstParticles) targets.insert(block.item);
        collectBurstTargets(block.children, targets);
    }
}

bool referencesSounds(const QList<Block>& blocks) {
    return std::ranges::any_of(blocks, [](const Block& block) {
        return blockInfo(block.type).usesSound || referencesSounds(block.children);
    });
}

bool usesSaveData(const QList<Block>& blocks) {
    return std::ranges::any_of(blocks, [](const Block& block) {
        return block.type == BlockType::SaveVariables || block.type == BlockType::LoadVariables || usesSaveData(block.children);
    });
}

bool referencesItems(const QList<Block>& blocks) {
    for (const auto& block : blocks) {
        const auto& info = blockInfo(block.type);
        if (info.usesItem || info.usesOtherItem || referencesItems(block.children)) return true;
    }
    return false;
}

QByteArray assembleHeader(bool includeScene, bool includeSounds, bool includeButtons, int itemCount, int soundCount, int touchEvents, const QByteArray& startCode, const QByteArray& frameCode) {
    QByteArray header = "// Generated by AnyDevKit Studio from logic.json. Do not edit.\n#pragma once\n\n";
    if (includeScene) header += "#include \"GeneratedScene.hpp\"\n";
    if (includeSounds) header += "#include \"GeneratedSounds.hpp\"\n";
    if (includeButtons) header += "#include \"Input/Buttons.hpp\"\n";
    if (includeScene || includeSounds || includeButtons) header += '\n';
    header += "#include <array>\n#include <cstddef>\n\nnamespace GeneratedLogic {\n\n";
    if (includeScene) {
        header += "static_assert(GeneratedScene::Items.size() == " + QByteArray::number(itemCount)
            + ", \"GeneratedScene.hpp and GeneratedLogic.hpp do not match. Build the project again from Studio.\");\n\n";
    }
    if (includeSounds) {
        header += "static_assert(GeneratedSounds::SoundCount == " + QByteArray::number(soundCount)
            + ", \"GeneratedSounds.hpp and GeneratedLogic.hpp do not match. Build the project again from Studio.\");\n\n";
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
    //                                                                              event  holds  button item   other  pos    color  value  cmp    variable sound
    static const QList<BlockInfo> catalog{
        {BlockType::WhenGameStarts, "whenGameStarts", "When the game starts",        true,  true,  false, false, false, false, false, false, false, false, false},
        {BlockType::WhileButtonHeld, "whileButtonHeld", "While a button is held",    true,  true,  true,  false, false, false, false, false, false, false, false},
        {BlockType::EveryFrame, "everyFrame", "Every frame",                         true,  true,  false, false, false, false, false, false, false, false, false},
        {BlockType::WhenItemsTouch, "whenItemsTouch", "When two items touch",        true,  true,  false, true,  true,  false, false, false, false, false, false},
        {BlockType::MoveBy, "moveBy", "Move item",                                   false, false, false, true,  false, true,  false, false, false, false, false},
        {BlockType::SetPosition, "setPosition", "Set item position",                 false, false, false, true,  false, true,  false, false, false, false, false},
        {BlockType::Show, "show", "Show item",                                       false, false, false, true,  false, false, false, false, false, false, false},
        {BlockType::Hide, "hide", "Hide item",                                       false, false, false, true,  false, false, false, false, false, false, false},
        {BlockType::SetColor, "setColor", "Set item color",                          false, false, false, true,  false, false, true,  false, false, false, false},
        {BlockType::SetVariable, "setVariable", "Set variable",                      false, false, false, false, false, false, false, true,  false, true,  false},
        {BlockType::ChangeVariable, "changeVariable", "Change variable",             false, false, false, false, false, false, false, true,  false, true,  false},
        {BlockType::IfVariable, "ifVariable", "If variable",                         false, true,  false, false, false, false, false, true,  true,  true,  false},
        {BlockType::BurstParticles, "burstParticles", "Burst particles from item",   false, false, false, true,  false, false, false, false, false, false, false},
        {BlockType::PlaySound, "playSound", "Play sound",                            false, false, false, false, false, false, false, false, false, false, true},
        {BlockType::PlayMusic, "playMusic", "Play music on loop",                    false, false, false, false, false, false, false, false, false, false, true},
        {BlockType::StopMusic, "stopMusic", "Stop music",                            false, false, false, false, false, false, false, false, false, false, false},
        {BlockType::SetVolume, "setVolume", "Set volume",                            false, false, false, false, false, false, false, true,  false, false, false},
        {BlockType::SaveVariables, "saveVariables", "Save variables",                false, false, false, false, false, false, false, false, false, false, false},
        {BlockType::LoadVariables, "loadVariables", "Load variables",                false, false, false, false, false, false, false, false, false, false, false},
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
        {VariableComparison::Equal, "equal", "is equal to"},
        {VariableComparison::NotEqual, "notEqual", "is not equal to"},
        {VariableComparison::Less, "less", "is less than"},
        {VariableComparison::LessOrEqual, "lessOrEqual", "is at most"},
        {VariableComparison::Greater, "greater", "is greater than"},
        {VariableComparison::GreaterOrEqual, "greaterOrEqual", "is at least"},
    };
    return catalog;
}

QList<LogicProblem> validateLogicStructure(const LogicProgram& program) {
    return Validator(nullptr, program).run(program);
}

QList<LogicProblem> validateLogic(const LogicProgram& program, const Scene& scene) {
    return Validator(&scene, program).run(program);
}

QList<LogicProblem> validateTextBindings(const Scene& scene, const LogicProgram& program) {
    QList<LogicProblem> problems;
    for (const auto& item : scene.items) {
        if (item.type != "text") continue;
        for (const auto& name : textVariables(item.text)) {
            if (std::ranges::find(program.variables, name, &LogicVariable::name) != program.variables.end()) continue;
            const auto shown = item.name.isEmpty() ? item.id : item.name;
            problems.append({{}, QString("The text item \"%1\" shows the variable \"%2\", which does not exist. Create it in the Variables area on the Logic tab, or fix the text in the Scene tab.").arg(shown, name)});
        }
    }
    return problems;
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
    const auto version = schema.toInt(-1);
    if (!schema.isDouble() || version < OldestLoadableSchema || version > CurrentSchema) return failure(QString("%1 needs a schemaVersion from %2 to %3, the versions this Studio supports.").arg(fileName).arg(OldestLoadableSchema).arg(CurrentSchema));
    if (!root.value("events").isArray()) return failure(QString("%1 has no events list. Add \"events\": [] to the file.").arg(fileName));

    LogicProgram program;
    if (version >= VariableSchema) {
        if (!root.value("variables").isArray()) return failure(QString("%1 has no variables list. Add \"variables\": [] to the file.").arg(fileName));
        const auto problem = parseVariables(root.value("variables").toArray(), program.variables);
        if (!problem.isEmpty()) return failure(problem);
    }
    if (version >= SoundSchema) {
        if (!root.value("sounds").isArray()) return failure(QString("%1 has no sounds list. Add \"sounds\": [] to the file.").arg(fileName));
        const auto problem = parseSounds(root.value("sounds").toArray(), program.sounds);
        if (!problem.isEmpty()) return failure(problem);
    }

    Parsing parsing;
    parsing.schema = version;
    const auto events = root.value("events").toArray();
    for (qsizetype index = 0; index < events.size(); ++index) {
        Block block;
        const auto problem = parseBlock(events.at(index), index, 1, parsing, block);
        if (!problem.isEmpty()) return failure(problem);
        program.events.append(block);
    }
    if (parsing.usesScore) program.variables.append({ScoreVariableName, 0});

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
    const QJsonObject root{{"schemaVersion", CurrentSchema}, {"variables", variablesToJson(program.variables)}, {"sounds", soundsToJson(program.sounds)}, {"events", events}};
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
    return assembleHeader(false, false, false, 0, 0, 0, {}, {});
}

QByteArray generateLogicHeader(const LogicProgram& program, const Scene& scene) {
    if (!validateScene(scene).isEmpty() || !validateLogic(program, scene).isEmpty()) return {};
    if (program.events.isEmpty()) return generateEmptyLogicHeader();

    const Generator generator(scene, program);
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
    return assembleHeader(referencesItems(program.events), referencesSounds(program.events), usesButtons, static_cast<int>(scene.items.size()), static_cast<int>(program.sounds.size()), touchEvents, startCode, frameCode);
}

QByteArray generateVariablesHeader(const LogicProgram& program, const Scene& scene) {
    if (!validateScene(scene).isEmpty() || !validateLogic(program, scene).isEmpty() || !validateTextBindings(scene, program).isEmpty()) return {};

    QHash<QString, qsizetype> variableIndexes;
    QByteArrayList initialValues;
    QByteArrayList names;
    QByteArrayList hashes;
    for (qsizetype index = 0; index < program.variables.size(); ++index) {
        variableIndexes.insert(program.variables[index].name, index);
        initialValues << QByteArray::number(program.variables[index].initial);
        names << program.variables[index].name.toLatin1(); // names are letters, digits and _, safe in a comment
        std::uint32_t hash = 2166136261u; // FNV-1a, the same as SaveData::NameHash in the runtime
        for (const char byte : program.variables[index].name.toUtf8()) hash = (hash ^ static_cast<unsigned char>(byte)) * 16777619u;
        hashes << QByteArray::number(hash) + "u";
    }

    QByteArray parts;
    QByteArray bound;
    QByteArray asserts;
    int partCount = 0;
    int boundCount = 0;
    for (qsizetype itemIndex = 0; itemIndex < scene.items.size(); ++itemIndex) {
        const auto& item = scene.items[itemIndex];
        if (item.type != "text" || !textUsesVariables(item.text)) continue;
        const auto parsed = parseTextTemplate(item.text);
        const auto firstPart = partCount;
        int quads = 0;
        for (const auto& part : parsed.parts) {
            if (part.variable.isEmpty()) {
                parts += "    {\"" + textLiteral(part.text) + "\", -1},\n";
                quads += part.text.size();
            } else {
                parts += "    {\"\", " + QByteArray::number(variableIndexes.value(part.variable)) + "},\n";
                quads += MaximumVariableCharacters;
            }
            ++partCount;
        }
        bound += "    {" + QByteArray::number(itemIndex) + ", " + QByteArray::number(firstPart) + ", " + QByteArray::number(partCount - firstPart) + ", " + QByteArray::number(quads) + "},\n";
        asserts += "static_assert(GeneratedScene::Items[" + QByteArray::number(itemIndex) + "].text.size() == " + QByteArray::number(quads) + ", \"GeneratedScene.hpp and GeneratedVariables.hpp do not match. Build the project again from Studio.\");\n";
        ++boundCount;
    }

    QByteArray header = "// Generated by AnyDevKit Studio from logic.json and scene.json. Do not edit.\n#pragma once\n\n";
    if (boundCount > 0) header += "#include \"GeneratedScene.hpp\"\n\n";
    header += "#include <array>\n#include <cstddef>\n#include <cstdint>\n#include <string_view>\n\nnamespace GeneratedVariables {\n\n";
    header += "inline constexpr std::size_t VariableCount = " + QByteArray::number(program.variables.size()) + ";\n";
    header += "inline constexpr std::array<std::int32_t, " + QByteArray::number(program.variables.size()) + "> InitialValues";
    header += initialValues.isEmpty() ? QByteArray("{};\n") : "{{" + initialValues.join(", ") + "}}; // " + names.join(", ") + "\n";
    header += "inline constexpr std::array<std::uint32_t, " + QByteArray::number(program.variables.size()) + "> NameHashes";
    header += hashes.isEmpty() ? QByteArray("{};\n") : "{{" + hashes.join(", ") + "}}; // how a saved value finds its variable\n";
    header += QByteArray("inline constexpr bool UsesSaveData = ") + (usesSaveData(program.events) ? "true" : "false") + ";\n";
    header += R"cpp(
// One piece of a text item that shows variables: plain text, or a variable's value when variable is not -1.
struct Part {
    std::string_view text;
    std::int32_t variable;
};

// A text item that shows variables. quads is how many characters it can ever need, sign and all digits included.
struct BoundText {
    std::size_t item, firstPart, partCount, quads;
};

)cpp";
    if (boundCount == 0) {
        header += "inline constexpr std::array<Part, 0> Parts{};\ninline constexpr std::array<BoundText, 0> BoundTexts{};\n";
    } else {
        header += "inline constexpr std::array<Part, " + QByteArray::number(partCount) + "> Parts{{\n" + parts + "}};\n";
        header += "inline constexpr std::array<BoundText, " + QByteArray::number(boundCount) + "> BoundTexts{{\n" + bound + "}};\n\n";
        header += "static_assert(GeneratedScene::Items.size() == " + QByteArray::number(scene.items.size()) + ", \"GeneratedScene.hpp and GeneratedVariables.hpp do not match. Build the project again from Studio.\");\n";
        header += asserts;
    }
    header += "\n}\n";
    return header;
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

    const auto describeProblems = [](const QList<LogicProblem>& problems) {
        auto message = problems.first().message;
        const auto others = problems.size() - 1;
        if (others == 1) message += " There is 1 more problem to fix.";
        if (others > 1) message += QString(" There are %1 more problems to fix.").arg(others);
        return message;
    };

    LogicProgram program;
    QByteArray header;
    if (!logicInfo.exists() && !logicInfo.isSymLink()) {
        header = generateEmptyLogicHeader();
    } else {
        const auto loaded = loadLogic(logicPath);
        if (!loaded.program) return fail(loaded.error);
        program = *loaded.program;
        const auto problems = validateLogic(program, scene);
        if (!problems.isEmpty()) return fail(describeProblems(problems));
        header = generateLogicHeader(program, scene);
        if (header.isEmpty()) return fail("The game logic could not be generated because the scene has a problem. Fix the scene and try again.");
    }
    const auto textProblems = validateTextBindings(scene, program);
    if (!textProblems.isEmpty()) return fail(describeProblems(textProblems));
    const auto variablesHeader = generateVariablesHeader(program, scene);
    if (variablesHeader.isEmpty()) return fail("The game logic could not be generated because the scene has a problem. Fix the scene and try again.");
    QString soundProblem;
    if (!exportSounds(root, program.sounds, soundProblem)) return fail(soundProblem);

    // A link could lead outside the project, and a build must only change files inside it.
    const auto folder = root + '/' + HeaderFolder;
    const auto path = folder + '/' + HeaderFileName;
    const auto variablesPath = folder + '/' + VariablesHeaderFileName;
    for (const auto& candidate : {folder, path, variablesPath}) {
        const QFileInfo info(candidate);
        if (info.isSymLink() || info.isJunction()) return fail(QString("%1 is a link, and Studio will not write through it. Replace it with a regular file or folder and try again.").arg(QDir(root).relativeFilePath(candidate)));
    }
    if (!QDir().mkpath(folder)) return fail(QString("%1 could not be created. Check that the project folder is writable.").arg(HeaderFolder));

    for (const auto& [target, contents, name] : {std::tuple(path, header, HeaderFileName), std::tuple(variablesPath, variablesHeader, VariablesHeaderFileName)}) {
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
            return fail(QString("%1/%2 could not be written: %3. Check that the folder is writable.").arg(HeaderFolder, name, file.errorString()));
        }
    }
    return true;
}

}
