#include "StudioController.h"

#include "GamePage.h"

#include "Blocks.h"
#include "SceneExport.h"
#include "SceneImageImport.h"
#include "SoundImport.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace Studio {

namespace {

constexpr int MaximumLogLines = 20000;
constexpr auto SdkRootSetting = "adk/sdkRoot";
constexpr auto ProjectsFolderSetting = "newGame/projectsFolder";
constexpr auto DependencyRootSetting = "adk/dependencyRoot";
constexpr auto PkgModuliSetting = "export/pkg/passcodeModuli";
constexpr auto PkgMountSetting = "export/pkg/mountModulus";
constexpr auto PkgPasscodeSetting = "export/pkg/passcode";
constexpr auto PkgImageSeedSetting = "export/pkg/imageSeed";

// Project paths contain the separators QSettings gives a meaning to, so the key uses a hash.
QString exportFormatSetting(const QString& root) {
    return "export/format/" + QString::fromLatin1(QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
}

// Where AnyDevKit and its libraries are installed. A test or scripted run points this at its own folder.
QString setupDataDirectory() {
    const auto overridden = qEnvironmentVariable("ANYDEVKIT_STUDIO_DATA_DIR");
    return overridden.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : overridden;
}

// The address releases are looked up at. Only a local test server may replace GitHub.
QString setupApiBase() {
    const auto overridden = qEnvironmentVariable("ANYDEVKIT_STUDIO_RELEASE_API");
    const auto host = QUrl(overridden).host();
    if (!overridden.isEmpty() && (host == "127.0.0.1" || host == "localhost")) return overridden;
    return "https://api.github.com";
}

SetupFlowConfig setupFlowConfig(HttpClient& http, const ToolProbe& probe) {
    SetupFlowConfig config;
    config.dataDirectory = setupDataDirectory();
    config.platform = currentHostPlatform();
    config.apiBase = setupApiBase();
    config.toolDirectories = hostToolDirectories();
    config.environment = QProcessEnvironment::systemEnvironment();
    // A scripted run can show the "missing tool" screen by limiting where tools are looked for.
    if (const auto toolPath = qEnvironmentVariable("ANYDEVKIT_STUDIO_TOOL_PATH"); !toolPath.isEmpty()) {
        config.toolDirectories = {};
        config.environment.insert("PATH", toolPath);
    }
    config.http = &http;
    config.probe = &probe;
    config.homeDirectory = QDir::homePath();
    config.existingSdkRoot = QSettings().value(SdkRootSetting).toString();
    config.existingDependencyRoot = QSettings().value(DependencyRootSetting).toString();
    return config;
}

}

StudioController::StudioController(QObject* parent) : QObject(parent), setupProbe(hostToolDirectories()), setupFlow(setupFlowConfig(setupHttp, setupProbe)) {
    detectedEditors = detectCodeEditors(hostEditorLocations());
    const auto saved = QSettings().value("codeEditor", "system").toString();
    if (std::ranges::any_of(detectedEditors, [&](const CodeEditor& editor) { return editor.id == saved; })) editorId = saved;
    savedSdkRoot = QSettings().value(SdkRootSetting).toString();
    savedDependencyRoot = QSettings().value(DependencyRootSetting).toString();
    pkgKeys.passcodeModuliPath = QSettings().value(PkgModuliSetting).toString();
    pkgKeys.mountModulusPath = QSettings().value(PkgMountSetting).toString();
    pkgKeys.passcode = QSettings().value(PkgPasscodeSetting).toString();
    pkgKeys.imageSeed = QSettings().value(PkgImageSeedSetting).toString();

    connect(&setupFlow, &SetupFlow::finished, this, [this](const QString& sdkFolder, const QString& dependencyFolder) {
        rememberSdkRoot(sdkFolder);
        rememberDependencyRoot(dependencyFolder);
    });

    connect(&gameCreator, &BuildRunner::outputLine, this, [this](const QString& line) { gameCreatorOutput << line; });
    connect(&gameCreator, &BuildRunner::finished, this, &StudioController::finishCreatingGame);

    connect(&runner, &BuildRunner::stepStarted, this, [this](int index) {
        setStageState(index, "running");
        appendLog({"$ " + describeBuildStep(plannedSteps.at(index))});
    });
    connect(&runner, &BuildRunner::stepFinished, this, [this](int index, bool success) { setStageState(index, success ? "done" : "failed"); });
    connect(&runner, &BuildRunner::outputLine, this, [this](const QString& line) { appendLog({line}); });
    connect(&runner, &BuildRunner::finished, this, [this](bool success, const QString& text) {
        message = text;
        state = success ? "done" : text.startsWith("Cancelled") ? "cancelled" : "failed";
        const QFileInfo output(project ? exportOutputPath(*project, selectedFormat) : QString());
        size = success && output.exists() ? QLocale::system().formattedDataSize(output.size(), 1, QLocale::DataSizeSIFormat) : QString();
        emit buildChanged();
        emit buildFinished(success);
    });

    const auto refreshLogicScene = [this] { logicDocument.setSceneSet(sceneDocument.sceneSet(), sceneDocument.currentSceneIndex()); };
    connect(&sceneDocument, &QAbstractItemModel::modelReset, this, refreshLogicScene);
    connect(&sceneDocument, &QAbstractItemModel::rowsInserted, this, refreshLogicScene);
    connect(&sceneDocument, &QAbstractItemModel::rowsRemoved, this, refreshLogicScene);
    connect(&sceneDocument, &QAbstractItemModel::rowsMoved, this, refreshLogicScene);
    connect(&sceneDocument, &QAbstractItemModel::dataChanged, this, refreshLogicScene);
    connect(&sceneDocument, &SceneDocument::scenesChanged, this, refreshLogicScene);
    connect(&sceneDocument, &SceneDocument::sceneDuplicated, &logicDocument, &LogicDocument::duplicateSceneLogic);
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
QString StudioController::outputName() const { return project && !size.isEmpty() ? QFileInfo(exportOutputPath(*project, selectedFormat)).fileName() : QString(); }
QString StudioController::outputSize() const { return size; }
QString StudioController::exportFormat() const { return exportFormatInfo(selectedFormat).id; }
QString StudioController::pkgPasscodeModuli() const { return pkgKeys.passcodeModuliPath; }
QString StudioController::pkgMountModulus() const { return pkgKeys.mountModulusPath; }
bool StudioController::pkgPasscodeSaved() const { return !pkgKeys.passcode.isEmpty(); }
bool StudioController::pkgImageSeedSaved() const { return !pkgKeys.imageSeed.isEmpty(); }
QStringList StudioController::pkgKeyIssues() const { return pkgKeyProblems(pkgKeys); }

QVariantList StudioController::exportFormats() const {
    QVariantList entries;
    for (const auto& info : Studio::exportFormats()) entries.append(QVariantMap{{"id", info.id}, {"label", info.label}});
    return entries;
}

void StudioController::setExportFormat(const QString& id) {
    const auto format = exportFormatFromId(id);
    if (!format || *format == selectedFormat || runner.isRunning()) return;
    selectedFormat = *format;
    if (project) QSettings().setValue(exportFormatSetting(project->root), id);
    emit exportFormatChanged();
    resetBuild();
}

QString StudioController::buildActionLabel() const {
    const auto info = exportFormatInfo(selectedFormat);
    return state == "idle" || state == "blocked" ? info.buildLabel : info.buildAgainLabel;
}
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

void StudioController::addSceneTilemap(const QUrl& file) {
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
    if (sceneDocument.addTilemap(image.source, image.width, image.height, image.name, 0, 0) < 0) return;
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

bool StudioController::canPlaySoundPreview() const { return SoundPreviewPlayer::available(); }
bool StudioController::canDecodeCompressedSound() const { return Studio::canDecodeCompressedSound(); }

void StudioController::importSound(const QUrl& file) {
    if (!project || !logicDocument.isLoaded()) return;
    ImportedSound imported;
    QString problem;
    if (!importSoundFile(project->root, logicDocument.program().sounds, file.toLocalFile(), imported, problem)) {
        logicDocument.reportProblem(problem);
        return;
    }
    if (!logicDocument.addSound(imported.name, imported.source)) return;
    refreshFiles();
    emit projectChanged();
}

QVariantMap StudioController::soundPreview(const QString& source) const {
    if (!project) return {};
    constexpr int Columns = 48; // what fits in the Sounds area's waveform strip
    const auto preview = Studio::soundPreview(project->root, source, Columns);
    if (!preview.error.isEmpty()) return {{"error", preview.error}};
    QVariantList peaks;
    for (const auto peak : preview.peaks) peaks.append(peak);
    return {{"seconds", preview.seconds}, {"sampleRate", preview.sampleRate}, {"channels", preview.channels}, {"peaks", peaks}};
}

void StudioController::playSoundPreview(const QString& source) {
    if (!project || !SoundPreviewPlayer::available() || !soundFileProblem(project->root, source).isEmpty()) return;
    soundPlayer.play(project->root + '/' + source);
}

void StudioController::stopSoundPreview() { soundPlayer.stop(); }

void StudioController::openLogic() {
    if (!project || !sceneDocument.isLoaded()) {
        logicDocument.close();
        return;
    }
    logicDocument.setSceneSet(sceneDocument.sceneSet(), sceneDocument.currentSceneIndex());
    logicDocument.open(project->root + "/logic.json");
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
    const auto scenes = sceneDocument.sceneSet();
    if (!Studio::exportScene(project->root, scenes, problem)) {
        state = "failed";
        message = problem;
        emit buildChanged();
        return false;
    }
    if (!exportLogicHeader(project->root, scenes, problem)) {
        state = "failed";
        message = problem;
        emit buildChanged();
        // Only a block can be shown on the Logic tab; a text problem stays on the build page, named.
        if (logicDocument.problemCount() > 0) emit logicBlockedBuild();
        return false;
    }

    // A project that started with an older Studio needs game code that knows about what the scene uses.
    if ((usesVariables() && sceneRuntimeNeedsUpdate(project->root)) || sceneRuntimeNeedsUpdate(project->root, sceneDocument.scene()) || (programUsesWorldBlocks(logicDocument.program()) && sceneRuntimeLacksWorld(project->root)) || ((sceneSetUsesPhysics(scenes) || programUsesPhysicsBlocks(logicDocument.program())) && sceneRuntimeLacksPhysics(project->root)) || (usesSoundFeatures() && soundRuntimeNeedsUpdate(project->root))) {
        if (!installSceneRuntime(":/runtime/SceneGame", project->root, project->name, problem)) {
            state = "failed";
            message = problem;
            emit buildChanged();
            return false;
        }
        appendLog({"Updated this project's game code to run what this scene uses. Its previous files are in .studio-backup."});
        refreshFiles();
    }
    return true;
}

bool StudioController::usesSoundFeatures() const {
    const auto& program = logicDocument.program();
    if (!program.sounds.isEmpty()) return true;
    const auto uses = [](const auto& self, const QList<Block>& blocks) -> bool {
        return std::ranges::any_of(blocks, [&](const Block& block) {
            return block.type == BlockType::SaveVariables || block.type == BlockType::LoadVariables || block.type == BlockType::StopMusic || block.type == BlockType::SetVolume || self(self, block.children);
        });
    };
    if (uses(uses, program.events)) return true;
    return std::ranges::any_of(program.otherScenes, [&](const SceneLogic& other) { return uses(uses, other.events); });
}

bool StudioController::usesVariables() const {
    if (!logicDocument.program().variables.isEmpty()) return true;
    for (const auto& scene : sceneDocument.sceneSet().scenes) {
        if (std::ranges::any_of(scene.items, [](const SceneItem& item) { return item.type == "text" && textUsesVariables(item.text); })) return true;
    }
    return false;
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
    selectedFormat = exportFormatFromId(QSettings().value(exportFormatSetting(project->root)).toString()).value_or(ExportFormat::Ffpfsc);
    emit exportFormatChanged();
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
        for (const auto& step : exportBuildSteps(*project, selectedFormat, pkgKeys, desktopContext())) stageEntries.append(QVariantMap{{"title", step.title}, {"state", "waiting"}});
    }
    emit buildChanged();
}

void StudioController::build() {
    if (!project || runner.isRunning() || gamePageDocument.isBusy()) return;
    resetBuild();
    if (const auto problem = exportProblem(); !problem.isEmpty()) {
        state = "blocked";
        message = problem;
        emit buildChanged();
        emit buildFinished(false);
        return;
    }
    if (!exportScene()) {
        emit buildFinished(false);
        return;
    }
    auto environment = withToolDirectories(QProcessEnvironment::systemEnvironment(), hostToolDirectories());
    if (isDesktopFormat(selectedFormat)) {
        QString problem;
        if (!writeDesktopHelpers(*project, problem)) {
            state = "blocked";
            message = problem;
            emit buildChanged();
            emit buildFinished(false);
            return;
        }
        if (const auto tools = desktopToolPath(*project, selectedFormat, desktopContext().hostIsWindows); !tools.isEmpty()) {
            environment.insert("PATH", tools + QDir::listSeparator() + environment.value("PATH"));
        }
#ifdef Q_OS_MACOS
        // The SDK builds desktop programs with LLVM's own clang and lld, which Apple's clang does not stand in for.
        for (const auto* llvm : {"/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin"}) {
            if (QFileInfo(QString(llvm) + "/clang").isExecutable()) {
                environment.insert("PATH", QString(llvm) + QDir::listSeparator() + environment.value("PATH"));
                break;
            }
        }
#endif
    }
    state = "running";
    plannedSteps = exportBuildSteps(*project, selectedFormat, pkgKeys, desktopContext());
    runner.start(plannedSteps, project->root, environment);
    emit buildChanged();
}

DesktopBuildContext StudioController::desktopContext() const {
    DesktopBuildContext context;
    if (!project || !isDesktopFormat(selectedFormat)) return context;
    const auto target = desktopTarget(selectedFormat);
    context.dependencyRoot = savedDependencyRoot;
    context.cacheDirectory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/adk-cache";
    context.needsDependencies = !savedDependencyRoot.isEmpty() && !QFileInfo(savedDependencyRoot + '/' + target).isDir();
#ifdef Q_OS_WIN
    context.hostIsWindows = true;
#endif
    context.needsWindowsStartup = selectedFormat == ExportFormat::Windows && !QFileInfo::exists(project->sdkRoot + "/lib/libAnyDevKitWindowsStartup.a");
    return context;
}

void StudioController::cancelBuild() {
    runner.cancel();
}

QString StudioController::exportProblem() const {
    const auto paths = checkExportPaths(*project, selectedFormat);
    if (!paths.isEmpty()) return paths;
    return selectedFormat == ExportFormat::Pkg ? describePkgKeyProblems(pkgKeyProblems(pkgKeys)) : QString();
}

void StudioController::pkgKeysUpdated() {
    emit pkgKeysChanged();
    if (state == "blocked") resetBuild();
}

void StudioController::rememberPkgKeyFile(const QString& which, const QString& path) {
    if (runner.isRunning()) return;
    const auto setting = which == "passcodeModuli" ? PkgModuliSetting : which == "mountModulus" ? PkgMountSetting : nullptr;
    auto& target = which == "passcodeModuli" ? pkgKeys.passcodeModuliPath : pkgKeys.mountModulusPath;
    if (!setting || target == path) return;
    target = path;
    QSettings().setValue(setting, path);
    pkgKeysUpdated();
}

void StudioController::rememberPkgSecret(const QString& which, const QString& value) {
    if (runner.isRunning()) return;
    const auto setting = which == "passcode" ? PkgPasscodeSetting : which == "imageSeed" ? PkgImageSeedSetting : nullptr;
    auto& target = which == "passcode" ? pkgKeys.passcode : pkgKeys.imageSeed;
    const auto trimmed = value.trimmed();
    if (!setting || trimmed.isEmpty() || target == trimmed) return;
    target = trimmed;
    QSettings().setValue(setting, trimmed);
    pkgKeysUpdated();
}

void StudioController::forgetPkgKeys() {
    if (runner.isRunning()) return;
    pkgKeys = {};
    QSettings settings;
    for (const auto* key : {PkgModuliSetting, PkgMountSetting, PkgPasscodeSetting, PkgImageSeedSetting}) settings.remove(key);
    pkgKeysUpdated();
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
    if (project) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(exportOutputPath(*project, selectedFormat)).absolutePath()));
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

void StudioController::copyText(const QString& text) const {
    QGuiApplication::clipboard()->setText(text);
}

QString StudioController::sdkRoot() const { return savedSdkRoot; }
QString StudioController::dependencyRoot() const { return savedDependencyRoot; }
bool StudioController::creatingGame() const { return gameCreator.isRunning(); }
QString StudioController::newGameError() const { return lastNewGameError; }
QString StudioController::openingTab() const { return currentOpeningTab; }
SetupFlow* StudioController::setup() { return &setupFlow; }
bool StudioController::needsToolSetup() const { return savedSdkRoot.isEmpty() || savedDependencyRoot.isEmpty(); }

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
    // Pictures are a starting point, so a failure here must not stop the project from opening.
    static_cast<void>(installDefaultGamePictures(":/qt/qml/AnyDevKitStudio/assets/defaults", directory + "/app0/sce_sys"));
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
