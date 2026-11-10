#include "Blocks.h"
#include "Formula.h"

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
#include <functional>
#include <limits>
#include <tuple>
#include <utility>

namespace Studio {

namespace {

// The version saved today. Older files still load: version 1's score blocks become variable blocks,
// version 2 has no sounds, version 3 adds sounds, saving, animation, flip and camera blocks, and in
// version 4 logic is per scene, in a "scenes" list, and there are physics and Go to scene blocks. Version 5 adds
// formulas, players, templates and their copies, behaviors, and player and copy variables.
constexpr int CurrentSchema = 5;
constexpr int LanguageSchema = 5;
constexpr int OldestLoadableSchema = 1;
constexpr int VariableSchema = 2;
constexpr int SoundSchema = 3; // sounds and the blocks that play them, set the volume, save or load
constexpr int WorldBlockSchema = 3;
constexpr int PhysicsBlockSchema = 4;
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

bool isSpecialItem(const QString& id) {
    return id == QLatin1String(SelfItem) || id == QLatin1String(PlayerItem) || id == QLatin1String(NearestPlayerItem);
}

bool isLanguageBlock(BlockType type) {
    return type >= BlockType::SetFormula;
}

// Blocks whose item must be a template: they work on its copies.
bool needsTemplate(BlockType type) {
    return type == BlockType::EveryFrameForEachCopy || type == BlockType::WhenCopyTouches || type == BlockType::SpawnCopy || type == BlockType::ForEachCopy || type == BlockType::AlertNearby;
}

// Blocks whose actions run for one copy at a time.
bool startsCopyContext(BlockType type) {
    return type == BlockType::EveryFrameForEachCopy || type == BlockType::WhenCopyTouches || type == BlockType::SpawnCopy || type == BlockType::ForEachCopy;
}

bool isIfBlock(BlockType type) {
    return type == BlockType::IfVariable || type == BlockType::IfFormula || type == BlockType::IfItemDistance || type == BlockType::IfOnGround;
}

bool isPlayerButtonEvent(BlockType type) {
    return type == BlockType::WhenPlayerPresses || type == BlockType::WhilePlayerHolds;
}

QString scopeName(VariableScope scope) {
    return scope == VariableScope::Player ? QStringLiteral("player") : scope == VariableScope::Copy ? QStringLiteral("copy") : QStringLiteral("game");
}

// One walk over the tree, so problems come out in the order the blocks appear. With no scene, only
// the rules a saved file must follow are checked.
class Validator {
public:
    Validator(const Scene* scene, const LogicProgram& program, const QStringList& sceneIds = {}) : scene(scene), sceneIds(sceneIds) {
        for (const auto& variable : program.variables) {
            variableNames.insert(variable.name);
            variableScopes.insert(variable.name, variable.scope);
        }
        for (const auto& sound : program.sounds) soundNames.insert(sound.name);
        if (scene) {
            for (const auto& item : scene->items) {
                itemIds.insert(item.id);
                itemsById.insert(item.id, item);
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
        visitList(program.events, 1, {});
        // Another scene's blocks are not checked against this scene, only that each is a well formed block.
        const auto* checkedScene = scene;
        scene = nullptr;
        for (const auto& other : program.otherScenes) {
            if (other.events.size() > MaximumLogicEvents) {
                problems.append({{}, QString("The scene \"%1\" has %2 events, but a scene can hold at most %3. Remove some events and try again.").arg(other.scene).arg(other.events.size()).arg(MaximumLogicEvents)});
                continue;
            }
            qsizetype total = 0;
            countBlocks(other.events, 1, total);
            if (total > MaximumLogicBlocks) {
                problems.append({{}, QString("The scene \"%1\" has %2 blocks, but a scene can hold at most %3. Remove some blocks and try again.").arg(other.scene).arg(total).arg(MaximumLogicBlocks)});
                continue;
            }
            visitList(other.events, 1, {});
        }
        scene = checkedScene;
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
            if (folded == "player" || folded == "players" || isFormulaPlaceName(variable.name)) {
                problems.append({{}, QString("\"%1\" is a name formulas already use. Give the variable another name.").arg(variable.name)});
            }
        }
    }

    void report(const Block& block, const QString& message) {
        problems.append({block.id, message});
    }

    struct Context {
        QString copyTemplate; // the template whose copy the blocks run for; empty outside copy blocks
    };

    bool isTemplate(const QString& id) const { return itemsById.contains(id) && itemsById.value(id).copies > 0; }

    // The scene item a choice stands for, when it is known before the game runs.
    const SceneItem* resolve(const QString& id, const Context& context) const {
        if (id == QLatin1String(SelfItem)) return itemsById.contains(context.copyTemplate) ? &*itemsById.find(context.copyTemplate) : nullptr;
        return itemsById.contains(id) ? &*itemsById.find(id) : nullptr;
    }

    void checkItem(const Block& block, const QString& id, const Context& context, bool wantsTemplate = false) {
        if (!scene) return;
        const auto label = describeBlock(block);
        if (id == QLatin1String(SelfItem) || id == QLatin1String(NearestPlayerItem)) {
            if (wantsTemplate) report(block, QString("%1 needs a template, not %2. Choose an item with copies.").arg(label, id));
            else if (context.copyTemplate.isEmpty()) report(block, QString("%1 uses %2, which only means something for a copy: inside \"Every frame, for each copy of\", \"When a copy touches\", \"Spawn a copy\" or \"For each copy of\".").arg(label, id));
            return;
        }
        if (id == QLatin1String(PlayerItem)) {
            if (wantsTemplate) report(block, QString("%1 needs a template, not %2. Choose an item with copies.").arg(label, id));
            return;
        }
        if (id.isEmpty()) {
            report(block, QString("%1 has no item chosen. Choose an item from the scene.").arg(label));
        } else if (!itemIds.contains(id)) {
            report(block, QString("%1 uses the item \"%2\", which is not in the scene. Choose an item that exists, or delete this block.").arg(label, id));
        } else if (wantsTemplate && (!isTemplate(id) || !itemsById.value(id).follows.isEmpty())) {
            if (isTemplate(id)) report(block, QString("%1 uses \"%2\", which follows another template and comes and goes with it. Choose the template it follows.").arg(label, id));
            else report(block, QString("%1 needs a template, but \"%2\" is an ordinary item. Give it a number of copies in the Scene tab, or choose another item.").arg(label, id));
        } else if (!wantsTemplate && isTemplate(id)) {
            report(block, QString("%1 uses the template \"%2\" directly. A template stands for its copies: use @self inside one of its copy blocks, such as \"Every frame, for each copy of\".").arg(label, id));
        }
    }

    void checkVariable(const Block& block, const Context& context) {
        if (!scene) return;
        if (block.variable.isEmpty()) {
            report(block, QString("%1 has no variable chosen. Choose a variable, or add one in the Variables area.").arg(describeBlock(block)));
        } else if (!variableNames.contains(block.variable)) {
            report(block, QString("%1 uses the variable \"%2\", which does not exist. Choose another variable, or add one named %2 in the Variables area.").arg(describeBlock(block), block.variable));
        } else if (block.type == BlockType::AlertNearby) {
            if (variableScopes.value(block.variable) != VariableScope::Copy) report(block, QString("%1 sets \"%2\" on other copies, so it must be a copy variable. Choose a copy variable.").arg(describeBlock(block), block.variable));
        } else if (variableScopes.value(block.variable) == VariableScope::Copy && context.copyTemplate.isEmpty()) {
            report(block, QString("%1 uses the copy variable \"%2\", which each copy has its own of. Use it inside a copy block, such as \"Every frame, for each copy of\".").arg(describeBlock(block), block.variable));
        }
    }

    void checkFormula(const Block& block, const Context& context) {
        const auto label = describeBlock(block);
        const auto result = compileFormula(block.formula, [&](const QString& name) {
            const auto lower = name.toLower();
            if (lower == "player") return FormulaName{FormulaNameKind::Player, 0};
            if (lower == "players") return FormulaName{FormulaNameKind::Players, 0};
            if (!variableNames.contains(name)) return FormulaName{};
            const auto scope = variableScopes.value(name);
            return FormulaName{scope == VariableScope::Player ? FormulaNameKind::PlayerVariable : scope == VariableScope::Copy ? FormulaNameKind::CopyVariable : FormulaNameKind::GameVariable, 0};
        });
        if (!result.problem.isEmpty()) {
            // Without a scene a file may be unfinished, so only its form is checked.
            if (scene || compileFormula(block.formula).problem.size() > 0) report(block, QString("%1: %2").arg(label, scene ? result.problem : compileFormula(block.formula).problem));
            return;
        }
        if (scene && result.usesCopyVariables && context.copyTemplate.isEmpty()) {
            report(block, QString("%1 uses a copy variable, which each copy has its own of. Use it inside a copy block, such as \"Every frame, for each copy of\".").arg(label));
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

    void checkWorldBlock(const Block& block, const QString& label, const SceneItem& item) {
        if (block.type == BlockType::SetLook && !isSpriteSheet(item)) {
            report(block, QString("%1 uses the item \"%2\", which is not a sprite sheet. Choose an image with frames, and give it more than one look.").arg(label, block.item));
        }
        if (block.type == BlockType::PlayAnimation) {
            if (!isSpriteSheet(item)) {
                report(block, QString("%1 uses the item \"%2\", which is not a sprite sheet. Choose an image with frames, or give this image a frame size.").arg(label, block.item));
            } else if (!block.animation.isEmpty() && std::ranges::find(item.animations, block.animation, &SceneAnimation::name) == item.animations.end()) {
                report(block, QString("%1 plays the animation \"%2\", which this item does not have. Choose an animation it has.").arg(label, block.animation));
            }
        } else if (block.type == BlockType::FlipItem && item.type != "image") {
            report(block, QString("%1 uses the item \"%2\", but only an image can be flipped.").arg(label, block.item));
        } else if ((block.type == BlockType::CameraFollow || block.type == BlockType::CameraFollowBoth) && item.screenFixed) {
            report(block, QString("%1 follows the item \"%2\", which has \"Stays on screen\" turned on, so the camera could never leave it. Choose another item.").arg(label, block.item));
        }
        if (isBodyBlock(block.type) && item.physics != "body") {
            report(block, QString("%1 uses the item \"%2\", which is not a moving body. Turn on Physics, Moving body for it in the Scene tab, or choose another item.").arg(label, block.item));
        }
    }

    static bool isBodyBlock(BlockType type) {
        return type == BlockType::WhenItemLands || type == BlockType::WhenItemHitsSolid || type == BlockType::WhenItemLeavesWorld || type == BlockType::SetVelocity || type == BlockType::JumpItem || type == BlockType::IfOnGround || type == BlockType::ChaseItem;
    }

    void checkScene(const Block& block) {
        if (!scene) return;
        if (block.scene.isEmpty()) {
            report(block, QString("%1 has no scene chosen. Choose a scene to go to.").arg(describeBlock(block)));
        } else if (!sceneIds.isEmpty() && !sceneIds.contains(block.scene)) {
            report(block, QString("%1 goes to the scene \"%2\", which is not in this project. Choose a scene that exists, or delete this block.").arg(describeBlock(block), block.scene));
        }
    }

    void visitList(const QList<Block>& blocks, int depth, const Context& context) {
        const Block* previous = nullptr;
        for (const auto& block : blocks) {
            if (block.type == BlockType::Otherwise && (!previous || !isIfBlock(previous->type))) {
                report(block, QString("%1 must come right after an If block, and runs when that one does not. Move it under an If block.").arg(describeBlock(block)));
            }
            visit(block, depth, context);
            previous = &block;
        }
    }

    void visit(const Block& block, int depth, const Context& context) {
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

        if (info.usesItem) checkItem(block, block.item, context, needsTemplate(block.type));
        if (info.usesVariable) checkVariable(block, context);
        if (info.usesFormula) checkFormula(block, context);
        if (info.usesRadius && (block.radius < 0 || block.radius > MaximumRadius)) report(block, QString("%1 needs a distance from 0 to %2.").arg(label).arg(MaximumRadius));
        if ((block.type == BlockType::Wander || block.type == BlockType::Flee) && (block.value < 0 || block.value > MaximumChaseSpeed)) {
            report(block, QString("%1 needs a speed from 0 to %2 units per second.").arg(label).arg(MaximumChaseSpeed));
        }
        if (isPlayerButtonEvent(block.type) && block.button >= ControllerButton::P2Cross) {
            report(block, QString("%1 runs for every player with their own controller, so choose a button without \"Player 2\" in its name.").arg(label));
        }
        if (block.type == BlockType::RemoveCopy && block.item != QLatin1String(SelfItem)) {
            report(block, QString("%1 removes the copy it runs for, so its item must be @self.").arg(label));
        }
        if (scene && block.type == BlockType::SpawnCopy && block.otherItem == QLatin1String(NearestPlayerItem) && context.copyTemplate.isEmpty()) {
            report(block, QString("%1 spawns at %2, which only means something inside a copy block.").arg(label, block.otherItem));
        }
        if (scene && block.type == BlockType::WhenCopyTouches && (block.otherItem == QLatin1String(SelfItem) || block.otherItem == QLatin1String(NearestPlayerItem))) {
            report(block, QString("%1 checks each copy against one other item. Choose a scene item or @player.").arg(label));
        }
        if (info.usesSound) checkSound(block);
        if (info.usesScene) checkScene(block);
        if (block.type == BlockType::IfItemDistance && block.value < 0) report(block, QString("%1 needs a distance of 0 or more.").arg(label));
        if (block.type == BlockType::ChaseItem && (block.value < 0 || block.value > MaximumChaseSpeed)) {
            report(block, QString("%1 needs a speed from 0 to %2 units per second.").arg(label).arg(MaximumChaseSpeed));
        }
        if (block.type == BlockType::SetVolume && (block.value < 0 || block.value > MaximumVolumePercent)) {
            report(block, QString("%1 needs a volume from 0 to %2 percent.").arg(label).arg(MaximumVolumePercent));
        }
        if (block.type == BlockType::BurstParticles && scene && itemIds.contains(block.item) && !particleIds.contains(block.item)) {
            report(block, QString("%1 uses the item \"%2\", which is not a Particles item. Choose a Particles item to burst from.").arg(label, block.item));
        }
        if (const auto* resolved = scene ? resolve(block.item, context) : nullptr) checkWorldBlock(block, label, *resolved);
        if (block.type == BlockType::CameraFollowBoth && scene && itemIds.contains(block.otherItem) && itemsById.value(block.otherItem).screenFixed) {
            report(block, QString("%1 follows the item \"%2\", which has \"Stays on screen\" turned on, so the camera could never leave it. Choose another item.").arg(label, block.otherItem));
        }
        if (info.usesAnimation && scene && block.animation.isEmpty()) report(block, QString("%1 has no animation chosen. Choose one of this item's animations.").arg(label));
        if (info.usesOtherItem) {
            const auto touchesPlayer = block.type == BlockType::WhenCopyTouches && block.otherItem == QLatin1String(PlayerItem);
            if (!touchesPlayer && !(block.type == BlockType::WhenCopyTouches && isSpecialItem(block.otherItem))) checkItem(block, block.otherItem, context);
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
        auto inner = context;
        if (startsCopyContext(block.type) && !isSpecialItem(block.item)) inner.copyTemplate = block.item;
        visitList(block.children, depth + 1, inner);
    }

    const Scene* scene;
    QStringList sceneIds;
    QSet<QString> itemIds;
    QSet<QString> particleIds;
    QHash<QString, SceneItem> itemsById;
    QSet<QString> variableNames;
    QHash<QString, VariableScope> variableScopes;
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

bool isWorldBlock(BlockType type) {
    return type == BlockType::PlayAnimation || type == BlockType::FlipItem || type == BlockType::CameraFollow || type == BlockType::SetCameraPosition || type == BlockType::CameraFollowBoth;
}

bool isPhysicsBlock(BlockType type) {
    return type == BlockType::WhenItemLands || type == BlockType::WhenItemHitsSolid || type == BlockType::WhenItemLeavesWorld || type == BlockType::SetVelocity || type == BlockType::JumpItem || type == BlockType::IfOnGround || type == BlockType::GoToScene || type == BlockType::RestartScene || type == BlockType::WhenButtonPressed || type == BlockType::IfItemDistance || type == BlockType::ChaseItem || type == BlockType::PutAtItem || type == BlockType::PauseGame || type == BlockType::ResumeGame || type == BlockType::CameraFollowBoth;
}

// The stick directions, Options and the second player's buttons came with the top-down runtime or later.
bool usesStickButton(const Block& block) {
    return blockInfo(block.type).usesButton && block.button >= ControllerButton::MoveUp;
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
    if (parsing.schema < WorldBlockSchema && isWorldBlock(info.type)) {
        return QString("%1 needs schemaVersion %2. Change schemaVersion to %2 and try again.").arg(label).arg(WorldBlockSchema);
    }
    if (parsing.schema < PhysicsBlockSchema && isPhysicsBlock(info.type)) {
        return QString("%1 needs schemaVersion %2. Change schemaVersion to %2 and try again.").arg(label).arg(PhysicsBlockSchema);
    }
    if (info.usesVariable && parsing.schema < VariableSchema && !legacyScore) {
        return QString("%1 uses a variable, which needs schemaVersion %2. Change schemaVersion to %2 and add a \"variables\" list.").arg(label).arg(VariableSchema);
    }
    if (isLanguageBlock(info.type) && parsing.schema < LanguageSchema) {
        return QString("%1 needs schemaVersion %2. Change schemaVersion to %2 and try again.").arg(label).arg(LanguageSchema);
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
    if (info.usesAnimation) block.animation = object.value("animation").toString();
    if (info.usesScene) block.scene = object.value("scene").toString();
    if (info.usesFlipped) {
        const auto flipped = object.value("flipped");
        if (!flipped.isBool()) return QString("%1 needs flipped to be true or false.").arg(label);
        block.flipped = flipped.toBool();
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
    if (info.usesFormula) {
        const auto formula = object.value("formula");
        if (!formula.isString()) return QString("%1 needs a formula, written as text.").arg(label);
        block.formula = formula.toString();
    }
    if (info.usesRadius) {
        const auto radius = object.value("radius");
        if (!radius.isDouble() || radius.toDouble() != std::floor(radius.toDouble()) || radius.toDouble() < 0 || radius.toDouble() > MaximumRadius) {
            return QString("%1 needs a radius, a whole number from 0 to %2.").arg(label).arg(MaximumRadius);
        }
        block.radius = static_cast<int>(radius.toDouble());
    }
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

QString parseVariables(const QJsonArray& list, QList<LogicVariable>& variables, int schema) {
    for (qsizetype index = 0; index < list.size(); ++index) {
        if (!list.at(index).isObject()) return QString("Variable number %1 must be a JSON object.").arg(index + 1);
        const auto object = list.at(index).toObject();
        const auto name = object.value("name").toString();
        const auto initial = object.value("initial");
        const auto number = initial.toDouble();
        if (!initial.isDouble() || number != std::floor(number) || std::abs(number) > MaximumVariableValue) {
            return QString("The variable \"%1\" needs a whole number from -%2 to %2 to start at.").arg(name).arg(MaximumVariableValue);
        }
        auto scope = VariableScope::Game;
        if (const auto value = object.value("scope"); !value.isUndefined()) {
            const auto text = value.toString();
            if (text == "player") scope = VariableScope::Player;
            else if (text == "copy") scope = VariableScope::Copy;
            else if (text != "game") return QString("The variable \"%1\" has the scope \"%2\". A scope is game, player or copy.").arg(name, text);
            if (scope != VariableScope::Game && schema < LanguageSchema) return QString("The variable \"%1\" is a %2 variable, which needs schemaVersion %3.").arg(name, text).arg(LanguageSchema);
        }
        variables.append({name, static_cast<int>(number), scope});
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
    if (info.usesAnimation) object.insert("animation", block.animation);
    if (info.usesScene) object.insert("scene", block.scene);
    if (info.usesFlipped) object.insert("flipped", block.flipped);
    if (info.usesItem) object.insert("item", block.item);
    if (info.usesOtherItem) object.insert("otherItem", block.otherItem);
    if (info.usesVariable) object.insert("variable", block.variable);
    if (info.usesSound) object.insert("sound", block.sound);
    if (info.usesPosition) {
        object.insert("x", block.x);
        object.insert("y", block.y);
    }
    if (info.usesColor) object.insert("color", block.color);
    if (info.usesFormula) object.insert("formula", block.formula);
    if (info.usesRadius) object.insert("radius", block.radius);
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
    for (const auto& variable : variables) {
        QJsonObject object{{"name", variable.name}, {"initial", variable.initial}};
        if (variable.scope != VariableScope::Game) object.insert("scope", scopeName(variable.scope));
        list.append(object);
    }
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
    case ControllerButton::MoveUp: return "MoveUp";
    case ControllerButton::MoveDown: return "MoveDown";
    case ControllerButton::MoveLeft: return "MoveLeft";
    case ControllerButton::MoveRight: return "MoveRight";
    case ControllerButton::Options: return "Options";
    case ControllerButton::P2Cross: return "P2Cross";
    case ControllerButton::P2Circle: return "P2Circle";
    case ControllerButton::P2Square: return "P2Square";
    case ControllerButton::P2Triangle: return "P2Triangle";
    case ControllerButton::P2Options: return "P2Options";
    case ControllerButton::P2DpadUp: return "P2DpadUp";
    case ControllerButton::P2DpadDown: return "P2DpadDown";
    case ControllerButton::P2DpadLeft: return "P2DpadLeft";
    case ControllerButton::P2DpadRight: return "P2DpadRight";
    case ControllerButton::P2MoveUp: return "P2MoveUp";
    case ControllerButton::P2MoveDown: return "P2MoveDown";
    case ControllerButton::P2MoveLeft: return "P2MoveLeft";
    case ControllerButton::P2MoveRight: return "P2MoveRight";
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
    Generator(const Scene& scene, const LogicProgram& program, int firstItem = 0, QHash<QString, int> sceneIndexes = {}) : scene(scene), firstItem(firstItem), sceneIndexes(std::move(sceneIndexes)) {
        for (qsizetype index = 0; index < scene.items.size(); ++index) itemIndexes.insert(scene.items[index].id, index);
        std::array<qsizetype, 3> counts{};
        for (const auto& variable : program.variables) {
            auto& count = counts[static_cast<std::size_t>(variable.scope)];
            variableIndexes.insert(variable.name, count++);
            variableScopes.insert(variable.name, variable.scope);
        }
        for (qsizetype index = 0; index < program.sounds.size(); ++index) soundIndexes.insert(program.sounds[index].name, index);
    }

    QByteArray index(const QString& itemId) const { return QByteArray::number(firstItem + itemIndexes.value(itemId)); }

    QByteArray variable(const QString& name) const { return QByteArray::number(variableIndexes.value(name)); }

    // The C++ for an item choice: a scene item's number, or the runtime's answer for @self, @player and @nearestPlayer.
    QByteArray item(const QString& id) const {
        if (id == QLatin1String(SelfItem)) return "world.Self()";
        if (id == QLatin1String(PlayerItem)) return "world.PlayerHero()";
        if (id == QLatin1String(NearestPlayerItem)) return "world.NearestPlayerHero(world.Self())";
        return index(id);
    }

    // The scene item an item choice stands for, as far as the generator knows: @self is its template.
    QString realItem(const QString& id) const { return id == QLatin1String(SelfItem) && !copyTemplates.isEmpty() ? copyTemplates.last() : id; }

    QByteArray scopeSuffix(const QString& name) const {
        const auto scope = variableScopes.value(name);
        return scope == VariableScope::Player ? "PlayerVariable" : scope == VariableScope::Copy ? "CopyVariable" : "Variable";
    }
    QByteArray readVariable(const QString& name) const { return "world." + scopeSuffix(name) + "(" + variable(name) + ")"; }
    QByteArray setVariable(const QString& name, const QByteArray& value) const { return "world.Set" + scopeSuffix(name) + "(" + variable(name) + ", " + value + ")"; }
    QByteArray changeVariable(const QString& name, const QByteArray& amount) const { return "world.Change" + scopeSuffix(name) + "(" + variable(name) + ", " + amount + ")"; }

    QByteArray formula(const QString& text) const {
        return compileFormula(text, [&](const QString& name) {
            const auto lower = name.toLower();
            if (lower == "player") return FormulaName{FormulaNameKind::Player, 0};
            if (lower == "players") return FormulaName{FormulaNameKind::Players, 0};
            if (!variableIndexes.contains(name)) return FormulaName{};
            const auto scope = variableScopes.value(name);
            const auto kind = scope == VariableScope::Player ? FormulaNameKind::PlayerVariable : scope == VariableScope::Copy ? FormulaNameKind::CopyVariable : FormulaNameKind::GameVariable;
            return FormulaName{kind, static_cast<int>(variableIndexes.value(name))};
        }).code;
    }

    // The first copy of a template and one past its last, as numbers in the game's item list.
    std::pair<QByteArray, QByteArray> copyRange(const QString& templateId) const {
        const auto first = firstItem + itemIndexes.value(templateId);
        const auto count = scene.items[itemIndexes.value(templateId)].copies;
        return {QByteArray::number(first), QByteArray::number(first + count)};
    }

    // Runs `body` once for each live copy of the template, with @self set to it, and puts @self back after.
    void forEachCopy(const QString& templateId, int level, QByteArray& code, const std::function<void(int)>& body) const {
        const auto pad = indentation(level);
        const auto [first, last] = copyRange(templateId);
        code += pad + "{\n";
        code += pad + "    const auto outerSelf = world.Self();\n";
        code += pad + "    for (std::size_t copyItem = " + first + "; copyItem < " + last + "; ++copyItem) {\n";
        code += pad + "        if (!world.Alive(copyItem)) continue;\n";
        code += pad + "        world.SetSelf(copyItem);\n";
        copyTemplates.append(templateId);
        body(level + 2);
        copyTemplates.removeLast();
        code += pad + "    }\n";
        code += pad + "    world.SetSelf(outerSelf);\n";
        code += pad + "}\n";
    }

    // Runs `body` once for each player in the scene, as the current player, and puts the current player back after.
    void forEachPlayer(int level, QByteArray& code, const QByteArray& condition, const std::function<void(int)>& body) const {
        const auto pad = indentation(level);
        code += pad + "{\n";
        code += pad + "    const auto outerPlayer = world.CurrentPlayer();\n";
        code += pad + "    for (std::size_t player = 0; player < world.PlayerSlots(); ++player) {\n";
        code += pad + "        if (!world.PlayerHere(player)) continue;\n";
        code += pad + "        world.SetCurrentPlayer(player);\n";
        if (condition.isEmpty()) {
            body(level + 2);
        } else {
            code += pad + "        if (" + condition + ") {\n";
            body(level + 3);
            code += pad + "        }\n";
        }
        code += pad + "    }\n";
        code += pad + "    world.SetCurrentPlayer(outerPlayer);\n";
        code += pad + "}\n";
    }

    QByteArray sound(const QString& name) const { return QByteArray::number(soundIndexes.value(name)); }
    int animationIndex(const Block& block) const {
        const auto& sheet = scene.items[itemIndexes.value(realItem(block.item))];
        const auto found = std::ranges::find(sheet.animations, block.animation, &SceneAnimation::name);
        return found == sheet.animations.end() ? 0 : static_cast<int>(found - sheet.animations.begin());
    }

    void actions(const QList<Block>& blocks, int level, QByteArray& code) const {
        for (const auto& block : blocks) action(block, level, code);
    }

    void action(const Block& block, int level, QByteArray& code) const {
        const auto pad = indentation(level);
        const auto comment = " // " + block.id.toLatin1() + '\n';
        switch (block.type) {
        case BlockType::MoveBy:
            code += pad + "world.MoveBy(" + item(block.item) + ", " + floatLiteral(block.x) + ", " + floatLiteral(block.y) + ");" + comment;
            return;
        case BlockType::SetPosition:
            code += pad + "world.SetPosition(" + item(block.item) + ", " + floatLiteral(block.x) + ", " + floatLiteral(block.y) + ");" + comment;
            return;
        case BlockType::Show:
            code += pad + "world.SetVisible(" + item(block.item) + ", true);" + comment;
            return;
        case BlockType::Hide:
            code += pad + "world.SetVisible(" + item(block.item) + ", false);" + comment;
            return;
        case BlockType::SetColor:
            code += pad + "world.SetColor(" + item(block.item) + ", " + colorLiterals(block.color) + ");" + comment;
            return;
        case BlockType::SetVariable:
            code += pad + setVariable(block.variable, QByteArray::number(block.value)) + ";" + comment;
            return;
        case BlockType::ChangeVariable:
            code += pad + changeVariable(block.variable, QByteArray::number(block.value)) + ";" + comment;
            return;
        case BlockType::SetFormula:
            code += pad + setVariable(block.variable, "Formula::ToVariable(" + formula(block.formula) + ")") + ";" + comment;
            return;
        case BlockType::IfFormula:
            code += pad + "if (" + formula(block.formula) + " != 0) {" + comment;
            actions(block.children, level + 1, code);
            code += pad + "}\n";
            return;
        case BlockType::Otherwise:
            code += pad + "else {" + comment;
            actions(block.children, level + 1, code);
            code += pad + "}\n";
            return;
        case BlockType::ForEachPlayer:
            code += pad + "// " + block.id.toLatin1() + ": " + blockInfo(block.type).title.toLatin1() + '\n';
            forEachPlayer(level, code, {}, [&](int inner) { actions(block.children, inner, code); });
            return;
        case BlockType::ForEachCopy:
            code += pad + "// " + block.id.toLatin1() + ": " + blockInfo(block.type).title.toLatin1() + '\n';
            forEachCopy(block.item, level, code, [&](int inner) { actions(block.children, inner, code); });
            return;
        case BlockType::SpawnCopy: {
            const auto [first, last] = copyRange(block.item);
            code += pad + "{" + comment;
            code += pad + "    const auto spawned = world.SpawnCopy(" + first + ", " + last + ", " + item(block.otherItem) + ", " + floatLiteral(block.x) + ", " + floatLiteral(block.y) + ");\n";
            if (!block.children.isEmpty()) {
                code += pad + "    if (spawned != world.NoItem()) {\n";
                code += pad + "        const auto outerSelf = world.Self();\n";
                code += pad + "        world.SetSelf(spawned);\n";
                copyTemplates.append(block.item);
                actions(block.children, level + 2, code);
                copyTemplates.removeLast();
                code += pad + "        world.SetSelf(outerSelf);\n";
                code += pad + "    }\n";
            } else {
                code += pad + "    static_cast<void>(spawned);\n";
            }
            code += pad + "}\n";
            return;
        }
        case BlockType::RemoveCopy:
            code += pad + "world.RemoveCopy(" + item(block.item) + ");" + comment;
            return;
        case BlockType::SetLook:
            code += pad + "world.SetLook(" + item(block.item) + ", " + formula(block.formula) + ");" + comment;
            return;
        case BlockType::Wander:
            code += pad + "world.Wander(" + item(block.item) + ", " + floatLiteral(block.value) + ", " + floatLiteral(block.radius) + ");" + comment;
            return;
        case BlockType::Flee:
            code += pad + "world.Flee(" + item(block.item) + ", " + item(block.otherItem) + ", " + floatLiteral(block.value) + ");" + comment;
            return;
        case BlockType::AlertNearby: {
            const auto [first, last] = copyRange(block.item);
            code += pad + "world.AlertNearby(" + first + ", " + last + ", " + variable(block.variable) + ", " + QByteArray::number(block.value) + ", " + floatLiteral(block.radius) + ");" + comment;
            return;
        }
        case BlockType::BurstParticles:
            code += pad + "world.BurstParticles(" + item(block.item) + ");" + comment;
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
        case BlockType::PlayAnimation:
            code += pad + "world.PlayAnimation(" + item(block.item) + ", " + QByteArray::number(animationIndex(block)) + ");" + comment;
            return;
        case BlockType::FlipItem:
            code += pad + "world.SetFlipped(" + item(block.item) + ", " + (block.flipped ? "true" : "false") + ");" + comment;
            return;
        case BlockType::CameraFollow:
            code += pad + "world.CameraFollow(" + item(block.item) + ");" + comment;
            return;
        case BlockType::SetCameraPosition:
            code += pad + "world.SetCameraPosition(" + floatLiteral(block.x) + ", " + floatLiteral(block.y) + ");" + comment;
            return;
        case BlockType::SetVelocity:
            code += pad + "world.SetVelocity(" + item(block.item) + ", " + floatLiteral(block.x) + ", " + floatLiteral(block.y) + ");" + comment;
            return;
        case BlockType::JumpItem:
            code += pad + "world.Jump(" + item(block.item) + ");" + comment;
            return;
        case BlockType::IfOnGround:
            code += pad + "if (world.OnGround(" + item(block.item) + ")) {" + comment;
            actions(block.children, level + 1, code);
            code += pad + "}\n";
            return;
        case BlockType::GoToScene:
            code += pad + "world.GoToScene(" + QByteArray::number(sceneIndexes.value(block.scene)) + ");" + comment;
            return;
        case BlockType::RestartScene:
            code += pad + "world.RestartScene();" + comment;
            return;
        case BlockType::IfItemDistance:
            code += pad + "if (world.Distance(" + item(block.item) + ", " + item(block.otherItem) + ") " + comparisonOperator(block.comparison) + ' ' + floatLiteral(block.value) + ") {" + comment;
            actions(block.children, level + 1, code);
            code += pad + "}\n";
            return;
        case BlockType::ChaseItem:
            code += pad + "world.Chase(" + item(block.item) + ", " + item(block.otherItem) + ", " + floatLiteral(block.value) + ");" + comment;
            return;
        case BlockType::PutAtItem:
            code += pad + "world.PutAt(" + item(block.item) + ", " + item(block.otherItem) + ", " + floatLiteral(block.x) + ", " + floatLiteral(block.y) + ");" + comment;
            return;
        case BlockType::PauseGame:
            code += pad + "world.PauseGame();" + comment;
            return;
        case BlockType::ResumeGame:
            code += pad + "world.ResumeGame();" + comment;
            return;
        case BlockType::CameraFollowBoth:
            code += pad + "world.CameraFollowBoth(" + item(block.item) + ", " + item(block.otherItem) + ");" + comment;
            return;
        case BlockType::IfVariable:
            code += pad + "if (" + readVariable(block.variable) + " " + comparisonOperator(block.comparison) + ' ' + QByteArray::number(block.value) + ") {" + comment;
            actions(block.children, level + 1, code);
            code += pad + "}\n";
            return;
        case BlockType::WhenGameStarts:
        case BlockType::WhileButtonHeld:
        case BlockType::WhenButtonPressed:
        case BlockType::EveryFrame:
        case BlockType::WhenItemsTouch:
        case BlockType::WhenItemLands:
        case BlockType::WhenItemHitsSolid:
        case BlockType::WhenItemLeavesWorld:
        case BlockType::WhenPlayerPresses:
        case BlockType::WhilePlayerHolds:
        case BlockType::WhenPlayerJoins:
        case BlockType::EveryFrameForEachCopy:
        case BlockType::WhenCopyTouches:
            return; // validation keeps events out of an action list
        }
    }

    // Writes one event that runs each frame, and returns how many touch slots of Memory it used from `touchIndex` on.
    int frameEvent(const Block& block, int touchIndex, QByteArray& code) const {
        code += "    // " + block.id.toLatin1() + ": " + blockInfo(block.type).title.toLatin1() + '\n';
        if (block.type == BlockType::EveryFrame) {
            actions(block.children, 1, code);
        } else if (block.type == BlockType::WhileButtonHeld) {
            code += "    if (world.Held(Buttons::" + buttonConstant(block.button) + ")) {\n";
            actions(block.children, 2, code);
            code += "    }\n";
        } else if (block.type == BlockType::WhenButtonPressed) {
            code += "    if (world.Pressed(Buttons::" + buttonConstant(block.button) + ")) {\n";
            actions(block.children, 2, code);
            code += "    }\n";
        } else if (block.type == BlockType::WhenItemsTouch && (block.item == QLatin1String(PlayerItem) || block.otherItem == QLatin1String(PlayerItem))) {
            const auto slot = "memory.touching[" + QByteArray::number(touchIndex) + " + player]";
            forEachPlayer(1, code, {}, [&](int inner) {
                const auto pad = indentation(inner);
                code += pad + "const bool touching = world.Touching(" + item(block.item) + ", " + item(block.otherItem) + ");\n";
                code += pad + "const bool started = touching && !" + slot + ";\n";
                code += pad + slot + " = touching;\n";
                code += pad + "if (started) {\n";
                actions(block.children, inner + 1, code);
                code += pad + "}\n";
            });
            return MaximumPlayers;
        } else if (block.type == BlockType::WhenItemsTouch) {
            const auto slot = "memory.touching[" + QByteArray::number(touchIndex) + "]";
            code += "    {\n";
            code += "        const bool touching = world.Touching(" + item(block.item) + ", " + item(block.otherItem) + ");\n";
            code += "        const bool started = touching && !" + slot + ";\n";
            code += "        " + slot + " = touching;\n";
            code += "        if (started) {\n";
            actions(block.children, 3, code);
            code += "        }\n";
            code += "    }\n";
            return 1;
        } else if (block.type == BlockType::WhenPlayerPresses || block.type == BlockType::WhilePlayerHolds) {
            const auto call = block.type == BlockType::WhenPlayerPresses ? "world.PlayerPressed(Buttons::" : "world.PlayerHeld(Buttons::";
            forEachPlayer(1, code, call + buttonConstant(block.button) + ")", [&](int inner) { actions(block.children, inner, code); });
        } else if (block.type == BlockType::WhenPlayerJoins) {
            forEachPlayer(1, code, "world.PlayerJoined(player)", [&](int inner) { actions(block.children, inner, code); });
        } else if (block.type == BlockType::EveryFrameForEachCopy) {
            forEachCopy(block.item, 1, code, [&](int inner) { actions(block.children, inner, code); });
        } else if (block.type == BlockType::WhenCopyTouches) {
            const auto [first, last] = copyRange(block.item);
            const bool perPlayer = block.otherItem == QLatin1String(PlayerItem);
            const auto base = QByteArray::number(touchIndex);
            const auto slot = perPlayer ? "memory.touching[" + base + " + (copyItem - " + first + ") * " + QByteArray::number(MaximumPlayers) + " + player]" : "memory.touching[" + base + " + (copyItem - " + first + ")]";
            const auto check = [&](int inner) {
                const auto pad = indentation(inner);
                code += pad + "const bool touching = world.Touching(copyItem, " + item(block.otherItem) + ");\n";
                code += pad + "const bool started = touching && !" + slot + ";\n";
                code += pad + slot + " = touching;\n";
                code += pad + "if (started) {\n";
                actions(block.children, inner + 1, code);
                code += pad + "}\n";
            };
            // A dead copy touches nothing, so its slots are cleared and it starts touching afresh when spawned again.
            code += "    {\n";
            code += "        const auto outerSelf = world.Self();\n";
            code += "        for (std::size_t copyItem = " + first + "; copyItem < " + last + "; ++copyItem) {\n";
            code += "            world.SetSelf(copyItem);\n";
            copyTemplates.append(block.item);
            if (perPlayer) forEachPlayer(3, code, {}, check);
            else check(3);
            copyTemplates.removeLast();
            code += "        }\n";
            code += "        world.SetSelf(outerSelf);\n";
            code += "    }\n";
            return static_cast<int>((last.toInt() - first.toInt()) * (perPlayer ? MaximumPlayers : 1));
        } else if (block.type == BlockType::WhenItemLands || block.type == BlockType::WhenItemHitsSolid || block.type == BlockType::WhenItemLeavesWorld) {
            const auto call = block.type == BlockType::WhenItemLands ? "Landed" : block.type == BlockType::WhenItemHitsSolid ? "HitSolid" : "LeftWorld";
            code += QByteArray("    if (world.") + call + "(" + item(block.item) + ")) {\n";
            actions(block.children, 2, code);
            code += "    }\n";
        }
        return 0;
    }

private:
    const Scene& scene;
    int firstItem;
    QHash<QString, int> sceneIndexes;
    QHash<QString, qsizetype> itemIndexes;
    QHash<QString, qsizetype> variableIndexes; // each variable's place among the variables of its scope
    QHash<QString, VariableScope> variableScopes;
    mutable QStringList copyTemplates; // the templates whose copy the blocks being written run for, innermost last
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

bool programUsesSaveData(const LogicProgram& program) {
    if (usesSaveData(program.events)) return true;
    return std::ranges::any_of(program.otherScenes, [](const SceneLogic& other) { return usesSaveData(other.events); });
}

bool usesFormulaBlock(const QList<Block>& blocks) {
    return std::ranges::any_of(blocks, [](const Block& block) { return blockInfo(block.type).usesFormula || usesFormulaBlock(block.children); });
}

bool usesFormulas(const LogicProgram& program) {
    if (usesFormulaBlock(program.events)) return true;
    return std::ranges::any_of(program.otherScenes, [](const SceneLogic& other) { return usesFormulaBlock(other.events); });
}

bool usesWorldBlock(const QList<Block>& blocks) {
    return std::ranges::any_of(blocks, [](const Block& block) { return isWorldBlock(block.type) || usesWorldBlock(block.children); });
}

bool referencesItems(const QList<Block>& blocks) {
    for (const auto& block : blocks) {
        const auto& info = blockInfo(block.type);
        if (info.usesItem || info.usesOtherItem || referencesItems(block.children)) return true;
    }
    return false;
}

QByteArray assembleHeader(bool includeScene, bool includeSounds, bool includeButtons, int itemCount, int soundCount, int touchEvents, const QList<QByteArray>& startCodes, const QList<QByteArray>& frameCodes, bool includeFormula = false) {
    QByteArray header = "// Generated by AnyDevKit Studio from logic.json. Do not edit.\n#pragma once\n\n";
    if (includeScene) header += "#include \"GeneratedScene.hpp\"\n";
    if (includeSounds) header += "#include \"GeneratedSounds.hpp\"\n";
    if (includeButtons) header += "#include \"Input/Buttons.hpp\"\n";
    if (includeFormula) header += "#include \"Scene/Formula.hpp\"\n";
    if (includeScene || includeSounds || includeButtons || includeFormula) header += '\n';
    header += "#include <array>\n#include <cstddef>\n#include <cstdint>\n\nnamespace GeneratedLogic {\n\n";
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
    const auto sceneCount = startCodes.size();
    for (qsizetype index = 0; index < sceneCount; ++index) {
        const auto suffix = sceneCount == 1 ? QByteArray() : QByteArray::number(index);
        header += "template <typename World>\nvoid OnStart" + suffix + "([[maybe_unused]] World& world) {\n" + startCodes[index] + "}\n\n";
        header += "template <typename World>\nvoid OnFrame" + suffix + "([[maybe_unused]] World& world, [[maybe_unused]] Memory& memory) {\n" + frameCodes[index] + "}\n\n";
    }
    QByteArray startCases;
    QByteArray frameCases;
    if (sceneCount == 1) {
        startCases = "    OnStart(world);\n";
        frameCases = "    OnFrame(world, memory);\n";
    } else {
        startCases = "    switch (scene) {\n";
        frameCases = "    switch (scene) {\n";
        for (qsizetype index = 0; index < sceneCount; ++index) {
            const auto number = QByteArray::number(index);
            startCases += "    case " + number + ": OnStart" + number + "(world); break;\n";
            frameCases += "    case " + number + ": OnFrame" + number + "(world, memory); break;\n";
        }
        startCases += "    default: break;\n    }\n";
        frameCases += "    default: break;\n    }\n";
    }
    header += "// Runs the blocks of one scene: when it starts, and each frame while it is showing.\n";
    header += "template <typename World>\nvoid OnSceneStart([[maybe_unused]] World& world, [[maybe_unused]] std::size_t scene) {\n" + startCases + "}\n\n";
    header += "template <typename World>\nvoid OnSceneFrame([[maybe_unused]] World& world, [[maybe_unused]] Memory& memory, [[maybe_unused]] std::size_t scene) {\n" + frameCases + "}\n\n";
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
        {BlockType::PlayAnimation, "playAnimation", "Play animation on item",        false, false, false, true, false, false, false, false, false, false, false, true, false},
        {BlockType::FlipItem, "flipItem", "Flip item",                               false, false, false, true, false, false, false, false, false, false, false, false, true},
        {BlockType::CameraFollow, "cameraFollow", "Camera follows item",             false, false, false, true, false, false, false, false, false, false, false},
        {BlockType::SetCameraPosition, "setCameraPosition", "Set camera position",   false, false, false, false, false, true, false, false, false, false, false},
        {BlockType::WhenItemLands, "whenItemLands", "When item lands",               true,  true,  false, true,  false, false, false, false, false, false, false},
        {BlockType::WhenItemHitsSolid, "whenItemHitsSolid", "When item hits a solid", true,  true,  false, true,  false, false, false, false, false, false, false},
        {BlockType::WhenItemLeavesWorld, "whenItemLeavesWorld", "When item leaves the world", true, true, false, true, false, false, false, false, false, false, false},
        {BlockType::SetVelocity, "setVelocity", "Set velocity",                      false, false, false, true,  false, true,  false, false, false, false, false},
        {BlockType::JumpItem, "jumpItem", "Jump item",                               false, false, false, true,  false, false, false, false, false, false, false},
        {BlockType::IfOnGround, "ifOnGround", "If item is on the ground",            false, true,  false, true,  false, false, false, false, false, false, false},
        {BlockType::GoToScene, "goToScene", "Go to scene",                           false, false, false, false, false, false, false, false, false, false, false, false, false, true},
        {BlockType::RestartScene, "restartScene", "Restart scene",                   false, false, false, false, false, false, false, false, false, false, false},
        {BlockType::WhenButtonPressed, "whenButtonPressed", "When a button is pressed", true, true, true, false, false, false, false, false, false, false, false},
        {BlockType::IfItemDistance, "ifItemDistance", "If distance between",         false, true,  false, true,  true,  false, false, true,  true,  false, false},
        {BlockType::ChaseItem, "chaseItem", "Chase",                                 false, false, false, true,  true,  false, false, true,  false, false, false},
        {BlockType::PutAtItem, "putAtItem", "Put item",                              false, false, false, true,  true,  true,  false, false, false, false, false},
        {BlockType::PauseGame, "pauseGame", "Pause the game",                        false, false, false, false, false, false, false, false, false, false, false},
        {BlockType::ResumeGame, "resumeGame", "Resume the game",                     false, false, false, false, false, false, false, false, false, false, false},
        {BlockType::CameraFollowBoth, "cameraFollowBoth", "Camera follows two items", false, false, false, true, true,  false, false, false, false, false, false},
        //                                                                                event  holds  button item   other  pos    color  value  cmp    variable sound  anim   flip   scene  formula radius
        {BlockType::SetFormula, "setFormula", "Set variable to a formula",            false, false, false, false, false, false, false, false, false, true,  false, false, false, false, true,  false},
        {BlockType::IfFormula, "ifFormula", "If",                                     false, true,  false, false, false, false, false, false, false, false, false, false, false, false, true,  false},
        {BlockType::Otherwise, "otherwise", "Otherwise",                              false, true,  false, false, false, false, false, false, false, false, false, false, false, false, false, false},
        {BlockType::WhenPlayerPresses, "whenPlayerPresses", "When a player presses",   true,  true,  true,  false, false, false, false, false, false, false, false},
        {BlockType::WhilePlayerHolds, "whilePlayerHolds", "While a player holds",      true,  true,  true,  false, false, false, false, false, false, false, false},
        {BlockType::WhenPlayerJoins, "whenPlayerJoins", "When a player joins",         true,  true,  false, false, false, false, false, false, false, false, false},
        {BlockType::ForEachPlayer, "forEachPlayer", "For each player",                false, true,  false, false, false, false, false, false, false, false, false},
        {BlockType::EveryFrameForEachCopy, "everyFrameForEachCopy", "Every frame, for each copy of", true, true, false, true, false, false, false, false, false, false, false},
        {BlockType::WhenCopyTouches, "whenCopyTouches", "When a copy touches",         true,  true,  false, true,  true,  false, false, false, false, false, false},
        {BlockType::SpawnCopy, "spawnCopy", "Spawn a copy",                           false, true,  false, true,  true,  true,  false, false, false, false, false},
        {BlockType::RemoveCopy, "removeCopy", "Remove this copy",                     false, false, false, true,  false, false, false, false, false, false, false},
        {BlockType::ForEachCopy, "forEachCopy", "For each copy of",                   false, true,  false, true,  false, false, false, false, false, false, false},
        {BlockType::Wander, "wander", "Wander",                                       false, false, false, true,  false, false, false, true,  false, false, false, false, false, false, false, true},
        {BlockType::Flee, "flee", "Run away from",                                    false, false, false, true,  true,  false, false, true,  false, false, false},
        {BlockType::AlertNearby, "alertNearby", "Tell nearby copies",                 false, false, false, true,  false, false, false, true,  false, true,  false, false, false, false, false, true},
        {BlockType::SetLook, "setLook", "Set look",                                   false, false, false, true,  false, false, false, false, false, false, false, false, false, false, true,  false},
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
        {ControllerButton::MoveUp, "moveUp", "Move up"},
        {ControllerButton::MoveDown, "moveDown", "Move down"},
        {ControllerButton::MoveLeft, "moveLeft", "Move left"},
        {ControllerButton::MoveRight, "moveRight", "Move right"},
        {ControllerButton::Options, "options", "Options"},
        {ControllerButton::P2Cross, "p2Cross", "Player 2 Cross"},
        {ControllerButton::P2Circle, "p2Circle", "Player 2 Circle"},
        {ControllerButton::P2Square, "p2Square", "Player 2 Square"},
        {ControllerButton::P2Triangle, "p2Triangle", "Player 2 Triangle"},
        {ControllerButton::P2Options, "p2Options", "Player 2 Options"},
        {ControllerButton::P2DpadUp, "p2DpadUp", "Player 2 D-pad up"},
        {ControllerButton::P2DpadDown, "p2DpadDown", "Player 2 D-pad down"},
        {ControllerButton::P2DpadLeft, "p2DpadLeft", "Player 2 D-pad left"},
        {ControllerButton::P2DpadRight, "p2DpadRight", "Player 2 D-pad right"},
        {ControllerButton::P2MoveUp, "p2MoveUp", "Player 2 move up"},
        {ControllerButton::P2MoveDown, "p2MoveDown", "Player 2 move down"},
        {ControllerButton::P2MoveLeft, "p2MoveLeft", "Player 2 move left"},
        {ControllerButton::P2MoveRight, "p2MoveRight", "Player 2 move right"},
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

QList<LogicProblem> validateLogic(const LogicProgram& program, const Scene& scene, const QStringList& sceneIds) {
    return Validator(&scene, program, sceneIds).run(program);
}

QList<Block> eventsOfScene(const LogicProgram& program, const QString& sceneId, bool isFirstScene) {
    if (!program.scene.isEmpty() && program.scene == sceneId) return program.events;
    for (const auto& other : program.otherScenes) {
        if (other.scene == sceneId) return other.events;
    }
    if (isFirstScene && program.scene.isEmpty()) return program.events;
    return {};
}

QList<LogicProblem> validateGameLogic(const LogicProgram& program, const SceneSet& set) {
    QStringList sceneIds;
    for (const auto& scene : set.scenes) sceneIds << scene.id;
    QList<LogicProblem> found = validateLogicStructure(program);
    if (!found.isEmpty()) return found;
    for (qsizetype index = 0; index < set.scenes.size(); ++index) {
        const auto& scene = set.scenes[index];
        LogicProgram sceneProgram = program;
        sceneProgram.events = eventsOfScene(program, scene.id, index == 0);
        sceneProgram.scene.clear();
        sceneProgram.otherScenes.clear();
        for (auto problem : validateLogic(sceneProgram, scene, sceneIds)) {
            if (set.scenes.size() > 1) problem.message = QString("In the scene \"%1\": %2").arg(scene.name, problem.message);
            found.append(problem);
        }
    }
    return found;
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
    if (version < PhysicsBlockSchema && !root.value("events").isArray()) return failure(QString("%1 has no events list. Add \"events\": [] to the file.").arg(fileName));
    if (version >= PhysicsBlockSchema && !root.value("scenes").isArray()) return failure(QString("%1 has no scenes list. Add \"scenes\": [] to the file.").arg(fileName));

    LogicProgram program;
    if (version >= VariableSchema) {
        if (!root.value("variables").isArray()) return failure(QString("%1 has no variables list. Add \"variables\": [] to the file.").arg(fileName));
        const auto problem = parseVariables(root.value("variables").toArray(), program.variables, version);
        if (!problem.isEmpty()) return failure(problem);
    }
    if (version >= SoundSchema) {
        if (!root.value("sounds").isArray()) return failure(QString("%1 has no sounds list. Add \"sounds\": [] to the file.").arg(fileName));
        const auto problem = parseSounds(root.value("sounds").toArray(), program.sounds);
        if (!problem.isEmpty()) return failure(problem);
    }

    Parsing parsing;
    parsing.schema = version;
    const auto parseEvents = [&](const QJsonArray& events, QList<Block>& out) -> QString {
        parsing.count = 0; // the block budget applies to each scene on its own
        for (qsizetype index = 0; index < events.size(); ++index) {
            Block block;
            const auto problem = parseBlock(events.at(index), index, 1, parsing, block);
            if (!problem.isEmpty()) return problem;
            out.append(block);
        }
        return {};
    };
    if (version < PhysicsBlockSchema) {
        if (const auto problem = parseEvents(root.value("events").toArray(), program.events); !problem.isEmpty()) return failure(problem);
    } else {
        const auto scenes = root.value("scenes").toArray();
        for (qsizetype index = 0; index < scenes.size(); ++index) {
            const auto entry = scenes.at(index);
            if (!entry.isObject() || !entry.toObject().value("events").isArray()) return failure(QString("%1: each entry in the scenes list needs an events list. Add \"events\": [] to it.").arg(fileName));
            const auto object = entry.toObject();
            if (index == 0) {
                program.scene = object.value("scene").toString();
                if (const auto problem = parseEvents(object.value("events").toArray(), program.events); !problem.isEmpty()) return failure(problem);
            } else {
                SceneLogic other;
                other.scene = object.value("scene").toString();
                if (const auto problem = parseEvents(object.value("events").toArray(), other.events); !problem.isEmpty()) return failure(problem);
                program.otherScenes.append(other);
            }
        }
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

    const auto sceneEntry = [](const QString& scene, const QList<Block>& blocks) {
        QJsonArray events;
        for (const auto& block : blocks) events.append(blockToJson(block));
        return QJsonObject{{"scene", scene}, {"events", events}};
    };
    QJsonArray scenes;
    scenes.append(sceneEntry(program.scene, program.events));
    for (const auto& other : program.otherScenes) scenes.append(sceneEntry(other.scene, other.events));
    const QJsonObject root{{"schemaVersion", CurrentSchema}, {"variables", variablesToJson(program.variables)}, {"sounds", soundsToJson(program.sounds)}, {"scenes", scenes}};
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
    for (const auto& other : program.otherScenes) collect(collect, other.events, 1);

    auto candidate = used.size() + 1;
    while (used.contains(QString("block-%1").arg(candidate))) ++candidate;
    return QString("block-%1").arg(candidate);
}

QByteArray generateEmptyLogicHeader() {
    return assembleHeader(false, false, false, 0, 0, 0, QList<QByteArray>{QByteArray()}, QList<QByteArray>{QByteArray()});
}

QByteArray generateLogicHeader(const LogicProgram& program, const Scene& scene) {
    SceneSet set;
    set.scenes = {scene};
    set.startScene = scene.id;
    LogicProgram sceneProgram = program;
    sceneProgram.scene.clear();
    sceneProgram.otherScenes.clear();
    return generateLogicHeader(sceneProgram, set);
}

QByteArray generateLogicHeader(const LogicProgram& program, const SceneSet& original) {
    if (!validateSceneSet(original).isEmpty() || !validateGameLogic(program, original).isEmpty()) return {};
    const auto set = expandCopies(original); // the game numbers every copy as an item of its own

    QHash<QString, int> sceneIndexes;
    for (qsizetype index = 0; index < set.scenes.size(); ++index) sceneIndexes.insert(set.scenes[index].id, static_cast<int>(index));

    QList<QByteArray> startCodes;
    QList<QByteArray> frameCodes;
    int touchEvents = 0;
    bool usesButtons = false;
    bool usesItems = false;
    bool usesSounds = false;
    bool anyEvents = false;
    int firstItem = 0;
    int totalItems = 0;
    for (const auto& scene : set.scenes) totalItems += static_cast<int>(scene.items.size());

    for (qsizetype index = 0; index < set.scenes.size(); ++index) {
        const auto& scene = set.scenes[index];
        const auto events = eventsOfScene(program, scene.id, index == 0);
        if (!events.isEmpty()) anyEvents = true;
        usesItems = usesItems || referencesItems(events);
        usesSounds = usesSounds || referencesSounds(events);

        const Generator generator(scene, program, firstItem, sceneIndexes);
        QByteArray startCode;
        // A one-shot item a block bursts plays when that block runs, not also when the game starts.
        QSet<QString> burstTargets;
        collectBurstTargets(events, burstTargets);
        for (const auto& item : scene.items) {
            if (item.type == "particles" && !item.looping && burstTargets.contains(item.id)) {
                startCode += "    world.AwaitBurst(" + generator.index(item.id) + ");\n";
            }
        }
        QByteArray frameCode;
        for (const auto& block : events) {
            if (block.type == BlockType::WhenGameStarts) {
                startCode += "    // " + block.id.toLatin1() + ": " + blockInfo(block.type).title.toLatin1() + '\n';
                generator.actions(block.children, 1, startCode);
                continue;
            }
            touchEvents += generator.frameEvent(block, touchEvents, frameCode);
            if (block.type == BlockType::WhileButtonHeld || block.type == BlockType::WhenButtonPressed || isPlayerButtonEvent(block.type)) usesButtons = true;
        }
        startCodes.append(startCode);
        frameCodes.append(frameCode);
        firstItem += static_cast<int>(scene.items.size());
    }
    if (!anyEvents) {
        if (set.scenes.size() == 1) return generateEmptyLogicHeader();
        return assembleHeader(false, false, false, 0, 0, 0, startCodes, frameCodes);
    }
    return assembleHeader(usesItems, usesSounds, usesButtons, totalItems, static_cast<int>(program.sounds.size()), touchEvents, startCodes, frameCodes, usesFormulas(program));
}

bool programUsesWorldBlocks(const LogicProgram& program) {
    if (usesWorldBlock(program.events)) return true;
    return std::ranges::any_of(program.otherScenes, [](const SceneLogic& other) { return usesWorldBlock(other.events); });
}

bool programUsesPhysicsBlocks(const LogicProgram& program) {
    const auto uses = [](const auto& self, const QList<Block>& blocks) -> bool {
        return std::ranges::any_of(blocks, [&](const Block& block) { return isPhysicsBlock(block.type) || usesStickButton(block) || self(self, block.children); });
    };
    if (uses(uses, program.events)) return true;
    return std::ranges::any_of(program.otherScenes, [&](const SceneLogic& other) { return uses(uses, other.events); });
}

QByteArray generateVariablesHeader(const LogicProgram& program, const Scene& scene) {
    SceneSet set;
    set.scenes = {scene};
    set.startScene = scene.id;
    LogicProgram sceneProgram = program;
    sceneProgram.scene.clear();
    sceneProgram.otherScenes.clear();
    return generateVariablesHeader(sceneProgram, set);
}

QByteArray generateVariablesHeader(const LogicProgram& program, const SceneSet& original) {
    if (!validateSceneSet(original).isEmpty() || !validateGameLogic(program, original).isEmpty()) return {};
    for (const auto& oneScene : original.scenes) {
        if (!validateTextBindings(oneScene, program).isEmpty()) return {};
    }
    const auto set = expandCopies(original);
    Scene scene; // every scene's items in one list, as the runtime numbers them
    for (const auto& oneScene : set.scenes) scene.items += oneScene.items;

    const auto nameHash = [](const QString& name) {
        std::uint32_t hash = 2166136261u; // FNV-1a, the same as SaveData::NameHash in the runtime
        for (const char byte : name.toUtf8()) hash = (hash ^ static_cast<unsigned char>(byte)) * 16777619u;
        return QByteArray::number(hash) + "u";
    };
    QHash<QString, qsizetype> variableIndexes; // each variable's place among the variables of its scope
    QHash<QString, VariableScope> scopes;
    std::array<QByteArrayList, 3> initialValues;
    std::array<QByteArrayList, 3> names;
    QByteArrayList gameHashes;
    QStringList playerNames;
    for (const auto& variable : program.variables) {
        const auto scope = static_cast<std::size_t>(variable.scope);
        variableIndexes.insert(variable.name, initialValues[scope].size());
        scopes.insert(variable.name, variable.scope);
        initialValues[scope] << QByteArray::number(variable.initial);
        names[scope] << variable.name.toLatin1(); // names are letters, digits and _, safe in a comment
        if (variable.scope == VariableScope::Game) gameHashes << nameHash(variable.name);
        if (variable.scope == VariableScope::Player) playerNames << variable.name;
    }
    // A save keeps the game variables first, as older games did, then each player's variables in turn.
    auto savedHashes = gameHashes;
    auto savedValues = initialValues[0];
    for (int player = 1; player <= MaximumPlayers; ++player) {
        for (const auto& name : playerNames) savedHashes << nameHash(name + "@" + QString::number(player));
        savedValues += initialValues[1];
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
                const auto scope = static_cast<int>(scopes.value(part.variable));
                auto extra = scope != 0 || part.player > 0 || part.letter ? ", " + QByteArray::number(scope) + ", " + QByteArray::number(part.player - 1) : QByteArray();
                if (part.letter) extra += ", true";
                parts += "    {\"\", " + QByteArray::number(variableIndexes.value(part.variable)) + extra + "},\n";
                quads += part.letter ? 1 : MaximumVariableCharacters;
            }
            ++partCount;
        }
        bound += "    {" + QByteArray::number(itemIndex) + ", " + QByteArray::number(firstPart) + ", " + QByteArray::number(partCount - firstPart) + ", " + QByteArray::number(quads) + "},\n";
        asserts += "static_assert(GeneratedScene::Items[" + QByteArray::number(itemIndex) + "].text.size() == " + QByteArray::number(quads) + ", \"GeneratedScene.hpp and GeneratedVariables.hpp do not match. Build the project again from Studio.\");\n";
        ++boundCount;
    }

    const auto list = [](const QByteArray& type, const QByteArray& name, const QByteArrayList& values, const QByteArray& comment) {
        QByteArray line = "inline constexpr std::array<" + type + ", " + QByteArray::number(values.size()) + "> " + name;
        return line + (values.isEmpty() ? QByteArray("{};\n") : "{{" + values.join(", ") + "}};" + (comment.isEmpty() ? QByteArray() : " // " + comment) + "\n");
    };
    QByteArray header = "// Generated by AnyDevKit Studio from logic.json and scene.json. Do not edit.\n#pragma once\n\n";
    if (boundCount > 0) header += "#include \"GeneratedScene.hpp\"\n\n";
    header += "#include <array>\n#include <cstddef>\n#include <cstdint>\n#include <string_view>\n\nnamespace GeneratedVariables {\n\n";
    header += "inline constexpr std::size_t VariableCount = " + QByteArray::number(initialValues[0].size()) + ";\n";
    header += list("std::int32_t", "InitialValues", initialValues[0], names[0].join(", "));
    header += list("std::uint32_t", "NameHashes", gameHashes, "how a saved value finds its variable");
    header += "// Each player has their own value of these, and each copy of a template its own value of the copy variables.\n";
    header += "inline constexpr std::size_t PlayerSlots = " + QByteArray::number(MaximumPlayers) + ";\n";
    header += "inline constexpr std::size_t PlayerVariableCount = " + QByteArray::number(initialValues[1].size()) + ";\n";
    header += list("std::int32_t", "PlayerInitialValues", initialValues[1], names[1].join(", "));
    header += "inline constexpr std::size_t CopyVariableCount = " + QByteArray::number(initialValues[2].size()) + ";\n";
    header += list("std::int32_t", "CopyInitialValues", initialValues[2], names[2].join(", "));
    header += "// What a save holds: the game variables, then each player's variables, player 1 first.\n";
    header += "inline constexpr std::size_t SavedCount = " + QByteArray::number(savedValues.size()) + ";\n";
    header += list("std::int32_t", "SavedInitialValues", savedValues, {});
    header += list("std::uint32_t", "SavedHashes", savedHashes, {});
    header += QByteArray("inline constexpr bool UsesSaveData = ") + (programUsesSaveData(program) ? "true" : "false") + ";\n";
    header += R"cpp(
// One piece of a text item that shows variables: plain text, or a variable's value when variable is not -1.
// scope is 0 for a game variable, 1 for a player variable and 2 for a copy variable; player is the player whose
// value it shows, or -1 for the first player.
struct Part {
    std::string_view text;
    std::int32_t variable;
    std::int32_t scope = 0;
    std::int32_t player = -1;
    bool letter = false; // shows the value as one letter: 1 to 26 as A to Z, 27 to 36 as 0 to 9, else a space
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
    SceneSet set;
    set.scenes = {scene};
    set.startScene = scene.id;
    return exportLogicHeader(projectRoot, set, error);
}

bool exportLogicHeader(const QString& projectRoot, const SceneSet& set, QString& error) {
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
        const auto problems = validateGameLogic(program, set);
        if (!problems.isEmpty()) return fail(describeProblems(problems));
        header = generateLogicHeader(program, set);
        if (header.isEmpty()) return fail("The game logic could not be generated because the scene has a problem. Fix the scene and try again.");
    }
    QList<LogicProblem> textProblems;
    for (const auto& scene : set.scenes) textProblems += validateTextBindings(scene, program);
    if (!textProblems.isEmpty()) return fail(describeProblems(textProblems));
    const auto variablesHeader = generateVariablesHeader(program, set);
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
