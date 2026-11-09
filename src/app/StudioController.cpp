#include "StudioController.h"

#include "Blocks.h"
#include "SceneExport.h"
#include "SceneImageImport.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>

#include <algorithm>

namespace Studio {

namespace {

constexpr int MaximumLogLines = 20000;
constexpr auto SdkRootSetting = "adk/sdkRoot";
constexpr auto ProjectsFolderSetting = "newGame/projectsFolder";
constexpr auto DependencyRootSetting = "adk/dependencyRoot";

}

StudioController::StudioController(QObject* parent) : QObject(parent) {
    detectedEditors = detectCodeEditors(hostEditorLocations());
    const auto saved = QSettings().value("codeEditor", "system").toString();
    if (std::ranges::any_of(detectedEditors, [&](const CodeEditor& editor) { return editor.id == saved; })) editorId = saved;
    savedSdkRoot = QSettings().value(SdkRootSetting).toString();
    savedDependencyRoot = QSettings().value(DependencyRootSetting).toString();

    connect(&gameCreator, &BuildRunner::outputLine, this, [this](const QString& line) { gameCreatorOutput << line; });
    connect(&gameCreator, &BuildRunner::finished, this, &StudioController::finishCreatingGame);

    connect(&runner, &BuildRunner::stepStarted, this, [this](int index) {
        setStageState(index, "running");
        const auto step = ps5BuildSteps(*project).at(index);
        appendLog({"$ " + QFileInfo(step.program).fileName() + ' ' + step.arguments.join(' ')});
    });
    connect(&runner, &BuildRunner::stepFinished, this, [this](int index, bool success) { setStageState(index, success ? "done" : "failed"); });
    connect(&runner, &BuildRunner::outputLine, this, [this](const QString& line) { appendLog({line}); });
    connect(&runner, &BuildRunner::finished, this, [this](bool success, const QString& text) {
        message = text;
        state = success ? "done" : text.startsWith("Cancelled") ? "cancelled" : "failed";
        const QFileInfo output(project ? project->outputPath : QString());
        size = success && output.exists() ? QLocale::system().formattedDataSize(output.size(), 1, QLocale::DataSizeSIFormat) : QString();
        emit buildChanged();
        emit buildFinished(success);
    });

    const auto refreshLogicScene = [this] { logicDocument.setScene(sceneDocument.scene()); };
    connect(&sceneDocument, &QAbstractItemModel::modelReset, this, refreshLogicScene);
    connect(&sceneDocument, &QAbstractItemModel::rowsInserted, this, refreshLogicScene);
    connect(&sceneDocument, &QAbstractItemModel::rowsRemoved, this, refreshLogicScene);
    connect(&sceneDocument, &QAbstractItemModel::dataChanged, this, refreshLogicScene);
    // The stage and the inspector show the logic's variables, and text follows a rename.
    connect(&logicDocument, &LogicDocument::programChanged, this, [this] { sceneDocument.setVariables(logicDocument.program().variables); });
    connect(&logicDocument, &LogicDocument::variableRenamed, &sceneDocument, &SceneDocument::renameVariable);

    connect(this, &StudioController::buildChanged, &gamePageDocument, [this] { gamePageDocument.setBuilding(runner.isRunning()); });
    connect(&gamePageDocument, &GamePageDocument::titleSaved, this, [this](const QString& title) {
        if (!project) return;
        project->titleName = title;
        emit projectChanged();
    });
    connect(&gamePageDocument, &GamePageDocument::filesChanged, this, [this] {
        refreshFiles();
        emit projectChanged();
    });
}

bool StudioController::hasProject() const { return project.has_value(); }
QString StudioController::projectName() const { return project ? project->name : QString(); }
QString StudioController::titleName() const { return project ? project->titleName : QString(); }
QString StudioController::titleId() const { return project ? project->titleId : QString(); }
QString StudioController::contentVersion() const { return project ? project->contentVersion : QString(); }
QVariantList StudioController::files() const { return fileEntries; }
QString StudioController::openError() const { return lastOpenError; }
bool StudioController::building() const { return runner.isRunning(); }
QString StudioController::buildState() const { return state; }
QString StudioController::buildMessage() const { return message; }
QVariantList StudioController::stages() const { return stageEntries; }
QString StudioController::outputName() const { return project && !size.isEmpty() ? QFileInfo(project->outputPath).fileName() : QString(); }
QString StudioController::outputSize() const { return size; }
QStringListModel* StudioController::log() { return &logModel; }
SceneDocument* StudioController::scene() { return &sceneDocument; }
LogicDocument* StudioController::logic() { return &logicDocument; }
GamePageDocument* StudioController::gamePage() { return &gamePageDocument; }

void StudioController::addSceneEditor() {
    if (!project || runner.isRunning()) return;
    QString error;
    if (!installSceneRuntime(":/runtime/SceneGame", project->root, project->name, error)) {
        sceneDocument.setError(error);
        return;
    }
    sceneDocument.open(project->root + "/scene.json");
    openLogic();
    refreshFiles();
    emit projectChanged();
}

void StudioController::addSceneImage(const QUrl& file, double centerX, double centerY) {
    if (!project || !sceneDocument.isLoaded()) return;
    if (const auto room = sceneDocument.roomProblem(); !room.isEmpty()) {
        sceneDocument.setNotice(room, true);
        return;
    }
    ImportedSceneImage image;
    QString problem;
    if (!importSceneImage(project->root, sceneDocument.scene(), file.toLocalFile(), image, problem)) {
        sceneDocument.setNotice(problem, true);
        return;
    }
    if (sceneDocument.addImage(image.source, image.width, image.height, image.name, centerX, centerY) < 0) return;
    sceneDocument.setNotice(image.notice, false);
    refreshFiles();
    emit projectChanged();
}

void StudioController::replaceSceneImage(int index, const QUrl& file) {
    if (!project || !sceneDocument.isLoaded() || index < 0 || index >= sceneDocument.count()) return;
    // The picture being replaced no longer counts towards the scene's image memory.
    auto others = sceneDocument.scene();
    others.items.removeAt(index);
    ImportedSceneImage image;
    QString problem;
    if (!importSceneImage(project->root, others, file.toLocalFile(), image, problem)) {
        sceneDocument.setNotice(problem, true);
        return;
    }
    if (!sceneDocument.replaceImage(index, image.source, image.width, image.height)) return;
    sceneDocument.setNotice(image.notice, false);
    refreshFiles();
    emit projectChanged();
}

void StudioController::openLogic() {
    if (!project || !sceneDocument.isLoaded()) {
        logicDocument.close();
        return;
    }
    logicDocument.open(project->root + "/logic.json");
    logicDocument.setScene(sceneDocument.scene());
}

void StudioController::refreshFiles() {
    fileEntries.clear();
    if (!project) return;
    for (const auto& file : listProjectFiles(project->root)) {
        fileEntries.append(QVariantMap{{"name", file.name}, {"path", file.relativePath}, {"depth", file.depth}, {"isDirectory", file.isDirectory}});
    }
}

bool StudioController::exportScene() {
    if (!sceneDocument.isLoaded()) return true;
    QString problem;
    if (!Studio::exportScene(project->root, sceneDocument.scene(), problem)) {
        state = "failed";
        message = problem;
        emit buildChanged();
        return false;
    }
    if (!exportLogicHeader(project->root, sceneDocument.scene(), problem)) {
        state = "failed";
        message = problem;
        emit buildChanged();
        // Only a block can be shown on the Logic tab; a text problem stays on the build page, named.
        if (logicDocument.problemCount() > 0) emit logicBlockedBuild();
        return false;
    }

    // A project that started with an older Studio needs game code that knows about variables.
    if (usesVariables() && sceneRuntimeNeedsUpdate(project->root)) {
        if (!installSceneRuntime(":/runtime/SceneGame", project->root, project->name, problem)) {
            state = "failed";
            message = problem;
            emit buildChanged();
            return false;
        }
        appendLog({"Updated this project's game code to show variables. Its previous files are in .studio-backup."});
        refreshFiles();
    }
    return true;
}

bool StudioController::usesVariables() const {
    if (!logicDocument.program().variables.isEmpty()) return true;
    return std::ranges::any_of(sceneDocument.scene().items, [](const SceneItem& item) { return item.type == "text" && textUsesVariables(item.text); });
}

bool StudioController::openFolder(const QUrl& folder) {
    return openPath(folder.toLocalFile());
}

bool StudioController::openPath(const QString& path, const QString& tab) {
    if (runner.isRunning() || gameCreator.isRunning()) return false;
    const auto result = loadProject(path);
    lastOpenError = result.error;
    emit openErrorChanged();
    if (!result.project) return false;

    project = result.project;
    currentOpeningTab = tab;
    refreshFiles();
    sceneDocument.open(project->root + "/scene.json");
    openLogic();
    gamePageDocument.open(project->root, project->app0Path, project->paramPath, project->titleName);
    resetBuild();
    emit projectChanged();
    return true;
}

void StudioController::closeProject() {
    if (runner.isRunning()) return;
    project.reset();
    fileEntries.clear();
    sceneDocument.close();
    logicDocument.close();
    gamePageDocument.close();
    resetBuild();
    emit projectChanged();
}

void StudioController::resetBuild() {
    state = "idle";
    message.clear();
    size.clear();
    logModel.setStringList({});
    stageEntries.clear();
    if (project) {
        for (const auto& step : ps5BuildSteps(*project)) stageEntries.append(QVariantMap{{"title", step.title}, {"state", "waiting"}});
    }
    emit buildChanged();
}

void StudioController::build() {
    if (!project || runner.isRunning() || gamePageDocument.isBusy()) return;
    resetBuild();
    if (!exportScene()) {
        emit buildFinished(false);
        return;
    }
    state = "running";
    runner.start(ps5BuildSteps(*project), project->root, withToolDirectories(QProcessEnvironment::systemEnvironment(), hostToolDirectories()));
    emit buildChanged();
}

void StudioController::cancelBuild() {
    runner.cancel();
}

void StudioController::setStageState(int index, const QString& newState) {
    auto stage = stageEntries.at(index).toMap();
    stage["state"] = newState;
    stageEntries[index] = stage;
    emit buildChanged();
}

void StudioController::appendLog(const QStringList& lines) {
    if (logModel.rowCount() >= MaximumLogLines) logModel.removeRows(0, lines.size());
    const auto row = logModel.rowCount();
    logModel.insertRows(row, lines.size());
    for (int index = 0; index < lines.size(); ++index) logModel.setData(logModel.index(row + index), lines[index]);
}

void StudioController::revealOutput() const {
    if (project) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(project->outputPath).absolutePath()));
}

void StudioController::openFile(const QString& relativePath) {
    if (!project) return;
    const auto path = QDir::cleanPath(project->root + '/' + relativePath);
    if (!isInsideDirectory(path, project->root)) return;
    if (const auto* editor = currentEditor()) launchEditor(*editor, path);
    else QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void StudioController::openProjectInEditor() {
    const auto* editor = currentEditor();
    if (project && editor) launchEditor(*editor, {});
}

const CodeEditor* StudioController::currentEditor() const {
    const auto found = std::ranges::find(detectedEditors, editorId, &CodeEditor::id);
    return found == detectedEditors.end() ? nullptr : &*found;
}

void StudioController::launchEditor(const CodeEditor& editor, const QString& file) {
    const auto launch = editorLaunch(editor, project->root, file);
    lastEditorError = QProcess::startDetached(launch.program, launch.arguments, project->root) ? QString() : QString("%1 could not be opened. Choose another editor.").arg(editor.name);
    emit editorChanged();
}

QVariantList StudioController::editors() const {
    QVariantList entries{QVariantMap{{"id", "system"}, {"name", "System default"}}};
    for (const auto& editor : detectedEditors) entries.append(QVariantMap{{"id", editor.id}, {"name", editor.name}});
    return entries;
}

QString StudioController::selectedEditor() const { return editorId; }
QString StudioController::selectedEditorName() const { return currentEditor() ? currentEditor()->name : QString(); }
QString StudioController::editorError() const { return lastEditorError; }

void StudioController::setSelectedEditor(const QString& id) {
    if (id == editorId) return;
    editorId = id;
    lastEditorError.clear();
    QSettings().setValue("codeEditor", id);
    emit editorChanged();
}

void StudioController::copyLog() const {
    QGuiApplication::clipboard()->setText(logModel.stringList().join('\n'));
}

QString StudioController::sdkRoot() const { return savedSdkRoot; }
QString StudioController::dependencyRoot() const { return savedDependencyRoot; }
bool StudioController::creatingGame() const { return gameCreator.isRunning(); }
QString StudioController::newGameError() const { return lastNewGameError; }
QString StudioController::openingTab() const { return currentOpeningTab; }

QVariantList StudioController::projectTypes() const {
    QVariantList entries;
    for (const auto& type : Studio::projectTypes()) {
        entries.append(QVariantMap{{"id", type.id}, {"title", type.title}, {"description", type.description}, {"noun", type.noun}, {"available", type.available}, {"note", type.note}});
    }
    return entries;
}

QString StudioController::defaultProjectType() const {
    const auto types = Studio::projectTypes();
    const auto found = std::ranges::find(types, Studio::defaultProjectType(), &ProjectTypeInfo::type);
    return found == types.end() ? QString() : found->id;
}

QString StudioController::gameNameProblem(const QString& name, const QString& noun) const { return validateGameName(name, noun); }
QString StudioController::projectsFolder() const {
    return QSettings().value(ProjectsFolderSetting, defaultProjectsFolder(QDir::homePath())).toString();
}

QString StudioController::suggestedProjectName(const QString& parentFolder, const QString& noun) const {
    return suggestProjectName(parentFolder, noun);
}

QString StudioController::parentFolderProblem(const QString& path, const QString& noun) const {
    // The default folder is created with the first project.
    return path == defaultProjectsFolder(QDir::homePath()) ? QString() : validateParentDirectory(path, noun);
}
QString StudioController::sdkFolderProblem(const QString& path) const { return validateSdkRoot(path); }
QString StudioController::dependencyFolderProblem(const QString& path) const { return validateDependencyRoot(path); }
QString StudioController::localPath(const QUrl& folder) const { return folder.toLocalFile(); }

void StudioController::rememberSdkRoot(const QString& path) {
    if (path == savedSdkRoot || !validateSdkRoot(path).isEmpty()) return;
    savedSdkRoot = path;
    QSettings().setValue(SdkRootSetting, path);
    emit toolFoldersChanged();
}

void StudioController::rememberDependencyRoot(const QString& path) {
    if (path == savedDependencyRoot || !validateDependencyRoot(path).isEmpty()) return;
    savedDependencyRoot = path;
    QSettings().setValue(DependencyRootSetting, path);
    emit toolFoldersChanged();
}

void StudioController::setNewGameError(const QString& error) {
    if (error == lastNewGameError) return;
    lastNewGameError = error;
    emit newGameErrorChanged();
}

void StudioController::clearNewGameError() {
    setNewGameError({});
}

void StudioController::createGame(const QString& name, const QString& parentFolder, const QString& sdkFolder, const QString& dependencyFolder, const QString& typeId) {
    if (runner.isRunning() || gameCreator.isRunning()) return;
    const auto type = projectTypeFromId(typeId);
    if (!type) {
        setNewGameError("Studio does not know that project type. Go back and choose one from the list.");
        return;
    }
    const NewGameRequest request{name, parentFolder, sdkFolder, dependencyFolder, *type};
    // Remembered before the checks below: folders that are right stay right even if the name is not.
    rememberSdkRoot(sdkFolder);
    rememberDependencyRoot(dependencyFolder);
    if (parentFolder == defaultProjectsFolder(QDir::homePath())) QDir().mkpath(parentFolder);
    const auto problem = validateNewGame(request);
    setNewGameError(problem);
    if (!problem.isEmpty()) return;
    QSettings().setValue(ProjectsFolderSetting, parentFolder);

    pendingGame = request;
    gameCreatorOutput.clear();
    gameCreator.start({adkInitStep(request)}, parentFolder, withToolDirectories(QProcessEnvironment::systemEnvironment(), hostToolDirectories()));
    emit creatingGameChanged();
}

void StudioController::finishCreatingGame(bool success, const QString& runnerMessage) {
    emit creatingGameChanged();
    const auto noun = projectTypeNoun(pendingGame.type);
    if (!success) {
        setNewGameError(describeAdkInitFailure(gameCreatorOutput, runnerMessage, noun));
        return;
    }

    const auto directory = newGameDirectory(pendingGame);
    if (newGameSteps(pendingGame.type).contains(NewGameStep::AddSceneRuntime)) {
        QString error;
        if (!installSceneRuntime(":/runtime/SceneGame", directory, pendingGame.name, error, *starterSceneFor(pendingGame.type))) {
            setNewGameError(QString("The %1 was created in %2, but its scene editor could not be added. %3 Open that folder and choose Add the scene editor to try again.").arg(noun, directory, error));
            return;
        }
    }
    if (!openPath(directory, Studio::openingTab(pendingGame.type))) setNewGameError(QString("The %1 was created in %2, but it could not be opened. %3").arg(noun, directory, lastOpenError));
}

}
