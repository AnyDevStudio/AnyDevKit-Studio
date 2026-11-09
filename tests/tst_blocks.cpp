#include "Blocks.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

#include <limits>

using namespace Studio;

namespace {

SceneItem rectangle(const QString& id, double x, double y, double width, double height, const QString& color = "#00FF00", bool controllable = false) {
    SceneItem item;
    item.id = id;
    item.type = "rectangle";
    item.name = id;
    item.x = x;
    item.y = y;
    item.width = width;
    item.height = height;
    item.color = color;
    item.controllable = controllable;
    return item;
}

SceneItem text(const QString& id, double x, double y, const QString& content, int scale) {
    SceneItem item;
    item.id = id;
    item.type = "text";
    item.name = id;
    item.x = x;
    item.y = y;
    item.text = content;
    item.scale = scale;
    item.color = "#0000FF";
    return item;
}

// The scene used by most tests: index 0 is the player, 1 the coin, 2 the label.
Scene gameScene() {
    Scene scene;
    scene.items = {
        rectangle("player", 100, 100, 50, 50, "#FFFFFF", true),
        rectangle("coin", 400, 100, 20, 20),
        text("label", 0, 0, "Hi", 2),
    };
    return scene;
}

Block eventBlock(const QString& id, BlockType type, const QList<Block>& actions = {}) {
    Block block;
    block.id = id;
    block.type = type;
    block.children = actions;
    return block;
}

Block held(const QString& id, ControllerButton button, const QList<Block>& actions = {}) {
    auto block = eventBlock(id, BlockType::WhileButtonHeld, actions);
    block.button = button;
    return block;
}

Block touch(const QString& id, const QString& first, const QString& second, const QList<Block>& actions = {}) {
    auto block = eventBlock(id, BlockType::WhenItemsTouch, actions);
    block.item = first;
    block.otherItem = second;
    return block;
}

Block moveBy(const QString& id, const QString& item, double x, double y) {
    Block block;
    block.id = id;
    block.type = BlockType::MoveBy;
    block.item = item;
    block.x = x;
    block.y = y;
    return block;
}

Block setPosition(const QString& id, const QString& item, double x, double y) {
    auto block = moveBy(id, item, x, y);
    block.type = BlockType::SetPosition;
    return block;
}

Block itemBlock(const QString& id, BlockType type, const QString& item) {
    Block block;
    block.id = id;
    block.type = type;
    block.item = item;
    return block;
}

Block setColor(const QString& id, const QString& item, const QString& color) {
    auto block = itemBlock(id, BlockType::SetColor, item);
    block.color = color;
    return block;
}

Block variableBlock(const QString& id, BlockType type, const QString& variable, int value) {
    Block block;
    block.id = id;
    block.type = type;
    block.variable = variable;
    block.value = value;
    return block;
}

Block scoreBlock(const QString& id, BlockType type, int value) {
    return variableBlock(id, type, "score", value);
}

Block ifScore(const QString& id, VariableComparison comparison, int value, const QList<Block>& actions = {}) {
    auto block = scoreBlock(id, BlockType::IfVariable, value);
    block.comparison = comparison;
    block.children = actions;
    return block;
}

// A program with one variable, "score", which starts at 0.
LogicProgram scoreProgram() {
    LogicProgram program;
    program.variables = {{"score", 0}};
    return program;
}

SceneItem particlesItem(const QString& id, const QString& preset, bool looping) {
    SceneItem item;
    item.id = id;
    item.type = "particles";
    item.name = id;
    item.x = 800;
    item.y = 400;
    item.width = 200;
    item.height = 200;
    applyParticlePreset(item, preset);
    item.x = 800;
    item.y = 400;
    item.width = 200;
    item.height = 200;
    item.amount = 200;
    item.looping = looping;
    return item;
}

Scene burstScene(bool looping) {
    auto scene = gameScene();
    scene.items.append(particlesItem("fx", "sparks", looping));
    return scene;
}

// Uses every event, action, and a nested If score.
LogicProgram everyBlockProgram() {
    auto program = scoreProgram();
    program.events = {
        eventBlock("start", BlockType::WhenGameStarts, {
            setPosition("s1", "coin", 200, 100),
            setColor("s2", "coin", "#FF0000"),
            scoreBlock("s3", BlockType::SetVariable, 10),
            itemBlock("s4", BlockType::Hide, "label"),
        }),
        held("held-cross", ControllerButton::Cross, {moveBy("c1", "coin", -60, 0)}),
        eventBlock("frame", BlockType::EveryFrame, {
            scoreBlock("f1", BlockType::ChangeVariable, 1),
            ifScore("f2", VariableComparison::GreaterOrEqual, 12, {
                setColor("f3", "player", "#000000"),
                ifScore("f4", VariableComparison::Less, 100, {itemBlock("f5", BlockType::Show, "label")}),
            }),
        }),
        touch("touch", "player", "coin", {scoreBlock("t1", BlockType::ChangeVariable, 100)}),
        held("held-square", ControllerButton::Square, {itemBlock("q1", BlockType::Hide, "coin")}),
    };
    return program;
}

QJsonObject minimalLogic(const QJsonArray& events) {
    return {{"schemaVersion", 1}, {"events", events}};
}

QJsonObject frameEvent(const QJsonArray& actions) {
    return {{"id", "e"}, {"type", "everyFrame"}, {"actions", actions}};
}

LogicLoadResult loadBytes(const QByteArray& bytes) {
    QTemporaryDir directory;
    const auto path = directory.path() + "/logic.json";
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {std::nullopt, "test could not write its file"};
    file.write(bytes);
    file.close();
    return loadLogic(path);
}

LogicLoadResult loadJson(const QJsonObject& root) {
    return loadBytes(QJsonDocument(root).toJson());
}

LogicLoadResult loadAction(const QJsonObject& action) {
    return loadJson(minimalLogic({frameEvent({action})}));
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeBytes(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}

QStringList messages(const QList<LogicProblem>& problems) {
    QStringList result;
    for (const auto& problem : problems) result << problem.blockId + ": " + problem.message;
    return result;
}

bool sameBlock(const Block& left, const Block& right) {
    if (left.id != right.id || left.type != right.type || left.children.size() != right.children.size() || left.sound != right.sound) return false;
    const auto& info = blockInfo(left.type);
    if (info.usesButton && left.button != right.button) return false;
    if (info.usesItem && left.item != right.item) return false;
    if (info.usesOtherItem && left.otherItem != right.otherItem) return false;
    if (info.usesPosition && (left.x != right.x || left.y != right.y)) return false;
    if (info.usesColor && left.color != right.color) return false;
    if (info.usesValue && left.value != right.value) return false;
    if (info.usesComparison && left.comparison != right.comparison) return false;
    if (info.usesVariable && left.variable != right.variable) return false;
    for (qsizetype index = 0; index < left.children.size(); ++index) {
        if (!sameBlock(left.children[index], right.children[index])) return false;
    }
    return true;
}

// The first ```json block of docs/blocks-format.md, so the example there has to stay valid.
QByteArray docExample() {
    const auto doc = readBytes(QString(STUDIO_TEST_SOURCE_DIR) + "/docs/blocks-format.md").replace("\r\n", "\n");
    const auto match = QRegularExpression("```json\\n(.*?)```", QRegularExpression::DotMatchesEverythingOption).match(QString::fromUtf8(doc));
    return match.hasMatch() ? match.captured(1).toUtf8() : QByteArray();
}

Scene docScene() {
    Scene scene;
    scene.items = {rectangle("item-1", 120, 300, 160, 160, "#FFD23F", true), text("item-2", 400, 96, "Hello PS5", 12)};
    return scene;
}

const QByteArray DocHeader = R"cpp(// Generated by AnyDevKit Studio from logic.json. Do not edit.
#pragma once

#include "GeneratedScene.hpp"
#include "Input/Buttons.hpp"

#include <array>
#include <cstddef>

namespace GeneratedLogic {

static_assert(GeneratedScene::Items.size() == 2, "GeneratedScene.hpp and GeneratedLogic.hpp do not match. Build the project again from Studio.");

inline constexpr std::size_t TouchEventCount = 1;

struct Memory {
    std::array<bool, TouchEventCount> touching{};
};

template <typename World>
void OnStart([[maybe_unused]] World& world) {
    // event-1: When the game starts
    world.SetVariable(0, 0); // block-1
}

template <typename World>
void OnFrame([[maybe_unused]] World& world, [[maybe_unused]] Memory& memory) {
    // event-2: While a button is held
    if (world.Held(Buttons::Cross)) {
        world.MoveBy(0, 4.0f, 0.0f); // block-2
    }
    // event-3: When two items touch
    {
        const bool touching = world.Touching(0, 1);
        const bool started = touching && !memory.touching[0];
        memory.touching[0] = touching;
        if (started) {
            world.ChangeVariable(0, 1); // block-3
            world.SetVisible(1, false); // block-4
        }
    }
    // event-4: Every frame
    if (world.Variable(0) >= 10) { // block-5
        world.SetColor(0, 1.0f, 0.301961f, 0.301961f); // block-6
    }
}

// Runs the blocks of one scene: when it starts, and each frame while it is showing.
template <typename World>
void OnSceneStart([[maybe_unused]] World& world, [[maybe_unused]] std::size_t scene) {
    OnStart(world);
}

template <typename World>
void OnSceneFrame([[maybe_unused]] World& world, [[maybe_unused]] Memory& memory, [[maybe_unused]] std::size_t scene) {
    OnFrame(world, memory);
}

}
)cpp";

const QByteArray EmptyVariablesHeader = R"cpp(// Generated by AnyDevKit Studio from logic.json and scene.json. Do not edit.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace GeneratedVariables {

inline constexpr std::size_t VariableCount = 0;
inline constexpr std::array<std::int32_t, 0> InitialValues{};
inline constexpr std::array<std::uint32_t, 0> NameHashes{};
inline constexpr bool UsesSaveData = false;

// One piece of a text item that shows variables: plain text, or a variable's value when variable is not -1.
struct Part {
    std::string_view text;
    std::int32_t variable;
};

// A text item that shows variables. quads is how many characters it can ever need, sign and all digits included.
struct BoundText {
    std::size_t item, firstPart, partCount, quads;
};

inline constexpr std::array<Part, 0> Parts{};
inline constexpr std::array<BoundText, 0> BoundTexts{};

}
)cpp";

const QByteArray DocVariablesHeader = R"cpp(// Generated by AnyDevKit Studio from logic.json and scene.json. Do not edit.
#pragma once

#include "GeneratedScene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace GeneratedVariables {

inline constexpr std::size_t VariableCount = 2;
inline constexpr std::array<std::int32_t, 2> InitialValues{{0, 3}}; // score, lives
inline constexpr std::array<std::uint32_t, 2> NameHashes{{3526332565u, 1526042452u}}; // how a saved value finds its variable
inline constexpr bool UsesSaveData = false;

// One piece of a text item that shows variables: plain text, or a variable's value when variable is not -1.
struct Part {
    std::string_view text;
    std::int32_t variable;
};

// A text item that shows variables. quads is how many characters it can ever need, sign and all digits included.
struct BoundText {
    std::size_t item, firstPart, partCount, quads;
};

inline constexpr std::array<Part, 2> Parts{{
    {"Score: ", -1},
    {"", 0},
}};
inline constexpr std::array<BoundText, 1> BoundTexts{{
    {1, 0, 2, 18},
}};

static_assert(GeneratedScene::Items.size() == 2, "GeneratedScene.hpp and GeneratedVariables.hpp do not match. Build the project again from Studio.");
static_assert(GeneratedScene::Items[1].text.size() == 18, "GeneratedScene.hpp and GeneratedVariables.hpp do not match. Build the project again from Studio.");

}
)cpp";

const QByteArray EmptyHeader = R"cpp(// Generated by AnyDevKit Studio from logic.json. Do not edit.
#pragma once

#include <array>
#include <cstddef>

namespace GeneratedLogic {

inline constexpr std::size_t TouchEventCount = 0;

struct Memory {
    std::array<bool, TouchEventCount> touching{};
};

template <typename World>
void OnStart([[maybe_unused]] World& world) {
}

template <typename World>
void OnFrame([[maybe_unused]] World& world, [[maybe_unused]] Memory& memory) {
}

// Runs the blocks of one scene: when it starts, and each frame while it is showing.
template <typename World>
void OnSceneStart([[maybe_unused]] World& world, [[maybe_unused]] std::size_t scene) {
    OnStart(world);
}

template <typename World>
void OnSceneFrame([[maybe_unused]] World& world, [[maybe_unused]] Memory& memory, [[maybe_unused]] std::size_t scene) {
    OnFrame(world, memory);
}

}
)cpp";

// Prints one line per call: the score, then each item's position, visibility and color.
const QByteArray HarnessPrelude = R"cpp(#include "Scene/SceneState.hpp"
#include "Input/Buttons.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

// Every heap allocation in the program goes through here, so a test can prove a frame makes none.
static std::size_t allocations = 0;
void* operator new(std::size_t size) {
    ++allocations;
    if (void* memory = std::malloc(size)) return memory;
    std::abort();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

static_assert(Buttons::Cross == 0x4000);
static_assert(Buttons::Circle == 0x2000);
static_assert(Buttons::Triangle == 0x1000);
static_assert(Buttons::Square == 0x8000);
static_assert(Buttons::DpadUp == 0x10);
static_assert(Buttons::DpadRight == 0x20);
static_assert(Buttons::DpadDown == 0x40);
static_assert(Buttons::DpadLeft == 0x80);

namespace {
void show(const SceneState& scene) {
    int score = 0;
    if (GeneratedVariables::VariableCount != 0) score = static_cast<int>(scene.Variable(0));
    std::printf("score=%d", score);
    for (std::size_t index = 0; index < scene.Current().size(); ++index) {
        const auto [x, y] = scene.Current()[index];
        const auto [red, green, blue] = scene.CurrentColors()[index];
        std::printf(" [%.1f,%.1f %d %.2f,%.2f,%.2f]", static_cast<double>(x), static_cast<double>(y), scene.CurrentVisibility()[index] ? 1 : 0, static_cast<double>(red), static_cast<double>(green), static_cast<double>(blue));
    }
    std::printf("\n");
}
[[maybe_unused]] void frame(SceneState& scene, std::uint32_t buttons = 0, float directionX = 0.0f, double seconds = 0.0) {
    scene.Update(seconds, {directionX, 0.0f}, buttons);
    show(scene);
}
// What a text item shows right now and how many quads it may use.
[[maybe_unused]] void showText(const SceneState& scene, std::size_t item) {
    const auto text = scene.CurrentText(item);
    std::printf("[%.*s] %zu/%zu\n", static_cast<int>(text.size()), text.data(), text.size(), GeneratedScene::Items[item].text.size());
}
}

int main() {
    SceneState scene(1920, 1080);
    show(scene);
)cpp";

struct HarnessResult {
    bool built = false;
    QString buildOutput;
    QStringList lines;
};

bool hasPosixCompiler() {
#ifdef _MSC_VER
    return false;
#else
    return !QString(STUDIO_TEST_CXX).isEmpty();
#endif
}

// Compiles the real runtime SceneState with the generated headers, under the game's warning flags,
// and runs `body` against it on this computer. No console or SDK is involved.
HarnessResult runHarness(const Scene& scene, const LogicProgram& program, const QByteArray& body, bool sanitize = false, const QList<SceneImageData>& images = {}) {
    HarnessResult result;
    const auto logicHeader = generateLogicHeader(program, scene);
    const auto variablesHeader = generateVariablesHeader(program, scene);
    if (logicHeader.isEmpty() || variablesHeader.isEmpty()) {
        result.buildOutput = "the program or the scene is invalid: " + messages(validateLogic(program, scene)).join("; ");
        return result;
    }
    QTemporaryDir directory;
    const auto root = directory.path();
    const QString runtime = QString(STUDIO_TEST_SOURCE_DIR) + "/runtime/SceneGame";

    for (const auto& relative : {QString("include/Scene/SceneState.hpp"), QString("include/Scene/Particles.hpp"), QString("src/Scene/SceneState.cpp"), QString("include/Input/Buttons.hpp"), QString("include/Audio/Mixer.hpp"), QString("include/Save/SaveData.hpp"), QString("include/Scene/World2D.hpp"), QString("include/Scene/Physics2D.hpp")}) {
        auto contents = readBytes(runtime + '/' + relative + ".in");
        if (contents.isEmpty()) {
            result.buildOutput = "missing runtime file " + relative;
            return result;
        }
        writeBytes(root + '/' + relative, contents.replace("@NAME@", "Harness"));
    }
    writeBytes(root + "/src/GeneratedScene.hpp", generateSceneHeader(scene, images));
    writeBytes(root + "/src/GeneratedLogic.hpp", logicHeader);
    writeBytes(root + "/src/GeneratedVariables.hpp", variablesHeader);
    writeBytes(root + "/src/GeneratedSounds.hpp", generateSoundsHeader(static_cast<int>(program.sounds.size())));
    writeBytes(root + "/src/main.cpp", HarnessPrelude + body + "\n    return 0;\n}\n");

    QStringList arguments{"-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I" + root + "/include", "-I" + root + "/src", root + "/src/main.cpp", root + "/src/Scene/SceneState.cpp", "-o", root + "/harness"};
    const QString sysroot(STUDIO_TEST_SYSROOT);
    if (!sysroot.isEmpty()) arguments << "-isysroot" << sysroot;
    if (sanitize) arguments << "-g" << "-fsanitize=address,undefined" << "-fno-sanitize-recover=all";

    QProcess compiler;
    compiler.setProcessChannelMode(QProcess::MergedChannels);
    compiler.start(STUDIO_TEST_CXX, arguments);
    if (!compiler.waitForFinished(120000)) {
        result.buildOutput = "the compiler did not finish: " + compiler.errorString();
        return result;
    }
    result.buildOutput = QString::fromUtf8(compiler.readAll());
    if (compiler.exitCode() != 0) return result;
    result.built = true;

    QProcess harness;
    harness.setProcessChannelMode(QProcess::MergedChannels);
    harness.start(root + "/harness", QStringList{});
    if (!harness.waitForFinished(30000) || harness.exitCode() != 0) {
        result.built = false;
        result.buildOutput = "the harness failed: " + QString::fromUtf8(harness.readAll());
        return result;
    }
    result.lines = QString::fromUtf8(harness.readAll()).split('\n', Qt::SkipEmptyParts);
    return result;
}

}

HarnessResult runHarness(const Scene& scene, const LogicProgram& program, const QByteArray& body, const QList<SceneImageData>& images) {
    return runHarness(scene, program, body, false, images);
}

class TstBlocks : public QObject {
    Q_OBJECT

private slots:
    void catalogDescribesEveryBlockOnce() {
        QCOMPARE(blockCatalog().size(), 31);
        QSet<QString> names;
        QSet<QString> titles;
        for (const auto& info : blockCatalog()) {
            QVERIFY(!info.jsonName.isEmpty());
            QVERIFY(!info.title.isEmpty());
            names.insert(info.jsonName);
            titles.insert(info.title);
            QCOMPARE(blockInfo(info.type).jsonName, info.jsonName);
        }
        QCOMPARE(names.size(), 31);
        QCOMPARE(titles.size(), 31);
        QCOMPARE(buttonCatalog().size(), 8);
        QCOMPARE(comparisonCatalog().size(), 6);
        QVERIFY(blockInfo(BlockType::EveryFrame).isEvent);
        QVERIFY(blockInfo(BlockType::IfVariable).holdsActions);
        QVERIFY(!blockInfo(BlockType::IfVariable).isEvent);
        QVERIFY(!blockInfo(BlockType::MoveBy).holdsActions);
        QVERIFY(blockInfo(BlockType::WhenItemsTouch).usesOtherItem);
        QVERIFY(blockInfo(BlockType::SetVariable).usesVariable);
        QVERIFY(blockInfo(BlockType::ChangeVariable).usesVariable);
        QVERIFY(blockInfo(BlockType::IfVariable).usesVariable);
    }

    void thePaletteHasVariableBlocksInsteadOfScoreBlocks() {
        QSet<QString> names;
        for (const auto& info : blockCatalog()) names.insert(info.jsonName);
        for (const auto* name : {"setVariable", "changeVariable", "ifVariable"}) QVERIFY2(names.contains(name), name);
        for (const auto* name : {"addScore", "setScore", "ifScore"}) QVERIFY2(!names.contains(name), name);
        QCOMPARE(blockInfo(BlockType::SetVariable).title, QString("Set variable"));
        QCOMPARE(blockInfo(BlockType::ChangeVariable).title, QString("Change variable"));
        QCOMPARE(blockInfo(BlockType::IfVariable).title, QString("If variable"));
    }

    void docExampleLoadsAndValidates() {
        const auto example = docExample();
        QVERIFY2(!example.isEmpty(), "docs/blocks-format.md has no json example");
        const auto result = loadBytes(example);
        QVERIFY2(result.program.has_value(), qPrintable(result.error));
        QCOMPARE(result.program->events.size(), 4);
        QCOMPARE(messages(validateLogic(*result.program, docScene())), QStringList());
    }

    void generatesTheHeaderShownInTheDocs() {
        const auto result = loadBytes(docExample());
        QVERIFY2(result.program.has_value(), qPrintable(result.error));
        QCOMPARE(generateLogicHeader(*result.program, docScene()), DocHeader);
    }

    void headerSectionOfTheDocsMatchesTheGenerator() {
        const auto doc = readBytes(QString(STUDIO_TEST_SOURCE_DIR) + "/docs/blocks-format.md").replace("\r\n", "\n");
        QVERIFY2(doc.contains(DocHeader), "the header in docs/blocks-format.md differs from the generator");
    }

    void saveThenLoadKeepsEveryBlock() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        const auto program = everyBlockProgram();
        QString error;
        QVERIFY2(saveLogic(path, program, error), qPrintable(error));
        const auto loaded = loadLogic(path);
        QVERIFY2(loaded.program.has_value(), qPrintable(loaded.error));
        QCOMPARE(loaded.program->events.size(), program.events.size());
        for (qsizetype index = 0; index < program.events.size(); ++index) {
            QVERIFY(sameBlock(program.events[index], loaded.program->events[index]));
        }
    }

    void savedFileUsesTheDocumentedShape() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        QString error;
        QVERIFY2(saveLogic(path, everyBlockProgram(), error), qPrintable(error));
        const auto root = QJsonDocument::fromJson(readBytes(path)).object();
        QCOMPARE(root.value("schemaVersion").toInt(), 4);
        const auto savedVariables = root.value("variables").toArray();
        QCOMPARE(savedVariables.size(), qsizetype(1));
        QCOMPARE(savedVariables.at(0).toObject().value("name").toString(), QString("score"));
        QCOMPARE(savedVariables.at(0).toObject().value("initial").toInt(), 0);
        const auto events = root.value("scenes").toArray().at(0).toObject().value("events").toArray();
        QCOMPARE(events.size(), 5);
        const auto held = events.at(1).toObject();
        QCOMPARE(held.value("type").toString(), QString("whileButtonHeld"));
        QCOMPARE(held.value("button").toString(), QString("cross"));
        const auto move = held.value("actions").toArray().at(0).toObject();
        QCOMPARE(move.value("type").toString(), QString("moveBy"));
        QCOMPARE(move.value("x").toDouble(), -60.0);
        QVERIFY(!move.contains("color"));
        QVERIFY(!move.contains("actions"));
    }

    void saveRefusesAnInvalidProgramAndKeepsTheOldFile() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        writeBytes(path, "previous");
        auto program = scoreProgram();
        program.events = {eventBlock("a", BlockType::EveryFrame), eventBlock("a", BlockType::EveryFrame)};
        QString error;
        QVERIFY(!saveLogic(path, program, error));
        QVERIFY2(error.contains("\"a\""), qPrintable(error));
        QCOMPARE(readBytes(path), QByteArray("previous"));
    }

    void saveReportsAFolderItCannotWriteTo() {
        QTemporaryDir directory;
        QString error;
        QVERIFY(!saveLogic(directory.path() + "/missing/logic.json", everyBlockProgram(), error));
        QVERIFY2(error.contains("logic.json"), qPrintable(error));
    }

    void unfinishedProgramsCanBeSavedAndReopened() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        auto program = scoreProgram();
        program.events = {eventBlock("e", BlockType::EveryFrame, {itemBlock("no-item", BlockType::Hide, ""), itemBlock("gone", BlockType::Show, "deleted")})};
        QString error;
        QVERIFY2(saveLogic(path, program, error), qPrintable(error));
        QVERIFY2(loadLogic(path).program.has_value(), qPrintable(loadLogic(path).error));
    }

    void loadExplainsFileProblems_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("expected");
        QTest::newRow("bad json") << QByteArray("{nope") << "not valid JSON";
        QTest::newRow("not an object") << QByteArray("[]") << "must contain";
        QTest::newRow("wrong schema") << QJsonDocument(QJsonObject{{"schemaVersion", 9}, {"events", QJsonArray{}}}).toJson() << "schemaVersion";
        QTest::newRow("no variables list") << QJsonDocument(QJsonObject{{"schemaVersion", 2}, {"events", QJsonArray{}}}).toJson() << "\"variables\"";
        QTest::newRow("no events list") << QJsonDocument(QJsonObject{{"schemaVersion", 1}}).toJson() << "\"events\"";
        QTest::newRow("event not object") << QJsonDocument(minimalLogic({1})).toJson() << "number 1";
    }

    void loadExplainsFileProblems() {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, expected);
        const auto result = loadBytes(bytes);
        QVERIFY(!result.program.has_value());
        QVERIFY2(result.error.contains(expected), qPrintable(result.error));
    }

    void loadExplainsAMissingFile() {
        QTemporaryDir directory;
        const auto result = loadLogic(directory.path() + "/logic.json");
        QVERIFY(!result.program.has_value());
        QVERIFY2(result.error.contains("logic.json"), qPrintable(result.error));
    }

    void loadRefusesAHugeFile() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(MaximumLogicFileBytes + 1));
        file.close();
        const auto result = loadLogic(path);
        QVERIFY(!result.program.has_value());
        QVERIFY2(result.error.contains("too large"), qPrintable(result.error));
    }

    void blockShapeErrorsNameTheBlock_data() {
        QTest::addColumn<QJsonObject>("action");
        QTest::addColumn<QString>("expected");
        QTest::newRow("unknown type") << QJsonObject{{"id", "b"}, {"type", "explode"}} << "\"explode\"";
        QTest::newRow("no type") << QJsonObject{{"id", "b"}} << "(b)";
        QTest::newRow("no id") << QJsonObject{{"type", "hide"}, {"item", "coin"}} << "Hide item";
        QTest::newRow("bad id") << QJsonObject{{"id", "b\n"}, {"type", "hide"}, {"item", "coin"}} << "letters";
        QTest::newRow("long id") << QJsonObject{{"id", QString(MaximumBlockIdLength + 1, 'b')}, {"type", "hide"}, {"item", "coin"}} << "letters";
        QTest::newRow("x not a number") << QJsonObject{{"id", "b"}, {"type", "moveBy"}, {"item", "coin"}, {"x", "1"}, {"y", 0}} << "Move item";
        QTest::newRow("x too large") << QJsonObject{{"id", "b"}, {"type", "moveBy"}, {"item", "coin"}, {"x", 2000000}, {"y", 0}} << "Move item";
        QTest::newRow("bad color") << QJsonObject{{"id", "b"}, {"type", "setColor"}, {"item", "coin"}, {"color", "red"}} << "Set item color";
        QTest::newRow("fractional score") << QJsonObject{{"id", "b"}, {"type", "addScore"}, {"value", 1.5}} << "Change variable";
        QTest::newRow("score too large") << QJsonObject{{"id", "b"}, {"type", "setScore"}, {"value", 2000000000}} << "Set variable";
        QTest::newRow("bad comparison") << QJsonObject{{"id", "b"}, {"type", "ifScore"}, {"comparison", "like"}, {"value", 1}, {"actions", QJsonArray{}}} << "If variable";
        QTest::newRow("actions not a list") << QJsonObject{{"id", "b"}, {"type", "ifScore"}, {"comparison", "equal"}, {"value", 1}} << "If variable";
        QTest::newRow("holds blocks but cannot") << QJsonObject{{"id", "b"}, {"type", "hide"}, {"item", "coin"}, {"actions", QJsonArray{}}} << "Hide item";
        QTest::newRow("event inside event") << QJsonObject{{"id", "b"}, {"type", "everyFrame"}, {"actions", QJsonArray{}}} << "Every frame";
    }

    void blockShapeErrorsNameTheBlock() {
        QFETCH(QJsonObject, action);
        QFETCH(QString, expected);
        const auto result = loadAction(action);
        QVERIFY(!result.program.has_value());
        QVERIFY2(result.error.contains(expected), qPrintable(result.error));
    }

    void actionAtTheTopLevelIsRefused() {
        const auto result = loadJson(minimalLogic({QJsonObject{{"id", "b"}, {"type", "hide"}, {"item", "coin"}}}));
        QVERIFY(!result.program.has_value());
        QVERIFY2(result.error.contains("Hide item"), qPrintable(result.error));
        QVERIFY2(result.error.contains("inside"), qPrintable(result.error));
    }

    void unknownButtonNamesTheEvent() {
        const auto result = loadJson(minimalLogic({QJsonObject{{"id", "e"}, {"type", "whileButtonHeld"}, {"button", "start"}, {"actions", QJsonArray{}}}}));
        QVERIFY(!result.program.has_value());
        QVERIFY2(result.error.contains("While a button is held"), qPrintable(result.error));
    }

    void duplicateIdsAreRefused() {
        const auto result = loadAction(QJsonObject{{"id", "e"}, {"type", "hide"}, {"item", "coin"}});
        QVERIFY(!result.program.has_value());
        QVERIFY2(result.error.contains("\"e\""), qPrintable(result.error));
    }

    void limitsAreEnforced() {
        LogicProgram manyEvents;
        for (int index = 0; index < MaximumLogicEvents + 1; ++index) manyEvents.events << eventBlock(QString("e%1").arg(index), BlockType::EveryFrame);
        QVERIFY2(messages(validateLogicStructure(manyEvents)).join(' ').contains(QString::number(MaximumLogicEvents)), "the event limit was not enforced");

        LogicProgram manyBlocks;
        QList<Block> actions;
        for (int index = 0; index < MaximumLogicBlocks; ++index) actions << itemBlock(QString("b%1").arg(index), BlockType::Hide, "coin");
        manyBlocks.events = {eventBlock("e", BlockType::EveryFrame, actions)};
        QVERIFY2(messages(validateLogicStructure(manyBlocks)).join(' ').contains(QString::number(MaximumLogicBlocks)), "the block limit was not enforced");
        QCOMPARE(validateLogicStructure(manyBlocks).size(), 1);

        const auto nested = [](int depth) {
            Block block = itemBlock("leaf", BlockType::Hide, "coin");
            for (int level = depth - 1; level >= 2; --level) block = ifScore(QString("if%1").arg(level), VariableComparison::Equal, 0, {block});
            auto program = scoreProgram();
            program.events = {eventBlock("root", BlockType::EveryFrame, {block})};
            return program;
        };
        QCOMPARE(messages(validateLogicStructure(nested(MaximumLogicDepth))), QStringList());
        QVERIFY2(messages(validateLogicStructure(nested(MaximumLogicDepth + 1))).join(' ').contains("inside each other"), "the nesting limit was not enforced");
    }

    void aVeryDeepProgramIsReportedWithoutRecursingForever() {
        Block block = itemBlock("leaf", BlockType::Hide, "coin");
        for (int level = 0; level < 1000; ++level) block = ifScore(QString("i%1").arg(level), VariableComparison::Equal, 0, {block});
        auto program = scoreProgram();
        program.events = {eventBlock("root", BlockType::EveryFrame, {block})};
        QVERIFY(!validateLogicStructure(program).isEmpty());
    }

    void deepJsonIsRefusedAtLoad() {
        QJsonObject block{{"id", "leaf"}, {"type", "hide"}, {"item", "coin"}};
        for (int level = 0; level < MaximumLogicDepth + 4; ++level) {
            block = {{"id", QString("i%1").arg(level)}, {"type", "ifScore"}, {"comparison", "equal"}, {"value", 0}, {"actions", QJsonArray{block}}};
        }
        const auto result = loadAction(block);
        QVERIFY(!result.program.has_value());
        QVERIFY2(result.error.contains("inside each other"), qPrintable(result.error));
    }

    void validatePassesAGoodProgram() {
        QCOMPARE(messages(validateLogic(everyBlockProgram(), gameScene())), QStringList());
    }

    void missingItemIsAnErrorThatNamesTheBlockAndItem() {
        auto program = scoreProgram();
        program.events = {eventBlock("e", BlockType::EveryFrame, {moveBy("m1", "goblin", 1, 0)})};
        const auto problems = validateLogic(program, gameScene());
        QCOMPARE(problems.size(), 1);
        QCOMPARE(problems[0].blockId, QString("m1"));
        QVERIFY2(problems[0].message.contains("Move item"), qPrintable(problems[0].message));
        QVERIFY2(problems[0].message.contains("goblin"), qPrintable(problems[0].message));
        QVERIFY2(problems[0].message.contains("not in the scene"), qPrintable(problems[0].message));
    }

    void unchosenItemsAndSameItemTouchesAreErrors() {
        auto program = scoreProgram();
        program.events = {
            eventBlock("e", BlockType::EveryFrame, {itemBlock("h", BlockType::Hide, "")}),
            touch("t1", "coin", "coin"),
            touch("t2", "", "coin"),
        };
        const auto problems = validateLogic(program, gameScene());
        QCOMPARE(problems.size(), 3);
        QCOMPARE(problems[0].blockId, QString("h"));
        QVERIFY2(problems[0].message.contains("Hide item"), qPrintable(problems[0].message));
        QVERIFY2(problems[0].message.contains("Choose"), qPrintable(problems[0].message));
        QCOMPARE(problems[1].blockId, QString("t1"));
        QVERIFY2(problems[1].message.contains("two different items"), qPrintable(problems[1].message));
        QCOMPARE(problems[2].blockId, QString("t2"));
    }

    void validateReportsEveryProblemInOrder() {
        auto program = scoreProgram();
        program.events = {eventBlock("e", BlockType::EveryFrame, {
            moveBy("m1", "ghost", 1, 0),
            setColor("c1", "coin", "nope"),
        })};
        const auto problems = validateLogic(program, gameScene());
        QCOMPARE(problems.size(), 2);
        QCOMPARE(problems[0].blockId, QString("m1"));
        QCOMPARE(problems[1].blockId, QString("c1"));
    }

    void newBlockIdIsUnused() {
        auto program = scoreProgram();
        program.events = {eventBlock("block-1", BlockType::EveryFrame, {itemBlock("block-2", BlockType::Hide, "coin")})};
        const auto id = newBlockId(program);
        QVERIFY(!id.isEmpty());
        QVERIFY(id != "block-1");
        QVERIFY(id != "block-2");
        program.events[0].children << itemBlock(id, BlockType::Show, "coin");
        QCOMPARE(messages(validateLogicStructure(program)), QStringList());
    }

    void emptyProgramGeneratesTheEmptyHeader() {
        QCOMPARE(generateEmptyLogicHeader(), EmptyHeader);
        QCOMPARE(generateLogicHeader(LogicProgram{}, gameScene()), EmptyHeader);
        QCOMPARE(generateLogicHeader(LogicProgram{}, Scene{}), EmptyHeader);
    }

    void invalidProgramOrSceneGeneratesNothing() {
        auto program = scoreProgram();
        program.events = {eventBlock("e", BlockType::EveryFrame, {moveBy("m1", "ghost", 1, 0)})};
        QVERIFY(generateLogicHeader(program, gameScene()).isEmpty());
        Scene broken;
        broken.background = "nope";
        QVERIFY(generateLogicHeader(everyBlockProgram(), broken).isEmpty());
    }

    void itemTextNeverReachesTheHeader() {
        auto scene = gameScene();
        scene.items[1].name = "evil\n#error injected";
        scene.items[1].id = "coin\n#error injected";
        auto program = scoreProgram();
        program.events = {eventBlock("e", BlockType::EveryFrame, {itemBlock("h", BlockType::Hide, scene.items[1].id)})};
        const auto header = generateLogicHeader(program, scene);
        QVERIFY(!header.isEmpty());
        QVERIFY(!header.contains("injected"));
    }

    void exportWritesTheEmptyHeaderWithoutALogicFile() {
        QTemporaryDir project;
        QString error;
        QVERIFY2(exportLogicHeader(project.path(), gameScene(), error), qPrintable(error));
        QCOMPARE(readBytes(project.path() + "/src/GeneratedLogic.hpp"), EmptyHeader);
    }

    void exportGeneratesFromLogicFile() {
        QTemporaryDir project;
        QString error;
        QVERIFY2(saveLogic(project.path() + "/logic.json", everyBlockProgram(), error), qPrintable(error));
        QVERIFY2(exportLogicHeader(project.path(), gameScene(), error), qPrintable(error));
        QCOMPARE(readBytes(project.path() + "/src/GeneratedLogic.hpp"), generateLogicHeader(everyBlockProgram(), gameScene()));
    }

    void exportFailureNamesTheBlockAndKeepsTheOldHeader() {
        QTemporaryDir project;
        const auto header = project.path() + "/src/GeneratedLogic.hpp";
        writeBytes(header, "previous");
        auto program = scoreProgram();
        program.events = {eventBlock("e", BlockType::EveryFrame, {moveBy("m1", "ghost", 1, 0), moveBy("m2", "ghost", 1, 0)})};
        QString error;
        QVERIFY2(saveLogic(project.path() + "/logic.json", program, error), qPrintable(error));
        QVERIFY(!exportLogicHeader(project.path(), gameScene(), error));
        QVERIFY2(error.contains("Move item"), qPrintable(error));
        QVERIFY2(error.contains("ghost"), qPrintable(error));
        QVERIFY2(error.contains("1 more"), qPrintable(error));
        QCOMPARE(readBytes(header), QByteArray("previous"));
    }

    void exportReportsABrokenLogicFile() {
        QTemporaryDir project;
        writeBytes(project.path() + "/logic.json", "{nope");
        QString error;
        QVERIFY(!exportLogicHeader(project.path(), gameScene(), error));
        QVERIFY2(error.contains("logic.json"), qPrintable(error));
    }

    void exportWillNotWriteThroughALink() {
#ifdef Q_OS_WIN
        QSKIP("QFile::link makes .lnk shortcuts on Windows, not symbolic links.");
#endif
        QTemporaryDir project;
        QTemporaryDir outside;
        QDir().mkpath(project.path() + "/src");
        QVERIFY(QFile::link(outside.path() + "/target.hpp", project.path() + "/src/GeneratedLogic.hpp"));
        QString error;
        QVERIFY(!exportLogicHeader(project.path(), gameScene(), error));
        QVERIFY2(error.contains("link"), qPrintable(error));
        QVERIFY(!QFile::exists(outside.path() + "/target.hpp"));

        QTemporaryDir second;
        QVERIFY(QFile::link(outside.path(), second.path() + "/src"));
        QVERIFY(!exportLogicHeader(second.path(), gameScene(), error));
        QVERIFY2(error.contains("link"), qPrintable(error));
        QVERIFY(!QFile::exists(outside.path() + "/GeneratedLogic.hpp"));
    }

    void exportTreatsADanglingLogicLinkAsAProblem() {
#ifdef Q_OS_WIN
        QSKIP("QFile::link makes .lnk shortcuts on Windows, not symbolic links.");
#endif
        QTemporaryDir project;
        QVERIFY(QFile::link(project.path() + "/nowhere.json", project.path() + "/logic.json"));
        QString error;
        QVERIFY(!exportLogicHeader(project.path(), gameScene(), error));
        QVERIFY2(error.contains("logic.json"), qPrintable(error));
    }

    void runtimeRunsEveryBlock() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        const auto header = generateLogicHeader(everyBlockProgram(), gameScene());
        QVERIFY(!header.isEmpty());
        const auto result = runHarness(gameScene(), everyBlockProgram(), R"cpp(
    frame(scene);
    frame(scene, Buttons::Cross);
    frame(scene);
    frame(scene, Buttons::Square);
    frame(scene);
    frame(scene, Buttons::Cross);)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        const QStringList expected{
            "score=10 [100.0,100.0 1 1.00,1.00,1.00] [200.0,100.0 1 1.00,0.00,0.00] [0.0,0.0 0 0.00,0.00,1.00]",
            "score=11 [100.0,100.0 1 1.00,1.00,1.00] [200.0,100.0 1 1.00,0.00,0.00] [0.0,0.0 0 0.00,0.00,1.00]",
            "score=112 [100.0,100.0 1 0.00,0.00,0.00] [140.0,100.0 1 1.00,0.00,0.00] [0.0,0.0 1 0.00,0.00,1.00]",
            "score=113 [100.0,100.0 1 0.00,0.00,0.00] [140.0,100.0 1 1.00,0.00,0.00] [0.0,0.0 1 0.00,0.00,1.00]",
            "score=114 [100.0,100.0 1 0.00,0.00,0.00] [140.0,100.0 0 1.00,0.00,0.00] [0.0,0.0 1 0.00,0.00,1.00]",
            "score=115 [100.0,100.0 1 0.00,0.00,0.00] [140.0,100.0 0 1.00,0.00,0.00] [0.0,0.0 1 0.00,0.00,1.00]",
            "score=116 [100.0,100.0 1 0.00,0.00,0.00] [80.0,100.0 0 1.00,0.00,0.00] [0.0,0.0 1 0.00,0.00,1.00]",
        };
        QCOMPARE(result.lines, expected);
    }

    void soundBlocksLoadAndSave() {
        const auto result = loadBytes(R"json({"schemaVersion": 3, "variables": [], "sounds": [
            {"name": "coin", "source": "assets/sounds/coin.wav"}, {"name": "theme", "source": "assets/sounds/theme.wav"}
        ], "events": [{"id": "e", "type": "everyFrame", "actions": [
            {"id": "a", "type": "playSound", "sound": "coin"},
            {"id": "b", "type": "playMusic", "sound": "theme"},
            {"id": "c", "type": "stopMusic"},
            {"id": "d", "type": "setVolume", "value": 40},
            {"id": "f", "type": "saveVariables"},
            {"id": "g", "type": "loadVariables"}
        ]}]})json");
        QVERIFY2(result.program.has_value(), qPrintable(result.error));
        const auto& actions = result.program->events.first().children;
        QCOMPARE(result.program->sounds.size(), qsizetype(2));
        QCOMPARE(result.program->sounds[1].source, QString("assets/sounds/theme.wav"));
        QCOMPARE(actions[0].sound, QString("coin"));
        QCOMPARE(actions[1].type, BlockType::PlayMusic);
        QCOMPARE(actions[3].value, 40);

        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        QString error;
        QVERIFY2(saveLogic(path, *result.program, error), qPrintable(error));
        const auto again = loadLogic(path);
        QVERIFY2(again.program.has_value(), qPrintable(again.error));
        QCOMPARE(again.program->sounds.size(), qsizetype(2));
        QCOMPARE(again.program->events.first().children[0].sound, QString("coin"));
        QVERIFY(QJsonDocument::fromJson(readBytes(path)).object().value("sounds").isArray());
    }

    void soundBlocksNeedTheLatestSchema() {
        const auto result = loadJson({{"schemaVersion", 2}, {"variables", QJsonArray{}}, {"events", QJsonArray{frameEvent({QJsonObject{{"id", "b"}, {"type", "stopMusic"}}})}}});
        QVERIFY(!result.program.has_value());
        QVERIFY2(result.error.contains("schemaVersion 3"), qPrintable(result.error));

        const auto noList = loadJson({{"schemaVersion", 3}, {"variables", QJsonArray{}}, {"events", QJsonArray{}}});
        QVERIFY(!noList.program.has_value());
        QVERIFY2(noList.error.contains("sounds"), qPrintable(noList.error));

        const auto badSound = loadJson({{"schemaVersion", 3}, {"variables", QJsonArray{}}, {"sounds", QJsonArray{QJsonObject{{"name", "bad name"}, {"source", "assets/sounds/a.wav"}}}}, {"events", QJsonArray{}}});
        QVERIFY(!badSound.program.has_value());
        QVERIFY2(badSound.error.contains("sound name"), qPrintable(badSound.error));
    }

    void aSoundBlockNeedsASoundThatExists() {
        LogicProgram program;
        Block play;
        play.id = "b";
        play.type = BlockType::PlaySound;
        program.events = {eventBlock("e", BlockType::EveryFrame, {play})};
        auto problems = validateLogic(program, gameScene());
        QCOMPARE(problems.size(), qsizetype(1));
        QVERIFY2(problems.first().message.contains("no sound"), qPrintable(problems.first().message));
        QVERIFY(generateLogicHeader(program, gameScene()).isEmpty());

        program.events.first().children.first().sound = "gone";
        problems = validateLogic(program, gameScene());
        QCOMPARE(problems.size(), qsizetype(1));
        QVERIFY2(problems.first().message.contains("gone"), qPrintable(problems.first().message));
        // A program with a missing sound can still be saved and repaired.
        QVERIFY(validateLogicStructure(program).isEmpty());
    }

    void volumeIsZeroToAHundred() {
        LogicProgram program;
        Block volume;
        volume.id = "b";
        volume.type = BlockType::SetVolume;
        volume.value = 101;
        program.events = {eventBlock("e", BlockType::EveryFrame, {volume})};
        auto problems = validateLogicStructure(program);
        QCOMPARE(problems.size(), qsizetype(1));
        QVERIFY2(problems.first().message.contains("0 to 100"), qPrintable(problems.first().message));
        program.events.first().children.first().value = -1;
        QVERIFY(!validateLogicStructure(program).isEmpty());
        program.events.first().children.first().value = 100;
        QVERIFY(validateLogicStructure(program).isEmpty());
    }

    void soundBlocksGenerateTheirCalls() {
        LogicProgram program;
        program.sounds = {{"coin", "assets/sounds/coin.wav"}, {"theme", "assets/sounds/theme.wav"}};
        Block play;
        play.id = "a";
        play.type = BlockType::PlaySound;
        play.sound = "theme"; // index 1, so it cannot be mistaken for the first sound
        Block music = play;
        music.id = "b";
        music.type = BlockType::PlayMusic;
        music.sound = "coin";
        Block stop;
        stop.id = "c";
        stop.type = BlockType::StopMusic;
        Block volume;
        volume.id = "d";
        volume.type = BlockType::SetVolume;
        volume.value = 40;
        Block save;
        save.id = "f";
        save.type = BlockType::SaveVariables;
        Block load;
        load.id = "g";
        load.type = BlockType::LoadVariables;
        program.events = {eventBlock("e", BlockType::EveryFrame, {play, music, stop, volume, save, load})};

        const auto header = generateLogicHeader(program, gameScene());
        QVERIFY2(header.contains("world.PlaySound(1); // a"), header.constData());
        QVERIFY2(header.contains("world.PlayMusic(0); // b"), header.constData());
        QVERIFY2(header.contains("world.StopMusic(); // c"), header.constData());
        QVERIFY2(header.contains("world.SetVolume(40); // d"), header.constData());
        QVERIFY2(header.contains("world.SaveVariables(); // f"), header.constData());
        QVERIFY2(header.contains("world.LoadVariables(); // g"), header.constData());
        QVERIFY2(header.contains("#include \"GeneratedSounds.hpp\""), header.constData());
        QVERIFY2(header.contains("GeneratedSounds::SoundCount == 2"), header.constData());
    }

    void aProgramWithoutSoundsDoesNotIncludeThem() {
        auto program = scoreProgram();
        program.events = {eventBlock("e", BlockType::EveryFrame, {moveBy("m", "player", 1, 0)})};
        QVERIFY(!generateLogicHeader(program, gameScene()).contains("GeneratedSounds"));
        QVERIFY(!generateEmptyLogicHeader().contains("GeneratedSounds"));
    }

    void variablesHeaderCarriesSaveDataHashes() {
        auto program = scoreProgram();
        const auto plain = generateVariablesHeader(program, gameScene());
        QVERIFY2(plain.contains("UsesSaveData = false"), plain.constData());
        // FNV-1a of "score", the value runtime/SceneGame/include/Save/SaveData.hpp computes.
        QVERIFY2(plain.contains("NameHashes{{3526332565u}}"), plain.constData());

        LogicProgram withSave;
        withSave.variables = program.variables;
        Block save;
        save.id = "s";
        save.type = BlockType::SaveVariables;
        withSave.events = {eventBlock("e", BlockType::EveryFrame, {save})};
        QVERIFY(generateVariablesHeader(withSave, gameScene()).contains("UsesSaveData = true"));

        const auto empty = generateVariablesHeader(LogicProgram{}, gameScene());
        QVERIFY2(empty.contains("std::array<std::uint32_t, 0> NameHashes{};"), empty.constData());
    }

    void exportingLogicAlsoExportsSounds() {
        QTemporaryDir project;
        const auto wav = QByteArray::fromHex("524946462600000057415645666d7420100000000100010080bb000000770100020010006461746102000000" "0500");
        writeBytes(project.path() + "/assets/sounds/coin.wav", wav);
        writeBytes(project.path() + "/logic.json", R"json({"schemaVersion": 3, "variables": [], "sounds": [{"name": "coin", "source": "assets/sounds/coin.wav"}], "events": []})json");
        QString error;
        QVERIFY2(exportLogicHeader(project.path(), gameScene(), error), qPrintable(error));
        QVERIFY(readBytes(project.path() + "/src/GeneratedSounds.hpp").contains("SoundCount = 1"));
        QVERIFY(QFile::exists(project.path() + "/src/GeneratedSounds/sound-0.pcm"));

        QFile::remove(project.path() + "/assets/sounds/coin.wav");
        QVERIFY(!exportLogicHeader(project.path(), gameScene(), error));
        QVERIFY2(error.contains("coin.wav"), qPrintable(error));
    }

    void aProjectWithNoLogicGetsAnEmptySoundTable() {
        QTemporaryDir project;
        QString error;
        QVERIFY2(exportLogicHeader(project.path(), gameScene(), error), qPrintable(error));
        QVERIFY(readBytes(project.path() + "/src/GeneratedSounds.hpp").contains("SoundCount = 0"));
    }

    void generatedSoundBlocksRunAgainstTheRealMixerUnderSanitizers() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        LogicProgram program;
        program.variables = {{"score", 7}};
        program.sounds = {{"coin", "assets/sounds/coin.wav"}, {"theme", "assets/sounds/theme.wav"}};
        Block music;
        music.id = "m";
        music.type = BlockType::PlayMusic;
        music.sound = "theme";
        Block volume;
        volume.id = "v";
        volume.type = BlockType::SetVolume;
        volume.value = 50;
        Block play = music;
        play.id = "p";
        play.type = BlockType::PlaySound;
        play.sound = "coin";
        Block save;
        save.id = "s";
        save.type = BlockType::SaveVariables;
        auto changed = variableBlock("c", BlockType::SetVariable, "score", 99);
        Block load;
        load.id = "l";
        load.type = BlockType::LoadVariables;
        program.events = {eventBlock("st", BlockType::WhenGameStarts, {music, volume}), held("h", ControllerButton::Cross, {play, save, changed, load})};

        const auto result = runHarness(gameScene(), program, R"cpp(
    std::array<std::int16_t, 8> coin{500, 500, 500, 500, 500, 500, 500, 500};
    std::array<std::int16_t, 8> theme{-100, -100, -100, -100, -100, -100, -100, -100};
    std::array clips{Audio::Clip{coin.data(), 4}, Audio::Clip{theme.data(), 4}};
    Audio::Mixer mixer(clips);
    SceneState::SaveMailbox mailbox;
    SceneState world(1920, 1080, &mixer, &mailbox);
    std::array<std::int16_t, 4> output{};
    mixer.Mix(output);
    std::printf("after start: %d\n", output[0]);
    world.Update(1.0 / 60.0, {0.0f, 0.0f}, Buttons::Cross);
    mixer.Mix(output);
    std::printf("after press: %d\n", output[0]);
    std::array<std::int32_t, 1> saved{};
    std::printf("saved %d %d\n", mailbox.TakeSave(saved) ? 1 : 0, saved[0]);
    std::printf("load requested %d\n", mailbox.TakeLoadRequest() ? 1 : 0);
    mailbox.PublishLoaded(std::array<std::int32_t, 1>{123});
    std::printf("before update %d\n", world.Variable(0));
    world.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);
    std::printf("after update %d\n", world.Variable(0));
    SceneState silent(1920, 1080);
    silent.Update(1.0 / 60.0, {0.0f, 0.0f}, Buttons::Cross); // no mixer, no mailbox: every block is a no-op
    std::printf("silent %d\n", silent.Variable(0));
)cpp", true);
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        // The first line comes from the prelude's own scene, which has no mixer.
        QVERIFY2(result.lines.size() >= 7, qPrintable(result.lines.join('|')));
        const auto bodyLines = result.lines.mid(result.lines.size() - 7);
        const QStringList expected{
            "after start: -50",
            "after press: 200",
            "saved 1 7",
            "load requested 1",
            "before update 99",
            "after update 123",
            "silent 99",
        };
        QCOMPARE(bodyLines, expected);
    }

    void burstParticlesLoadsAndSaves() {
        const auto result = loadAction(QJsonObject{{"id", "b"}, {"type", "burstParticles"}, {"item", "fx"}});
        QVERIFY2(result.program.has_value(), qPrintable(result.error));
        const auto& block = result.program->events.first().children.first();
        QCOMPARE(block.item, QString("fx"));
        QCOMPARE(blockInfo(block.type).title, QString("Burst particles from item"));
        QCOMPARE(blockInfo(block.type).jsonName, QString("burstParticles"));
        QVERIFY(blockInfo(block.type).usesItem);
    }

    void burstNeedsAParticlesItem() {
        auto program = scoreProgram();
        program.events = {eventBlock("e", BlockType::EveryFrame, {itemBlock("b", BlockType::BurstParticles, "coin")})};
        const auto problems = validateLogic(program, burstScene(true));
        QCOMPARE(problems.size(), qsizetype(1));
        QVERIFY2(problems.first().message.contains("Particles"), qPrintable(problems.first().message));
        QCOMPARE(problems.first().blockId, QString("b"));
        QVERIFY(generateLogicHeader(program, burstScene(true)).isEmpty());
    }

    void burstGeneratesItsCall() {
        auto program = scoreProgram();
        program.events = {touch("t", "player", "coin", {itemBlock("b", BlockType::BurstParticles, "fx")})};
        const auto header = generateLogicHeader(program, burstScene(true));
        QVERIFY2(header.contains("world.BurstParticles(3); // b"), header.constData());
        QVERIFY(!header.contains("AwaitBurst"));
    }

    void aOneShotItemTheBlocksBurstWaitsForTheBlock() {
        auto program = scoreProgram();
        program.events = {touch("t", "player", "coin", {itemBlock("b", BlockType::BurstParticles, "fx")})};
        const auto header = generateLogicHeader(program, burstScene(false));
        QVERIFY2(header.contains("world.AwaitBurst(3);"), header.constData());
        // Waiting has to be set up before the first frame, in OnStart.
        QVERIFY(header.indexOf("world.AwaitBurst(3);") < header.indexOf("OnFrame"));
    }

    void runtimeBurstsWhenTouched() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        auto program = scoreProgram();
        program.events = {touch("t", "player", "coin", {itemBlock("b", BlockType::BurstParticles, "fx")})};
        const auto header = generateLogicHeader(program, burstScene(false));
        QVERIFY(!header.isEmpty());
        const auto result = runHarness(burstScene(false), program, R"cpp(
    scene.Update(0.5, {0.0f, 0.0f}, 0);
    std::printf("waiting=%zu\n", scene.ParticleQuads(3).size());
    scene.SetPosition(0, 400.0f, 100.0f);
    scene.Update(1.0 / 120.0, {0.0f, 0.0f}, 0);
    std::printf("burst=%zu\n", scene.ParticleQuads(3).size());
    scene.Update(5.0, {0.0f, 0.0f}, 0);
    scene.Update(5.0, {0.0f, 0.0f}, 0);
    std::printf("after=%d\n", scene.ParticleQuads(3).size() <= 200 ? 1 : 0);)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        const auto lines = result.lines.mid(1); // the first line is the state before any frame
        QCOMPARE(lines.size(), qsizetype(3));
        QCOMPARE(lines[0], QString("waiting=0"));
        QVERIFY2(lines[1] != "burst=0", qPrintable(lines[1]));
        QCOMPARE(lines[2], QString("after=1"));
    }

    void runtimeRunsLoopingParticlesWithoutBlocks() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        const auto result = runHarness(burstScene(true), LogicProgram{}, R"cpp(
    for (int index = 0; index < 120; ++index) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);
    const auto quads = scene.ParticleQuads(3);
    std::printf("quads=%d\n", quads.size() > 0 && quads.size() <= 200 ? 1 : 0);
    std::printf("capacity=%d\n", static_cast<int>(GeneratedScene::ParticleCapacity));
    scene.SetVisible(3, false);
    scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);
    std::printf("hidden=%zu\n", scene.ParticleQuads(3).size());)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.mid(1), QStringList({"quads=1", "capacity=200", "hidden=0"}));
    }

    void runtimeWithEmptyLogicBehavesAsBefore() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        const auto result = runHarness(gameScene(), LogicProgram{}, R"cpp(
    frame(scene, Buttons::Cross, 1.0f, 0.5);
    frame(scene, 0, 1.0f, 100.0);)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        const QStringList expected{
            "score=0 [100.0,100.0 1 1.00,1.00,1.00] [400.0,100.0 1 0.00,1.00,0.00] [0.0,0.0 1 0.00,0.00,1.00]",
            "score=0 [400.0,100.0 1 1.00,1.00,1.00] [400.0,100.0 1 0.00,1.00,0.00] [0.0,0.0 1 0.00,0.00,1.00]",
            "score=0 [1870.0,100.0 1 1.00,1.00,1.00] [400.0,100.0 1 0.00,1.00,0.00] [0.0,0.0 1 0.00,0.00,1.00]",
        };
        QCOMPARE(result.lines, expected);
    }

    void runtimeCompilesWithAnEmptyScene() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        const auto result = runHarness(Scene{}, LogicProgram{}, "    frame(scene);");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines, (QStringList{"score=0", "score=0"}));
    }

    void touchFiresOnceUntilTheItemsSeparate() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        Scene scene;
        scene.items = {rectangle("a", 0, 0, 10, 10), rectangle("b", 20, 0, 10, 10)};
        auto program = scoreProgram();
        program.events = {
            held("edge", ControllerButton::Cross, {setPosition("p1", "b", 10, 0)}),
            held("over", ControllerButton::Square, {setPosition("p2", "b", 5, 0)}),
            held("away", ControllerButton::Triangle, {setPosition("p3", "b", 20, 0)}),
            touch("touch", "a", "b", {scoreBlock("s", BlockType::ChangeVariable, 1)}),
        };
        const auto header = generateLogicHeader(program, scene);
        QVERIFY(!header.isEmpty());
        const auto result = runHarness(scene, program, R"cpp(
    frame(scene);
    frame(scene, Buttons::Cross);
    frame(scene, Buttons::Square);
    frame(scene);
    frame(scene, Buttons::Triangle);
    frame(scene, Buttons::Square);)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QStringList scores;
        for (const auto& line : result.lines) scores << line.section(' ', 0, 0);
        // The edges meeting at b.x == 10 is not a touch.
        QCOMPARE(scores, (QStringList{"score=0", "score=0", "score=0", "score=1", "score=1", "score=1", "score=2"}));
    }

    void hiddenItemsDoNotTouchAndShowingAgainFiresAgain() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        Scene scene;
        scene.items = {rectangle("a", 0, 0, 10, 10), rectangle("b", 5, 0, 10, 10)};
        auto program = scoreProgram();
        program.events = {
            held("hide", ControllerButton::Cross, {itemBlock("h", BlockType::Hide, "b")}),
            held("show", ControllerButton::Circle, {itemBlock("v", BlockType::Show, "b")}),
            touch("touch", "a", "b", {scoreBlock("s", BlockType::ChangeVariable, 1)}),
        };
        const auto header = generateLogicHeader(program, scene);
        QVERIFY(!header.isEmpty());
        const auto result = runHarness(scene, program, R"cpp(
    frame(scene);
    frame(scene, Buttons::Cross);
    frame(scene);
    frame(scene, Buttons::Circle);)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QStringList scores;
        for (const auto& line : result.lines) scores << line.section(' ', 0, 0);
        QCOMPARE(scores, (QStringList{"score=0", "score=1", "score=1", "score=1", "score=2"}));
    }

    void touchUsesTheDrawnSizeOfText() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        // "Hi" at scale 2 is drawn 22 wide and 14 tall.
        Scene scene;
        scene.items = {text("label", 0, 0, "Hi", 2), rectangle("inside", 21, 13, 5, 5), rectangle("right", 22, 13, 5, 5), rectangle("below", 21, 14, 5, 5)};
        auto program = scoreProgram();
        program.events = {
            touch("t1", "label", "inside", {scoreBlock("s1", BlockType::ChangeVariable, 1)}),
            touch("t2", "label", "right", {scoreBlock("s2", BlockType::ChangeVariable, 10)}),
            touch("t3", "label", "below", {scoreBlock("s3", BlockType::ChangeVariable, 100)}),
        };
        const auto header = generateLogicHeader(program, scene);
        QVERIFY(!header.isEmpty());
        const auto result = runHarness(scene, program, "    frame(scene);");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.last().section(' ', 0, 0), QString("score=1"));
    }

    void everyComparisonAndButtonWorks() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        Scene scene;
        scene.items = {rectangle("five", 0, 0, 1, 1), rectangle("six", 0, 10, 1, 1), rectangle("four", 0, 20, 1, 1), rectangle("pad", 0, 30, 1, 1)};
        const QList<std::pair<QString, int>> targets{{"five", 5}, {"six", 6}, {"four", 4}};
        const QList<std::pair<VariableComparison, double>> comparisons{
            {VariableComparison::Equal, 1}, {VariableComparison::NotEqual, 2}, {VariableComparison::Less, 4},
            {VariableComparison::LessOrEqual, 8}, {VariableComparison::Greater, 16}, {VariableComparison::GreaterOrEqual, 32},
        };
        QList<Block> checks;
        int nextId = 0;
        for (const auto& [item, number] : targets) {
            for (const auto& [comparison, weight] : comparisons) {
                checks << ifScore(QString("if%1").arg(nextId), comparison, number, {moveBy(QString("mv%1").arg(nextId), item, weight, 0)});
                ++nextId;
            }
        }
        auto program = scoreProgram();
        program.events = {eventBlock("start", BlockType::WhenGameStarts, {scoreBlock("s", BlockType::SetVariable, 5)}), eventBlock("frame", BlockType::EveryFrame, checks)};
        double weight = 1;
        for (const auto button : {ControllerButton::Cross, ControllerButton::Circle, ControllerButton::Square, ControllerButton::Triangle, ControllerButton::DpadUp, ControllerButton::DpadDown, ControllerButton::DpadLeft, ControllerButton::DpadRight}) {
            program.events << held(QString("b%1").arg(int(button)), button, {moveBy(QString("bm%1").arg(int(button)), "pad", weight, 0)});
            weight *= 2;
        }
        const auto header = generateLogicHeader(program, scene);
        QVERIFY(!header.isEmpty());
        const auto result = runHarness(scene, program, R"cpp(
    frame(scene, Buttons::Cross);
    frame(scene, Buttons::Circle | Buttons::Triangle);
    frame(scene, Buttons::Square | Buttons::DpadUp | Buttons::DpadDown | Buttons::DpadLeft | Buttons::DpadRight);)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.size(), 4);
        const QRegularExpression pattern("\\[([0-9.]+),");
        QStringList xs;
        auto matches = pattern.globalMatch(result.lines.at(1));
        while (matches.hasNext()) xs << matches.next().captured(1);
        // five: equal + <= + >= ; six: != + < + <= ; four: != + > + >= ; pad: Cross only.
        QCOMPARE(xs, (QStringList{"41.0", "14.0", "50.0", "1.0"}));
        xs.clear();
        matches = pattern.globalMatch(result.lines.at(2));
        while (matches.hasNext()) xs << matches.next().captured(1);
        QCOMPARE(xs.last(), QString("11.0")); // 1 + Circle (2) + Triangle (8)
        xs.clear();
        matches = pattern.globalMatch(result.lines.at(3));
        while (matches.hasNext()) xs << matches.next().captured(1);
        QCOMPARE(xs.last(), QString("255.0")); // 11 + 4 + 16 + 32 + 64 + 128
    }

    void scoreSaturatesInsteadOfOverflowing() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        Scene scene;
        auto program = scoreProgram();
        program.events = {
            held("up", ControllerButton::Cross, {scoreBlock("a", BlockType::ChangeVariable, 1000000000)}),
            held("down", ControllerButton::Circle, {scoreBlock("b", BlockType::ChangeVariable, -1000000000)}),
        };
        const auto header = generateLogicHeader(program, scene);
        QVERIFY(!header.isEmpty());
        const auto result = runHarness(scene, program, R"cpp(
    frame(scene, Buttons::Cross);
    frame(scene, Buttons::Cross);
    frame(scene, Buttons::Cross);
    frame(scene, Buttons::Circle);
    frame(scene, Buttons::Circle);
    frame(scene, Buttons::Circle);
    frame(scene, Buttons::Circle);
    frame(scene, Buttons::Circle);
    frame(scene, Buttons::Circle);)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.at(3), QString("score=2147483647"));
        QCOMPARE(result.lines.at(4), QString("score=1147483647"));
        QCOMPARE(result.lines.last(), QString("score=-2147483648"));
    }

    void positionsStayWithinTheirLimits() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        Scene scene;
        scene.items = {rectangle("player", 0, 0, 50, 50, "#FFFFFF", true), rectangle("free", 0, 0, 10, 10)};
        auto program = scoreProgram();
        program.events = {
            held("cross", ControllerButton::Cross, {moveBy("m1", "player", -500, 0), moveBy("m2", "free", 1000000, 1000000), moveBy("m3", "free", 1000000, 1000000)}),
            held("circle", ControllerButton::Circle, {setPosition("p1", "player", 5000, 5000)}),
        };
        const auto header = generateLogicHeader(program, scene);
        QVERIFY(!header.isEmpty());
        const auto result = runHarness(scene, program, R"cpp(
    frame(scene, Buttons::Cross);
    frame(scene, Buttons::Circle);)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.at(1), QString("score=0 [0.0,0.0 1 1.00,1.00,1.00] [1000000.0,1000000.0 1 0.00,1.00,0.00]"));
        QCOMPARE(result.lines.at(2), QString("score=0 [1870.0,1030.0 1 1.00,1.00,1.00] [1000000.0,1000000.0 1 0.00,1.00,0.00]"));
    }
    // Variables in logic.json.

    void aSchemaOneFileMovesItsScoreBlocksToAVariableNamedScore() {
        const auto action = QJsonObject{{"id", "b"}, {"type", "addScore"}, {"value", 2}};
        const auto nested = QJsonObject{{"id", "c"}, {"type", "ifScore"}, {"comparison", "greater"}, {"value", 9}, {"actions", QJsonArray{QJsonObject{{"id", "d"}, {"type", "setScore"}, {"value", -4}}}}};
        const auto result = loadJson(minimalLogic({frameEvent({action, nested})}));
        QVERIFY2(result.program.has_value(), qPrintable(result.error));
        QCOMPARE(result.program->variables.size(), qsizetype(1));
        QCOMPARE(result.program->variables.first().name, QString("score"));
        QCOMPARE(result.program->variables.first().initial, 0);
        const auto& first = result.program->events.first().children[0];
        QCOMPARE(first.type, BlockType::ChangeVariable);
        QCOMPARE(first.variable, QString("score"));
        QCOMPARE(first.value, 2);
        const auto& wrapper = result.program->events.first().children[1];
        QCOMPARE(wrapper.type, BlockType::IfVariable);
        QCOMPARE(wrapper.variable, QString("score"));
        QCOMPARE(wrapper.comparison, VariableComparison::Greater);
        QCOMPARE(wrapper.children.first().type, BlockType::SetVariable);
        QCOMPARE(wrapper.children.first().variable, QString("score"));
        QCOMPARE(messages(validateLogic(*result.program, gameScene())), QStringList());
    }

    void aSchemaOneFileWithoutScoreBlocksHasNoVariables() {
        const auto result = loadJson(minimalLogic({frameEvent({QJsonObject{{"id", "b"}, {"type", "hide"}, {"item", "coin"}}})}));
        QVERIFY2(result.program.has_value(), qPrintable(result.error));
        QVERIFY(result.program->variables.isEmpty());
    }

    void oldBlockNamesAreNotAcceptedInNewFiles() {
        const auto result = loadJson({{"schemaVersion", 2}, {"variables", QJsonArray{}}, {"events", QJsonArray{frameEvent({QJsonObject{{"id", "b"}, {"type", "addScore"}, {"value", 1}}})}}});
        QVERIFY(!result.program.has_value());
        QVERIFY2(result.error.contains("addScore"), qPrintable(result.error));
    }

    void variableBlocksNeedANewFile() {
        const auto result = loadAction(QJsonObject{{"id", "b"}, {"type", "setVariable"}, {"variable", "score"}, {"value", 1}});
        QVERIFY(!result.program.has_value());
        QVERIFY2(result.error.contains("schemaVersion 2"), qPrintable(result.error));
    }

    void loadReadsVariablesAndTheirBlocks() {
        const QJsonArray variables{QJsonObject{{"name", "lives"}, {"initial", 3}}, QJsonObject{{"name", "score"}, {"initial", -7}}};
        const QJsonArray actions{QJsonObject{{"id", "b"}, {"type", "changeVariable"}, {"variable", "lives"}, {"value", -1}}};
        const auto result = loadJson({{"schemaVersion", 2}, {"variables", variables}, {"events", QJsonArray{frameEvent(actions)}}});
        QVERIFY2(result.program.has_value(), qPrintable(result.error));
        QCOMPARE(result.program->variables.size(), qsizetype(2));
        QCOMPARE(result.program->variables[0].name, QString("lives"));
        QCOMPARE(result.program->variables[0].initial, 3);
        QCOMPARE(result.program->variables[1].initial, -7);
        QCOMPARE(result.program->events.first().children.first().variable, QString("lives"));
    }

    void savingAndLoadingKeepsVariables() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        auto program = everyBlockProgram();
        program.variables = {{"score", 5}, {"lives", -3}};
        QString error;
        QVERIFY2(saveLogic(path, program, error), qPrintable(error));
        const auto loaded = loadLogic(path);
        QVERIFY2(loaded.program.has_value(), qPrintable(loaded.error));
        QCOMPARE(loaded.program->variables.size(), qsizetype(2));
        QCOMPARE(loaded.program->variables[0].name, QString("score"));
        QCOMPARE(loaded.program->variables[0].initial, 5);
        QCOMPARE(loaded.program->variables[1].name, QString("lives"));
        QCOMPARE(loaded.program->variables[1].initial, -3);
    }

    void badVariablesAreRefusedWithAReason_data() {
        QTest::addColumn<QJsonArray>("variables");
        QTest::addColumn<QString>("expected");
        const auto one = [](const QJsonValue& name, const QJsonValue& initial) { return QJsonArray{QJsonObject{{"name", name}, {"initial", initial}}}; };
        QTest::newRow("not an object") << QJsonArray{1} << "number 1";
        QTest::newRow("empty name") << one("", 0) << "name";
        QTest::newRow("starts with a digit") << one("2x", 0) << "\"2x\"";
        QTest::newRow("space") << one("my score", 0) << "\"my score\"";
        QTest::newRow("brace") << one("a}b", 0) << "\"a}b\"";
        QTest::newRow("too long") << one(QString(MaximumVariableNameLength + 1, 'a'), 0) << "letters";
        QTest::newRow("fraction") << one("a", 1.5) << "whole number";
        QTest::newRow("too large") << one("a", 2000000000) << "whole number";
        QTest::newRow("not a number") << one("a", "1") << "whole number";
        QTest::newRow("duplicate") << QJsonArray{QJsonObject{{"name", "score"}, {"initial", 0}}, QJsonObject{{"name", "score"}, {"initial", 1}}} << "more than one";
        QTest::newRow("duplicate in another case") << QJsonArray{QJsonObject{{"name", "score"}, {"initial", 0}}, QJsonObject{{"name", "Score"}, {"initial", 1}}} << "more than one";
    }

    void badVariablesAreRefusedWithAReason() {
        QFETCH(QJsonArray, variables);
        QFETCH(QString, expected);
        const auto result = loadJson({{"schemaVersion", 2}, {"variables", variables}, {"events", QJsonArray{}}});
        QVERIFY(!result.program.has_value());
        QVERIFY2(result.error.contains(expected), qPrintable(result.error));
    }

    void tooManyVariablesAreRefused() {
        LogicProgram program;
        for (int index = 0; index < MaximumLogicVariables + 1; ++index) program.variables << LogicVariable{QString("v%1").arg(index), 0};
        const auto text = messages(validateLogicStructure(program)).join(' ');
        QVERIFY2(text.contains(QString::number(MaximumLogicVariables)), qPrintable(text));
    }

    void isVariableNameFollowsTheRules_data() {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("valid");
        QTest::newRow("simple") << "score" << true;
        QTest::newRow("capitals") << "Score" << true;
        QTest::newRow("underscore") << "player_2" << true;
        QTest::newRow("leading underscore") << "_x" << true;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("digit first") << "1st" << false;
        QTest::newRow("space") << "a b" << false;
        QTest::newRow("dash") << "a-b" << false;
        QTest::newRow("accent") << QString("caf") + QChar(0xE9) << false;
        QTest::newRow("longest") << QString(MaximumVariableNameLength, 'a') << true;
        QTest::newRow("too long") << QString(MaximumVariableNameLength + 1, 'a') << false;
    }

    void isVariableNameFollowsTheRules() {
        QFETCH(QString, name);
        QFETCH(bool, valid);
        QCOMPARE(isVariableName(name), valid);
    }

    void blocksWithAMissingVariableAreReportedButCanBeSaved() {
        LogicProgram program;
        program.events = {eventBlock("e", BlockType::EveryFrame, {variableBlock("b", BlockType::SetVariable, "lives", 1), variableBlock("c", BlockType::ChangeVariable, "", 1)})};
        QCOMPARE(messages(validateLogicStructure(program)), QStringList());
        QTemporaryDir directory;
        QString error;
        QVERIFY2(saveLogic(directory.path() + "/logic.json", program, error), qPrintable(error));

        const auto problems = validateLogic(program, gameScene());
        QCOMPARE(problems.size(), qsizetype(2));
        QCOMPARE(problems[0].blockId, QString("b"));
        QVERIFY2(problems[0].message.contains("\"lives\""), qPrintable(problems[0].message));
        QVERIFY2(problems[0].message.contains("Set variable"), qPrintable(problems[0].message));
        QCOMPARE(problems[1].blockId, QString("c"));
        QVERIFY2(problems[1].message.contains("no variable chosen"), qPrintable(problems[1].message));
    }

    void variableBlocksGenerateIndexesNotNames() {
        auto program = scoreProgram();
        program.variables << LogicVariable{"lives", 3};
        program.events = {
            eventBlock("start", BlockType::WhenGameStarts, {variableBlock("a", BlockType::SetVariable, "lives", 9)}),
            eventBlock("frame", BlockType::EveryFrame, {
                variableBlock("b", BlockType::ChangeVariable, "lives", -2),
                ifScore("c", VariableComparison::LessOrEqual, 4, {}),
            }),
        };
        const auto header = generateLogicHeader(program, gameScene());
        QVERIFY2(header.contains("world.SetVariable(1, 9); // a"), header.constData());
        QVERIFY2(header.contains("world.ChangeVariable(1, -2); // b"), header.constData());
        QVERIFY2(header.contains("if (world.Variable(0) <= 4) { // c"), header.constData());
        QVERIFY(!header.contains("lives"));
    }

    void aSchemaOneFileGeneratesWhatTheNewBlocksGenerate() {
        const auto oldFile = loadBytes(R"json({"schemaVersion": 1, "events": [{"id": "e", "type": "everyFrame", "actions": [{"id": "b", "type": "addScore", "value": 1}]}]})json");
        QVERIFY2(oldFile.program.has_value(), qPrintable(oldFile.error));
        auto program = scoreProgram();
        program.events = {eventBlock("e", BlockType::EveryFrame, {scoreBlock("b", BlockType::ChangeVariable, 1)})};
        QCOMPARE(generateLogicHeader(*oldFile.program, gameScene()), generateLogicHeader(program, gameScene()));
        QCOMPARE(generateVariablesHeader(*oldFile.program, gameScene()), generateVariablesHeader(program, gameScene()));
    }

    // Text that shows variables.

    void aSceneCanOnlyUseVariablesThatExist() {
        auto scene = gameScene();
        scene.items[2].text = "Lives {lives}";
        const auto problems = validateTextBindings(scene, scoreProgram());
        QCOMPARE(problems.size(), qsizetype(1));
        QVERIFY2(problems[0].message.contains("label"), qPrintable(problems[0].message));
        QVERIFY2(problems[0].message.contains("\"lives\""), qPrintable(problems[0].message));
        QVERIFY2(problems[0].message.contains("Logic tab"), qPrintable(problems[0].message));
        QCOMPARE(generateVariablesHeader(scoreProgram(), scene), QByteArray());
    }

    void textWithKnownVariablesPassesValidation() {
        auto scene = gameScene();
        scene.items[2].text = "{score} and {{braces}}";
        QCOMPARE(messages(validateTextBindings(scene, scoreProgram())), QStringList());
        scene.items[2].text = "no variables here";
        QCOMPARE(messages(validateTextBindings(scene, LogicProgram{})), QStringList());
    }

    void aProjectWithoutVariablesGetsAnEmptyTable() {
        const auto header = generateVariablesHeader(LogicProgram{}, gameScene());
        QCOMPARE(header, EmptyVariablesHeader);
        QVERIFY2(!header.contains("GeneratedScene"), header.constData());
    }

    void boundTextIsDescribedInTheVariablesHeader() {
        auto scene = gameScene();
        scene.items[2].text = "Score: {score}!";
        auto program = scoreProgram();
        program.variables[0].initial = 7;
        const auto header = generateVariablesHeader(program, scene);
        QVERIFY2(header.contains("VariableCount = 1;"), header.constData());
        QVERIFY2(header.contains("InitialValues{{7}}"), header.constData());
        QVERIFY2(header.contains("{\"Score: \", -1}"), header.constData());
        QVERIFY2(header.contains("{\"\", 0}"), header.constData());
        QVERIFY2(header.contains("{\"!\", -1}"), header.constData());
        QVERIFY2(header.contains("{2, 0, 3, 19}"), header.constData());
        QVERIFY2(header.contains("static_assert(GeneratedScene::Items.size() == 3"), header.constData());
        QVERIFY2(header.contains("GeneratedScene::Items[2].text.size() == 19"), header.constData());
    }

    void exportWritesTheVariablesHeader() {
        QTemporaryDir project;
        auto scene = gameScene();
        scene.items[2].text = "{score}";
        auto program = scoreProgram();
        QString error;
        QVERIFY2(saveLogic(project.path() + "/logic.json", program, error), qPrintable(error));
        QVERIFY2(exportLogicHeader(project.path(), scene, error), qPrintable(error));
        QCOMPARE(readBytes(project.path() + "/src/GeneratedVariables.hpp"), generateVariablesHeader(program, scene));
    }

    void exportWritesAnEmptyTableWithoutALogicFile() {
        QTemporaryDir project;
        QString error;
        QVERIFY2(exportLogicHeader(project.path(), gameScene(), error), qPrintable(error));
        QCOMPARE(readBytes(project.path() + "/src/GeneratedVariables.hpp"), EmptyVariablesHeader);
        QCOMPARE(readBytes(project.path() + "/src/GeneratedLogic.hpp"), EmptyHeader);
    }

    void exportRefusesAnUnknownVariableInText() {
        QTemporaryDir project;
        writeBytes(project.path() + "/src/GeneratedVariables.hpp", "previous");
        auto scene = gameScene();
        scene.items[2].text = "{score}";
        QString error;
        QVERIFY(!exportLogicHeader(project.path(), scene, error)); // no logic.json, so no variables at all
        QVERIFY2(error.contains("\"score\""), qPrintable(error));
        QVERIFY2(error.contains("label"), qPrintable(error));
        QCOMPARE(readBytes(project.path() + "/src/GeneratedVariables.hpp"), QByteArray("previous"));
    }

    void anUnknownVariableInABlockStillNamesTheBlock() {
        QTemporaryDir project;
        LogicProgram program;
        program.events = {eventBlock("e", BlockType::EveryFrame, {variableBlock("b", BlockType::SetVariable, "gone", 1)})};
        QString error;
        QVERIFY2(saveLogic(project.path() + "/logic.json", program, error), qPrintable(error));
        QVERIFY(!exportLogicHeader(project.path(), gameScene(), error));
        QVERIFY2(error.contains("Set variable"), qPrintable(error));
        QVERIFY2(error.contains("\"gone\""), qPrintable(error));
    }

    // The runtime. Each of these compiles the real SceneState with the generated headers.

    void runtimeRunsVariableBlocks() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        auto program = scoreProgram();
        program.variables[0].initial = 5;
        program.variables << LogicVariable{"lives", 3};
        program.events = {
            held("up", ControllerButton::Cross, {variableBlock("a", BlockType::ChangeVariable, "score", 10)}),
            held("down", ControllerButton::Circle, {variableBlock("b", BlockType::ChangeVariable, "score", -1)}),
            held("reset", ControllerButton::Square, {variableBlock("c", BlockType::SetVariable, "score", 100)}),
            eventBlock("frame", BlockType::EveryFrame, {
                [] {
                    auto check = ifScore("d", VariableComparison::GreaterOrEqual, 100, {variableBlock("e", BlockType::SetVariable, "lives", 9)});
                    return check;
                }(),
            }),
        };
        const auto result = runHarness(gameScene(), program, R"cpp(
    std::printf("%d %d\n", static_cast<int>(scene.Variable(0)), static_cast<int>(scene.Variable(1)));
    scene.Update(0, {}, Buttons::Cross);
    std::printf("%d %d\n", static_cast<int>(scene.Variable(0)), static_cast<int>(scene.Variable(1)));
    scene.Update(0, {}, Buttons::Circle);
    std::printf("%d %d\n", static_cast<int>(scene.Variable(0)), static_cast<int>(scene.Variable(1)));
    scene.Update(0, {}, Buttons::Square);
    std::printf("%d %d\n", static_cast<int>(scene.Variable(0)), static_cast<int>(scene.Variable(1)));)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.mid(1), QStringList({"5 3", "15 3", "14 3", "100 9"}));
    }

    void displayedTextChangesWithTheVariable() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        auto scene = gameScene();
        scene.items.append(text("hud", 10, 10, "Score: {score}", 2));
        auto program = scoreProgram();
        program.variables[0].initial = 5;
        program.events = {held("up", ControllerButton::Cross, {scoreBlock("a", BlockType::ChangeVariable, 1)})};
        const auto result = runHarness(scene, program, R"cpp(
    showText(scene, 3);
    scene.Update(0, {}, Buttons::Cross);
    showText(scene, 3);
    scene.Update(0, {}, Buttons::Cross);
    showText(scene, 3);
    scene.Update(0, {}, 0);
    showText(scene, 3);)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.mid(1), QStringList({"[Score: 5] 8/18", "[Score: 6] 8/18", "[Score: 7] 8/18", "[Score: 7] 8/18"}));
    }

    void reservedQuadsHoldTheSmallestAndLargestValues() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        auto scene = gameScene();
        scene.items.append(text("hud", 10, 10, "A{score}B{score}", 2));
        auto program = scoreProgram();
        const auto result = runHarness(scene, program, R"cpp(
    scene.SetVariable(0, 2147483647);
    scene.Update(0, {}, 0);
    showText(scene, 3);
    scene.SetVariable(0, -2147483647 - 1);
    scene.Update(0, {}, 0);
    showText(scene, 3);
    scene.ChangeVariable(0, -1);
    scene.Update(0, {}, 0);
    showText(scene, 3);)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        const QStringList expected{"[A2147483647B2147483647] 22/24", "[A-2147483648B-2147483648] 24/24", "[A-2147483648B-2147483648] 24/24"};
        QCOMPARE(result.lines.mid(1), expected);
    }

    void aFrameMakesNoAllocationsWhenTextChanges() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        auto scene = gameScene();
        scene.items.append(text("hud", 10, 10, "{score}/{score}", 2));
        auto program = scoreProgram();
        program.events = {eventBlock("frame", BlockType::EveryFrame, {scoreBlock("a", BlockType::ChangeVariable, 1234567)})};
        const auto result = runHarness(scene, program, R"cpp(
    const auto before = allocations;
    for (int index = 0; index < 500; ++index) scene.Update(1.0 / 60.0, {}, 0);
    std::printf("allocations=%zu\n", allocations - before);)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.last(), QString("allocations=0"));
    }

    void textWithNoVariablesUsesTheSceneTextDirectly() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        auto scene = gameScene();
        scene.items.append(text("hud", 10, 10, "{score}", 2));
        auto program = scoreProgram();
        const auto result = runHarness(scene, program, R"cpp(
    std::printf("same=%d\n", scene.CurrentText(2).data() == GeneratedScene::Items[2].text.data() ? 1 : 0);
    showText(scene, 2);)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.mid(1), QStringList({"same=1", "[Hi] 2/2"}));
    }

    void literalBracesShowAsOneBraceAtRuntime() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        auto scene = gameScene();
        scene.items.append(text("hud", 10, 10, "{{{score}}}", 2));
        auto program = scoreProgram();
        const auto result = runHarness(scene, program, "    showText(scene, 3);");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.last(), QString("[{0}] 3/13"));
    }

    void touchUsesTheWidthTheTextHasNow() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        Scene scene;
        scene.items = {text("hud", 0, 0, "{score}", 2), rectangle("right", 30, 0, 5, 5)};
        auto program = scoreProgram();
        program.variables << LogicVariable{"hits", 0};
        program.events = {
            held("big", ControllerButton::Cross, {scoreBlock("a", BlockType::SetVariable, 1000)}),
            touch("t", "hud", "right", {variableBlock("b", BlockType::ChangeVariable, "hits", 1)}),
        };
        const auto result = runHarness(scene, program, R"cpp(
    std::printf("%d\n", static_cast<int>(scene.Variable(1)));
    scene.Update(0, {}, Buttons::Cross);
    scene.Update(0, {}, Buttons::Cross);
    std::printf("%d\n", static_cast<int>(scene.Variable(1)));)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        // "0" at scale 2 is 10 wide. "1000" is (4 * 6 - 1) * 2 = 46 wide, which reaches x = 30.
        QCOMPARE(result.lines.mid(1), QStringList({"0", "1"}));
    }

    void aControllableTextStaysOnStageAsItsValueGrows() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        Scene scene;
        auto hud = text("hud", 1900, 0, "{score}", 4);
        hud.controllable = true;
        scene.items = {hud};
        auto program = scoreProgram();
        program.events = {held("big", ControllerButton::Cross, {scoreBlock("a", BlockType::SetVariable, 123456)})};
        const auto result = runHarness(scene, program, R"cpp(
    frame(scene);
    frame(scene, Buttons::Cross);
    frame(scene, Buttons::Cross);)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        // At first "0" is 20 wide, so x can be 1900. Six digits are (36 - 1) * 4 = 140 wide.
        QCOMPARE(result.lines.at(1).section(' ', 1, 1).section(',', 0, 0), QString("[1900.0"));
        QCOMPARE(result.lines.at(3).section(' ', 1, 1).section(',', 0, 0), QString("[1780.0"));
    }

    void variablesSaturateInsteadOfOverflowing() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        auto program = scoreProgram();
        program.variables << LogicVariable{"other", -1};
        program.events = {held("up", ControllerButton::Cross, {variableBlock("a", BlockType::ChangeVariable, "other", -1000000000)})};
        const auto result = runHarness(Scene{}, program, R"cpp(
    for (int index = 0; index < 5; ++index) scene.Update(0, {}, Buttons::Cross);
    std::printf("%d %d\n", static_cast<int>(scene.Variable(0)), static_cast<int>(scene.Variable(1)));)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.last(), QString("0 -2147483648"));
    }

    // The world, the camera and sprite animation, through the real SceneState.
    void theCameraFollowsAndStaysInsideTheWorld() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        Scene scene;
        scene.worldWidth = 3840;
        scene.worldHeight = 2160;
        auto player = rectangle("player", 120, 300, 160, 160, "#00FF00", true);
        scene.items = {player};
        LogicProgram program;
        auto follow = itemBlock("f", BlockType::CameraFollow, "player");
        program.events = {eventBlock("start", BlockType::WhenGameStarts, {follow})};
        const auto result = runHarness(scene, program, R"cpp(
    scene.Update(0.0, {0.0f, 0.0f}, 0);
    std::printf("%.1f,%.1f\n", static_cast<double>(scene.Camera().x), static_cast<double>(scene.Camera().y));
    for (int step = 0; step < 2000; ++step) scene.Update(0.016, {1.0f, 0.0f}, 0);
    std::printf("%.1f,%.1f %.1f\n", static_cast<double>(scene.Camera().x), static_cast<double>(scene.Camera().y), static_cast<double>(scene.Current()[0][0]));)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.at(1), QString("0.0,0.0")); // near the corner the camera cannot go further
        QCOMPARE(result.lines.at(2), QString("1920.0,0.0 3680.0")); // stops at the world's edge, and the player inside it
    }

    void setCameraPositionClampsAndStopsFollowing() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        Scene scene;
        scene.worldWidth = 3840;
        scene.worldHeight = 2160;
        scene.items = {rectangle("item", 200, 200, 64, 64)};
        LogicProgram program;
        auto follow = itemBlock("f", BlockType::CameraFollow, "item");
        Block move;
        move.id = "m";
        move.type = BlockType::SetCameraPosition;
        move.x = 99999;
        move.y = -5;
        program.events = {eventBlock("start", BlockType::WhenGameStarts, {follow}), held("press", ControllerButton::Cross, {move})};
        const auto result = runHarness(scene, program, R"cpp(
    scene.Update(0.0, {0.0f, 0.0f}, Buttons::Cross);
    std::printf("%.1f,%.1f\n", static_cast<double>(scene.Camera().x), static_cast<double>(scene.Camera().y));
    scene.Update(0.0, {0.0f, 0.0f}, 0);
    std::printf("%.1f\n", static_cast<double>(scene.Camera().x));)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.at(1), QString("1920.0,0.0"));
        QCOMPARE(result.lines.at(2), QString("1920.0")); // no longer following
    }

    void aSpritePlaysAndFlips() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        Scene scene;
        SceneItem hero;
        hero.id = "hero";
        hero.type = "image";
        hero.source = "assets/hero.png";
        hero.width = 64;
        hero.height = 64;
        hero.frameWidth = 16;
        hero.frameHeight = 16;
        hero.animations = {{"walk", {3, 2, 1, 0}, 10}, {"jump", {2}, 4}};
        hero.animation = "walk";
        scene.items = {hero};
        LogicProgram program;
        auto play = itemBlock("p", BlockType::PlayAnimation, "hero");
        play.animation = "jump";
        auto flip = itemBlock("f", BlockType::FlipItem, "hero");
        flip.flipped = true;
        program.events = {held("press", ControllerButton::Cross, {play, flip})};
        const QList<SceneImageData> images{{"assets/hero.png", 64, 32, QByteArray(64 * 32 * 4, '\0')}};
        const auto result = runHarness(scene, program, R"cpp(
    std::printf("%u\n", scene.SpriteFrame(0));
    scene.Update(0.1, {0.0f, 0.0f}, 0);
    std::printf("%u\n", scene.SpriteFrame(0));
    scene.Update(0.1, {0.0f, 0.0f}, Buttons::Cross);
    std::printf("%u %d\n", scene.SpriteFrame(0), scene.Flipped(0) ? 1 : 0);)cpp", images);
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.at(1), QString("3")); // walk starts at its first frame, the number 3 on the sheet
        QCOMPARE(result.lines.at(2), QString("2")); // a tenth of a second at 10 per second is one frame further
        QCOMPARE(result.lines.at(3), QString("2 1")); // jump is one frame, 2, and the block flipped it
    }

    void aScreenFixedControllableItemStaysOnTheScreen() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        Scene scene;
        scene.worldWidth = 3840;
        auto pinned = rectangle("pinned", 100, 300, 160, 160, "#00FF00", true);
        pinned.screenFixed = true;
        scene.items = {pinned};
        const auto result = runHarness(scene, LogicProgram{}, R"cpp(
    for (int step = 0; step < 2000; ++step) scene.Update(0.016, {1.0f, 0.0f}, 0);
    show(scene);)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        // It stops at the screen's edge, 1920 - 160, even though the world is wider.
        QCOMPARE(result.lines.at(1).section(' ', 1, 1).section(',', 0, 0), QString("[1760.0"));
    }

    void variablesHeaderDocMatchesTheGenerator() {
        const auto doc = readBytes(QString(STUDIO_TEST_SOURCE_DIR) + "/docs/blocks-format.md").replace("\r\n", "\n");
        QVERIFY2(doc.contains(DocVariablesHeader), "the variables header in docs/blocks-format.md differs from the generator");
        auto scene = docScene();
        scene.items[1].text = "Score: {score}";
        auto program = scoreProgram();
        program.variables << LogicVariable{"lives", 3};
        QCOMPARE(generateVariablesHeader(program, scene), DocVariablesHeader);
    }
};

QTEST_GUILESS_MAIN(TstBlocks)
#include "tst_blocks.moc"
