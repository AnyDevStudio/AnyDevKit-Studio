#include "BuildRunner.h"
#include "CodeEditors.h"
#include "Project.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

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
};

QTEST_GUILESS_MAIN(CoreTests)
#include "tst_core.moc"
