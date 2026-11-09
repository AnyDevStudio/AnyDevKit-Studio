#pragma once

#include "BuildRunner.h"
#include "HostTools.h"
#include "HttpClient.h"
#include "ReleaseAssets.h"
#include "SafeArchive.h"
#include "SetupProgress.h"

#include <QFile>
#include <QObject>
#include <QPointer>
#include <QProcessEnvironment>
#include <QStringList>
#include <QStringListModel>
#include <QVariantList>

#include <atomic>
#include <optional>

class QThread;

namespace Studio {

struct SetupFlowConfig {
    QString dataDirectory; // everything Studio downloads lives under this folder
    HostPlatform platform;
    QString apiBase = "https://api.github.com";
    QStringList toolDirectories; // searched before PATH
    QProcessEnvironment environment;
    HttpClient* http = nullptr;     // not owned
    const ToolProbe* probe = nullptr; // not owned
    QString homeDirectory;
    QString existingSdkRoot;        // a valid saved folder is used instead of downloading
    QString existingDependencyRoot;
};

// Everything on the "Set up PS5 tools" screen: the checks, the download, unpacking and adk setup.
class SetupFlow : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QVariantList steps READ steps NOTIFY changed)
    Q_PROPERTY(QVariantList tools READ tools NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(QString toolsHint READ toolsHint NOTIFY changed)
    Q_PROPERTY(QStringListModel* log READ log CONSTANT)

public:
    // The phase names QML reads: checking, needsTools, ready, running, failed, cancelled, done.
    explicit SetupFlow(SetupFlowConfig config, QObject* parent = nullptr);
    ~SetupFlow() override;

    QString phase() const;
    bool running() const;
    QVariantList steps() const;
    QVariantList tools() const;
    QString problem() const;
    QString toolsHint() const;
    QStringListModel* log();

    QString sdkRoot() const;
    QString dependencyRoot() const;

    Q_INVOKABLE void checkTools();
    Q_INVOKABLE void checkTool(const QString& id);
    Q_INVOKABLE void start(); // also continues after a failure or a cancel
    Q_INVOKABLE void cancel();

signals:
    void changed();
    void finished(const QString& sdkRoot, const QString& dependencyRoot);

private:
    void setPhase(const QString& newPhase);
    void applyToolCheck(const ToolCheck& check);
    void runToolChecks(const QList<HostTool>& tools);
    void refreshToolsPhase();
    void fail(SetupStep step, const QString& message);
    void appendLog(const QString& line);
    void fetchRelease();
    void handleReleaseResponse();
    void useRelease();
    void startDownload();
    void writeDownloadData();
    void handleDownloadFinished();
    void verifyDownloadAndUnpack(bool freshDownload);
    void beginUnpack(const QString& archive, const QString& staging);
    void finishUnpack(const ExtractResult& result, const QString& staging);
    void finishCancelled();
    void startDependencies();
    void finishDependencies(bool success, const QString& runnerMessage);
    void finishFlow();
    QString dataPath(const QString& relative) const;
    void removeInsideData(const QString& path);
    QString sdkFolderFor(const QString& tag) const;
    QString dependencyFolderFor(const QString& tag) const;
    bool prepareDependencyRetry(QString& problem);
    QProcessEnvironment dependencyEnvironment(QString& problem);

    SetupFlowConfig config;
    SetupProgress progress;
    QList<ToolCheck> toolChecks;
    QString currentPhase = "checking";
    QString currentProblem;
    QString currentToolsHint;
    QStringListModel logModel;
    QPointer<HttpTransfer> transfer;
    QFile partFile;
    std::optional<ReleaseAsset> release;
    QString chosenSdkRoot;
    QString chosenDependencyRoot;
    BuildRunner dependencyRunner;
    QStringList dependencyOutput;
    QList<QThread*> threads;
    int checksRunning = 0;
    std::atomic<bool> cancelRequested{false};
    int generation = 0;
    qint64 resumedBytes = 0;
    bool cancelledByUser = false;
    bool busy = false;
    bool downloadStarted = false;
    bool downloadWriteFailed = false;
    bool downloadTooLarge = false;
};

}
