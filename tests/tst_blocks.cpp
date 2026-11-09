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

Block scoreBlock(const QString& id, BlockType type, int value) {
    Block block;
    block.id = id;
    block.type = type;
    block.value = value;
    return block;
}

Block ifScore(const QString& id, ScoreComparison comparison, int value, const QList<Block>& actions = {}) {
    auto block = scoreBlock(id, BlockType::IfScore, value);
    block.comparison = comparison;
    block.children = actions;
    return block;
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
    LogicProgram program;
    program.events = {
        eventBlock("start", BlockType::WhenGameStarts, {
            setPosition("s1", "coin", 200, 100),
            setColor("s2", "coin", "#FF0000"),
            scoreBlock("s3", BlockType::SetScore, 10),
            itemBlock("s4", BlockType::Hide, "label"),
        }),
        held("held-cross", ControllerButton::Cross, {moveBy("c1", "coin", -60, 0)}),
        eventBlock("frame", BlockType::EveryFrame, {
            scoreBlock("f1", BlockType::AddScore, 1),
            ifScore("f2", ScoreComparison::GreaterOrEqual, 12, {
                setColor("f3", "player", "#000000"),
                ifScore("f4", ScoreComparison::Less, 100, {itemBlock("f5", BlockType::Show, "label")}),
            }),
        }),
        touch("touch", "player", "coin", {scoreBlock("t1", BlockType::AddScore, 100)}),
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
    if (left.id != right.id || left.type != right.type || left.children.size() != right.children.size()) return false;
    const auto& info = blockInfo(left.type);
    if (info.usesButton && left.button != right.button) return false;
    if (info.usesItem && left.item != right.item) return false;
    if (info.usesOtherItem && left.otherItem != right.otherItem) return false;
    if (info.usesPosition && (left.x != right.x || left.y != right.y)) return false;
    if (info.usesColor && left.color != right.color) return false;
    if (info.usesValue && left.value != right.value) return false;
    if (info.usesComparison && left.comparison != right.comparison) return false;
    for (qsizetype index = 0; index < left.children.size(); ++index) {
        if (!sameBlock(left.children[index], right.children[index])) return false;
    }
    return true;
}

// The first ```json block of docs/blocks-format.md, so the example there has to stay valid.
QByteArray docExample() {
    const auto doc = readBytes(QString(STUDIO_TEST_SOURCE_DIR) + "/docs/blocks-format.md");
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
    world.SetScore(0); // block-1
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
            world.AddScore(1); // block-3
            world.SetVisible(1, false); // block-4
        }
    }
    // event-4: Every frame
    if (world.Score() >= 10) { // block-5
        world.SetColor(0, 1.0f, 0.301961f, 0.301961f); // block-6
    }
}

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

}
)cpp";

// Prints one line per call: the score, then each item's position, visibility and color.
const QByteArray HarnessPrelude = R"cpp(#include "Scene/SceneState.hpp"
#include "Input/Buttons.hpp"
#include <cstdint>
#include <cstdio>

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
    std::printf("score=%d", static_cast<int>(scene.Score()));
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
HarnessResult runHarness(const Scene& scene, const QByteArray& logicHeader, const QByteArray& body) {
    HarnessResult result;
    QTemporaryDir directory;
    const auto root = directory.path();
    const QString runtime = QString(STUDIO_TEST_SOURCE_DIR) + "/runtime/SceneGame";

    for (const auto& relative : {QString("include/Scene/SceneState.hpp"), QString("include/Scene/Particles.hpp"), QString("src/Scene/SceneState.cpp"), QString("include/Input/Buttons.hpp")}) {
        auto contents = readBytes(runtime + '/' + relative + ".in");
        if (contents.isEmpty()) {
            result.buildOutput = "missing runtime file " + relative;
            return result;
        }
        writeBytes(root + '/' + relative, contents.replace("@NAME@", "Harness"));
    }
    writeBytes(root + "/src/GeneratedScene.hpp", generateSceneHeader(scene));
    writeBytes(root + "/src/GeneratedLogic.hpp", logicHeader);
    writeBytes(root + "/src/main.cpp", HarnessPrelude + body + "\n    return 0;\n}\n");

    QStringList arguments{"-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I" + root + "/include", "-I" + root + "/src", root + "/src/main.cpp", root + "/src/Scene/SceneState.cpp", "-o", root + "/harness"};
    const QString sysroot(STUDIO_TEST_SYSROOT);
    if (!sysroot.isEmpty()) arguments << "-isysroot" << sysroot;

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

class TstBlocks : public QObject {
    Q_OBJECT

private slots:
    void catalogDescribesEveryBlockOnce() {
        QCOMPARE(blockCatalog().size(), 13);
        QSet<QString> names;
        QSet<QString> titles;
        for (const auto& info : blockCatalog()) {
            QVERIFY(!info.jsonName.isEmpty());
            QVERIFY(!info.title.isEmpty());
            names.insert(info.jsonName);
            titles.insert(info.title);
            QCOMPARE(blockInfo(info.type).jsonName, info.jsonName);
        }
        QCOMPARE(names.size(), 13);
        QCOMPARE(titles.size(), 13);
        QCOMPARE(buttonCatalog().size(), 8);
        QCOMPARE(comparisonCatalog().size(), 6);
        QVERIFY(blockInfo(BlockType::EveryFrame).isEvent);
        QVERIFY(blockInfo(BlockType::IfScore).holdsActions);
        QVERIFY(!blockInfo(BlockType::IfScore).isEvent);
        QVERIFY(!blockInfo(BlockType::MoveBy).holdsActions);
        QVERIFY(blockInfo(BlockType::WhenItemsTouch).usesOtherItem);
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
        const auto doc = readBytes(QString(STUDIO_TEST_SOURCE_DIR) + "/docs/blocks-format.md");
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
        QCOMPARE(root.value("schemaVersion").toInt(), 1);
        const auto events = root.value("events").toArray();
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
        LogicProgram program;
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
        LogicProgram program;
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
        QTest::newRow("wrong schema") << QJsonDocument(QJsonObject{{"schemaVersion", 2}, {"events", QJsonArray{}}}).toJson() << "schemaVersion";
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
        QTest::newRow("fractional score") << QJsonObject{{"id", "b"}, {"type", "addScore"}, {"value", 1.5}} << "Add to score";
        QTest::newRow("score too large") << QJsonObject{{"id", "b"}, {"type", "setScore"}, {"value", 2000000000}} << "Set score";
        QTest::newRow("bad comparison") << QJsonObject{{"id", "b"}, {"type", "ifScore"}, {"comparison", "like"}, {"value", 1}, {"actions", QJsonArray{}}} << "If score";
        QTest::newRow("actions not a list") << QJsonObject{{"id", "b"}, {"type", "ifScore"}, {"comparison", "equal"}, {"value", 1}} << "If score";
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
            for (int level = depth - 1; level >= 2; --level) block = ifScore(QString("if%1").arg(level), ScoreComparison::Equal, 0, {block});
            LogicProgram program;
            program.events = {eventBlock("root", BlockType::EveryFrame, {block})};
            return program;
        };
        QCOMPARE(messages(validateLogicStructure(nested(MaximumLogicDepth))), QStringList());
        QVERIFY2(messages(validateLogicStructure(nested(MaximumLogicDepth + 1))).join(' ').contains("inside each other"), "the nesting limit was not enforced");
    }

    void aVeryDeepProgramIsReportedWithoutRecursingForever() {
        Block block = itemBlock("leaf", BlockType::Hide, "coin");
        for (int level = 0; level < 1000; ++level) block = ifScore(QString("i%1").arg(level), ScoreComparison::Equal, 0, {block});
        LogicProgram program;
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
        LogicProgram program;
        program.events = {eventBlock("e", BlockType::EveryFrame, {moveBy("m1", "goblin", 1, 0)})};
        const auto problems = validateLogic(program, gameScene());
        QCOMPARE(problems.size(), 1);
        QCOMPARE(problems[0].blockId, QString("m1"));
        QVERIFY2(problems[0].message.contains("Move item"), qPrintable(problems[0].message));
        QVERIFY2(problems[0].message.contains("goblin"), qPrintable(problems[0].message));
        QVERIFY2(problems[0].message.contains("not in the scene"), qPrintable(problems[0].message));
    }

    void unchosenItemsAndSameItemTouchesAreErrors() {
        LogicProgram program;
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
        LogicProgram program;
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
        LogicProgram program;
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
        LogicProgram program;
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
        LogicProgram program;
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
        LogicProgram program;
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
        const auto result = runHarness(gameScene(), header, R"cpp(
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
        LogicProgram program;
        program.events = {eventBlock("e", BlockType::EveryFrame, {itemBlock("b", BlockType::BurstParticles, "coin")})};
        const auto problems = validateLogic(program, burstScene(true));
        QCOMPARE(problems.size(), qsizetype(1));
        QVERIFY2(problems.first().message.contains("Particles"), qPrintable(problems.first().message));
        QCOMPARE(problems.first().blockId, QString("b"));
        QVERIFY(generateLogicHeader(program, burstScene(true)).isEmpty());
    }

    void burstGeneratesItsCall() {
        LogicProgram program;
        program.events = {touch("t", "player", "coin", {itemBlock("b", BlockType::BurstParticles, "fx")})};
        const auto header = generateLogicHeader(program, burstScene(true));
        QVERIFY2(header.contains("world.BurstParticles(3); // b"), header.constData());
        QVERIFY(!header.contains("AwaitBurst"));
    }

    void aOneShotItemTheBlocksBurstWaitsForTheBlock() {
        LogicProgram program;
        program.events = {touch("t", "player", "coin", {itemBlock("b", BlockType::BurstParticles, "fx")})};
        const auto header = generateLogicHeader(program, burstScene(false));
        QVERIFY2(header.contains("world.AwaitBurst(3);"), header.constData());
        // Waiting has to be set up before the first frame, in OnStart.
        QVERIFY(header.indexOf("world.AwaitBurst(3);") < header.indexOf("OnFrame"));
    }

    void runtimeBurstsWhenTouched() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        LogicProgram program;
        program.events = {touch("t", "player", "coin", {itemBlock("b", BlockType::BurstParticles, "fx")})};
        const auto header = generateLogicHeader(program, burstScene(false));
        QVERIFY(!header.isEmpty());
        const auto result = runHarness(burstScene(false), header, R"cpp(
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
        const auto result = runHarness(burstScene(true), generateEmptyLogicHeader(), R"cpp(
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
        const auto result = runHarness(gameScene(), generateEmptyLogicHeader(), R"cpp(
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
        const auto result = runHarness(Scene{}, generateEmptyLogicHeader(), "    frame(scene);");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines, (QStringList{"score=0", "score=0"}));
    }

    void touchFiresOnceUntilTheItemsSeparate() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        Scene scene;
        scene.items = {rectangle("a", 0, 0, 10, 10), rectangle("b", 20, 0, 10, 10)};
        LogicProgram program;
        program.events = {
            held("edge", ControllerButton::Cross, {setPosition("p1", "b", 10, 0)}),
            held("over", ControllerButton::Square, {setPosition("p2", "b", 5, 0)}),
            held("away", ControllerButton::Triangle, {setPosition("p3", "b", 20, 0)}),
            touch("touch", "a", "b", {scoreBlock("s", BlockType::AddScore, 1)}),
        };
        const auto header = generateLogicHeader(program, scene);
        QVERIFY(!header.isEmpty());
        const auto result = runHarness(scene, header, R"cpp(
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
        LogicProgram program;
        program.events = {
            held("hide", ControllerButton::Cross, {itemBlock("h", BlockType::Hide, "b")}),
            held("show", ControllerButton::Circle, {itemBlock("v", BlockType::Show, "b")}),
            touch("touch", "a", "b", {scoreBlock("s", BlockType::AddScore, 1)}),
        };
        const auto header = generateLogicHeader(program, scene);
        QVERIFY(!header.isEmpty());
        const auto result = runHarness(scene, header, R"cpp(
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
        LogicProgram program;
        program.events = {
            touch("t1", "label", "inside", {scoreBlock("s1", BlockType::AddScore, 1)}),
            touch("t2", "label", "right", {scoreBlock("s2", BlockType::AddScore, 10)}),
            touch("t3", "label", "below", {scoreBlock("s3", BlockType::AddScore, 100)}),
        };
        const auto header = generateLogicHeader(program, scene);
        QVERIFY(!header.isEmpty());
        const auto result = runHarness(scene, header, "    frame(scene);");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.last().section(' ', 0, 0), QString("score=1"));
    }

    void everyComparisonAndButtonWorks() {
        if (!hasPosixCompiler()) QSKIP("The generated logic is only compiled here with a GCC style compiler.");
        Scene scene;
        scene.items = {rectangle("five", 0, 0, 1, 1), rectangle("six", 0, 10, 1, 1), rectangle("four", 0, 20, 1, 1), rectangle("pad", 0, 30, 1, 1)};
        const QList<std::pair<QString, int>> targets{{"five", 5}, {"six", 6}, {"four", 4}};
        const QList<std::pair<ScoreComparison, double>> comparisons{
            {ScoreComparison::Equal, 1}, {ScoreComparison::NotEqual, 2}, {ScoreComparison::Less, 4},
            {ScoreComparison::LessOrEqual, 8}, {ScoreComparison::Greater, 16}, {ScoreComparison::GreaterOrEqual, 32},
        };
        QList<Block> checks;
        int nextId = 0;
        for (const auto& [item, number] : targets) {
            for (const auto& [comparison, weight] : comparisons) {
                checks << ifScore(QString("if%1").arg(nextId), comparison, number, {moveBy(QString("mv%1").arg(nextId), item, weight, 0)});
                ++nextId;
            }
        }
        LogicProgram program;
        program.events = {eventBlock("start", BlockType::WhenGameStarts, {scoreBlock("s", BlockType::SetScore, 5)}), eventBlock("frame", BlockType::EveryFrame, checks)};
        double weight = 1;
        for (const auto button : {ControllerButton::Cross, ControllerButton::Circle, ControllerButton::Square, ControllerButton::Triangle, ControllerButton::DpadUp, ControllerButton::DpadDown, ControllerButton::DpadLeft, ControllerButton::DpadRight}) {
            program.events << held(QString("b%1").arg(int(button)), button, {moveBy(QString("bm%1").arg(int(button)), "pad", weight, 0)});
            weight *= 2;
        }
        const auto header = generateLogicHeader(program, scene);
        QVERIFY(!header.isEmpty());
        const auto result = runHarness(scene, header, R"cpp(
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
        LogicProgram program;
        program.events = {
            held("up", ControllerButton::Cross, {scoreBlock("a", BlockType::AddScore, 1000000000)}),
            held("down", ControllerButton::Circle, {scoreBlock("b", BlockType::AddScore, -1000000000)}),
        };
        const auto header = generateLogicHeader(program, scene);
        QVERIFY(!header.isEmpty());
        const auto result = runHarness(scene, header, R"cpp(
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
        LogicProgram program;
        program.events = {
            held("cross", ControllerButton::Cross, {moveBy("m1", "player", -500, 0), moveBy("m2", "free", 1000000, 1000000), moveBy("m3", "free", 1000000, 1000000)}),
            held("circle", ControllerButton::Circle, {setPosition("p1", "player", 5000, 5000)}),
        };
        const auto header = generateLogicHeader(program, scene);
        QVERIFY(!header.isEmpty());
        const auto result = runHarness(scene, header, R"cpp(
    frame(scene, Buttons::Cross);
    frame(scene, Buttons::Circle);)cpp");
        QVERIFY2(result.built, qPrintable(result.buildOutput));
        QCOMPARE(result.lines.at(1), QString("score=0 [0.0,0.0 1 1.00,1.00,1.00] [1000000.0,1000000.0 1 0.00,1.00,0.00]"));
        QCOMPARE(result.lines.at(2), QString("score=0 [1870.0,1030.0 1 1.00,1.00,1.00] [1000000.0,1000000.0 1 0.00,1.00,0.00]"));
    }
};

QTEST_GUILESS_MAIN(TstBlocks)
#include "tst_blocks.moc"
