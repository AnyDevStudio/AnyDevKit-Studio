#include "BuildRunner.h"
#include "CodeEditors.h"
#include "ExportFormat.h"
#include "NewGame.h"
#include "Project.h"
#include "Scene.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

using namespace Studio;

namespace {

void writeJson(const QString& path, const QJsonObject& object) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(object).toJson());
}

void writeText(const QString& path, const QByteArray& text = "x") {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

// Mirrors what `adk init` generates (AnyDevKit ProjectCommand.cpp).
void writeProject(const QString& root, const QString& output = "dist/ps5/Demo.ffpfsc", int schema = 1) {
    writeJson(root + "/adk.json", QJsonObject{
        {"schemaVersion", schema},
        {"name", "Demo"},
        {"targets", QJsonObject{{"ps5", QJsonObject{
            {"input", "build/ps5/Demo.elf"},
            {"app0", "app0"},
            {"param", "app0/sce_sys/param.json"},
            {"output", output},
            {"workspace", "build/package"},
            {"format", "ffpfsc"},
        }}}},
    });
    writeJson(root + "/app0/sce_sys/param.json", QJsonObject{
        {"titleId", "FAKE12345"},
        {"contentVersion", "01.000.000"},
        {"localizedParameters", QJsonObject{
            {"defaultLanguage", "en-US"},
            {"en-US", QJsonObject{{"titleName", "Demo Game"}}},
        }},
    });
    writeJson(root + "/CMakePresets.json", QJsonObject{
        {"version", 3},
        {"configurePresets", QJsonArray{QJsonObject{
            {"name", "ps5"},
            {"cacheVariables", QJsonObject{{"AnyDevKit_DIR", "/opt/adk/lib/cmake/AnyDevKit"}}},
        }}},
    });
}

BuildStep cmakeStep(const QString& id, const QStringList& arguments) {
    return BuildStep{id, id, QStringLiteral(STUDIO_TEST_CMAKE), arguments, {}};
}

// A complete, throwaway key set. Random bytes of the sizes adk validates: never usable on a console.
PkgKeys writeKeyFiles(const QString& folder) {
    writeText(folder + "/moduli.bin", QByteArray(7 * 384, 'a'));
    writeText(folder + "/mount.bin", QByteArray(384, 'b'));
    return {folder + "/moduli.bin", folder + "/mount.bin", "00112233445566778899aabbccddeeff", "ffeeddccbbaa99887766554433221100"};
}

// The parts of an SDK and of an `adk setup` folder that a new game relies on.
void writeSdk(const QString& root) {
    writeText(adkExecutable(root));
    QVERIFY(QFile::setPermissions(adkExecutable(root), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    QVERIFY(QDir().mkpath(root + "/lib/cmake/AnyDevKit"));
}

void writeDependencies(const QString& root) {
    QVERIFY(QDir().mkpath(root + "/ps5/sysroot"));
}

struct NewGameFolders {
    QTemporaryDir base;
    NewGameRequest request;

    NewGameFolders() {
        request.name = "Demo";
        request.parentDirectory = base.path() + "/games";
        request.sdkRoot = base.path() + "/sdk";
        request.dependencyRoot = base.path() + "/deps";
        QDir().mkpath(request.parentDirectory);
        writeSdk(request.sdkRoot);
        writeDependencies(request.dependencyRoot);
    }
};

}

class CoreTests : public QObject {
    Q_OBJECT

private slots:
    void loadsGeneratedProject() {
        QTemporaryDir directory;
        writeProject(directory.path());
        const auto result = loadProject(directory.path());
        QVERIFY2(result.project.has_value(), qPrintable(result.error));
        QCOMPARE(result.project->name, QString("Demo"));
        QCOMPARE(result.project->titleId, QString("FAKE12345"));
        QCOMPARE(result.project->titleName, QString("Demo Game"));
        QCOMPARE(result.project->contentVersion, QString("01.000.000"));
        QCOMPARE(result.project->sdkRoot, QString("/opt/adk"));
        QCOMPARE(result.project->outputPath, QDir(directory.path()).filePath("dist/ps5/Demo.ffpfsc"));
        QCOMPARE(result.project->workspacePath, QDir(directory.path()).filePath("build/package"));
    }

    void explainsFolderThatIsNotAProject() {
        QTemporaryDir directory;
        const auto result = loadProject(directory.path());
        QVERIFY(!result.project.has_value());
        QVERIFY2(result.error.contains("adk.json"), qPrintable(result.error));
    }

    void rejectsUnsupportedSchema() {
        QTemporaryDir directory;
        writeProject(directory.path(), "dist/ps5/Demo.ffpfsc", 2);
        const auto result = loadProject(directory.path());
        QVERIFY(!result.project.has_value());
        QVERIFY2(result.error.contains("schemaVersion"), qPrintable(result.error));
    }

    void rejectsOutputOutsideProject() {
        QTemporaryDir directory;
        writeProject(directory.path(), "../../escape.ffpfsc");
        const auto result = loadProject(directory.path());
        QVERIFY(!result.project.has_value());
        QVERIFY2(result.error.contains("output"), qPrintable(result.error));
    }

    void plansPs5BuildInOrder() {
        QTemporaryDir directory;
        writeProject(directory.path());
        const auto project = *loadProject(directory.path()).project;
        const auto steps = ps5BuildSteps(project);
        QCOMPARE(steps.size(), 3);
        QCOMPARE(steps[0].id, QString("configure"));
        QCOMPARE(steps[0].arguments, QStringList({"--preset", "ps5"}));
        QCOMPARE(steps[1].id, QString("compile"));
        QCOMPARE(steps[1].arguments, QStringList({"--build", "--preset", "ps5"}));
        QCOMPARE(steps[2].id, QString("package"));
        QCOMPARE(steps[2].program, adkExecutable(project));
        QVERIFY(steps[2].program.startsWith("/opt/adk/bin/adk"));
        QCOMPARE(steps[2].arguments, QStringList({"package", "--target", "ps5"}));
        QCOMPARE(steps[2].removeFirst, QStringList({project.outputPath, project.workspacePath}));
    }

    void listsSourcesAndSkipsBuildOutput() {
        QTemporaryDir directory;
        writeText(directory.path() + "/src/Main.cpp");
        writeText(directory.path() + "/build/ps5/x.o");
        writeText(directory.path() + "/dist/ps5/x.ffpfsc");
        writeText(directory.path() + "/.git/config");
        writeText(directory.path() + "/.studio-backup/20260101-000000/src/Main.cpp");
        const auto files = listProjectFiles(directory.path());
        QStringList paths;
        for (const auto& file : files) paths << file.relativePath;
        QCOMPARE(paths, QStringList({"src", "src/Main.cpp"}));
        QCOMPARE(files[0].depth, 0);
        QVERIFY(files[0].isDirectory);
        QCOMPARE(files[1].depth, 1);
        QCOMPARE(files[1].name, QString("Main.cpp"));
    }

    void detectsPathsOutsideDirectory() {
        QVERIFY(isInsideDirectory("/a/b/c", "/a/b"));
        QVERIFY(!isInsideDirectory("/a/b", "/a/b"));
        QVERIFY(!isInsideDirectory("/a/bc/d", "/a/b"));
        QVERIFY(!isInsideDirectory("/a/b/../../x", "/a/b"));
    }

    void prependsToolDirectoriesToPath() {
        QProcessEnvironment environment;
        environment.insert("PATH", "/usr/bin");
        const auto result = withToolDirectories(environment, {"/first", "/second"});
        QCOMPARE(result.value("PATH"), QString("/first") + QDir::listSeparator() + "/second" + QDir::listSeparator() + "/usr/bin");
    }

    void findsEditorsOnPath() {
        QTemporaryDir directory;
#ifdef Q_OS_WIN
        const QString fake = directory.path() + "/code.exe";
#else
        const QString fake = directory.path() + "/code";
#endif
        writeText(fake);
        QFile::setPermissions(fake, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        const auto editors = detectCodeEditors({{directory.path()}, {}, {}});
        QCOMPARE(editors.size(), 1);
        QCOMPARE(editors[0].id, QString("vscode"));
        QCOMPARE(editors[0].name, QString("VS Code"));
        QCOMPARE(editors[0].program, fake);
    }

    void findsMacApplications() {
        QTemporaryDir directory;
        QVERIFY(QDir().mkpath(directory.path() + "/Visual Studio Code.app"));
        QVERIFY(QDir().mkpath(directory.path() + "/Xcode.app"));
        const auto editors = detectCodeEditors({{}, {directory.path()}, {}});
        QCOMPARE(editors.size(), 2);
        QCOMPARE(editors[0].id, QString("vscode"));
        QCOMPARE(editors[0].program, QString("open"));
        QCOMPARE(editors[0].launcherArguments, QStringList({"-a", directory.path() + "/Visual Studio Code.app"}));
        QCOMPARE(editors[1].id, QString("xcode"));
    }

    void findsVisualStudioOfAnyYearAndEdition() {
        QTemporaryDir directory;
        const auto devenv = directory.path() + "/Microsoft Visual Studio/2022/Community/Common7/IDE/devenv.exe";
        writeText(devenv);
        const auto editors = detectCodeEditors({{}, {}, {directory.path()}});
        QCOMPARE(editors.size(), 1);
        QCOMPARE(editors[0].id, QString("visualstudio"));
        QCOMPARE(editors[0].name, QString("Visual Studio"));
        QCOMPARE(editors[0].program, devenv);
    }

    void listsEachEditorOnce() {
        QTemporaryDir directory;
#ifdef Q_OS_WIN
        const QString fake = directory.path() + "/code.exe";
#else
        const QString fake = directory.path() + "/code";
#endif
        writeText(fake);
        QFile::setPermissions(fake, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QVERIFY(QDir().mkpath(directory.path() + "/Visual Studio Code.app"));
        QCOMPARE(detectCodeEditors({{directory.path()}, {directory.path()}, {}}).size(), 1);
    }

    void opensFileInsideTheProjectFolder() {
        const CodeEditor vscode{"vscode", "VS Code", "/bin/code", {}};
        const auto launch = editorLaunch(vscode, "/game", "/game/src/Main.cpp");
        QCOMPARE(launch.program, QString("/bin/code"));
        QCOMPARE(launch.arguments, QStringList({"/game", "/game/src/Main.cpp"}));
        QCOMPARE(editorLaunch(vscode, "/game").arguments, QStringList({"/game"}));
    }

    void visualStudioUsesItsEditSwitchForFiles() {
        const CodeEditor visualStudio{"visualstudio", "Visual Studio", "C:/VS/devenv.exe", {}};
        QCOMPARE(editorLaunch(visualStudio, "C:/game", "C:/game/a.cpp").arguments, QStringList({"/Edit", "C:/game/a.cpp"}));
        QCOMPARE(editorLaunch(visualStudio, "C:/game").arguments, QStringList({"C:/game"}));
    }

    void macApplicationsKeepTheirLauncherArguments() {
        const CodeEditor editor{"vscode", "VS Code", "open", {"-a", "/Applications/Visual Studio Code.app"}};
        const auto launch = editorLaunch(editor, "/game", "/game/a.cpp");
        QCOMPARE(launch.program, QString("open"));
        QCOMPARE(launch.arguments, QStringList({"-a", "/Applications/Visual Studio Code.app", "/game", "/game/a.cpp"}));
    }

    void newProjectsDefaultToAFolderInDocuments() {
        QCOMPARE(defaultProjectsFolder("/home/ada"), QString("/home/ada/Documents/AnyDevKit Projects"));
    }

    void suggestsTheNextFreeProjectName() {
        QTemporaryDir directory;
        QCOMPARE(suggestProjectName(directory.path(), "game"), QString("MyFirstGame"));
        QVERIFY(QDir(directory.path()).mkdir("MyFirstGame"));
        QCOMPARE(suggestProjectName(directory.path(), "game"), QString("MyFirstGame1"));
        QVERIFY(QDir(directory.path()).mkdir("MyFirstGame1"));
        QVERIFY(QDir(directory.path()).mkdir("MyFirstGame2"));
        QCOMPARE(suggestProjectName(directory.path(), "game"), QString("MyFirstGame3"));
        QCOMPARE(suggestProjectName(directory.path(), "app"), QString("MyFirstApp"));
        QCOMPARE(suggestProjectName(directory.path(), "project"), QString("MyFirstProject"));
    }

    void aMissingFolderStillGetsAProjectName() {
        QCOMPARE(suggestProjectName("/does/not/exist", "game"), QString("MyFirstGame"));
    }

    void runsStepsAndForwardsOutput() {
        QTemporaryDir directory;
        BuildRunner runner;
        QSignalSpy lines(&runner, &BuildRunner::outputLine);
        QSignalSpy started(&runner, &BuildRunner::stepStarted);
        QSignalSpy finished(&runner, &BuildRunner::finished);
        runner.start({cmakeStep("one", {"-E", "echo", "first"}), cmakeStep("two", {"-E", "echo", "second"})}, directory.path(), QProcessEnvironment::systemEnvironment());
        QVERIFY(finished.wait(20000));
        QCOMPARE(finished.at(0).at(0).toBool(), true);
        QCOMPARE(started.size(), 2);
        QStringList text;
        for (const auto& line : lines) text << line.at(0).toString();
        QCOMPARE(text, QStringList({"first", "second"}));
        QVERIFY(!runner.isRunning());
    }

    void stopsAtFirstFailedStep() {
        QTemporaryDir directory;
        BuildRunner runner;
        QSignalSpy started(&runner, &BuildRunner::stepStarted);
        QSignalSpy step(&runner, &BuildRunner::stepFinished);
        QSignalSpy finished(&runner, &BuildRunner::finished);
        runner.start({cmakeStep("bad", {"-E", "false"}), cmakeStep("never", {"-E", "echo", "x"})}, directory.path(), QProcessEnvironment::systemEnvironment());
        QVERIFY(finished.wait(20000));
        QCOMPARE(finished.at(0).at(0).toBool(), false);
        QCOMPARE(started.size(), 1);
        QCOMPARE(step.at(0).at(1).toBool(), false);
        QVERIFY2(finished.at(0).at(1).toString().contains("bad"), qPrintable(finished.at(0).at(1).toString()));
    }

    void removesStaleOutputsBeforeStep() {
        QTemporaryDir directory;
        const auto file = directory.path() + "/dist/ps5/Demo.ffpfsc";
        const auto workspace = directory.path() + "/build/package";
        writeText(file);
        writeText(workspace + "/app0/eboot.bin");
        auto step = cmakeStep("package", {"-E", "true"});
        step.removeFirst = {file, workspace};
        BuildRunner runner;
        QSignalSpy finished(&runner, &BuildRunner::finished);
        runner.start({step}, directory.path(), QProcessEnvironment::systemEnvironment());
        QVERIFY(finished.wait(20000));
        QCOMPARE(finished.at(0).at(0).toBool(), true);
        QVERIFY(!QFile::exists(file));
        QVERIFY(!QFile::exists(workspace));
    }

    void refusesToRemoveOutsideProject() {
        QTemporaryDir project;
        QTemporaryDir other;
        const auto precious = other.path() + "/precious.txt";
        writeText(precious);
        auto step = cmakeStep("package", {"-E", "true"});
        step.removeFirst = {precious};
        BuildRunner runner;
        QSignalSpy finished(&runner, &BuildRunner::finished);
        runner.start({step}, project.path(), QProcessEnvironment::systemEnvironment());
        QVERIFY(finished.wait(20000));
        QCOMPARE(finished.at(0).at(0).toBool(), false);
        QVERIFY(QFile::exists(precious));
    }

    void exposesTheGameFilesFolders() {
        QTemporaryDir directory;
        writeProject(directory.path());
        const auto result = loadProject(directory.path());
        QVERIFY(result.project);
        QCOMPARE(result.project->app0Path, QDir::cleanPath(directory.path() + "/app0"));
        QCOMPARE(result.project->paramPath, QDir::cleanPath(directory.path() + "/app0/sce_sys/param.json"));
    }

    void explainsMissingProgram() {
        QTemporaryDir directory;
        BuildRunner runner;
        QSignalSpy finished(&runner, &BuildRunner::finished);
        runner.start({BuildStep{"configure", "Configure", "definitely-not-installed-tool", {}, {}}}, directory.path(), QProcessEnvironment::systemEnvironment());
        QVERIFY(finished.wait(20000));
        QCOMPARE(finished.at(0).at(0).toBool(), false);
        QVERIFY2(finished.at(0).at(1).toString().contains("definitely-not-installed-tool"), qPrintable(finished.at(0).at(1).toString()));
    }

    void cancelStopsTheBuild() {
        QTemporaryDir directory;
        BuildRunner runner;
        QSignalSpy finished(&runner, &BuildRunner::finished);
        runner.start({cmakeStep("sleep", {"-E", "sleep", "30"}), cmakeStep("never", {"-E", "echo", "x"})}, directory.path(), QProcessEnvironment::systemEnvironment());
        QVERIFY(runner.isRunning());
        QTest::qWait(200);
        runner.cancel();
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.at(0).at(0).toBool(), false);
        QVERIFY(finished.at(0).at(1).toString().contains("Cancelled"));
    }

    void gameNameFollowsAdkInit_data() {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("mention");

        // Accepted by the real `adk init`, including near misses of the reserved names.
        for (const auto* name : {"MyGame", "a", "_x", "__x", "Ab_9", "test", "package", "class", "main", "ALL", "Install", "Clean", "Help", "all_", "install2"}) {
            QTest::newRow(name) << QString(name) << QString();
        }
        QTest::newRow("exactly 64 characters") << QString(64, 'a') << QString();

        QTest::newRow("empty") << QString() << "name";
        QTest::newRow("65 characters") << QString(65, 'a') << "64";
        QTest::newRow("starts with a digit") << "9Lives" << "digit";
        QTest::newRow("space") << "My Game" << "letters";
        QTest::newRow("hyphen") << "My-Game" << "letters";
        QTest::newRow("dot") << "Game.v2" << "letters";
        QTest::newRow("slash") << "../Game" << "letters";
        QTest::newRow("turkish letter") << QString::fromUtf8("Oyun\xC4\x9F") << "letters";
        QTest::newRow("accented first letter") << QString::fromUtf8("\xC3\x9Cnal") << "letters";

        for (const auto* name : {"all", "ALL_BUILD", "help", "install", "INSTALL", "preinstall", "clean", "edit_cache", "rebuild_cache", "ZERO_CHECK"}) {
            QTest::newRow(name) << QString(name) << "CMake";
        }
    }

    void gameNameFollowsAdkInit() {
        QFETCH(QString, name);
        QFETCH(QString, mention);
        const auto problem = validateGameName(name);
        if (mention.isEmpty()) {
            QVERIFY2(problem.isEmpty(), qPrintable(problem));
        } else {
            QVERIFY2(problem.contains(mention, Qt::CaseInsensitive), qPrintable(QString("Expected '%1' in: %2").arg(mention, problem)));
        }
    }

    void adkInitStepMatchesTheCommandLine() {
        const NewGameRequest request{"Demo", "/games/my stuff", "/sdk", "/home/.adk"};
        const auto step = adkInitStep(request);
        QCOMPARE(step.program, adkExecutable(QString("/sdk")));
        QCOMPARE(step.arguments, QStringList({"init", "Demo", "--deps", "/home/.adk", "--directory", "/games/my stuff/Demo"}));
        QVERIFY(!step.title.isEmpty());
        QVERIFY2(step.removeFirst.isEmpty(), "Creating a game must never delete anything.");
    }

    void newGameDirectoryIsCleaned() {
        QCOMPARE(newGameDirectory({"Demo", "/games/", "/sdk", "/deps"}), QString("/games/Demo"));
        QCOMPARE(newGameDirectory({"Demo", "/games/./a/../", "/sdk", "/deps"}), QString("/games/Demo"));
        QCOMPARE(adkInitStep({"Demo", "/games/", "/sdk/", "/deps/"}).arguments.last(), QString("/games/Demo"));
        QCOMPARE(adkInitStep({"Demo", "/games", "/sdk/", "/deps/"}).program, adkExecutable(QString("/sdk")));
        QCOMPARE(adkInitStep({"Demo", "/games", "/sdk/", "/deps/"}).arguments.at(3), QString("/deps"));
    }

    void projectTypesAreOfferedInOrder() {
        QStringList ids;
        QStringList titles;
        for (const auto& type : projectTypes()) {
            ids << type.id;
            titles << type.title;
            QVERIFY2(!type.description.isEmpty(), qPrintable(type.id));
            QVERIFY2(!type.description.contains('\n'), qPrintable(type.description));
            QVERIFY2(type.description.size() <= 56, qPrintable("Too long for one line on the card at the minimum window: " + type.description));
            QVERIFY2(type.description.endsWith('.'), qPrintable(type.description));
            QVERIFY2(!type.noun.isEmpty(), qPrintable(type.id));
        }
        QCOMPARE(ids, QStringList({"2d", "app", "code", "3d"}));
        QCOMPARE(titles, QStringList({"2D game", "App", "Code only", "3D game"}));
        QCOMPARE(projectTypes().first().type, ProjectType::Game2D);
        QCOMPARE(defaultProjectType(), ProjectType::Game2D);
    }

    void onlyThreeDIsNotAvailableYet() {
        for (const auto& type : projectTypes()) {
            QCOMPARE(type.available, type.type != ProjectType::Game3D);
            if (type.available) QVERIFY2(type.note.isEmpty(), qPrintable(type.id));
        }
        const auto threeD = projectTypes().last();
        QVERIFY2(threeD.note.contains("later", Qt::CaseInsensitive), qPrintable(threeD.note));
        QVERIFY2(threeD.description.contains("no 3D", Qt::CaseInsensitive), qPrintable(threeD.description));
    }

    void projectTypeIdsTurnBackIntoTypes() {
        for (const auto& type : projectTypes()) QCOMPARE(projectTypeFromId(type.id), std::optional(type.type));
        QCOMPARE(projectTypeFromId("nope"), std::optional<ProjectType>{});
        QCOMPARE(projectTypeFromId(""), std::optional<ProjectType>{});
    }

    void eachTypeRunsOnlyItsSteps() {
        QCOMPARE(newGameSteps(ProjectType::Game2D), QList<NewGameStep>({NewGameStep::AdkInit, NewGameStep::AddSceneRuntime}));
        QCOMPARE(newGameSteps(ProjectType::App), QList<NewGameStep>({NewGameStep::AdkInit, NewGameStep::AddSceneRuntime}));
        QCOMPARE(newGameSteps(ProjectType::CodeOnly), QList<NewGameStep>({NewGameStep::AdkInit}));
        QVERIFY(newGameSteps(ProjectType::Game3D).isEmpty());
    }

    void eachTypeOpensOnItsTab() {
        QCOMPARE(openingTab(ProjectType::Game2D), QString("scene"));
        QCOMPARE(openingTab(ProjectType::App), QString("scene"));
        QCOMPARE(openingTab(ProjectType::CodeOnly), QString("build"));
    }

    void onlyTypesWithARuntimeHaveAValidStarterScene() {
        for (const auto& type : projectTypes()) {
            const auto scene = starterSceneFor(type.type);
            QCOMPARE(scene.has_value(), newGameSteps(type.type).contains(NewGameStep::AddSceneRuntime));
            if (!scene) continue;
            const auto problem = validateScene(*scene);
            QVERIFY2(problem.isEmpty(), qPrintable(type.id + ": " + problem));
            QVERIFY2(!generateSceneHeader(*scene).isEmpty(), qPrintable(type.id));
        }
        QVERIFY(!starterSceneFor(ProjectType::CodeOnly).has_value());
        QVERIFY(!starterSceneFor(ProjectType::Game3D).has_value());
    }

    void twoDGameKeepsTheExistingStarterScene() {
        const auto scene = starterSceneFor(ProjectType::Game2D);
        QVERIFY(scene.has_value());
        QCOMPARE(generateSceneHeader(*scene), generateSceneHeader(starterScene()));
        const auto controllable = std::ranges::count_if(scene->items, &SceneItem::controllable);
        QCOMPARE(controllable, 1);
    }

    void threeDGameIsRefusedBeforeAnythingRuns() {
        NewGameFolders folders;
        folders.request.type = ProjectType::Game3D;
        const auto problem = validateNewGame(folders.request);
        QVERIFY2(problem.contains("later", Qt::CaseInsensitive), qPrintable(problem));
    }

    void otherTypesAreAccepted() {
        for (const auto type : {ProjectType::Game2D, ProjectType::App, ProjectType::CodeOnly}) {
            NewGameFolders folders;
            folders.request.type = type;
            const auto problem = validateNewGame(folders.request);
            QVERIFY2(problem.isEmpty(), qPrintable(problem));
        }
    }

    void everyTypeStartsWithTheSameAdkInit() {
        const NewGameRequest game{"Demo", "/games", "/sdk", "/deps"};
        for (const auto type : {ProjectType::App, ProjectType::CodeOnly}) {
            auto request = game;
            request.type = type;
            QCOMPARE(adkInitStep(request).program, adkInitStep(game).program);
            QCOMPARE(adkInitStep(request).arguments, adkInitStep(game).arguments);
        }
    }

    void messagesNameWhatIsBeingMade() {
        QCOMPARE(validateGameName(""), QString("Give your game a name."));
        QCOMPARE(validateGameName("", "app"), QString("Give your app a name."));
        QCOMPARE(validateParentDirectory(""), QString("Choose the folder to create the game in."));
        QCOMPARE(validateParentDirectory("", "project"), QString("Choose the folder to create the project in."));
        QVERIFY(describeAdkInitFailure({"adk: nope"}, "x").startsWith("The game could not be created."));
        QVERIFY(describeAdkInitFailure({"adk: nope"}, "x", "app").startsWith("The app could not be created."));
    }

    void sdkRootMustHoldAdk() {
        QTemporaryDir base;
        QVERIFY(validateSdkRoot("").contains("SDK"));

        const auto missing = validateSdkRoot(base.path() + "/nowhere");
        QVERIFY2(missing.contains(base.path() + "/nowhere") && missing.contains("exist"), qPrintable(missing));

        const auto empty = validateSdkRoot(base.path());
        QVERIFY2(empty.contains("bin/adk"), qPrintable(empty));

        writeSdk(base.path() + "/good");
        const auto good = validateSdkRoot(base.path() + "/good");
        QVERIFY2(good.isEmpty(), qPrintable(good));

#ifndef Q_OS_WIN
        writeSdk(base.path() + "/noexec");
        QVERIFY(QFile::setPermissions(adkExecutable(base.path() + "/noexec"), QFile::ReadOwner | QFile::WriteOwner));
        const auto notRunnable = validateSdkRoot(base.path() + "/noexec");
        QVERIFY2(notRunnable.contains("run", Qt::CaseInsensitive), qPrintable(notRunnable));
#endif

        writeText(adkExecutable(base.path() + "/partial"));
        QVERIFY(QFile::setPermissions(adkExecutable(base.path() + "/partial"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const auto partial = validateSdkRoot(base.path() + "/partial");
        QVERIFY2(partial.contains("lib/cmake/AnyDevKit"), qPrintable(partial));
    }

    void dependencyRootMustHoldThePs5Sysroot() {
        QTemporaryDir base;
        QVERIFY(validateDependencyRoot("").contains("dependency", Qt::CaseInsensitive));

        const auto missing = validateDependencyRoot(base.path() + "/nowhere");
        QVERIFY2(missing.contains("exist"), qPrintable(missing));

        const auto empty = validateDependencyRoot(base.path());
        QVERIFY2(empty.contains("adk setup"), qPrintable(empty));

        writeDependencies(base.path() + "/good");
        const auto good = validateDependencyRoot(base.path() + "/good");
        QVERIFY2(good.isEmpty(), qPrintable(good));

        // A likely mistake: picking the ps5 folder inside it.
        const auto inner = validateDependencyRoot(base.path() + "/good/ps5");
        QVERIFY2(inner.contains("parent", Qt::CaseInsensitive), qPrintable(inner));
    }

    void parentDirectoryMustBeAWritableFolder() {
        QTemporaryDir base;
        QVERIFY(!validateParentDirectory("").isEmpty());

        const auto relative = validateParentDirectory("games/mine");
        QVERIFY2(relative.contains("full path"), qPrintable(relative));

        const auto missing = validateParentDirectory(base.path() + "/nowhere");
        QVERIFY2(missing.contains("exist"), qPrintable(missing));

        writeText(base.path() + "/file");
        const auto file = validateParentDirectory(base.path() + "/file");
        QVERIFY2(file.contains("not a folder"), qPrintable(file));

        const auto good = validateParentDirectory(base.path());
        QVERIFY2(good.isEmpty(), qPrintable(good));

#ifndef Q_OS_WIN
        QVERIFY(QDir().mkpath(base.path() + "/locked"));
        QVERIFY(QFile::setPermissions(base.path() + "/locked", QFile::ReadOwner | QFile::ExeOwner));
        const auto locked = validateParentDirectory(base.path() + "/locked");
        QFile::setPermissions(base.path() + "/locked", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        if (::geteuid() != 0) QVERIFY2(locked.contains("write", Qt::CaseInsensitive), qPrintable(locked));
#endif
    }

    void newGameIsAcceptedWhenEverythingIsReady() {
        NewGameFolders folders;
        const auto problem = validateNewGame(folders.request);
        QVERIFY2(problem.isEmpty(), qPrintable(problem));
    }

    void newGameReportsEachBadInput() {
        {
            NewGameFolders folders;
            folders.request.name = "all";
            QVERIFY(validateNewGame(folders.request).contains("CMake"));
        }
        {
            NewGameFolders folders;
            folders.request.parentDirectory = folders.base.path() + "/nowhere";
            QVERIFY(validateNewGame(folders.request).contains("exist"));
        }
        {
            NewGameFolders folders;
            folders.request.sdkRoot = folders.base.path();
            QVERIFY(validateNewGame(folders.request).contains("bin/adk"));
        }
        {
            NewGameFolders folders;
            folders.request.dependencyRoot = folders.base.path();
            QVERIFY(validateNewGame(folders.request).contains("adk setup"));
        }
    }

    void newGameRefusesAnExistingDestination() {
        NewGameFolders folders;
        QVERIFY(QDir().mkpath(newGameDirectory(folders.request)));
        const auto folder = validateNewGame(folders.request);
        QVERIFY2(folder.contains("already exists") && folder.contains("Demo"), qPrintable(folder));

        NewGameFolders fileFolders;
        writeText(newGameDirectory(fileFolders.request));
        QVERIFY(validateNewGame(fileFolders.request).contains("already exists"));

#ifndef Q_OS_WIN
        NewGameFolders linkFolders;
        QVERIFY(QFile::link(linkFolders.base.path() + "/gone", newGameDirectory(linkFolders.request)));
        QVERIFY2(validateNewGame(linkFolders.request).contains("already exists"), "A dangling link still blocks adk init.");
#endif
    }

    void adkInitFailureSaysWhatHappenedAndWhatToDo() {
        const auto reported = describeAdkInitFailure({"note", "adk: Output parent does not exist: /x", ""}, "Create game failed with exit code 1. The log has the details.");
        QVERIFY2(reported.contains("Output parent does not exist: /x"), qPrintable(reported));
        QVERIFY2(!reported.contains("adk: "), qPrintable(reported));
        QVERIFY2(!reported.contains("log"), "The welcome screen has no log to point at.");
        QVERIFY2(reported.contains("try again"), qPrintable(reported));

        const auto silent = describeAdkInitFailure({}, "Create game failed with exit code 3. The log has the details.");
        QVERIFY2(silent.contains("exit code 3"), qPrintable(silent));
        QVERIFY2(!silent.contains("log"), qPrintable(silent));
        QVERIFY2(silent.contains("try again"), qPrintable(silent));

        const auto notStarted = describeAdkInitFailure({"   "}, "Create game could not be started: permission denied");
        QVERIFY2(notStarted.contains("permission denied"), qPrintable(notStarted));
    }

    void exportFormatIdsRoundTrip() {
        QCOMPARE(exportFormats().size(), 3);
        QVERIFY(exportFormats().first().format == ExportFormat::Ffpfsc);
        for (const auto& info : exportFormats()) QVERIFY(exportFormatFromId(info.id) == info.format);
        QVERIFY(!exportFormatFromId("rar"));
        QCOMPARE(exportFormatInfo(ExportFormat::Ffpfsc).buildLabel, QString("Build PS5 package"));
        QCOMPARE(exportFormatInfo(ExportFormat::Zip).buildLabel, QString("Export ZIP"));
        QCOMPARE(exportFormatInfo(ExportFormat::Pkg).buildLabel, QString("Build PKG"));
    }

    void ffpfscExportIsTodaysBuild() {
        QTemporaryDir directory;
        writeProject(directory.path());
        const auto project = *loadProject(directory.path()).project;
        const auto steps = exportBuildSteps(project, ExportFormat::Ffpfsc);
        const auto today = ps5BuildSteps(project);
        QCOMPARE(steps.size(), today.size());
        for (qsizetype index = 0; index < steps.size(); ++index) {
            QCOMPARE(steps[index].id, today[index].id);
            QCOMPARE(steps[index].program, today[index].program);
            QCOMPARE(steps[index].arguments, today[index].arguments);
            QCOMPARE(steps[index].removeFirst, today[index].removeFirst);
            QVERIFY(steps[index].workingDirectory.isEmpty());
            QVERIFY(steps[index].secrets.isEmpty());
        }
        QVERIFY(exportOutputPath(project, ExportFormat::Ffpfsc).endsWith("/dist/ps5/Demo.ffpfsc"));
    }

    void zipExportPacksThePreparedApp0() {
        QTemporaryDir directory;
        writeProject(directory.path());
        const auto project = *loadProject(directory.path()).project;
        const auto zip = exportOutputPath(project, ExportFormat::Zip);
        QVERIFY(zip.endsWith("/dist/ps5/Demo.zip"));
        const auto steps = exportBuildSteps(project, ExportFormat::Zip);
        QCOMPARE(steps.size(), 4);
        QCOMPARE(steps[2].id, QString("package"));
        QCOMPARE(steps[2].program, adkExecutable(project));

        // The by-product archive stays out of dist so the export folder only holds what was chosen.
        const auto intermediateIndex = steps[2].arguments.indexOf("--output") + 1;
        QCOMPARE(intermediateIndex, 4);
        const auto intermediate = steps[2].arguments[intermediateIndex];
        QVERIFY(isInsideDirectory(intermediate, directory.path() + "/build"));
        QVERIFY(!intermediate.contains("/dist/"));
        QCOMPARE(steps[2].removeFirst, QStringList({zip, intermediate, project.workspacePath}));

        QCOMPARE(steps[3].id, QString("zip"));
        QCOMPARE(steps[3].program, QString("cmake"));
        QCOMPARE(steps[3].arguments, QStringList({"-E", "tar", "cf", zip, "--format=zip", "."}));
        QCOMPARE(steps[3].workingDirectory, project.workspacePath + "/app0");
        QCOMPARE(steps[3].createFirst, QStringList({QFileInfo(zip).absolutePath()}));
        QVERIFY(steps[3].secrets.isEmpty());
    }

    void zipStepPutsPreparedFilesAtTheZipRoot() {
        QTemporaryDir directory;
        writeProject(directory.path());
        const auto project = *loadProject(directory.path()).project;
        writeText(project.workspacePath + "/app0/eboot.bin", "eboot");
        writeText(project.workspacePath + "/app0/sce_sys/param.json", "{}");
        const auto zipSteps = exportBuildSteps(project, ExportFormat::Zip);
        QVERIFY(!zipSteps.isEmpty());
        auto step = zipSteps.last();
        step.program = QStringLiteral(STUDIO_TEST_CMAKE);
        BuildRunner runner;
        QSignalSpy finished(&runner, &BuildRunner::finished);
        runner.start({step}, directory.path(), QProcessEnvironment::systemEnvironment());
        QVERIFY(finished.wait(20000));
        QVERIFY2(finished.at(0).at(0).toBool(), qPrintable(finished.at(0).at(1).toString()));

        QProcess list;
        list.start(QStringLiteral(STUDIO_TEST_CMAKE), {"-E", "tar", "tf", exportOutputPath(project, ExportFormat::Zip)});
        QVERIFY(list.waitForFinished(20000));
        const auto entries = QString::fromUtf8(list.readAll()).split('\n', Qt::SkipEmptyParts);
        QVERIFY2(entries.contains("eboot.bin"), qPrintable(entries.join(' ')));
        QVERIFY2(entries.contains("sce_sys/param.json"), qPrintable(entries.join(' ')));
        QVERIFY2(!entries.join('\n').contains("app0"), qPrintable(entries.join(' ')));
    }

    void pkgExportPassesTheProvidedKeys() {
        QTemporaryDir directory;
        writeProject(directory.path());
        const auto project = *loadProject(directory.path()).project;
        const auto keys = writeKeyFiles(directory.path());
        const auto output = exportOutputPath(project, ExportFormat::Pkg);
        QVERIFY(output.endsWith("/dist/ps5/Demo.pkg"));
        const auto steps = exportBuildSteps(project, ExportFormat::Pkg, keys);
        QCOMPARE(steps.size(), 3);
        QCOMPARE(steps[2].program, adkExecutable(project));
        QCOMPARE(steps[2].arguments, QStringList({"package", "--target", "ps5", "--format", "pkg", "--output", output, "--passcode-moduli", keys.passcodeModuliPath, "--mount-modulus", keys.mountModulusPath, "--passcode", keys.passcode, "--image-seed", keys.imageSeed}));
        QCOMPARE(steps[2].removeFirst, QStringList({output, project.workspacePath}));
        QCOMPARE(steps[2].secrets, QStringList({keys.passcode, keys.imageSeed}));
    }

    void describedStepsNeverShowSecrets() {
        QTemporaryDir directory;
        writeProject(directory.path());
        const auto project = *loadProject(directory.path()).project;
        const auto keys = writeKeyFiles(directory.path());
        const auto pkgSteps = exportBuildSteps(project, ExportFormat::Pkg, keys);
        const auto ffpfscSteps = exportBuildSteps(project, ExportFormat::Ffpfsc);
        QVERIFY(!pkgSteps.isEmpty());
        QVERIFY(!ffpfscSteps.isEmpty());
        const auto shown = describeBuildStep(pkgSteps.last());
        QVERIFY2((shown.startsWith("adk package --target ps5 --format pkg") || shown.startsWith("adk.exe package --target ps5 --format pkg")), qPrintable(shown));
        QVERIFY2(!shown.contains(keys.passcode), qPrintable(shown));
        QVERIFY2(!shown.contains(keys.imageSeed), qPrintable(shown));
        QVERIFY2(shown.contains("--passcode <hidden>"), qPrintable(shown));
        QVERIFY2(shown.contains("--image-seed <hidden>"), qPrintable(shown));
        QVERIFY2(shown.contains(keys.passcodeModuliPath), "Key file paths are not secret and help when a build fails.");
        QVERIFY2(describeBuildStep(ffpfscSteps.last()).endsWith("adk package --target ps5"), "A step without secrets is shown as it runs.");
    }

    void redactsSecretsWhereverTheyAppear() {
        const QString secret = "00112233445566778899aabbccddeeff";
        QCOMPARE(redactSecrets("a 00112233445566778899aabbccddeeff b", {secret}), QString("a <hidden> b"));
        QCOMPARE(redactSecrets("x 00112233445566778899AABBCCDDEEFF y", {secret}), QString("x <hidden> y"));
        QCOMPARE(redactSecrets("twice 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff", {secret}), QString("twice <hidden><hidden>"));
        QCOMPARE(redactSecrets("nothing here", {}), QString("nothing here"));
        QCOMPARE(redactSecrets("nothing here", {QString()}), QString("nothing here"));
    }

    void runnerHidesSecretsInToolOutput() {
        QTemporaryDir directory;
        const QString secret = "00112233445566778899aabbccddeeff";
        auto step = cmakeStep("echo", {"-E", "echo", "passcode=" + secret});
        step.secrets = {secret};
        BuildRunner runner;
        QSignalSpy output(&runner, &BuildRunner::outputLine);
        QSignalSpy finished(&runner, &BuildRunner::finished);
        runner.start({step}, directory.path(), QProcessEnvironment::systemEnvironment());
        QVERIFY(finished.wait(20000));
        QVERIFY(!output.isEmpty());
        for (const auto& line : output) QVERIFY2(!line.at(0).toString().contains(secret), qPrintable(line.at(0).toString()));
        QVERIFY2(output.at(0).at(0).toString().contains("<hidden>"), qPrintable(output.at(0).at(0).toString()));
    }

    void runsStepsInTheirWorkingDirectoryAndCreatesFolders() {
        QTemporaryDir directory;
        writeText(directory.path() + "/sub/here.txt");
        auto step = cmakeStep("touch", {"-E", "touch", "made-here.txt"});
        step.workingDirectory = directory.path() + "/sub";
        step.createFirst = {directory.path() + "/dist/ps5"};
        BuildRunner runner;
        QSignalSpy finished(&runner, &BuildRunner::finished);
        runner.start({step}, directory.path(), QProcessEnvironment::systemEnvironment());
        QVERIFY(finished.wait(20000));
        QVERIFY2(finished.at(0).at(0).toBool(), qPrintable(finished.at(0).at(1).toString()));
        QVERIFY(QFile::exists(directory.path() + "/sub/made-here.txt"));
        QVERIFY(QFileInfo(directory.path() + "/dist/ps5").isDir());
    }

    void refusesToRunOrCreateOutsideProject() {
        QTemporaryDir project;
        QTemporaryDir other;
        for (const auto usesCreate : {false, true}) {
            auto step = cmakeStep("touch", {"-E", "touch", "made.txt"});
            if (usesCreate) step.createFirst = {other.path() + "/made-folder"};
            else step.workingDirectory = other.path();
            BuildRunner runner;
            QSignalSpy finished(&runner, &BuildRunner::finished);
            runner.start({step}, project.path(), QProcessEnvironment::systemEnvironment());
            QVERIFY(finished.wait(20000));
            QVERIFY2(!finished.at(0).at(0).toBool(), "A step must not run or create folders outside the project.");
        }
        QVERIFY(!QFile::exists(other.path() + "/made.txt"));
        QVERIFY(!QFile::exists(other.path() + "/made-folder"));
    }

    void exportPathsStayInsideTheProject() {
        QTemporaryDir directory;
        writeProject(directory.path());
        auto project = *loadProject(directory.path()).project;
        QVERIFY2(checkExportPaths(project, ExportFormat::Zip).isEmpty(), qPrintable(checkExportPaths(project, ExportFormat::Zip)));
        QVERIFY2(checkExportPaths(project, ExportFormat::Pkg).isEmpty(), qPrintable(checkExportPaths(project, ExportFormat::Pkg)));

        for (const auto name : {"../../escaped", "nested/name", "dir\\name", "..", ""}) {
            project.name = QString::fromLatin1(name);
            const auto problem = checkExportPaths(project, ExportFormat::Zip);
            QVERIFY2(!problem.isEmpty(), name);
            QVERIFY2(problem.contains("name"), qPrintable(problem));
        }
    }

    void exportPathsRefuseLinksOutOfTheProject() {
#ifdef Q_OS_UNIX
        QTemporaryDir directory;
        QTemporaryDir other;
        writeProject(directory.path());
        const auto project = *loadProject(directory.path()).project;
        QVERIFY(QFile::link(other.path(), directory.path() + "/dist"));
        const auto zipProblem = checkExportPaths(project, ExportFormat::Zip);
        QVERIFY2(zipProblem.contains("dist"), qPrintable(zipProblem));
        QVERIFY2(zipProblem.contains("link"), qPrintable(zipProblem));
        QVERIFY2(checkExportPaths(project, ExportFormat::Ffpfsc).isEmpty(), "FFPFSC keeps today's checks.");

        QVERIFY(QFile::remove(directory.path() + "/dist"));
        QVERIFY(QFile::link(other.path(), directory.path() + "/build"));
        const auto workspaceProblem = checkExportPaths(project, ExportFormat::Pkg);
        QVERIFY2(workspaceProblem.contains("build"), qPrintable(workspaceProblem));
#else
        QSKIP("Links need a Unix file system in this test.");
#endif
    }

    void pkgKeyProblemsNameEverythingMissing() {
        const auto problems = pkgKeyProblems({});
        QCOMPARE(problems.size(), 4);
        const auto text = problems.join(' ');
        QVERIFY2(text.contains("passcode moduli"), qPrintable(text));
        QVERIFY2(text.contains("mount modulus"), qPrintable(text));
        QVERIFY2(text.contains("passcode"), qPrintable(text));
        QVERIFY2(text.contains("image seed"), qPrintable(text));
        const auto message = describePkgKeyProblems(problems);
        QVERIFY2(message.contains("PKG"), qPrintable(message));
        QVERIFY2(message.contains("does not include"), qPrintable(message));
        QVERIFY2(message.contains("Code and build"), "The message must say where to add the keys.");
        QVERIFY(describePkgKeyProblems({}).isEmpty());
    }

    void pkgKeyProblemsCheckFileSizesAndValues() {
        QTemporaryDir directory;
        const auto keys = writeKeyFiles(directory.path());
        QVERIFY2(pkgKeyProblems(keys).isEmpty(), qPrintable(pkgKeyProblems(keys).join(' ')));

        auto wrongSize = keys;
        writeText(directory.path() + "/short.bin", QByteArray(383, 'c'));
        wrongSize.passcodeModuliPath = directory.path() + "/short.bin";
        wrongSize.mountModulusPath = directory.path() + "/short.bin";
        const auto sizeProblems = pkgKeyProblems(wrongSize);
        QCOMPARE(sizeProblems.size(), 2);
        QVERIFY2(sizeProblems[0].contains("2688"), qPrintable(sizeProblems[0]));
        QVERIFY2(sizeProblems[1].contains("384"), qPrintable(sizeProblems[1]));

        auto missingFile = keys;
        missingFile.mountModulusPath = directory.path() + "/gone.bin";
        QVERIFY2(pkgKeyProblems(missingFile).join(' ').contains("not found"), "A missing key file should be named as missing.");

        auto badValues = keys;
        badValues.passcode = "zz112233445566778899aabbccddeeff";
        badValues.imageSeed = "00112233";
        const auto valueProblems = pkgKeyProblems(badValues);
        QCOMPARE(valueProblems.size(), 2);
        QVERIFY2(valueProblems[0].contains("32 hexadecimal"), qPrintable(valueProblems[0]));
        QVERIFY2(!valueProblems.join(' ').contains(badValues.passcode), "Problem text must not repeat the secret.");
        QVERIFY2(!valueProblems.join(' ').contains(badValues.imageSeed), "Problem text must not repeat the secret.");
    }
};

QTEST_GUILESS_MAIN(CoreTests)
#include "tst_core.moc"
