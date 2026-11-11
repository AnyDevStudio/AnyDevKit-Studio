// The physics and scene blocks: the catalog, logic.json, validation and generated code, and the real runtime
// SceneState compiled and run on this computer with a platformer scene and a Go to scene block.
#include "Blocks.h"
#include "LogicDocument.h"
#include "Scene.h"
#include "Sounds.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace Studio;

namespace {

const BlockInfo* findBlock(const QString& jsonName) {
    for (const auto& info : blockCatalog()) {
        if (info.jsonName == jsonName) return &info;
    }
    return nullptr;
}

Block eventBlock(const QString& id, BlockType type, const QString& item = {}, const QList<Block>& actions = {}) {
    Block block;
    block.id = id;
    block.type = type;
    block.item = item;
    block.children = actions;
    return block;
}

Block action(const QString& id, BlockType type, const QString& item = {}, double x = 0, double y = 0) {
    Block block;
    block.id = id;
    block.type = type;
    block.item = item;
    block.x = x;
    block.y = y;
    return block;
}

Block setVariable(const QString& id, const QString& name, int value) {
    Block block;
    block.id = id;
    block.type = BlockType::SetVariable;
    block.variable = name;
    block.value = value;
    return block;
}

Block changeVariable(const QString& id, const QString& name, int value) {
    auto block = setVariable(id, name, value);
    block.type = BlockType::ChangeVariable;
    return block;
}

SceneItem rectangle(const QString& id, double x, double y, double width, double height) {
    SceneItem item;
    item.id = id;
    item.type = "rectangle";
    item.name = id;
    item.x = x;
    item.y = y;
    item.width = width;
    item.height = height;
    return item;
}

SceneItem hero(double x = 100, double y = 100) {
    auto item = rectangle("hero", x, y, 64, 64);
    item.physics = "body";
    item.platformer = true;
    return item;
}

SceneItem floorAt(double y) {
    auto item = rectangle("floor", 0, y, 1920, 200);
    item.physics = "solid";
    return item;
}

SceneItem topDownHero() {
    auto item = rectangle("hero", 100, 100, 64, 64);
    item.physics = "body";
    item.topDown = true;
    return item;
}

Block between(const QString& id, BlockType type, const QString& item, const QString& other, int value, const QList<Block>& actions = {}) {
    Block block;
    block.id = id;
    block.type = type;
    block.item = item;
    block.otherItem = other;
    block.value = value;
    block.children = actions;
    return block;
}

Scene sceneNamed(const QString& id, const QString& name, const QList<SceneItem>& items) {
    Scene scene;
    scene.id = id;
    scene.name = name;
    scene.items = items;
    return scene;
}

SceneSet setOf(const QList<Scene>& scenes, const QString& start) {
    SceneSet set;
    set.scenes = scenes;
    set.startScene = start;
    return set;
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeBytes(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) file.write(bytes);
}

LogicProgram twoScenePlatformer() {
    LogicProgram program;
    program.variables = {{"landed", 0}, {"pits", 0}, {"visits", 0}};
    program.scene = "level";
    program.events = {
        eventBlock("land", BlockType::WhenItemLands, "hero", {setVariable("b1", "landed", 1)}),
        eventBlock("pit", BlockType::WhenItemLeavesWorld, "hero", {changeVariable("b2", "pits", 1), action("b3", BlockType::RestartScene)}),
    };
    SceneLogic second;
    second.scene = "bonus";
    second.events = {eventBlock("start2", BlockType::WhenGameStarts, {}, {changeVariable("b4", "visits", 1)})};
    program.otherScenes = {second};
    return program;
}

SceneSet levelAndBonus() {
    return setOf({sceneNamed("level", "Level", {hero(), floorAt(800)}), sceneNamed("bonus", "Bonus", {rectangle("sign", 10, 10, 50, 50)})}, "level");
}

bool hasPosixCompiler() {
#ifdef _MSC_VER
    return false;
#else
    return !QString(STUDIO_TEST_CXX).isEmpty();
#endif
}

struct HarnessResult {
    bool built = false;
    QString output;
    QStringList lines;
};

// Compiles the real runtime SceneState with the generated headers for `set` and `program`, under the
// game's warning flags and sanitizers, and runs `body` inside main with a `scene` object made for it.
HarnessResult runHarness(const SceneSet& set, const LogicProgram& program, const QByteArray& body, const QList<SceneImageData>& images = {}, const QByteArray& definitions = {}) {
    HarnessResult result;
    const auto sceneHeader = generateSceneHeader(set, images);
    const auto logicHeader = generateLogicHeader(program, set);
    const auto variablesHeader = generateVariablesHeader(program, set);
    if (sceneHeader.isEmpty() || logicHeader.isEmpty() || variablesHeader.isEmpty()) {
        result.output = "invalid project: " + QString::number(sceneHeader.size()) + " " + QString::number(logicHeader.size()) + " " + QString::number(variablesHeader.size());
        return result;
    }
    QTemporaryDir directory;
    const auto root = directory.path();
    const QString runtime = QString(STUDIO_TEST_SOURCE_DIR) + "/runtime/SceneGame";
    for (const auto& relative : {QString("include/Scene/SceneState.hpp"), QString("include/Scene/Particles.hpp"), QString("include/Scene/Formula.hpp"), QString("include/Scene/Physics2D.hpp"), QString("src/Scene/SceneState.cpp"), QString("include/Input/Buttons.hpp"), QString("include/Audio/Mixer.hpp"), QString("include/Save/SaveData.hpp"), QString("include/Scene/World2D.hpp")}) {
        auto contents = readBytes(runtime + '/' + relative + ".in");
        writeBytes(root + '/' + relative, contents.replace("@NAME@", "Harness"));
    }
    writeBytes(root + "/src/GeneratedScene.hpp", sceneHeader);
    writeBytes(root + "/src/GeneratedLogic.hpp", logicHeader);
    writeBytes(root + "/src/GeneratedVariables.hpp", variablesHeader);
    writeBytes(root + "/src/GeneratedSounds.hpp", generateSoundsHeader(static_cast<int>(program.sounds.size())));
    QByteArray main = "#include \"Scene/SceneState.hpp\"\n#include \"Input/Buttons.hpp\"\n#include <cstdint>\n#include <cstdio>\n#include <cstdlib>\n#include <new>\n"
                      "static std::size_t allocations = 0;\n"
                      "void* operator new(std::size_t size) { ++allocations; if (void* memory = std::malloc(size)) return memory; std::abort(); }\n"
                      "void operator delete(void* memory) noexcept { std::free(memory); }\n"
                      "void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }\n";
    main += definitions;
    main += "int main() {\n    SceneState scene(1920, 1080);\n";
    main += body;
    main += "\n    return 0;\n}\n";
    writeBytes(root + "/src/main.cpp", main);

    QStringList arguments{"-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-I" + root + "/include", "-I" + root + "/src", root + "/src/main.cpp", root + "/src/Scene/SceneState.cpp", "-o", root + "/harness"};
    const QString sysroot(STUDIO_TEST_SYSROOT);
    if (!sysroot.isEmpty()) arguments << "-isysroot" << sysroot;
    QProcess compiler;
    compiler.setProcessChannelMode(QProcess::MergedChannels);
    compiler.start(STUDIO_TEST_CXX, arguments);
    if (!compiler.waitForFinished(180000)) {
        result.output = "the compiler did not finish: " + compiler.errorString();
        return result;
    }
    result.output = QString::fromUtf8(compiler.readAll());
    if (compiler.exitCode() != 0) return result;
    result.built = true;

    QProcess harness;
    harness.setProcessChannelMode(QProcess::MergedChannels);
    harness.start(root + "/harness", QStringList{});
    if (!harness.waitForFinished(60000) || harness.exitCode() != 0) {
        result.built = false;
        result.output = "the harness failed: " + QString::fromUtf8(harness.readAll());
        return result;
    }
    result.lines = QString::fromUtf8(harness.readAll()).split('\n', Qt::SkipEmptyParts);
    return result;
}

constexpr auto Frame = "1.0 / 60.0";

}

class TstPhysicsBlocks : public QObject {
    Q_OBJECT

private slots:
    void theCatalogHasTheNewBlocks() {
        for (const auto* name : {"whenItemLands", "whenItemHitsSolid", "whenItemLeavesWorld", "setVelocity", "jumpItem", "ifOnGround", "goToScene", "restartScene"}) {
            QVERIFY2(findBlock(name), name);
        }
        QVERIFY(findBlock("whenItemLands")->isEvent);
        QVERIFY(findBlock("whenItemLands")->usesItem);
        QVERIFY(findBlock("ifOnGround")->holdsActions);
        QVERIFY(!findBlock("ifOnGround")->isEvent);
        QVERIFY(findBlock("ifOnGround")->usesItem);
        QVERIFY(findBlock("setVelocity")->usesPosition);
        QVERIFY(findBlock("setVelocity")->usesItem);
        QVERIFY(findBlock("jumpItem")->usesItem);
        QVERIFY(findBlock("goToScene")->usesScene);
        QVERIFY(!findBlock("restartScene")->usesItem);
    }

    void aProgramRoundTripsWithPerSceneLogic() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        QString error;
        QVERIFY2(saveLogic(path, twoScenePlatformer(), error), qPrintable(error));
        const auto root = QJsonDocument::fromJson(readBytes(path)).object();
        QCOMPARE(root.value("schemaVersion").toInt(), 6);
        QVERIFY(!root.contains("events"));
        const auto scenes = root.value("scenes").toArray();
        QCOMPARE(scenes.size(), 2);
        QCOMPARE(scenes[0].toObject().value("scene").toString(), QString("level"));
        QCOMPARE(scenes[0].toObject().value("events").toArray().size(), 2);
        QCOMPARE(scenes[1].toObject().value("scene").toString(), QString("bonus"));

        const auto loaded = loadLogic(path);
        QVERIFY2(loaded.program.has_value(), qPrintable(loaded.error));
        QCOMPARE(loaded.program->scene, QString("level"));
        QCOMPARE(loaded.program->events.size(), 2);
        QCOMPARE(loaded.program->events[1].type, BlockType::WhenItemLeavesWorld);
        QCOMPARE(loaded.program->otherScenes.size(), 1);
        QCOMPARE(loaded.program->otherScenes[0].scene, QString("bonus"));
        QCOMPARE(loaded.program->otherScenes[0].events[0].children[0].type, BlockType::ChangeVariable);
    }

    void anOlderFilesEventsBelongToTheFirstScene() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        QJsonObject start{{"id", "e"}, {"type", "whenGameStarts"}, {"actions", QJsonArray{}}};
        writeBytes(path, QJsonDocument(QJsonObject{{"schemaVersion", 3}, {"variables", QJsonArray{}}, {"sounds", QJsonArray{}}, {"events", QJsonArray{start}}}).toJson());
        const auto loaded = loadLogic(path);
        QVERIFY2(loaded.program.has_value(), qPrintable(loaded.error));
        QCOMPARE(loaded.program->scene, QString());
        QCOMPARE(loaded.program->events.size(), 1);
        QCOMPARE(eventsOfScene(*loaded.program, "whatever", true).size(), 1);
        QCOMPARE(eventsOfScene(*loaded.program, "another", false).size(), 0);
    }

    void aSchemaFourFileNeedsItsScenesList() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        writeBytes(path, QJsonDocument(QJsonObject{{"schemaVersion", 4}, {"variables", QJsonArray{}}, {"sounds", QJsonArray{}}}).toJson());
        const auto loaded = loadLogic(path);
        QVERIFY(!loaded.program.has_value());
        QVERIFY2(loaded.error.contains("scenes"), qPrintable(loaded.error));
    }

    void physicsBlocksNeedSchemaFour() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        const QJsonObject action{{"id", "b"}, {"type", "jumpItem"}, {"item", "x"}};
        const QJsonObject frame{{"id", "e"}, {"type", "everyFrame"}, {"actions", QJsonArray{action}}};
        writeBytes(path, QJsonDocument(QJsonObject{{"schemaVersion", 3}, {"variables", QJsonArray{}}, {"sounds", QJsonArray{}}, {"events", QJsonArray{frame}}}).toJson());
        const auto loaded = loadLogic(path);
        QVERIFY(!loaded.program.has_value());
        QVERIFY2(loaded.error.contains("schemaVersion 4"), qPrintable(loaded.error));
    }

    void blockIdsAreUniqueAcrossAllScenes() {
        auto program = twoScenePlatformer();
        program.otherScenes[0].events[0].id = "land";
        const auto problems = validateLogicStructure(program);
        QVERIFY(!problems.isEmpty());
        QVERIFY2(problems.first().message.contains("land"), qPrintable(problems.first().message));
    }

    void eventsOfASceneAreFoundByIdOrAsTheFirstScene() {
        const auto program = twoScenePlatformer();
        QCOMPARE(eventsOfScene(program, "level", true).size(), 2);
        QCOMPARE(eventsOfScene(program, "bonus", false).size(), 1);
        QCOMPARE(eventsOfScene(program, "other", false).size(), 0);
    }

    void physicsBlocksNeedAMovingBody() {
        const auto scene = sceneNamed("s", "S", {rectangle("a", 0, 0, 10, 10), hero()});
        LogicProgram program;
        program.events = {eventBlock("e", BlockType::EveryFrame, {}, {action("b", BlockType::JumpItem, "a")})};
        const auto problems = validateLogic(program, scene);
        QCOMPARE(problems.size(), 1);
        QVERIFY2(problems.first().message.contains("moving body"), qPrintable(problems.first().message));

        program.events = {eventBlock("e", BlockType::WhenItemLands, "a")};
        QVERIFY(!validateLogic(program, scene).isEmpty());
        program.events = {eventBlock("e", BlockType::WhenItemLands, "hero")};
        QVERIFY(validateLogic(program, scene).isEmpty());

        Block ifBlock = eventBlock("e2", BlockType::EveryFrame, {}, {});
        Block onGround;
        onGround.id = "g";
        onGround.type = BlockType::IfOnGround;
        onGround.item = "hero";
        ifBlock.children = {onGround};
        program.events = {ifBlock};
        QVERIFY(validateLogic(program, scene).isEmpty());
    }

    void aGoToSceneBlockNeedsARealScene() {
        const auto set = levelAndBonus();
        LogicProgram program;
        program.scene = "level";
        Block go;
        go.id = "b";
        go.type = BlockType::GoToScene;
        program.events = {eventBlock("e", BlockType::EveryFrame, {}, {go})};
        auto problems = validateLogic(program, set.scenes[0], {"level", "bonus"});
        QVERIFY(!problems.isEmpty());
        QVERIFY2(problems.first().message.contains("no scene"), qPrintable(problems.first().message));
        program.events[0].children[0].scene = "gone";
        problems = validateLogic(program, set.scenes[0], {"level", "bonus"});
        QVERIFY(!problems.isEmpty());
        QVERIFY2(problems.first().message.contains("gone"), qPrintable(problems.first().message));
        program.events[0].children[0].scene = "bonus";
        QVERIFY(validateLogic(program, set.scenes[0], {"level", "bonus"}).isEmpty());
    }

    void theGeneratedHeaderDispatchesPerScene() {
        const auto header = generateLogicHeader(twoScenePlatformer(), levelAndBonus());
        QVERIFY2(!header.isEmpty(), "no header");
        QVERIFY2(header.contains("void OnStart0("), header.constData());
        QVERIFY2(header.contains("void OnFrame1("), header.constData());
        QVERIFY2(header.contains("void OnSceneFrame("), header.constData());
        QVERIFY2(header.contains("case 1: OnFrame1(world, memory); break;"), header.constData());
        QVERIFY2(header.contains("world.Landed(0)"), header.constData());
        QVERIFY2(header.contains("world.LeftWorld(0)"), header.constData());
        QVERIFY2(header.contains("world.RestartScene()"), header.constData());
        QVERIFY2(header.contains("static_assert(GeneratedScene::Items.size() == 3"), header.constData());
    }

    void aGoToSceneBlockBecomesASceneNumber() {
        auto program = twoScenePlatformer();
        Block go;
        go.id = "go";
        go.type = BlockType::GoToScene;
        go.scene = "bonus";
        program.events = {eventBlock("e", BlockType::EveryFrame, {}, {go})};
        const auto header = generateLogicHeader(program, levelAndBonus());
        QVERIFY2(header.contains("world.GoToScene(1)"), header.constData());
    }

    void itemIndexesAreCountedAcrossScenes() {
        auto set = levelAndBonus();
        set.scenes[1].items = {rectangle("sign", 0, 0, 10, 10), hero()};
        auto program = twoScenePlatformer();
        program.otherScenes[0].events = {eventBlock("e", BlockType::WhenItemHitsSolid, "hero")};
        const auto header = generateLogicHeader(program, set);
        QVERIFY2(!header.isEmpty(), "no header");
        QVERIFY2(header.contains("world.HitSolid(3)"), header.constData()); // scene 0 has 2 items, sign is 2, hero is 3
    }

    void generatedSceneHeadersCompileForTheHarnessScenes() {
        QVERIFY(!generateSceneHeader(levelAndBonus()).isEmpty());
    }

    void aPlatformerRunsThroughTheRealRuntime() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        auto program = twoScenePlatformer();
        program.events = {
            eventBlock("land", BlockType::WhenItemLands, "hero", {changeVariable("b1", "landed", 1)}),
        };
        const QByteArray body =
            "    for (int frame = 0; frame < 120; ++frame) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    std::printf(\"y=%.0f\\n\", static_cast<double>(scene.Current()[0][1]));\n"
            "    std::printf(\"landed=%d\\n\", static_cast<int>(scene.Variable(0)));\n";
        const auto result = runHarness(levelAndBonus(), program, body);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(0), QString("y=736")); // 800 - 64: hero stands on the floor
        QCOMPARE(result.lines.value(1), QString("landed=1"));
    }

    void walkingOnlyMovesWhileTheStickIsHeldAndJumpingUsesCross() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        const QByteArray body =
            "    for (int frame = 0; frame < 90; ++frame) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    std::printf(\"start=%.0f\\n\", static_cast<double>(scene.Current()[0][0]));\n"
            "    for (int frame = 0; frame < 60; ++frame) scene.Update(1.0 / 60.0, {1.0f, 0.0f}, 0);\n"
            "    std::printf(\"moved=%d\\n\", scene.Current()[0][0] > 400.0f ? 1 : 0);\n"
            "    for (int frame = 0; frame < 30; ++frame) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    float top = scene.Current()[0][1];\n"
            "    for (int frame = 0; frame < 60; ++frame) { scene.Update(1.0 / 60.0, {0.0f, 0.0f}, Buttons::Cross); top = std::min(top, scene.Current()[0][1]); }\n"
            "    std::printf(\"rose=%d\\n\", top < 736.0f - 200.0f ? 1 : 0);\n";
        const auto result = runHarness(levelAndBonus(), twoScenePlatformer(), body);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(1), QString("moved=1"));
        QCOMPARE(result.lines.value(2), QString("rose=1"));
    }

    void tilesBlockABodyThroughTheRealRuntime() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        auto scene = sceneNamed("s", "S", {hero(100, 0)});
        SceneItem map;
        map.id = "ground";
        map.type = "tilemap";
        map.name = "Ground";
        map.source = "assets/t.png";
        map.tileWidth = 16;
        map.tileHeight = 16;
        map.tileScale = 4;
        map.columns = 30;
        map.rows = 17;
        map.tiles = QList<int>(30 * 17, -1);
        for (int column = 0; column < 30; ++column) map.tiles[16 * 30 + column] = 1;
        map.solidTiles = {1};
        updateTilemapSize(map);
        scene.items.prepend(map);
        const SceneSet set = setOf({scene}, "s");
        QList<SceneImageData> images{{"assets/t.png", 32, 16, QByteArray(32 * 16 * 4, char(0))}};

        LogicProgram program;
        program.scene = "s";
        program.variables = {{"landed", 0}};
        program.events = {eventBlock("land", BlockType::WhenItemLands, "hero", {setVariable("b1", "landed", 1)})};
        QVERIFY(generateLogicHeader(program, set).contains("world.Landed(1)")); // hero is item 1 after the tilemap

        // The tilemap's cells, standing in for what GeneratedImages.cpp defines in the game.
        QByteArray definitions = "static const std::int32_t heroCells[510] = {";
        for (int cell = 0; cell < 510; ++cell) definitions += (cell >= 16 * 30 ? "1," : "-1,");
        definitions += "};\nnamespace GeneratedScene { const std::array<const std::int32_t*, TilemapCount> TilemapCells{{heroCells}}; }\n";
        const QByteArray body =
            "    for (int frame = 0; frame < 180; ++frame) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    std::printf(\"y=%.0f landed=%d\\n\", static_cast<double>(scene.Current()[1][1]), static_cast<int>(scene.Variable(0)));\n"
            "    for (int frame = 0; frame < 60; ++frame) scene.Update(1.0 / 60.0, {1.0f, 0.0f}, 0);\n"
            "    std::printf(\"y=%.0f\\n\", static_cast<double>(scene.Current()[1][1]));\n";
        const auto result = runHarness(set, program, body, images, definitions);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(0), QString("y=960 landed=1")); // tile row 16 starts at 16 * 64 = 1024, hero 64 tall
        QCOMPARE(result.lines.value(1), QString("y=960"));
    }

    void anEmptyProjectCompiles() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        const auto result = runHarness(setOf({Scene{}}, "scene-1"), LogicProgram{}, "    scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n    std::printf(\"scene=%zu\\n\", scene.CurrentScene());\n");
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(0), QString("scene=0"));
    }

    void goToSceneSwitchesScenesAndKeepsVariables() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        auto program = twoScenePlatformer();
        Block go;
        go.id = "go";
        go.type = BlockType::GoToScene;
        go.scene = "bonus";
        program.events = {eventBlock("land", BlockType::WhenItemLands, "hero", {changeVariable("b1", "landed", 1), go})};
        const QByteArray body =
            "    for (int frame = 0; frame < 120; ++frame) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    std::printf(\"scene=%zu landed=%d visits=%d\\n\", scene.CurrentScene(), static_cast<int>(scene.Variable(0)), static_cast<int>(scene.Variable(2)));\n"
            "    for (int frame = 0; frame < 120; ++frame) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    std::printf(\"scene=%zu visits=%d\\n\", scene.CurrentScene(), static_cast<int>(scene.Variable(2)));\n";
        const auto result = runHarness(levelAndBonus(), program, body);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(0), QString("scene=1 landed=1 visits=1"));
        QCOMPARE(result.lines.value(1), QString("scene=1 visits=1"));
    }

    void leavingTheWorldRestartsTheSceneInTheRealRuntime() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        auto set = levelAndBonus();
        set.scenes[0].items = {hero(100, 1000)}; // no floor: it falls straight out
        auto program = twoScenePlatformer();
        program.events = {eventBlock("pit", BlockType::WhenItemLeavesWorld, "hero", {changeVariable("b2", "pits", 1), action("b3", BlockType::RestartScene)})};
        const QByteArray body =
            "    int restarts = 0;\n"
            "    for (int frame = 0; frame < 240; ++frame) {\n"
            "        scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "        if (scene.Variable(1) > restarts) { restarts = static_cast<int>(scene.Variable(1)); std::printf(\"restart y=%.0f\\n\", static_cast<double>(scene.Current()[0][1])); if (restarts == 2) break; }\n"
            "    }\n"
            "    std::printf(\"pits=%d\\n\", static_cast<int>(scene.Variable(1)));\n";
        const auto result = runHarness(set, program, body);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(0), QString("restart y=1000"));
        QCOMPARE(result.lines.value(1), QString("restart y=1000"));
        QCOMPARE(result.lines.value(2), QString("pits=2"));
    }

    void onGroundAndJumpBlocksWorkInTheRealRuntime() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        LogicProgram program;
        program.variables = {{"ground", 0}};
        program.scene = "level";
        Block ifGround;
        ifGround.id = "if1";
        ifGround.type = BlockType::IfOnGround;
        ifGround.item = "hero";
        ifGround.children = {setVariable("b1", "ground", 1)};
        program.events = {eventBlock("e", BlockType::EveryFrame, {}, {setVariable("b0", "ground", 0), ifGround}),
                          eventBlock("e2", BlockType::WhileButtonHeld, {}, {action("b2", BlockType::JumpItem, "hero")})};
        program.events[1].button = ControllerButton::Square;
        const QByteArray body =
            "    for (int frame = 0; frame < 120; ++frame) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    std::printf(\"ground=%d\\n\", static_cast<int>(scene.Variable(0)));\n"
            "    float top = scene.Current()[0][1];\n"
            "    for (int frame = 0; frame < 10; ++frame) { scene.Update(1.0 / 60.0, {0.0f, 0.0f}, Buttons::Square); top = std::min(top, scene.Current()[0][1]); }\n"
            "    std::printf(\"jumped=%d\\n\", top < 700.0f ? 1 : 0);\n";
        const auto result = runHarness(setOf({sceneNamed("level", "Level", {hero(100, 700), floorAt(800)})}, "level"), program, body);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(0), QString("ground=1"));
        QCOMPARE(result.lines.value(1), QString("jumped=1"));
    }

    void setVelocityMovesABodyInTheRealRuntime() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        LogicProgram program;
        program.scene = "level";
        auto crate = hero(100, 700);
        crate.platformer = false;
        program.events = {eventBlock("e", BlockType::WhenGameStarts, {}, {action("b", BlockType::SetVelocity, "hero", 240, 0)})};
        const QByteArray body =
            "    for (int frame = 0; frame < 60; ++frame) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    std::printf(\"x=%.0f\\n\", static_cast<double>(scene.Current()[0][0]));\n";
        const auto result = runHarness(setOf({sceneNamed("level", "Level", {crate, floorAt(800)})}, "level"), program, body);
        QVERIFY2(result.built, qPrintable(result.output));
        const auto x = result.lines.value(0).mid(2).toDouble();
        QVERIFY2(x > 330 && x < 345, qPrintable(result.lines.value(0)));
    }

    void aSolidItemHiddenByABlockStopsBlocking() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        LogicProgram program;
        program.scene = "level";
        program.events = {eventBlock("e", BlockType::WhenGameStarts, {}, {action("b", BlockType::Hide, "floor")})};
        const QByteArray body =
            "    for (int frame = 0; frame < 120; ++frame) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    std::printf(\"past=%d\\n\", scene.Current()[0][1] > 800.0f ? 1 : 0);\n";
        const auto result = runHarness(setOf({sceneNamed("level", "Level", {hero(100, 700), floorAt(800)})}, "level"), program, body);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(0), QString("past=1"));
    }

    void movingABodyWithABlockTeleportsItAndKeepsSimulating() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        LogicProgram program;
        program.scene = "level";
        program.events = {eventBlock("e", BlockType::WhileButtonHeld, {}, {action("b", BlockType::SetPosition, "hero", 500, 200)})};
        program.events[0].button = ControllerButton::Triangle;
        const QByteArray body =
            "    for (int frame = 0; frame < 120; ++frame) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    scene.Update(1.0 / 60.0, {0.0f, 0.0f}, Buttons::Triangle);\n"
            "    std::printf(\"x=%.0f\\n\", static_cast<double>(scene.Current()[0][0]));\n"
            "    for (int frame = 0; frame < 180; ++frame) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    std::printf(\"y=%.0f\\n\", static_cast<double>(scene.Current()[0][1]));\n";
        const auto result = runHarness(setOf({sceneNamed("level", "Level", {hero(100, 700), floorAt(800)})}, "level"), program, body);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(0), QString("x=500"));
        QCOMPARE(result.lines.value(1), QString("y=736"));
    }

    void framesAllocateNothingWithPhysicsAndScenes() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        auto program = twoScenePlatformer();
        Block go;
        go.id = "go";
        go.type = BlockType::GoToScene;
        go.scene = "bonus";
        program.events.append(eventBlock("cross", BlockType::WhileButtonHeld, {}, {go}));
        program.events.last().button = ControllerButton::Cross;
        const QByteArray body =
            "    const auto before = allocations;\n"
            "    for (int frame = 0; frame < 2000; ++frame) scene.Update(1.0 / 60.0, {frame % 2 == 0 ? 1.0f : -1.0f, 0.0f}, frame % 300 == 299 ? Buttons::Cross : 0u);\n"
            "    std::printf(\"allocations=%zu\\n\", allocations - before);\n";
        const auto result = runHarness(levelAndBonus(), program, body);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.last(), QString("allocations=0"));
    }

    void anOldProgramAndSceneStillGenerateAndRun() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        Scene scene;
        auto box = rectangle("a", 0, 0, 10, 10);
        box.controllable = true;
        scene.items = {box};
        LogicProgram program;
        const QByteArray body =
            "    scene.Update(1.0, {1.0f, 0.0f}, 0);\n"
            "    std::printf(\"x=%.0f\\n\", static_cast<double>(scene.Current()[0][0]));\n";
        const auto result = runHarness(setOf({scene}, scene.id), program, body);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(0), QString("x=600"));
    }

    void logicDocumentKeepsPerSceneEvents() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        const auto set = levelAndBonus();
        QString error;
        QVERIFY(saveLogic(path, twoScenePlatformer(), error));
        LogicDocument document;
        document.setSceneSet(set, 0);
        QVERIFY(document.open(path));
        QCOMPARE(document.program().events.size(), 2);
        document.setSceneSet(set, 1);
        QCOMPARE(document.program().scene, QString("bonus"));
        QCOMPARE(document.program().events.size(), 1);
        document.setSceneSet(set, 0);
        QCOMPARE(document.program().events.size(), 2);
    }

    void logicEditsSaveIntoTheRightScene() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        const auto set = levelAndBonus();
        QString error;
        QVERIFY(saveLogic(path, twoScenePlatformer(), error));
        LogicDocument document;
        document.setSceneSet(set, 1);
        QVERIFY(document.open(path));
        QCOMPARE(document.program().events.size(), 1); // opened into the bonus scene
        QVERIFY(!document.addBlock("everyFrame", {}, 1).isEmpty());
        const auto reloaded = loadLogic(path);
        QVERIFY(reloaded.program.has_value());
        QCOMPARE(eventsOfScene(*reloaded.program, "bonus", false).size(), 2);
        QCOMPARE(eventsOfScene(*reloaded.program, "level", true).size(), 2);
    }

    void duplicatingASceneCopiesItsBlocksWithNewIds() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        auto set = levelAndBonus();
        QString error;
        QVERIFY(saveLogic(path, twoScenePlatformer(), error));
        LogicDocument document;
        document.setSceneSet(set, 0);
        QVERIFY(document.open(path));
        auto copy = set.scenes[0];
        copy.id = "level-copy";
        copy.name = "Level copy";
        set.scenes.append(copy);
        document.setSceneSet(set, 0);
        QVERIFY(document.duplicateSceneLogic("level", "level-copy"));
        document.setSceneSet(set, 2);
        QCOMPARE(document.program().scene, QString("level-copy"));
        QCOMPARE(document.program().events.size(), 2);
        QVERIFY(document.program().events[0].id != "land");
        QVERIFY(validateLogicStructure(document.program()).isEmpty());
    }

    void goToSceneChoicesComeFromTheProject() {
        const auto set = levelAndBonus();
        LogicDocument document;
        document.setSceneSet(set, 0);
        const auto choices = document.scenes();
        QCOMPARE(choices.size(), 2);
        QCOMPARE(choices[1].toMap().value("id").toString(), QString("bonus"));
        QCOMPARE(choices[1].toMap().value("name").toString(), QString("Bonus"));
    }

    void aGoToSceneBlockIsEditableThroughTheDocument() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        const auto set = levelAndBonus();
        LogicDocument document;
        document.setSceneSet(set, 0);
        QVERIFY(document.open(path));
        const auto event = document.addFromPalette("everyFrame");
        QVERIFY(!event.isEmpty());
        document.setSelectedId(event);
        const auto go = document.addFromPalette("goToScene");
        QVERIFY(!go.isEmpty());
        QVERIFY(document.setField(go, "scene", "bonus"));
        QCOMPARE(document.program().events[0].children[0].scene, QString("bonus"));
    }
    void theTopDownBlocksAreInTheCatalog() {
        for (const auto* name : {"whenButtonPressed", "ifItemDistance", "chaseItem", "putAtItem"}) QVERIFY2(findBlock(name), name);
        QVERIFY(findBlock("whenButtonPressed")->isEvent);
        QVERIFY(findBlock("whenButtonPressed")->usesButton);
        QVERIFY(findBlock("ifItemDistance")->holdsActions);
        QVERIFY(findBlock("ifItemDistance")->usesComparison);
        QVERIFY(findBlock("chaseItem")->usesValue);
        QVERIFY(findBlock("putAtItem")->usesPosition);
        QStringList buttons;
        for (const auto& button : buttonCatalog()) buttons << button.jsonName;
        for (const auto* name : {"moveUp", "moveDown", "moveLeft", "moveRight"}) QVERIFY2(buttons.contains(name), name);
    }

    void theNewBlocksRoundTripThroughLogicJson() {
        LogicProgram program;
        program.variables = {{"near", 0}};
        program.scene = "field";
        auto press = eventBlock("press", BlockType::WhenButtonPressed);
        press.button = ControllerButton::MoveLeft;
        auto distance = between("d", BlockType::IfItemDistance, "mob", "hero", 150);
        distance.comparison = VariableComparison::LessOrEqual;
        distance.children = {setVariable("s", "near", 1), action("put", BlockType::PutAtItem, "flag", 10, -20)};
        distance.children[1].otherItem = "hero";
        press.children = {distance, between("c", BlockType::ChaseItem, "mob", "hero", 240)};
        program.events = {press};
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        QString error;
        QVERIFY2(saveLogic(path, program, error), qPrintable(error));
        const auto loaded = loadLogic(path);
        QVERIFY2(loaded.program.has_value(), qPrintable(loaded.error));
        const auto& event = loaded.program->events[0];
        QCOMPARE(event.type, BlockType::WhenButtonPressed);
        QCOMPARE(event.button, ControllerButton::MoveLeft);
        QCOMPARE(event.children[0].value, 150);
        QCOMPARE(event.children[0].otherItem, QString("hero"));
        QCOMPARE(event.children[0].comparison, VariableComparison::LessOrEqual);
        QCOMPARE(event.children[0].children[1].x, 10.0);
        QCOMPARE(event.children[1].value, 240);
    }

    void chasingNeedsAMovingBodyAndASensibleNumber() {
        auto scene = sceneNamed("field", "Field", {topDownHero(), rectangle("flag", 0, 0, 10, 10)});
        LogicProgram program;
        program.scene = "field";
        program.events = {eventBlock("e", BlockType::EveryFrame, {}, {between("c", BlockType::ChaseItem, "flag", "hero", 100), between("n", BlockType::ChaseItem, "hero", "flag", -5), between("d", BlockType::IfItemDistance, "hero", "flag", -1)})};
        const auto problems = validateLogic(program, scene);
        QStringList ids;
        for (const auto& problem : problems) ids << problem.blockId;
        QVERIFY2(ids.contains("c"), "a rectangle with no physics cannot chase");
        QVERIFY2(ids.contains("n"), "a chase speed cannot be negative");
        QVERIFY2(ids.contains("d"), "a distance cannot be negative");
    }

    void aTopDownBodyWalksBothWaysStopsAtWallsAndNeverFalls() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        auto wall = rectangle("wall", 400, 0, 64, 1080);
        wall.physics = "solid";
        const auto set = setOf({sceneNamed("field", "Field", {topDownHero(), wall})}, "field");
        const QByteArray body =
            "    for (int frame = 0; frame < 60; ++frame) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    std::printf(\"idle=%.0f,%.0f\\n\", static_cast<double>(scene.Current()[0][0]), static_cast<double>(scene.Current()[0][1]));\n"
            "    for (int frame = 0; frame < 30; ++frame) scene.Update(1.0 / 60.0, {0.0f, 1.0f}, 0);\n"
            "    std::printf(\"down=%d\\n\", scene.Current()[0][1] > 350.0f && scene.Current()[0][0] == 100.0f ? 1 : 0);\n"
            "    for (int frame = 0; frame < 90; ++frame) scene.Update(1.0 / 60.0, {1.0f, 0.0f}, 0);\n"
            "    std::printf(\"wall=%.0f\\n\", static_cast<double>(scene.Current()[0][0]));\n"
            "    for (int frame = 0; frame < 120; ++frame) scene.Update(1.0 / 60.0, {-1.0f, 0.0f}, 0);\n"
            "    for (int frame = 0; frame < 120; ++frame) scene.Update(1.0 / 60.0, {0.0f, -1.0f}, 0);\n"
            "    std::printf(\"corner=%.0f,%.0f\\n\", static_cast<double>(scene.Current()[0][0]), static_cast<double>(scene.Current()[0][1]));\n"
            "    for (int frame = 0; frame < 240; ++frame) scene.Update(1.0 / 60.0, {0.0f, 1.0f}, 0);\n"
            "    std::printf(\"floor=%.0f\\n\", static_cast<double>(scene.Current()[0][1]));\n";
        LogicProgram program;
        const auto result = runHarness(set, program, body);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(0), QString("idle=100,100"));
        QCOMPARE(result.lines.value(1), QString("down=1"));
        QCOMPARE(result.lines.value(2), QString("wall=336"));
        QCOMPARE(result.lines.value(3), QString("corner=0,0"));
        QCOMPARE(result.lines.value(4), QString("floor=1016")); // 1080 - 64: the bottom of the world is a wall
    }

    void aPressRunsOnceAndTheStickCountsAsDirectionButtons() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        auto press = eventBlock("press", BlockType::WhenButtonPressed, {}, {changeVariable("a", "presses", 1)});
        press.button = ControllerButton::Cross;
        auto held = eventBlock("held", BlockType::WhileButtonHeld, {}, {changeVariable("b", "held", 1)});
        held.button = ControllerButton::MoveRight;
        LogicProgram program;
        program.variables = {{"presses", 0}, {"held", 0}};
        program.scene = "field";
        program.events = {press, held};
        const auto set = setOf({sceneNamed("field", "Field", {topDownHero()})}, "field");
        const QByteArray body =
            "    for (int frame = 0; frame < 5; ++frame) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, Buttons::Cross);\n"
            "    std::printf(\"one=%d\\n\", static_cast<int>(scene.Variable(0)));\n"
            "    scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    scene.Update(1.0 / 60.0, {0.0f, 0.0f}, Buttons::Cross);\n"
            "    std::printf(\"two=%d\\n\", static_cast<int>(scene.Variable(0)));\n"
            "    for (int frame = 0; frame < 4; ++frame) scene.Update(1.0 / 60.0, {1.0f, 0.0f}, 0);\n"
            "    scene.Update(1.0 / 60.0, {-1.0f, 0.0f}, 0);\n"
            "    std::printf(\"stick=%d\\n\", static_cast<int>(scene.Variable(1)));\n"
            "    std::size_t before = allocations;\n"
            "    for (int frame = 0; frame < 60; ++frame) scene.Update(1.0 / 60.0, {1.0f, 0.0f}, Buttons::Cross);\n"
            "    std::printf(\"allocations=%zu\\n\", allocations - before);\n";
        const auto result = runHarness(set, program, body);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(0), QString("one=1"));
        QCOMPARE(result.lines.value(1), QString("two=2"));
        QCOMPARE(result.lines.value(2), QString("stick=4"));
        QCOMPARE(result.lines.value(3), QString("allocations=0"));
    }

    void aMobChasesMeasuresItsDistanceAndAnItemCanBePutBesideAnother() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        auto mob = rectangle("mob", 800, 100, 64, 64);
        mob.physics = "body";
        mob.gravityScale = 0;
        auto near = between("n", BlockType::IfItemDistance, "mob", "hero", 100, {setVariable("a", "near", 1)});
        near.comparison = VariableComparison::LessOrEqual;
        auto far = between("f", BlockType::IfItemDistance, "mob", "hero", 100, {setVariable("b", "far", 1)});
        far.comparison = VariableComparison::Greater;
        auto put = action("p", BlockType::PutAtItem, "flag", 10, -20);
        put.otherItem = "hero";
        LogicProgram program;
        program.variables = {{"near", 0}, {"far", 0}};
        program.scene = "field";
        program.events = {eventBlock("e", BlockType::EveryFrame, {}, {between("c", BlockType::ChaseItem, "mob", "hero", 300), near, far, put})};
        const auto set = setOf({sceneNamed("field", "Field", {topDownHero(), mob, rectangle("flag", 0, 0, 10, 10)})}, "field");
        const QByteArray body =
            "    for (int frame = 0; frame < 2; ++frame) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    std::printf(\"first=%d,%d\\n\", static_cast<int>(scene.Variable(0)), static_cast<int>(scene.Variable(1)));\n"
            "    std::printf(\"flag=%.0f,%.0f\\n\", static_cast<double>(scene.Current()[2][0]), static_cast<double>(scene.Current()[2][1]));\n"
            "    std::printf(\"closer=%d\\n\", scene.Current()[1][0] < 800.0f ? 1 : 0);\n"
            "    for (int frame = 0; frame < 240; ++frame) scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    std::printf(\"caught=%d\\n\", static_cast<int>(scene.Variable(0)));\n"
            "    std::printf(\"settled=%d\\n\", scene.Current()[1][0] >= 95.0f && scene.Current()[1][0] <= 105.0f ? 1 : 0);\n";
        const auto result = runHarness(set, program, body);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(0), QString("first=0,1"));
        QCOMPARE(result.lines.value(1), QString("flag=110,80"));
        QCOMPARE(result.lines.value(2), QString("closer=1"));
        QCOMPARE(result.lines.value(3), QString("caught=1"));
        QCOMPARE(result.lines.value(4), QString("settled=1"));
    }

    void theCoopAndPauseBlocksAndButtonsAreInTheCatalog() {
        for (const auto* name : {"pauseGame", "resumeGame", "cameraFollowBoth"}) QVERIFY2(findBlock(name), name);
        QVERIFY(!findBlock("pauseGame")->isEvent);
        QVERIFY(findBlock("cameraFollowBoth")->usesItem);
        QVERIFY(findBlock("cameraFollowBoth")->usesOtherItem);
        QStringList buttons;
        for (const auto& button : buttonCatalog()) buttons << button.jsonName;
        for (const auto* name : {"options", "p2Cross", "p2Circle", "p2Square", "p2Triangle", "p2Options", "p2DpadUp", "p2DpadDown", "p2DpadLeft", "p2DpadRight", "p2MoveUp", "p2MoveDown", "p2MoveLeft", "p2MoveRight"}) {
            QVERIFY2(buttons.contains(name), name);
        }
    }

    void playerTwosButtonsAndBlocksRoundTripThroughLogicJson() {
        LogicProgram program;
        program.scene = "field";
        auto press = eventBlock("press", BlockType::WhenButtonPressed, {}, {action("pause", BlockType::PauseGame), action("resume", BlockType::ResumeGame)});
        press.button = ControllerButton::P2Options;
        auto both = action("both", BlockType::CameraFollowBoth, "hero");
        both.otherItem = "hero2";
        program.events = {press, eventBlock("start", BlockType::WhenGameStarts, {}, {both})};
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        QString error;
        QVERIFY2(saveLogic(path, program, error), qPrintable(error));
        const auto loaded = loadLogic(path);
        QVERIFY2(loaded.program.has_value(), qPrintable(loaded.error));
        QCOMPARE(loaded.program->events[0].button, ControllerButton::P2Options);
        QCOMPARE(loaded.program->events[0].children[0].type, BlockType::PauseGame);
        QCOMPARE(loaded.program->events[0].children[1].type, BlockType::ResumeGame);
        QCOMPARE(loaded.program->events[1].children[0].otherItem, QString("hero2"));
    }

    void onlyABodyWithControlsCanBelongToPlayerTwo() {
        auto second = topDownHero();
        second.id = "hero2";
        second.player = 2;
        QVERIFY2(validateScene(sceneNamed("field", "Field", {topDownHero(), second})).isEmpty(), "a top-down body can be player 2's");
        auto sign = rectangle("sign", 0, 0, 10, 10);
        sign.player = 2;
        QVERIFY2(!validateScene(sceneNamed("field", "Field", {sign})).isEmpty(), "a rectangle with no controls cannot belong to a player");
        auto fourth = topDownHero();
        fourth.player = 4;
        QVERIFY2(validateScene(sceneNamed("field", "Field", {fourth})).isEmpty(), "there are four players");
        auto fifth = topDownHero();
        fifth.player = 5;
        QVERIFY2(!validateScene(sceneNamed("field", "Field", {fifth})).isEmpty(), "there are four players");
    }

    void eachPlayerDrivesTheirOwnBodyAndButtons() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        auto second = topDownHero();
        second.id = "hero2";
        second.x = 600;
        second.player = 2;
        auto one = eventBlock("one", BlockType::WhenButtonPressed, {}, {changeVariable("a", "oneCross", 1)});
        one.button = ControllerButton::Cross;
        auto two = eventBlock("two", BlockType::WhenButtonPressed, {}, {changeVariable("b", "twoCross", 1)});
        two.button = ControllerButton::P2Cross;
        auto walk = eventBlock("walk", BlockType::WhileButtonHeld, {}, {changeVariable("c", "twoRight", 1)});
        walk.button = ControllerButton::P2MoveRight;
        LogicProgram program;
        program.variables = {{"oneCross", 0}, {"twoCross", 0}, {"twoRight", 0}};
        program.scene = "field";
        program.events = {one, two, walk};
        const auto set = setOf({sceneNamed("field", "Field", {topDownHero(), second})}, "field");
        const QByteArray body =
            "    for (int frame = 0; frame < 30; ++frame) scene.Update(1.0 / 60.0, {SceneState::PlayerInput{{0.0f, 0.0f}, 0}, SceneState::PlayerInput{{1.0f, 0.0f}, Buttons::Cross}});\n"
            "    std::printf(\"heroes=%.0f,%d\\n\", static_cast<double>(scene.Current()[0][0]), scene.Current()[1][0] > 650.0f ? 1 : 0);\n"
            "    std::printf(\"vars=%d,%d,%d\\n\", static_cast<int>(scene.Variable(0)), static_cast<int>(scene.Variable(1)), static_cast<int>(scene.Variable(2)));\n"
            "    const float second = scene.Current()[1][0];\n"
            "    for (int frame = 0; frame < 30; ++frame) scene.Update(1.0 / 60.0, {0.0f, 1.0f}, Buttons::Cross);\n"
            "    std::printf(\"one=%d,%d,%d\\n\", scene.Current()[0][1] > 150.0f ? 1 : 0, scene.Current()[1][0] == second ? 1 : 0, static_cast<int>(scene.Variable(0)));\n"
            "    std::size_t before = allocations;\n"
            "    for (int frame = 0; frame < 60; ++frame) scene.Update(1.0 / 60.0, {SceneState::PlayerInput{{1.0f, 0.0f}, Buttons::Square}, SceneState::PlayerInput{{-1.0f, 0.0f}, Buttons::Cross}});\n"
            "    std::printf(\"allocations=%zu\\n\", allocations - before);\n";
        const auto result = runHarness(set, program, body);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(0), QString("heroes=100,1"));
        QCOMPARE(result.lines.value(1), QString("vars=0,1,30"));
        QCOMPARE(result.lines.value(2), QString("one=1,1,1"));
        QCOMPARE(result.lines.value(3), QString("allocations=0"));
    }

    void pausingFreezesTheWorldWhileBlocksKeepRunning() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        auto mob = rectangle("mob", 1200, 100, 64, 64);
        mob.physics = "body";
        mob.gravityScale = 0;
        auto toggle = eventBlock("toggle", BlockType::WhenButtonPressed);
        toggle.button = ControllerButton::Options;
        auto ifRunning = eventBlock("running", BlockType::IfVariable, {}, {action("p", BlockType::PauseGame), setVariable("s", "paused", 2)});
        ifRunning.type = BlockType::IfVariable;
        ifRunning.variable = "paused";
        ifRunning.comparison = VariableComparison::Equal;
        ifRunning.value = 0;
        auto ifPaused = ifRunning;
        ifPaused.id = "stopped";
        ifPaused.value = 1;
        ifPaused.children = {action("r", BlockType::ResumeGame), setVariable("u", "paused", 0)};
        auto settle = ifRunning;
        settle.id = "settle";
        settle.value = 2;
        settle.children = {setVariable("v", "paused", 1)};
        toggle.children = {ifRunning, ifPaused, settle};
        LogicProgram program;
        program.variables = {{"paused", 0}, {"frames", 0}};
        program.scene = "field";
        program.events = {toggle, eventBlock("count", BlockType::EveryFrame, {}, {changeVariable("f", "frames", 1), between("c", BlockType::ChaseItem, "mob", "hero", 300)})};
        const auto set = setOf({sceneNamed("field", "Field", {topDownHero(), mob})}, "field");
        const QByteArray body =
            "    for (int frame = 0; frame < 10; ++frame) scene.Update(1.0 / 60.0, {1.0f, 0.0f}, 0);\n"
            "    scene.Update(1.0 / 60.0, {0.0f, 0.0f}, Buttons::Options);\n"
            "    const float hero = scene.Current()[0][0];\n"
            "    const float mob = scene.Current()[1][0];\n"
            "    for (int frame = 0; frame < 30; ++frame) scene.Update(1.0 / 60.0, {1.0f, 0.0f}, 0);\n"
            "    std::printf(\"paused=%d,%d,%d,%d\\n\", scene.Current()[0][0] == hero ? 1 : 0, scene.Current()[1][0] == mob ? 1 : 0, static_cast<int>(scene.Variable(1)), static_cast<int>(scene.Variable(0)));\n"
            "    scene.Update(1.0 / 60.0, {0.0f, 0.0f}, Buttons::Options);\n"
            "    for (int frame = 0; frame < 30; ++frame) scene.Update(1.0 / 60.0, {1.0f, 0.0f}, 0);\n"
            "    std::printf(\"resumed=%d,%d,%d\\n\", scene.Current()[0][0] > hero ? 1 : 0, scene.Current()[1][0] < mob ? 1 : 0, static_cast<int>(scene.Variable(0)));\n";
        const auto result = runHarness(set, program, body);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(0), QString("paused=1,1,41,1"));
        QCOMPARE(result.lines.value(1), QString("resumed=1,1,0"));
    }

    void theCameraKeepsBothPlayersInViewAndFollowsTheOneLeft() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        auto first = topDownHero();
        first.x = 1000;
        auto second = topDownHero();
        second.id = "hero2";
        second.x = 2000;
        second.player = 2;
        auto field = sceneNamed("field", "Field", {first, second});
        field.worldWidth = 4000;
        field.worldHeight = 1080;
        auto both = action("both", BlockType::CameraFollowBoth, "hero");
        both.otherItem = "hero2";
        auto hide = action("h", BlockType::Hide, "hero2");
        auto press = eventBlock("press", BlockType::WhenButtonPressed, {}, {hide});
        press.button = ControllerButton::Square;
        LogicProgram program;
        program.scene = "field";
        program.events = {eventBlock("start", BlockType::WhenGameStarts, {}, {both}), press};
        const auto set = setOf({field}, "field");
        const QByteArray body =
            "    scene.Update(1.0 / 60.0, {0.0f, 0.0f}, 0);\n"
            "    std::printf(\"both=%.0f\\n\", static_cast<double>(scene.Camera().x));\n"
            "    scene.Update(1.0 / 60.0, {0.0f, 0.0f}, Buttons::Square);\n"
            "    std::printf(\"one=%.0f\\n\", static_cast<double>(scene.Camera().x));\n";
        const auto result = runHarness(set, program, body);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.value(0), QString("both=572")); // the middle of 1000 and 2064 is 1532, less half the 1920 wide screen
        QCOMPARE(result.lines.value(1), QString("one=72"));   // only the first hero: 1032 - 960
    }
};

QTEST_GUILESS_MAIN(TstPhysicsBlocks)
#include "tst_physics_blocks.moc"
