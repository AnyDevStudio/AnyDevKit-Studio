// The blocks of logic.json version 5: formulas, players, templates and their copies, behaviors, and player and
// copy variables. The catalog, files, validation and generated code, and the real runtime SceneState compiled
// and run on this computer under sanitizers.
#include "Blocks.h"
#include "Formula.h"
#include "Scene.h"
#include "Sounds.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace Studio;

namespace {

Block block(const QString& id, BlockType type, const QList<Block>& children = {}) {
    Block result;
    result.id = id;
    result.type = type;
    result.children = children;
    return result;
}

Block withItem(Block result, const QString& item, const QString& other = {}) {
    result.item = item;
    result.otherItem = other;
    return result;
}

Block formulaBlock(const QString& id, const QString& variable, const QString& formula) {
    auto result = block(id, BlockType::SetFormula);
    result.variable = variable;
    result.formula = formula;
    return result;
}

Block ifFormula(const QString& id, const QString& formula, const QList<Block>& children) {
    auto result = block(id, BlockType::IfFormula, children);
    result.formula = formula;
    return result;
}

Block pressed(const QString& id, ControllerButton button, const QList<Block>& children) {
    auto result = block(id, BlockType::WhenPlayerPresses, children);
    result.button = button;
    return result;
}

SceneItem rectangle(const QString& id, double x, double y, double size = 40) {
    SceneItem item;
    item.id = id;
    item.type = "rectangle";
    item.name = id;
    item.x = x;
    item.y = y;
    item.width = size;
    item.height = size;
    return item;
}

SceneItem hero(const QString& id, int player, double x, double y) {
    auto item = rectangle(id, x, y, 64);
    item.physics = "body";
    item.topDown = true;
    item.gravityScale = 0;
    item.player = player;
    return item;
}

SceneItem mob(const QString& id, int copies, double x, double y) {
    auto item = rectangle(id, x, y, 48);
    item.physics = "body";
    item.gravityScale = 0;
    item.copies = copies;
    return item;
}

SceneItem label(const QString& id, const QString& text, const QString& follows, int copies) {
    SceneItem item;
    item.id = id;
    item.type = "text";
    item.name = id;
    item.text = text;
    item.scale = 2;
    item.follows = follows;
    item.followY = -20;
    item.copies = copies;
    return item;
}

SceneSet oneScene(const QList<SceneItem>& items) {
    Scene scene;
    scene.id = "field";
    scene.name = "Field";
    scene.worldWidth = 4000;
    scene.worldHeight = 3000;
    scene.items = items;
    SceneSet set;
    set.scenes = {scene};
    set.startScene = "field";
    return set;
}

LogicProgram programOf(const QList<LogicVariable>& variables, const QList<Block>& events) {
    LogicProgram program;
    program.variables = variables;
    program.scene = "field";
    program.events = events;
    return program;
}

QStringList problemsOf(const LogicProgram& program, const SceneSet& set) {
    QStringList messages;
    for (const auto& problem : validateGameLogic(program, set)) messages << problem.message;
    return messages;
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

// Compiles the real runtime SceneState with the generated headers, under the game's warning flags and
// sanitizers, and runs `body` inside main with a `scene` object made for it.
HarnessResult runHarness(const SceneSet& set, const LogicProgram& program, const QByteArray& body, const QList<SceneImageData>& images = {}, bool online = false) {
    HarnessResult result;
    const auto sceneHeader = generateSceneHeader(set, images);
    const auto logicHeader = generateLogicHeader(program, set);
    const auto variablesHeader = generateVariablesHeader(program, set);
    if (sceneHeader.isEmpty() || logicHeader.isEmpty() || variablesHeader.isEmpty()) {
        result.output = "invalid project: " + QString::number(sceneHeader.size()) + " " + QString::number(logicHeader.size()) + " " + QString::number(variablesHeader.size()) + " " + problemsOf(program, set).join(" | ") + " " + validateSceneSet(set);
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
    QByteArray main = "#include \"Scene/SceneState.hpp\"\n#include \"Input/Buttons.hpp\"\n#include <cstdint>\n#include <cstdio>\n#include <memory>\n";
    if (online) main += "#include \"Online/OnlinePlay.hpp\"\n#include <string>\n";
    main +=           "using Input = SceneState::PlayerInput;\n"
                      "[[maybe_unused]] static Input press(std::uint64_t button) { return Input{{0.0f, 0.0f}, static_cast<std::uint32_t>(button)}; }\n"
                      "[[maybe_unused]] static void step(SceneState& scene, int frames, Input first = {}, Input second = {}) { for (int i = 0; i < frames; ++i) scene.Update(1.0 / 60.0, {first, second}); }\n"
                      "int main() {\n    auto owned = std::make_unique<SceneState>(1920, 1080);\n    auto& scene = *owned;\n";
    main += body;
    main += "\n    return 0;\n}\n";
    writeBytes(root + "/src/main.cpp", main);

    QStringList arguments{"-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-I" + root + "/include", "-I" + root + "/src", root + "/src/main.cpp", root + "/src/Scene/SceneState.cpp", "-o", root + "/harness"};
    if (online) {
        // Playing online, with this computer's sockets in place of the console's.
        for (const auto& relative : {QString("include/Online/Lan.hpp"), QString("include/Online/Session.hpp"), QString("include/Online/OnlinePlay.hpp"), QString("src/Online/Session.cpp"), QString("src/Online/OnlinePlay.cpp")}) {
            writeBytes(root + '/' + relative, readBytes(runtime + '/' + relative + ".in"));
            if (relative.endsWith(".cpp")) arguments << root + '/' + relative;
        }
        arguments << QString(STUDIO_TEST_SOURCE_DIR) + "/tests/support/LanPosix.cpp";
    }
    const QString sysroot(STUDIO_TEST_SYSROOT);
    if (!sysroot.isEmpty()) arguments << "-isysroot" << sysroot;
    QProcess compiler;
    compiler.setProcessChannelMode(QProcess::MergedChannels);
    compiler.start(STUDIO_TEST_CXX, arguments);
    if (!compiler.waitForFinished(240000)) {
        result.output = "the compiler did not finish: " + compiler.errorString();
        return result;
    }
    result.output = QString::fromUtf8(compiler.readAll());
    if (compiler.exitCode() != 0) {
        result.output += "\n---- GeneratedLogic.hpp ----\n" + QString::fromUtf8(logicHeader);
        return result;
    }
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

}

class TstLanguage : public QObject {
    Q_OBJECT

private slots:
    void formulasCompileToSafeArithmetic() {
        const auto resolver = [](const QString& name) {
            if (name == "hp") return FormulaName{FormulaNameKind::GameVariable, 3};
            if (name == "atk") return FormulaName{FormulaNameKind::PlayerVariable, 1};
            if (name == "level") return FormulaName{FormulaNameKind::CopyVariable, 0};
            if (name == "player") return FormulaName{FormulaNameKind::Player, 0};
            return FormulaName{};
        };
        const auto result = compileFormula("hp - max(1, atk * 2 - level) + rand(1, 6)", resolver);
        QVERIFY2(result.problem.isEmpty(), qPrintable(result.problem));
        QVERIFY(result.code.contains("world.Variable(3)"));
        QVERIFY(result.code.contains("world.PlayerVariable(1)"));
        QVERIFY(result.code.contains("world.CopyVariable(0)"));
        QVERIFY(result.code.contains("world.Random("));
        QVERIFY(result.usesCopyVariables);
        QCOMPARE(result.names, (QStringList{"hp", "atk", "level"}));

        QVERIFY(compileFormula("level >= 3 and not (hp = 0) or player == 2", resolver).problem.isEmpty());
    }

    void badFormulasSayWhatToFix() {
        QVERIFY(compileFormula("").problem.contains("empty"));
        QVERIFY(compileFormula("(1 + 2").problem.contains("never closed"));
        QVERIFY(compileFormula("1 +").problem.contains("ends too soon"));
        QVERIFY(compileFormula("2 $ 3").problem.contains("cannot be used"));
        QVERIFY(compileFormula("power(2, 3)").problem.contains("no function called power"));
        QVERIFY(compileFormula("min(1)").problem.contains("takes 2 numbers"));
        QVERIFY(compileFormula("1 < 2 < 3").problem.contains("compares a comparison"));
        QVERIFY(compileFormula("3x").problem.contains("cannot start with a digit"));
        QVERIFY(compileFormula("99999999999").problem.contains("too large"));
        QVERIFY(compileFormula("ghost + 1", [](const QString&) { return FormulaName{}; }).problem.contains("\"ghost\""));
        QCOMPARE(renameVariableInFormula("hp + hpMax - hp", "hp", "health"), QString("health + hpMax - health"));
    }

    void newBlocksAndScopesRoundTripThroughLogicJson() {
        QTemporaryDir directory;
        const auto path = directory.filePath("logic.json");
        auto spawn = withItem(block("s1", BlockType::SpawnCopy, {formulaBlock("f2", "level", "rand(1, 3)")}), "slime", "gate");
        spawn.x = 10;
        auto wander = withItem(block("w1", BlockType::Wander), "@self");
        wander.value = 80;
        wander.radius = 300;
        auto alert = withItem(block("a1", BlockType::AlertNearby), "slime");
        alert.variable = "angry";
        alert.value = 1;
        alert.radius = 400;
        const auto program = programOf({{"gold", 5}, {"hp", 10, VariableScope::Player}, {"level", 1, VariableScope::Copy}, {"angry", 0, VariableScope::Copy}},
            {block("start", BlockType::WhenGameStarts, {spawn}),
             withItem(block("each", BlockType::EveryFrameForEachCopy, {wander, alert, ifFormula("i1", "angry = 1", {withItem(block("r1", BlockType::RemoveCopy), "@self")}), block("o1", BlockType::Otherwise)}), "slime"),
             pressed("p1", ControllerButton::Cross, {formulaBlock("f1", "hp", "hp - 1")})});
        QString error;
        QVERIFY2(saveLogic(path, program, error), qPrintable(error));
        QVERIFY(readBytes(path).contains("\"schemaVersion\": 6"));
        const auto loaded = loadLogic(path);
        QVERIFY2(loaded.program, qPrintable(loaded.error));
        QCOMPARE(loaded.program->variables.at(1).scope, VariableScope::Player);
        QCOMPARE(loaded.program->variables.at(2).scope, VariableScope::Copy);
        const auto& each = loaded.program->events.at(1);
        QCOMPARE(each.type, BlockType::EveryFrameForEachCopy);
        QCOMPARE(each.children.at(0).radius, 300);
        QCOMPARE(each.children.at(0).value, 80);
        QCOMPARE(each.children.at(2).formula, QString("angry = 1"));
        QCOMPARE(loaded.program->events.at(2).children.at(0).formula, QString("hp - 1"));
        QCOMPARE(loaded.program->events.at(0).children.at(0).otherItem, QString("gate"));
    }

    void anOlderSchemaCannotUseTheNewBlocks() {
        QTemporaryDir directory;
        const auto path = directory.filePath("logic.json");
        writeBytes(path, R"({"schemaVersion": 4, "variables": [], "sounds": [], "scenes": [{"scene": "field", "events": [{"id": "e", "type": "whenPlayerJoins", "actions": []}]}]})");
        const auto loaded = loadLogic(path);
        QVERIFY(!loaded.program);
        QVERIFY2(loaded.error.contains("schemaVersion 5"), qPrintable(loaded.error));
    }

    void copyChoicesOnlyWorkForCopies() {
        const auto set = oneScene({hero("hero", 1, 100, 100), mob("slime", 4, 500, 500), rectangle("gate", 900, 900)});
        auto hideSelf = withItem(block("h1", BlockType::Hide), "@self");
        auto useLevel = formulaBlock("f1", "level", "level + 1");
        auto hideTemplate = withItem(block("h2", BlockType::Hide), "slime");
        auto spawnPlain = withItem(block("s1", BlockType::SpawnCopy), "gate", "hero");
        const auto problems = problemsOf(programOf({{"level", 1, VariableScope::Copy}}, {block("e1", BlockType::EveryFrame, {hideSelf, useLevel, hideTemplate, spawnPlain})}), set);
        QVERIFY2(problems.join("\n").contains("@self, which only means something for a copy"), qPrintable(problems.join("\n")));
        QVERIFY2(problems.join("\n").contains("copy variable"), qPrintable(problems.join("\n")));
        QVERIFY2(problems.join("\n").contains("uses the template \"slime\" directly"), qPrintable(problems.join("\n")));
        QVERIFY2(problems.join("\n").contains("needs a template, but \"gate\" is an ordinary item"), qPrintable(problems.join("\n")));

        const auto inside = problemsOf(programOf({{"level", 1, VariableScope::Copy}}, {withItem(block("e2", BlockType::EveryFrameForEachCopy, {withItem(block("h3", BlockType::Hide), "@self"), formulaBlock("f3", "level", "level + 1")}), "slime")}), set);
        QVERIFY2(inside.isEmpty(), qPrintable(inside.join("\n")));
    }

    void otherwiseMustFollowAnIf() {
        const auto set = oneScene({hero("hero", 1, 100, 100)});
        const auto lonely = problemsOf(programOf({}, {block("e1", BlockType::EveryFrame, {block("o1", BlockType::Otherwise)})}), set);
        QVERIFY2(lonely.join("\n").contains("must come right after an If block"), qPrintable(lonely.join("\n")));
        const auto fine = problemsOf(programOf({}, {block("e2", BlockType::EveryFrame, {ifFormula("i1", "1", {}), block("o2", BlockType::Otherwise)})}), set);
        QVERIFY2(fine.isEmpty(), qPrintable(fine.join("\n")));
    }

    void playerEventsTakeOnlyEveryonesButtons() {
        const auto set = oneScene({hero("hero", 1, 100, 100)});
        const auto problems = problemsOf(programOf({}, {pressed("p1", ControllerButton::P2Cross, {})}), set);
        QVERIFY2(problems.join("\n").contains("without \"Player 2\""), qPrintable(problems.join("\n")));
    }

    void templatesAndFollowersAreCheckedInTheScene() {
        auto lonelyLabel = label("tag", "Lv {level}", "slime", 0);
        QVERIFY(validateSceneSet(oneScene({mob("slime", 3, 0, 0), lonelyLabel})).contains("Give this item 3 copies too"));
        auto self = label("tag", "Lv", "tag", 0);
        QVERIFY(validateSceneSet(oneScene({self})).contains("cannot follow itself"));
        auto controlled = hero("hero", 1, 0, 0);
        controlled.copies = 2;
        QVERIFY(validateSceneSet(oneScene({controlled})).contains("cannot be a template"));
        auto many = mob("slime", MaximumSceneItems, 0, 0);
        many.copies = MaximumCopies;
        QList<SceneItem> items;
        for (int index = 0; index < 5; ++index) {
            auto copy = many;
            copy.id = QString("slime%1").arg(index);
            copy.physics.clear();
            items << copy;
        }
        QVERIFY(validateSceneSet(oneScene(items)).contains("copies of templates included"));

        const auto expanded = expandCopies(oneScene({mob("slime", 3, 0, 0), label("tag", "Lv {level}", "slime", 3)}));
        QCOMPARE(expanded.scenes.first().items.size(), 6);
        QCOMPARE(expanded.scenes.first().items.at(1).id, QString("slime~2"));
        QCOMPARE(expanded.scenes.first().items.at(4).follows, QString("slime~2"));
    }

    void aFollowerFollowsTheItemOfItsOwnScene() {
        // Both scenes have an item called hero; each hat must follow the hero beside it, not the other scene's.
        auto hat = label("hat", "^", "hero", 0);
        Scene first; first.id = "a"; first.name = "A"; first.items = {rectangle("hero", 0, 0), hat};
        Scene second; second.id = "b"; second.name = "B"; second.items = {rectangle("pad", 0, 0), rectangle("hero", 0, 0), hat};
        SceneSet set; set.scenes = {first, second}; set.startScene = "a";
        const auto header = generateSceneHeader(set);
        QVERIFY2(header.contains("{1, 0, 0.0f, -20.0f},"), header.constData());
        QVERIFY2(header.contains("{4, 3, 0.0f, -20.0f},"), header.constData());
    }

    void formulasRunInTheRealRuntime() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        const auto program = programOf({{"a", 7}, {"b", 0}, {"c", 0}, {"d", 0}, {"e", 0}, {"r", 0}, {"big", 0}},
            {block("start", BlockType::WhenGameStarts, {
                formulaBlock("f1", "b", "a * 3 - (a / 2) % 2"),
                formulaBlock("f2", "c", "a / 0 + min(a, 2) + max(a, 100) + abs(-5) + clamp(a, 10, 20)"),
                formulaBlock("f3", "d", "a > 5 and a < 10"),
                formulaBlock("f4", "big", "1000000000 * 1000000000 * 1000000000"),
                ifFormula("i1", "a = 8", {formulaBlock("f5", "e", "1")}),
                block("o1", BlockType::Otherwise, {formulaBlock("f6", "e", "2")}),
                formulaBlock("f7", "r", "rand(3, 5)")})});
        const auto result = runHarness(oneScene({rectangle("dot", 0, 0)}), program,
            "    for (std::size_t i = 0; i < 7; ++i) std::printf(\"%d\\n\", scene.Variable(i));\n"
            "    int low = 100, high = 0; for (int i = 0; i < 400; ++i) { const auto v = scene.Random(3, 5); low = v < low ? int(v) : low; high = v > high ? int(v) : high; }\n"
            "    std::printf(\"%d %d\\n\", low, high);\n");
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.at(0), QString("7"));
        QCOMPARE(result.lines.at(1), QString("20"));  // 21 - (3 % 2)
        QCOMPARE(result.lines.at(2), QString("117")); // 0 + 2 + 100 + 5 + 10
        QCOMPARE(result.lines.at(3), QString("1"));
        QCOMPARE(result.lines.at(4), QString("2"));   // the Otherwise ran
        const auto rolled = result.lines.at(5).toInt();
        QVERIFY(rolled >= 3 && rolled <= 5);
        QCOMPARE(result.lines.at(6), QString("2147483647")); // capped, never overflowing
        QCOMPARE(result.lines.at(7), QString("3 5"));
    }

    void copiesSpawnLiveAndLeaveWithTheirOwnVariables() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        auto spawnA = withItem(block("s1", BlockType::SpawnCopy, {formulaBlock("f1", "level", "2")}), "slime", "gateA");
        auto spawnB = withItem(block("s2", BlockType::SpawnCopy, {formulaBlock("f2", "level", "5")}), "slime", "gateB");
        auto spawnC = withItem(block("s3", BlockType::SpawnCopy), "slime", "gateA");
        auto spawnFull = withItem(block("s4", BlockType::SpawnCopy, {formulaBlock("f3", "spawnedFourth", "1")}), "slime", "gateA");
        auto grow = formulaBlock("f4", "level", "level + 1");
        auto remove = ifFormula("i1", "level >= 4", {block("r1", BlockType::RemoveCopy)});
        auto removeSelf = withItem(remove.children.first(), "@self");
        remove.children = {removeSelf};
        const auto program = programOf({{"spawnedFourth", 0}, {"level", 1, VariableScope::Copy}},
            {block("start", BlockType::WhenGameStarts, {spawnA, spawnB, spawnC, spawnFull}),
             withItem(block("each", BlockType::EveryFrameForEachCopy, {grow, remove}), "slime")});
        const auto set = oneScene({rectangle("gateA", 100, 100), rectangle("gateB", 800, 600), mob("slime", 3, 0, 0), label("tag", "Lv{level}", "slime", 3)});
        const auto result = runHarness(set, program,
            // items: gateA 0, gateB 1, slime 2..4, tag 5..7
            "    std::printf(\"%d\\n\", scene.Variable(0));\n"
            "    auto report = [&] { for (std::size_t i = 2; i <= 4; ++i) { scene.SetSelf(i); std::printf(\"%zu %d %d %d %.0f %.0f %.*s %d\\n\", i, scene.Alive(i) ? 1 : 0, scene.CurrentVisibility()[i] ? 1 : 0, scene.CopyVariable(0), scene.Current()[i][0], scene.Current()[i][1], int(scene.CurrentText(i + 3).size()), scene.CurrentText(i + 3).data(), scene.CurrentVisibility()[i + 3] ? 1 : 0); } scene.SetSelf(scene.NoItem()); };\n"
            "    report();\n"
            "    step(scene, 1);\n"
            "    report();\n"
            "    step(scene, 1);\n"
            "    report();\n");
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.at(0), QString("0")); // every copy was in use, so the fourth spawn ran nothing
        // Before the first frame: spawned at their gates with the levels their spawn blocks gave them.
        QCOMPARE(result.lines.at(1), QString("2 1 1 2 100 100 Lv2 1"));
        QCOMPARE(result.lines.at(2), QString("3 1 1 5 800 600 Lv5 1"));
        QCOMPARE(result.lines.at(3), QString("4 1 1 1 100 100 Lv1 1"));
        // One frame: each grew by one, and the one at 6 removed itself; its label went with it.
        QCOMPARE(result.lines.at(4), QString("2 1 1 3 100 100 Lv3 1"));
        QCOMPARE(result.lines.at(5).section(' ', 0, 2), QString("3 0 0"));
        QCOMPARE(result.lines.at(5).section(' ', -1), QString("0"));
        QCOMPARE(result.lines.at(6), QString("4 1 1 2 100 100 Lv2 1"));
        QCOMPARE(result.lines.at(7), QString("2 0 0 4 100 100 Lv4 0"));
    }

    void eachPlayerHasTheirOwnButtonsVariablesAndCharacter() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        auto joined = block("j1", BlockType::WhenPlayerJoins, {formulaBlock("f1", "joins", "joins + player"), withItem(block("show", BlockType::Show), "@player")});
        auto hit = pressed("p1", ControllerButton::Square, {formulaBlock("f2", "swings", "swings + 1"), formulaBlock("f3", "lastSwing", "player")});
        auto nudge = withItem(block("m1", BlockType::MoveBy), "@player");
        nudge.x = 5;
        auto move = pressed("p2", ControllerButton::Cross, {nudge});
        auto hideTwo = withItem(block("h1", BlockType::Hide), "hero2");
        const auto program = programOf({{"joins", 0}, {"lastSwing", 0}, {"swings", 0, VariableScope::Player}},
            {block("start", BlockType::WhenGameStarts, {hideTwo}), joined, hit, move});
        const auto set = oneScene({hero("hero1", 1, 100, 100), hero("hero2", 2, 300, 100)});
        const auto result = runHarness(set, program,
            "    auto show = [&] { std::printf(\"%d %d\", scene.Variable(0), scene.Variable(1)); for (std::size_t p = 0; p < 2; ++p) { scene.SetCurrentPlayer(p); std::printf(\" %d\", scene.PlayerVariable(0)); } scene.SetCurrentPlayer(0); std::printf(\" %d %d %.0f %.0f\\n\", scene.PlayersHere() == 2 ? 2 : 1, scene.CurrentVisibility()[1] ? 1 : 0, scene.Current()[0][0], scene.Current()[1][0]); };\n"
            "    step(scene, 1);\n"
            "    show();\n" // player 1 joined on the first frame
            "    step(scene, 1, press(Buttons::Square));\n"
            "    show();\n"
            "    step(scene, 1, Input{}, press(Buttons::Square));\n"
            "    show();\n" // player 2 joined with that press, and swung
            "    step(scene, 1, press(Buttons::Cross), press(Buttons::Cross));\n"
            "    show();\n");
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.at(0), QString("1 0 0 0 1 0 100 300"));
        QCOMPARE(result.lines.at(1), QString("1 1 1 0 1 0 100 300"));
        QCOMPARE(result.lines.at(2), QString("3 2 1 1 2 1 100 300"));
        QCOMPARE(result.lines.at(3), QString("3 2 1 1 2 1 105 305"));
    }

    void copiesTouchingPlayersFireOncePerPlayerAndCopy() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        auto touched = withItem(block("t1", BlockType::WhenCopyTouches, {formulaBlock("f1", "hits", "hits + 1"), formulaBlock("f2", "hurt", "hurt + 1")}), "coin", "@player");
        const auto program = programOf({{"hits", 0}, {"hurt", 0, VariableScope::Player}},
            {block("start", BlockType::WhenGameStarts, {withItem(block("s1", BlockType::SpawnCopy), "coin", "spot"), withItem(block("s2", BlockType::SpawnCopy), "coin", "spot")}), touched});
        auto coin = rectangle("coin", 0, 0, 40);
        coin.copies = 2;
        const auto set = oneScene({hero("hero1", 1, 100, 100), hero("hero2", 2, 110, 100), rectangle("spot", 120, 120, 10), coin});
        const auto result = runHarness(set, program,
            "    step(scene, 1, Input{}, press(Buttons::Cross));\n"
            "    step(scene, 5, Input{}, Input{});\n"
            "    std::printf(\"%d\", scene.Variable(0)); for (std::size_t p = 0; p < 2; ++p) { scene.SetCurrentPlayer(p); std::printf(\" %d\", scene.PlayerVariable(0)); } std::printf(\"\\n\");\n");
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.at(0), QString("4 2 2")); // two coins times two players, each once while they keep touching
    }

    void wanderingStaysNearHomeAndFleeingRunsAway() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        auto wander = withItem(block("w1", BlockType::Wander), "@self");
        wander.value = 200;
        wander.radius = 150;
        auto flee = withItem(block("fl", BlockType::Flee), "deer", "hero1");
        flee.value = 300;
        const auto program = programOf({}, {block("start", BlockType::WhenGameStarts, {withItem(block("s1", BlockType::SpawnCopy), "slime", "den")}),
                                            withItem(block("each", BlockType::EveryFrameForEachCopy, {wander}), "slime"),
                                            block("e1", BlockType::EveryFrame, {flee})});
        auto deer = rectangle("deer", 1500, 1000, 40);
        const auto set = oneScene({hero("hero1", 1, 1400, 1000), rectangle("den", 2000, 2000, 10), mob("slime", 1, 0, 0), deer});
        const auto result = runHarness(set, program,
            "    float farthest = 0; float moved = 0; auto last = scene.Current()[2];\n"
            "    for (int i = 0; i < 1200; ++i) { step(scene, 1); const auto p = scene.Current()[2]; const float dx = p[0] - 2000, dy = p[1] - 2000; const float d = dx * dx + dy * dy; farthest = d > farthest ? d : farthest; moved += (p[0] - last[0]) * (p[0] - last[0]) + (p[1] - last[1]) * (p[1] - last[1]); last = p; }\n"
            "    std::printf(\"%d %d %d\\n\", farthest < 200.0f * 200.0f ? 1 : 0, moved > 0 ? 1 : 0, scene.Current()[3][0] > 1700 ? 1 : 0);\n");
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.at(0), QString("1 1 1"));
    }

    void looksPlacesAndLettersWorkInTheRealRuntime() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        // A 4x4 sheet of 16x16 frames holding 2 looks: frames 0 to 7 are the first look, 8 to 15 the second.
        SceneItem sheet;
        sheet.id = "pup";
        sheet.type = "image";
        sheet.name = "Pup";
        sheet.source = "assets/pup.png";
        sheet.x = 400;
        sheet.y = 300;
        sheet.width = 64;
        sheet.height = 64;
        sheet.frameWidth = 16;
        sheet.frameHeight = 16;
        sheet.looks = 2;
        sheet.animations = {{"walk", {1, 2}, 8}};
        sheet.copies = 1;
        SceneItem name;
        name.id = "name";
        name.type = "text";
        name.name = "Name";
        name.text = "{n0:letter}{n1:letter}{n2:letter}!";
        name.scale = 2;
        auto setLook = withItem(block("l1", BlockType::SetLook), "@self");
        setLook.formula = "kind";
        auto spawn = withItem(block("s1", BlockType::SpawnCopy, {formulaBlock("f0", "kind", "1"), setLook, formulaBlock("f1", "sx", "selfX"), formulaBlock("f2", "d", "dist"), formulaBlock("f3", "near", "nearestDist"), formulaBlock("f4", "px", "playerX")}), "pup", "spot");
        const auto program = programOf({{"n0", 1}, {"n1", 27}, {"n2", 0}, {"sx", 0}, {"d", 0}, {"near", 0}, {"px", 0}, {"kind", 0, VariableScope::Copy}},
            {block("start", BlockType::WhenGameStarts, {spawn})});
        QByteArray pixels(64 * 64 * 4, '\x7f');
        const auto set = oneScene({hero("hero", 1, 100, 300), rectangle("spot", 400, 300, 10), sheet, name});
        const auto result = runHarness(set, program,
            // items: hero 0, spot 1, pup 2, name 3
            "    for (std::size_t i = 3; i < 7; ++i) std::printf(\"%d \", scene.Variable(i));\n"
            "    std::printf(\"%u %.*s\\n\", scene.SpriteFrame(2), int(scene.CurrentText(3).size()), scene.CurrentText(3).data());\n",
            {{"assets/pup.png", 64, 64, pixels}});
        QVERIFY2(result.built, qPrintable(result.output));
        // The pup is 64 wide at x 400, so its middle is 432; the hero's middle is at 132, 332; the pup's at 432, 332.
        QCOMPARE(result.lines.at(0), QString("432 300 300 132 9 A0 !"));
    }

    void screenVariablesAndOnlineBlocksRoundTrip() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/logic.json";
        const auto set = oneScene({hero("hero1", 1, 100, 100)});
        const auto program = programOf({{"mode", 0, VariableScope::Screen}, {"gold", 5}},
            {pressed("p1", ControllerButton::Cross, {block("h", BlockType::HostOnline), block("j", BlockType::JoinOnline), block("s", BlockType::StopOnline), formulaBlock("f1", "mode", "seat + online")})});
        QVERIFY2(problemsOf(program, set).isEmpty(), qPrintable(problemsOf(program, set).join(" | ")));
        QString error;
        QVERIFY2(saveLogic(path, program, error), qPrintable(error));
        const auto saved = readBytes(path);
        QVERIFY2(saved.contains("\"scope\": \"screen\""), saved.constData());
        for (const auto* name : {"hostOnline", "joinOnline", "stopOnline"}) QVERIFY2(saved.contains(name), name);
        const auto loaded = loadLogic(path);
        QVERIFY2(loaded.program, qPrintable(loaded.error));
        QCOMPARE(loaded.program->variables[0].scope, VariableScope::Screen);
        QCOMPARE(loaded.program->events[0].children[1].type, BlockType::JoinOnline);

        // A screen variable is kept and saved with the game variables, and marked.
        const auto header = generateVariablesHeader(program, set);
        QVERIFY2(header.contains("VariableCount = 2;"), header.constData());
        QVERIFY2(header.contains("ScreenScoped{{true, false}}"), header.constData());
        QVERIFY2(header.contains("SavedCount = 2;"), header.constData());
        const auto logic = generateLogicHeader(program, set);
        for (const auto* call : {"world.HostOnline();", "world.JoinOnline();", "world.StopOnline();", "world.Seat()", "world.OnlineState()", "world.RestorePlayer(outerPlayer, outerScoped);"}) QVERIFY2(logic.contains(call), call);

        // Both need the version that brought them.
        auto older = saved;
        older.replace("\"schemaVersion\": 6", "\"schemaVersion\": 5");
        writeBytes(path, older);
        const auto refused = loadLogic(path);
        QVERIFY(!refused.program);
        QVERIFY2(refused.error.contains("schemaVersion 6"), qPrintable(refused.error));

        // seat and online are names formulas use, so a variable cannot take them.
        const auto taken = programOf({{"seat", 0}}, {});
        QVERIFY2(problemsOf(taken, set).join(" ").contains("already use"), qPrintable(problemsOf(taken, set).join(" | ")));
    }

    void aFriendsScreenHasItsOwnSceneCameraItemsAndVariables() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        // Two scenes. In the field a HUD square stays on the screen, and Square hides it and sets the screen's mode;
        // Cross walks through to the cave.
        auto set = oneScene({hero("hero1", 1, 100, 100), hero("hero4", 4, 3000, 2000), [] { auto hud = rectangle("hud", 20, 20); hud.screenFixed = true; return hud; }()});
        Scene cave;
        cave.id = "cave";
        cave.name = "Cave";
        cave.worldWidth = 4000;
        cave.worldHeight = 3000;
        cave.items = {hero("chero1", 1, 100, 100), hero("chero4", 4, 500, 500)};
        set.scenes.append(cave);

        auto follow = withItem(block("cf", BlockType::CameraFollow), "@player");
        auto joined = block("j1", BlockType::WhenPlayerJoins, {formulaBlock("f1", "joins", "joins + 1"), follow, formulaBlock("fs", "seatSeen", "seat")});
        auto square = pressed("p1", ControllerButton::Square, {formulaBlock("f2", "mode", "player * 10"), withItem(block("hh", BlockType::Hide), "hud")});
        auto go = block("go", BlockType::GoToScene);
        go.scene = "cave";
        auto cross = pressed("p2", ControllerButton::Cross, {go});
        auto program = programOf({{"mode", 0, VariableScope::Screen}, {"joins", 0}, {"starts", 0}, {"ticks", 0}, {"seatSeen", 0}, {"here", 0}},
            {joined, square, cross, block("ef", BlockType::EveryFrame, {formulaBlock("fh", "here", "players")})});
        SceneLogic caveLogic;
        caveLogic.scene = "cave";
        caveLogic.events = {block("cs", BlockType::WhenGameStarts, {formulaBlock("f3", "starts", "starts + 1")}), block("ce", BlockType::EveryFrame, {formulaBlock("f4", "ticks", "ticks + 1")}),
                            block("cj", BlockType::WhenPlayerJoins, {formulaBlock("f5", "joins", "joins + 100")})};
        program.otherScenes = {caveLogic};
        QVERIFY2(problemsOf(program, set).isEmpty(), qPrintable(problemsOf(program, set).join(" | ")));

        const auto result = runHarness(set, program,
            "    auto mirror = std::make_unique<SceneState>(1920, 1080);\n"
            "    static std::uint8_t bytes[SceneState::ScreenBytes];\n"
            "    auto send = [&] { const auto size = scene.WriteScreen(1, bytes); return size != 0 && mirror->ShowScreen({bytes, size}); };\n"
            "    auto modeOf = [&](std::size_t player) { scene.SetCurrentPlayer(player); const auto value = scene.Variable(0); scene.RestorePlayer(0, false); return value; };\n"
            "    step(scene, 1);\n"
            "    const auto screen = scene.OpenScreen();\n"
            "    std::printf(\"%zu %zu %zu\\n\", screen, scene.ScreenPlayer(screen), scene.ScreensOpen());\n"
            "    step(scene, 1);\n" // the friend joins in the field
            "    std::printf(\"%d %d %d\\n\", scene.Variable(1), scene.Variable(4), scene.Variable(5));\n"
            "    scene.SetRemoteInput(screen, press(Buttons::Square), 0);\n"
            "    step(scene, 1);\n" // the friend hides their HUD and sets their mode
            "    std::printf(\"%d %d %d %d\\n\", modeOf(0), modeOf(3), scene.CurrentVisibility()[2] ? 1 : 0, send() ? 1 : 0);\n"
            "    std::printf(\"%d %zu %.0f %.0f\\n\", mirror->CurrentVisibility()[2] ? 1 : 0, mirror->CurrentScene(), mirror->Camera().x, scene.Camera().x);\n"
            "    scene.SetRemoteInput(screen, press(Buttons::Cross), 0);\n"
            "    step(scene, 1);\n" // the friend leaves for the cave, which starts
            "    scene.SetRemoteInput(screen, Input{}, 0);\n"
            "    step(scene, 3);\n"
            "    std::printf(\"%zu %zu %d %d %d %d %d\\n\", scene.ScreenScene(0), scene.ScreenScene(screen), scene.Variable(2), scene.Variable(3), scene.Variable(5), scene.CurrentVisibility()[1] ? 1 : 0, send() ? 1 : 0);\n"
            "    std::printf(\"%zu %d\\n\", mirror->CurrentScene(), mirror->Mirroring() ? 1 : 0);\n"
            "    step(scene, 1, press(Buttons::Cross));\n" // the host follows: the cave is not started again, the host arrives
            "    step(scene, 2);\n"
            "    std::printf(\"%zu %d %d %d\\n\", scene.ScreenScene(0), scene.Variable(2), scene.Variable(1), scene.Variable(5));\n"
            "    scene.CloseScreen(screen);\n"
            "    step(scene, 1);\n"
            "    std::printf(\"%zu %d\\n\", scene.ScreensOpen(), scene.CurrentVisibility()[4] ? 1 : 0);\n");
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.at(0), QString("1 3 2"));     // screen 1, for player 4
        QCOMPARE(result.lines.at(1), QString("2 1 2"));     // both joined; the friend sits in seat 1 of their screen; 2 players here
        QCOMPARE(result.lines.at(2), QString("0 40 1 1"));  // the mode and the hidden HUD are the friend's alone
        QCOMPARE(result.lines.at(3), QString("0 0 2072 0")); // their console sees it hidden, and their camera on hero4
        QCOMPARE(result.lines.at(4), QString("0 1 1 3 1 0 1")); // two scenes run at once; hero4 left the field
        QCOMPARE(result.lines.at(5), QString("1 1"));
        QCOMPARE(result.lines.at(6), QString("1 1 102 1")); // the cave did not start again: the host arrived in it. The empty field stands still
        QCOMPARE(result.lines.at(7), QString("1 0"));       // the friend's character goes with them
    }

    void twoConsolesPlayOneGameOverTheNetwork() {
        if (!hasPosixCompiler()) QSKIP("This compiles the runtime with a GCC style compiler.");
        // One program on two consoles. Triangle hosts, Circle joins. Square adds to the pressing player's own score
        // and to the gold everyone shares; Cross moves their character. A label on the screen shows the score of
        // the screen's own player.
        auto scoreLabel = rectangle("score", 40, 40);
        scoreLabel.type = "text";
        scoreLabel.text = "S{score@1}";
        scoreLabel.scale = 4;
        scoreLabel.screenFixed = true;
        const auto set = oneScene({hero("hero1", 1, 100, 100), hero("hero4", 4, 3000, 2000), scoreLabel});
        auto nudge = withItem(block("m1", BlockType::MoveBy), "@player");
        nudge.x = 7;
        auto sendHome = pressed("p5", ControllerButton::Options, {ifFormula("ig", "guest = 1", {block("so", BlockType::StopOnline)})});
        const auto program = programOf({{"gold", 0}, {"state", 0}, {"score", 0, VariableScope::Player}},
            {pressed("p1", ControllerButton::Triangle, {block("h", BlockType::HostOnline)}), pressed("p2", ControllerButton::Circle, {block("j", BlockType::JoinOnline)}),
             pressed("p3", ControllerButton::Square, {formulaBlock("f1", "score", "score + 1"), formulaBlock("f2", "gold", "gold + 10")}), pressed("p4", ControllerButton::Cross, {nudge}), sendHome,
             block("ef", BlockType::EveryFrame, {formulaBlock("f3", "state", "online")}), block("jn", BlockType::WhenPlayerJoins, {withItem(block("cf", BlockType::CameraFollow), "@player")})});
        QVERIFY2(problemsOf(program, set).isEmpty(), qPrintable(problemsOf(program, set).join(" | ")));

        const auto result = runHarness(set, program,
            "    auto other = std::make_unique<SceneState>(1920, 1080);\n"
            "    auto& guest = *other;\n"
            "    const std::uint16_t gamePort = 47711, findPort = 47710;\n"
            "    OnlinePlay hostPlay({77, 1, gamePort, findPort, Lan::Loopback});\n"
            "    OnlinePlay guestPlay({77, 2, gamePort, findPort, Lan::Loopback});\n"
            "    std::uint64_t now = 1000;\n"
            "    auto frame = [&](Input hostPad = {}, Input guestPad = {}) {\n"
            "        now += 16;\n"
            "        hostPlay.BeforeFrame(scene, now, hostPad); scene.Update(1.0 / 60.0, {hostPad, Input{}}); hostPlay.AfterFrame(scene);\n"
            "        guestPlay.BeforeFrame(guest, now, guestPad); guest.Update(1.0 / 60.0, {guest.Mirroring() ? Input{} : guestPad, Input{}});\n"
            "    };\n"
            "    auto until = [&](auto done) { for (int tries = 0; tries < 600 && !done(); ++tries) frame(); return done(); };\n"
            "    frame(press(Buttons::Triangle));\n"
            "    frame({}, press(Buttons::Circle));\n"
            "    frame();\n"
            "    std::printf(\"%d %d\\n\", scene.Variable(1), guest.Variable(1));\n" // hosting, looking
            "    const bool joined = until([&] { return guest.Mirroring() && scene.ScreensOpen() == 2; });\n"
            "    std::printf(\"%d %lld %lld %zu\\n\", joined ? 1 : 0, static_cast<long long>(scene.OnlineState()), static_cast<long long>(guest.OnlineState()), scene.ScreenPlayer(1));\n"
            "    until([&] { return guest.Camera().x > 2000.0f; });\n"
            "    std::printf(\"%.0f %.0f %.0f\\n\", guest.Camera().x, guest.Current()[1][0], scene.Camera().x);\n" // the friend's camera is on their own character
            "    frame({}, press(Buttons::Square)); frame(); \n" // one tap on the friend's pad
            "    until([&] { return guest.CurrentText(2) == \"S1\"; });\n"
            "    std::printf(\"%d %s %s\\n\", scene.Variable(0), std::string(guest.CurrentText(2)).c_str(), std::string(scene.CurrentText(2)).c_str());\n"
            "    for (int i = 0; i < 5; ++i) { frame({}, press(Buttons::Cross)); frame(); }\n" // five taps move the friend's character on the host
            "    until([&] { return guest.Current()[1][0] >= 3035.0f; });\n"
            "    std::printf(\"%.0f %.0f %.0f\\n\", scene.Current()[1][0], guest.Current()[1][0], scene.Current()[0][0]);\n"
            "    frame({}, press(Buttons::Options)); \n" // a block sends the friend home
            "    const bool home = until([&] { return !guest.Mirroring() && scene.ScreensOpen() == 1; });\n"
            "    frame();\n"
            "    std::printf(\"%d %lld %lld %d\\n\", home ? 1 : 0, static_cast<long long>(scene.OnlineState()), static_cast<long long>(guest.OnlineState()), scene.CurrentVisibility()[1] ? 1 : 0);\n",
            {}, true);
        QVERIFY2(result.built, qPrintable(result.output));
        QCOMPARE(result.lines.at(0), QString("1 2"));
        QCOMPARE(result.lines.at(1), QString("1 1 3 3"));       // joined: the host hosts, the friend plays, as player 4
        QCOMPARE(result.lines.at(2), QString("2072 3000 0"));
        QCOMPARE(result.lines.at(3), QString("10 S1 S0"));      // the tap reached the host once; each screen shows its own player's score
        QCOMPARE(result.lines.at(4), QString("3035 3035 100")); // five taps, five moves, seen on both consoles
        QCOMPARE(result.lines.at(5), QString("1 1 0 0"));       // the friend is back at their own start; the host still hosts
    }

    void aSaveKeepsGameVariablesFirstThenEachPlayers() {
        const auto set = oneScene({hero("hero1", 1, 100, 100)});
        const auto program = programOf({{"gold", 3}, {"hp", 10, VariableScope::Player}, {"tag", 0, VariableScope::Copy}, {"quest", 1}}, {block("start", BlockType::WhenGameStarts, {block("sv", BlockType::SaveVariables)})});
        const auto header = generateVariablesHeader(program, set);
        QVERIFY2(header.contains("VariableCount = 2;"), header.constData());
        QVERIFY2(header.contains("PlayerVariableCount = 1;"), header.constData());
        QVERIFY2(header.contains("CopyVariableCount = 1;"), header.constData());
        QVERIFY2(header.contains("SavedCount = 6;"), header.constData());
        QVERIFY2(header.contains("SavedInitialValues{{3, 1, 10, 10, 10, 10}}"), header.constData());
    }
};

QTEST_GUILESS_MAIN(TstLanguage)
#include "tst_language.moc"
