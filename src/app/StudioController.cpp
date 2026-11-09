#include "StudioController.h"

#include "SceneExport.h"

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

}

StudioController::StudioController(QObject* parent) : QObject(parent) {
    detectedEditors = detectCodeEditors(hostEditorLocations());
    const auto saved = QSettings().value("codeEditor", "system").toString();
    if (std::ranges::any_of(detectedEditors, [&](const CodeEditor& editor) { return editor.id == saved; })) editorId = saved;

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

void StudioController::addSceneEditor() {
    if (!project || runner.isRunning()) return;
    QString error;
    if (!installSceneRuntime(":/runtime/SceneGame", project->root, project->name, error)) {
        sceneDocument.setError(error);
        return;
    }
    sceneDocument.open(project->root + "/scene.json");
    refreshFiles();
    emit projectChanged();
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
    if (Studio::exportScene(project->root, sceneDocument.scene(), problem)) return true;
    state = "failed";
    message = problem;
    emit buildChanged();
    return false;
}

bool StudioController::openFolder(const QUrl& folder) {
    return openPath(folder.toLocalFile());
}

bool StudioController::openPath(const QString& path) {
    if (runner.isRunning()) return false;
    const auto result = loadProject(path);
    lastOpenError = result.error;
    emit openErrorChanged();
    if (!result.project) return false;

    project = result.project;
    refreshFiles();
    sceneDocument.open(project->root + "/scene.json");
    resetBuild();
    emit projectChanged();
    return true;
}

void StudioController::closeProject() {
    if (runner.isRunning()) return;
    project.reset();
    fileEntries.clear();
    sceneDocument.close();
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
    if (!project || runner.isRunning()) return;
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

}
