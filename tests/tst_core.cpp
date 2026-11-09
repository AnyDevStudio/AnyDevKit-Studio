#include "BuildRunner.h"
#include "CodeEditors.h"
#include "NewGame.h"
#include "Project.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
};

QTEST_GUILESS_MAIN(CoreTests)
#include "tst_core.moc"
