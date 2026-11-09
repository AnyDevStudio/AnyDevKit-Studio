#pragma once

#include "BuildRunner.h"
#include "CodeEditors.h"
#include "ExportFormat.h"
#include "GamePageDocument.h"
#include "LogicDocument.h"
#include "NewGame.h"
#include "Project.h"
#include "SceneDocument.h"

#include <QObject>
#include <QStringListModel>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace Studio {

struct SceneDocumentForeign {
    Q_GADGET
    QML_FOREIGN(Studio::SceneDocument)
    QML_NAMED_ELEMENT(SceneDocument)
    QML_UNCREATABLE("Owned by the studio controller")
};

struct LogicDocumentForeign {
    Q_GADGET
    QML_FOREIGN(Studio::LogicDocument)
    QML_NAMED_ELEMENT(LogicDocument)
    QML_UNCREATABLE("Owned by the studio controller")
};

struct GamePageDocumentForeign {
    Q_GADGET
    QML_FOREIGN(Studio::GamePageDocument)
    QML_NAMED_ELEMENT(GamePageDocument)
    QML_UNCREATABLE("Owned by the studio controller")
};

class StudioController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY projectChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QString titleName READ titleName NOTIFY projectChanged)
    Q_PROPERTY(QString titleId READ titleId NOTIFY projectChanged)
    Q_PROPERTY(QString contentVersion READ contentVersion NOTIFY projectChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY projectChanged)
    Q_PROPERTY(QString openError READ openError NOTIFY openErrorChanged)
    Q_PROPERTY(bool building READ building NOTIFY buildChanged)
    Q_PROPERTY(QString buildState READ buildState NOTIFY buildChanged)
    Q_PROPERTY(QString buildMessage READ buildMessage NOTIFY buildChanged)
    Q_PROPERTY(QVariantList stages READ stages NOTIFY buildChanged)
    Q_PROPERTY(QString outputName READ outputName NOTIFY buildChanged)
    Q_PROPERTY(QString outputSize READ outputSize NOTIFY buildChanged)
    Q_PROPERTY(QVariantList exportFormats READ exportFormats CONSTANT)
    Q_PROPERTY(QString exportFormat READ exportFormat WRITE setExportFormat NOTIFY exportFormatChanged)
    Q_PROPERTY(QString buildActionLabel READ buildActionLabel NOTIFY buildChanged)
    Q_PROPERTY(QString pkgPasscodeModuli READ pkgPasscodeModuli NOTIFY pkgKeysChanged)
    Q_PROPERTY(QString pkgMountModulus READ pkgMountModulus NOTIFY pkgKeysChanged)
    Q_PROPERTY(bool pkgPasscodeSaved READ pkgPasscodeSaved NOTIFY pkgKeysChanged)
    Q_PROPERTY(bool pkgImageSeedSaved READ pkgImageSeedSaved NOTIFY pkgKeysChanged)
    Q_PROPERTY(QStringList pkgKeyIssues READ pkgKeyIssues NOTIFY pkgKeysChanged)
    Q_PROPERTY(QStringListModel* log READ log CONSTANT)
    Q_PROPERTY(Studio::SceneDocument* scene READ scene CONSTANT)
    Q_PROPERTY(Studio::LogicDocument* logic READ logic CONSTANT)
    Q_PROPERTY(Studio::GamePageDocument* gamePage READ gamePage CONSTANT)
    Q_PROPERTY(QVariantList editors READ editors CONSTANT)
    Q_PROPERTY(QString selectedEditor READ selectedEditor WRITE setSelectedEditor NOTIFY editorChanged)
    Q_PROPERTY(QString selectedEditorName READ selectedEditorName NOTIFY editorChanged)
    Q_PROPERTY(QString editorError READ editorError NOTIFY editorChanged)
    Q_PROPERTY(QString sdkRoot READ sdkRoot NOTIFY toolFoldersChanged)
    Q_PROPERTY(QString dependencyRoot READ dependencyRoot NOTIFY toolFoldersChanged)
    Q_PROPERTY(bool creatingGame READ creatingGame NOTIFY creatingGameChanged)
    Q_PROPERTY(QString newGameError READ newGameError NOTIFY newGameErrorChanged)
    Q_PROPERTY(QVariantList projectTypes READ projectTypes CONSTANT)
    Q_PROPERTY(QString defaultProjectType READ defaultProjectType CONSTANT)
    Q_PROPERTY(QString openingTab READ openingTab NOTIFY projectChanged)

public:
    explicit StudioController(QObject* parent = nullptr);

    bool hasProject() const;
    QString projectName() const;
    QString titleName() const;
    QString titleId() const;
    QString contentVersion() const;
    QVariantList files() const;
    QString openError() const;
    bool building() const;
    QString buildState() const;
    QString buildMessage() const;
    QVariantList stages() const;
    QString outputName() const;
    QString outputSize() const;
    QVariantList exportFormats() const;
    QString exportFormat() const;
    void setExportFormat(const QString& id);
    QString buildActionLabel() const;
    QString pkgPasscodeModuli() const;
    QString pkgMountModulus() const;
    bool pkgPasscodeSaved() const;
    bool pkgImageSeedSaved() const;
    QStringList pkgKeyIssues() const;
    QStringListModel* log();
    SceneDocument* scene();
    LogicDocument* logic();
    GamePageDocument* gamePage();
    QVariantList editors() const;
    QString selectedEditor() const;
    QString selectedEditorName() const;
    QString editorError() const;
    void setSelectedEditor(const QString& id);
    QString sdkRoot() const;
    QString dependencyRoot() const;
    bool creatingGame() const;
    QString newGameError() const;
    QVariantList projectTypes() const;
    QString defaultProjectType() const;
    QString openingTab() const;

    Q_INVOKABLE void addSceneEditor();
    Q_INVOKABLE void addSceneImage(const QUrl& file, double centerX, double centerY);
    Q_INVOKABLE void replaceSceneImage(int index, const QUrl& file);
    Q_INVOKABLE void openProjectInEditor();
    Q_INVOKABLE bool openFolder(const QUrl& folder);
    Q_INVOKABLE void closeProject();
    Q_INVOKABLE void build();
    Q_INVOKABLE void cancelBuild();
    Q_INVOKABLE void revealOutput() const;
    // `which` is "passcodeModuli" or "mountModulus" for files, "passcode" or "imageSeed" for values.
    Q_INVOKABLE void rememberPkgKeyFile(const QString& which, const QString& path);
    Q_INVOKABLE void rememberPkgSecret(const QString& which, const QString& value);
    Q_INVOKABLE void forgetPkgKeys();
    Q_INVOKABLE void openFile(const QString& relativePath);
    Q_INVOKABLE void copyLog() const;
    Q_INVOKABLE QString gameNameProblem(const QString& name, const QString& noun = "game") const;
    Q_INVOKABLE QString projectsFolder() const;
    Q_INVOKABLE QString suggestedProjectName(const QString& parentFolder, const QString& noun) const;
    Q_INVOKABLE QString parentFolderProblem(const QString& path, const QString& noun = "game") const;
    Q_INVOKABLE QString sdkFolderProblem(const QString& path) const;
    Q_INVOKABLE QString dependencyFolderProblem(const QString& path) const;
    Q_INVOKABLE QString localPath(const QUrl& folder) const;
    Q_INVOKABLE void rememberSdkRoot(const QString& path);
    Q_INVOKABLE void rememberDependencyRoot(const QString& path);
    Q_INVOKABLE void clearNewGameError();
    Q_INVOKABLE void createGame(const QString& name, const QString& parentFolder, const QString& sdkFolder, const QString& dependencyFolder, const QString& typeId);

    // `tab` is the ProjectView mode the project opens on.
    bool openPath(const QString& path, const QString& tab = "scene");

signals:
    void projectChanged();
    void openErrorChanged();
    void buildChanged();
    void exportFormatChanged();
    void pkgKeysChanged();
    void buildFinished(bool success);
    void logicBlockedBuild();
    void editorChanged();
    void toolFoldersChanged();
    void creatingGameChanged();
    void newGameErrorChanged();

private:
    const CodeEditor* currentEditor() const;
    void launchEditor(const CodeEditor& editor, const QString& file);
    void refreshFiles();
    bool exportScene();
    void openLogic();
    void resetBuild();
    QString exportProblem() const;
    void pkgKeysUpdated();
    void setStageState(int index, const QString& state);
    void appendLog(const QStringList& lines);
    void setNewGameError(const QString& error);
    void finishCreatingGame(bool success, const QString& runnerMessage);

    std::optional<Project> project;
    QVariantList fileEntries;
    QString lastOpenError;
    BuildRunner runner;
    QStringListModel logModel;
    SceneDocument sceneDocument;
    LogicDocument logicDocument;
    GamePageDocument gamePageDocument;
    QVariantList stageEntries;
    ExportFormat selectedFormat = ExportFormat::Ffpfsc;
    PkgKeys pkgKeys;
    QList<BuildStep> plannedSteps;
    QString state = "idle";
    QString message;
    QString size;
    QList<CodeEditor> detectedEditors;
    QString editorId = "system";
    QString lastEditorError;
    BuildRunner gameCreator;
    NewGameRequest pendingGame;
    QStringList gameCreatorOutput;
    QString lastNewGameError;
    QString savedSdkRoot;
    QString savedDependencyRoot;
    QString currentOpeningTab = "scene";
};

}
