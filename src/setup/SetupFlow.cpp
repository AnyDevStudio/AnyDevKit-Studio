#include "SetupFlow.h"

#include "ExportFormat.h"
#include "NewGame.h"
#include "Project.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>

namespace Studio {

namespace {

constexpr int MaximumLogLines = 5000;

QString safeFolderName(QString tag) {
    tag.replace(QRegularExpression("[^A-Za-z0-9._-]"), "_");
    return tag;
}

QString formatSize(qint64 bytes) {
    return QLocale::system().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
}

bool isLoopbackBase(const QString& base) {
    const auto host = QUrl(base).host();
    return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

}

SetupFlow::SetupFlow(SetupFlowConfig newConfig, QObject* parent) : QObject(parent), config(std::move(newConfig)) {
    connect(&dependencyRunner, &BuildRunner::outputLine, this, [this](const QString& line) {
        dependencyOutput << line;
        appendLog(line);
        if (const auto fraction = parseAdkSetupProgress(line); fraction) progress.report(SetupStep::Dependencies, line, *fraction);
        else if (!line.trimmed().isEmpty()) progress.report(SetupStep::Dependencies, line, progress.status(SetupStep::Dependencies).progress);
        emit changed();
    });
    connect(&dependencyRunner, &BuildRunner::finished, this, &SetupFlow::finishDependencies);
    for (const auto& tool : requiredHostTools()) {
        ToolCheck check;
        check.id = tool.id;
        check.name = tool.name;
        check.checking = true;
        toolChecks << check;
    }
}

SetupFlow::~SetupFlow() {
    ++generation;
    cancelRequested = true;
    if (transfer) {
        transfer->disconnect(this);
        transfer->abort();
        delete transfer.data();
    }
    dependencyRunner.disconnect(this);
    for (auto* thread : std::as_const(threads)) {
        thread->wait();
        delete thread;
    }
}

QString SetupFlow::phase() const { return currentPhase; }
bool SetupFlow::running() const { return busy; }
QString SetupFlow::problem() const { return currentProblem; }
QString SetupFlow::toolsHint() const { return currentToolsHint; }
QStringListModel* SetupFlow::log() { return &logModel; }
QString SetupFlow::sdkRoot() const { return chosenSdkRoot; }
QString SetupFlow::dependencyRoot() const { return chosenDependencyRoot; }

QVariantList SetupFlow::steps() const {
    QVariantList entries;
    for (const auto& step : progress.steps()) {
        entries.append(QVariantMap{{"id", setupStepId(step.step)}, {"title", setupStepTitle(step.step)}, {"state", setupStepStateName(step.state)}, {"detail", step.detail}, {"progress", step.progress}});
    }
    return entries;
}

QVariantList SetupFlow::tools() const {
    QVariantList entries;
    for (const auto& check : toolChecks) {
        entries.append(QVariantMap{{"id", check.id}, {"name", check.name}, {"checking", check.checking}, {"ok", check.ok}, {"version", check.version}, {"problem", check.problem}, {"command", check.command}});
    }
    return entries;
}

void SetupFlow::setPhase(const QString& newPhase) {
    currentPhase = newPhase;
    emit changed();
}

void SetupFlow::appendLog(const QString& line) {
    if (logModel.rowCount() >= MaximumLogLines) logModel.removeRows(0, 1);
    const auto row = logModel.rowCount();
    logModel.insertRows(row, 1);
    logModel.setData(logModel.index(row), line);
}

void SetupFlow::checkTools() {
    if (busy) return;
    ++generation;
    progress.restart();
    progress.begin(SetupStep::Tools, "Checking your computer...");
    currentProblem.clear();
    for (auto& check : toolChecks) {
        check.checking = true;
    }
    setPhase("checking");
    runToolChecks(requiredHostTools());
}

void SetupFlow::checkTool(const QString& id) {
    if (busy) return;
    QList<HostTool> tools;
    for (const auto& tool : requiredHostTools()) {
        if (tool.id == id) tools << tool;
    }
    if (tools.isEmpty()) return;
    for (auto& check : toolChecks) {
        if (check.id == id) check.checking = true;
    }
    progress.restart();
    progress.begin(SetupStep::Tools, "Checking again...");
    setPhase("checking");
    runToolChecks(tools);
}

void SetupFlow::runToolChecks(const QList<HostTool>& tools) {
    ++checksRunning;
    auto directories = config.toolDirectories;
    directories += config.environment.value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
    const auto expected = generation;
    auto* thread = QThread::create([this, tools, directories, expected, platform = config.platform, probe = config.probe, home = config.homeDirectory] {
        for (const auto& tool : tools) {
            const auto check = checkHostTool(tool, platform, *probe, directories, home);
            QMetaObject::invokeMethod(this, [this, check, expected] {
                if (expected == generation) applyToolCheck(check);
            }, Qt::QueuedConnection);
        }
    });
    connect(thread, &QThread::finished, this, [this, thread, expected] {
        threads.removeOne(thread);
        thread->deleteLater();
        --checksRunning;
        if (expected == generation) refreshToolsPhase();
    });
    threads << thread;
    thread->start();
}

void SetupFlow::applyToolCheck(const ToolCheck& check) {
    for (auto& known : toolChecks) {
        if (known.id == check.id) known = check;
    }
    refreshToolsPhase();
}

void SetupFlow::refreshToolsPhase() {
    if (std::ranges::any_of(toolChecks, &ToolCheck::checking)) {
        emit changed();
        return;
    }
    if (checksRunning > 0) {
        emit changed();
        return;
    }
    currentToolsHint.clear();
    const auto missing = std::ranges::count_if(toolChecks, [](const ToolCheck& check) { return !check.ok; });
    if (missing > 0) {
        if (config.platform.os == HostOs::MacOs && config.probe->findExecutable("brew", config.toolDirectories + config.environment.value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts)).isEmpty()) {
            currentToolsHint = "These commands use Homebrew. Install it first from brew.sh, then come back.";
        }
        progress.fail(SetupStep::Tools, missing == 1 ? "1 tool needs installing." : QString("%1 tools need installing.").arg(missing));
        setPhase("needsTools");
    } else {
        progress.complete(SetupStep::Tools, "All five tools are here.");
        setPhase("ready");
    }
}

void SetupFlow::fail(SetupStep step, const QString& message) {
    busy = false;
    currentProblem = message;
    progress.fail(step, message);
    appendLog("Stopped: " + message);
    setPhase("failed");
}

void SetupFlow::start() {
    if (busy || currentPhase == "checking" || currentPhase == "needsTools" || currentPhase == "done") return;
    if (std::ranges::any_of(toolChecks, [](const ToolCheck& check) { return !check.ok; })) return;

    busy = true;
    cancelRequested = false;
    cancelledByUser = false;
    currentProblem.clear();
    progress.clearFailure();
    setPhase("running");

    if (validateSdkRoot(config.existingSdkRoot).isEmpty()) {
        chosenSdkRoot = config.existingSdkRoot;
        progress.complete(SetupStep::Download, "Using your AnyDevKit folder.");
        progress.complete(SetupStep::Unpack, "Using your AnyDevKit folder.");
        startDependencies();
        return;
    }
    if (release) {
        useRelease();
        return;
    }
    fetchRelease();
}

void SetupFlow::cancel() {
    if (!busy) return;
    cancelledByUser = true;
    cancelRequested = true;
    appendLog("Cancelling...");
    if (transfer) transfer->abort();
    else if (dependencyRunner.isRunning()) dependencyRunner.cancel();
    // A worker thread notices cancelRequested and reports back on its own.
    emit changed();
}

QString SetupFlow::dataPath(const QString& relative) const {
    return config.dataDirectory + '/' + relative;
}

void SetupFlow::removeInsideData(const QString& path) {
    // Only ever deletes below Studio's own data folder, whatever path is passed.
    if (!isInsideDirectory(path, config.dataDirectory)) return;
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) return;
    if (info.isDir() && !info.isSymLink()) QDir(path).removeRecursively();
    else QFile::remove(path);
}

QString SetupFlow::sdkFolderFor(const QString& tag) const { return dataPath("sdk/" + safeFolderName(tag)); }
QString SetupFlow::dependencyFolderFor(const QString& tag) const { return dataPath("dependencies/" + safeFolderName(tag)); }

void SetupFlow::fetchRelease() {
    progress.begin(SetupStep::Download, "Finding the latest AnyDevKit release...");
    emit changed();
    const auto source = releaseSourceFor(config.platform);
    if (!source) {
        fail(SetupStep::Download, "AnyDevKit has no ready-made download for this computer. If you built it yourself, choose Use existing folders.");
        return;
    }
    const QUrl url(config.apiBase + releaseQueryPath(*source));
    appendLog("Looking for the release at " + url.toString());
    transfer = config.http->get(url, "application/vnd.github+json", 0, this);
    connect(transfer, &HttpTransfer::finished, this, &SetupFlow::handleReleaseResponse);
}

void SetupFlow::handleReleaseResponse() {
    const QPointer<HttpTransfer> done = transfer;
    transfer = nullptr;
    if (!done) return;
    const auto body = done->takeData();
    const auto status = done->status();
    const auto error = done->errorMessage();
    done->deleteLater();

    if (cancelledByUser) {
        busy = false;
        progress.pause("Cancelled. Choose Continue to pick up where this stopped.");
        setPhase("cancelled");
        return;
    }
    if (!error.isEmpty()) {
        fail(SetupStep::Download, "Studio could not reach GitHub. Check your internet connection, then choose Retry.");
        return;
    }
    if (status == 403 || status == 429) {
        fail(SetupStep::Download, "GitHub is limiting requests from this network right now. Wait a few minutes, then choose Retry.");
        return;
    }
    if (status == 404) {
        fail(SetupStep::Download, config.platform.os == HostOs::MacOs ? "AnyDevKit for Mac is not published yet. Try again later, or choose Use existing folders if you already have a copy." : "AnyDevKit has not published a release yet. Try again later, or choose Use existing folders.");
        return;
    }
    if (status >= 400) {
        fail(SetupStep::Download, QString("GitHub answered with an error (%1). Choose Retry to try again.").arg(status));
        return;
    }

    const auto source = releaseSourceFor(config.platform);
    const auto selection = selectReleaseAsset(body, *source, isLoopbackBase(config.apiBase));
    if (!selection.asset) {
        switch (selection.problem) {
        case ReleaseProblem::NotPublished:
            fail(SetupStep::Download, config.platform.os == HostOs::MacOs ? "AnyDevKit for Mac is not published yet. Try again later, or choose Use existing folders if you already have a copy." : "This AnyDevKit release has no download for this computer. Try again later, or choose Use existing folders.");
            break;
        case ReleaseProblem::NoChecksum:
            fail(SetupStep::Download, "GitHub did not publish a checksum for this download, so Studio will not use it. Try again later, or choose Use existing folders.");
            break;
        case ReleaseProblem::BadUrl:
            fail(SetupStep::Download, "The download address GitHub gave is not secure, so Studio will not use it. Try again later, or choose Use existing folders.");
            break;
        case ReleaseProblem::Ambiguous:
            fail(SetupStep::Download, "This release has more than one matching download, so Studio cannot tell which is right. Try again later, or choose Use existing folders.");
            break;
        default:
            fail(SetupStep::Download, "GitHub sent a reply Studio could not read. Choose Retry to try again.");
            break;
        }
        return;
    }
    release = *selection.asset;
    appendLog(QString("Found %1, release %2").arg(release->name, release->tag));
    useRelease();
}

void SetupFlow::useRelease() {
    // A folder from an earlier attempt that is complete needs no download or unpacking.
    if (validateSdkRoot(sdkFolderFor(release->tag)).isEmpty()) {
        chosenSdkRoot = sdkFolderFor(release->tag);
        progress.complete(SetupStep::Download, "Already downloaded.");
        progress.complete(SetupStep::Unpack, "Already unpacked.");
        startDependencies();
        return;
    }
    // A retry after a bad archive comes back here with Download already ticked off.
    progress.reopen(SetupStep::Download);
    progress.begin(SetupStep::Download, QString("%1...").arg(release->name));
    emit changed();
    // A whole file from an earlier attempt only needs its checksum confirmed.
    if (QFileInfo::exists(dataPath("downloads/" + release->name))) {
        verifyDownloadAndUnpack(false);
        return;
    }
    startDownload();
}

void SetupFlow::startDownload() {
    const auto partPath = dataPath("downloads/" + release->name + ".part");
    if (!QDir().mkpath(dataPath("downloads"))) {
        fail(SetupStep::Download, QString("Studio could not create %1. Check that your disk has room and try again.").arg(dataPath("downloads")));
        return;
    }
    partFile.setFileName(partPath);
    resumedBytes = partFile.exists() ? partFile.size() : 0;
    if (resumedBytes >= release->size) {
        partFile.remove();
        resumedBytes = 0;
    }
    if (!partFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
        fail(SetupStep::Download, QString("Studio could not write to %1. %2 Check that your disk has room, then choose Retry.").arg(partPath, partFile.errorString()));
        return;
    }
    downloadStarted = false;
    appendLog(resumedBytes > 0 ? QString("Continuing %1 from %2").arg(release->name, formatSize(resumedBytes)) : QString("Downloading %1 (%2)").arg(release->name, formatSize(release->size)));
    transfer = config.http->get(QUrl(release->url), "application/octet-stream", resumedBytes, this);
    connect(transfer, &HttpTransfer::dataReady, this, &SetupFlow::writeDownloadData);
    connect(transfer, &HttpTransfer::finished, this, &SetupFlow::handleDownloadFinished);
}

void SetupFlow::writeDownloadData() {
    if (!transfer || downloadWriteFailed || downloadTooLarge) return;
    const auto data = transfer->takeData();
    const auto status = transfer->status();
    // Nothing to write yet, or an error page rather than the file. An empty read must not
    // touch the partial file: a dropped connection also lands here with no data at all.
    if (data.isEmpty() || status == 0 || status >= 300) return;
    if (!downloadStarted) {
        downloadStarted = true;
        // A server that ignores the range sends the whole file again, so the old part is thrown away.
        if (status != 206 || transfer->rangeStart() != resumedBytes) {
            partFile.close();
            partFile.remove();
            resumedBytes = 0;
            if (!partFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
                downloadWriteFailed = true;
                transfer->abort();
                return;
            }
        }
    }
    if (partFile.write(data) != data.size()) {
        downloadWriteFailed = true;
        transfer->abort();
        return;
    }
    resumedBytes += data.size();
    const auto total = release->size > 0 ? release->size : resumedBytes;
    if (resumedBytes > total) {
        downloadTooLarge = true;
        transfer->abort();
        return;
    }
    progress.report(SetupStep::Download, QString("%1 of %2").arg(formatSize(resumedBytes), formatSize(total)), static_cast<double>(resumedBytes) / static_cast<double>(total));
    emit changed();
}

void SetupFlow::handleDownloadFinished() {
    writeDownloadData();
    const QPointer<HttpTransfer> done = transfer;
    transfer = nullptr;
    if (!done) return;
    const auto status = done->status();
    const auto error = done->errorMessage();
    done->deleteLater();
    partFile.close();
    const auto partPath = dataPath("downloads/" + release->name + ".part");

    if (downloadWriteFailed) {
        downloadWriteFailed = false;
        partFile.remove();
        fail(SetupStep::Download, QString("Studio could not save the download to %1. Check that your disk has room, then choose Retry.").arg(partPath));
        return;
    }
    if (downloadTooLarge) {
        downloadTooLarge = false;
        partFile.remove();
        fail(SetupStep::Download, "The download came out larger than GitHub said it would be. Choose Retry to download it again.");
        return;
    }
    if (cancelledByUser) {
        busy = false;
        progress.pause("Cancelled. Choose Continue to pick up where this stopped.");
        appendLog("Cancelled the download.");
        setPhase("cancelled");
        return;
    }
    if (!error.isEmpty()) {
        fail(SetupStep::Download, "The download stopped because the connection failed. Check your internet connection, then choose Retry.");
        return;
    }
    if (status == 416) {
        partFile.remove();
        fail(SetupStep::Download, "The earlier download could not be continued. Choose Retry to start it again.");
        return;
    }
    if (status >= 400) {
        if (status == 404) partFile.remove();
        fail(SetupStep::Download, QString("GitHub answered with an error (%1) while downloading. Choose Retry to try again.").arg(status));
        return;
    }
    if (release->size > 0 && QFileInfo(partPath).size() != release->size) {
        fail(SetupStep::Download, "The download ended early. Choose Retry to continue it.");
        return;
    }

    const auto finalPath = dataPath("downloads/" + release->name);
    QFile::remove(finalPath);
    if (!QFile::rename(partPath, finalPath)) {
        fail(SetupStep::Download, QString("Studio could not save the download to %1. Check that your disk has room, then choose Retry.").arg(finalPath));
        return;
    }
    verifyDownloadAndUnpack(true);
}

void SetupFlow::verifyDownloadAndUnpack(bool freshDownload) {
    const auto archive = dataPath("downloads/" + release->name);
    const auto expected = release->sha256;
    const auto staging = dataPath("sdk/.staging-" + safeFolderName(release->tag));
    removeInsideData(staging);
    QDir().mkpath(dataPath("sdk"));
    progress.report(SetupStep::Download, "Checking the download...", 1);
    emit changed();
    const auto expectedGeneration = ++generation;

    auto* thread = QThread::create([this, archive, expected, staging, freshDownload, expectedGeneration] {
        const auto post = [this, expectedGeneration](auto function) {
            QMetaObject::invokeMethod(this, [this, expectedGeneration, function] {
                if (expectedGeneration == generation) function();
            }, Qt::QueuedConnection);
        };

        const auto digest = sha256OfFile(archive, &cancelRequested);
        if (cancelRequested) {
            post([this] { finishCancelled(); });
            return;
        }
        if (!digest || *digest != expected) {
            post([this, archive, freshDownload] {
                removeInsideData(archive);
                if (freshDownload) {
                    fail(SetupStep::Download, "The download did not match its published checksum, so Studio threw it away. Choose Retry to download it again.");
                } else {
                    // A leftover file that no longer matches is simply replaced.
                    startDownload();
                }
            });
            return;
        }

        post([this, archive, staging] { beginUnpack(archive, staging); });
    });
    connect(thread, &QThread::finished, this, [this, thread] {
        threads.removeOne(thread);
        thread->deleteLater();
    });
    threads << thread;
    thread->start();
}

void SetupFlow::beginUnpack(const QString& archive, const QString& staging) {
    progress.complete(SetupStep::Download, QString("%1 checked.").arg(release->name));
    progress.begin(SetupStep::Unpack, "Unpacking...");
    appendLog("Unpacking " + release->name);
    emit changed();
    const auto expectedGeneration = ++generation;

    auto* thread = QThread::create([this, archive, staging, expectedGeneration] {
        ExtractOptions options;
        options.cancelled = &cancelRequested;
        options.progress = [this, expectedGeneration](int done, int total) {
            QMetaObject::invokeMethod(this, [this, done, total, expectedGeneration] {
                if (expectedGeneration != generation || total <= 0) return;
                progress.report(SetupStep::Unpack, QString("%1 of %2 files").arg(done).arg(total), static_cast<double>(done) / total);
                emit changed();
            }, Qt::QueuedConnection);
        };
        const auto result = extractArchive(archive, staging, options);
        QMetaObject::invokeMethod(this, [this, result, staging, expectedGeneration] {
            if (expectedGeneration == generation) finishUnpack(result, staging);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, this, [this, thread] {
        threads.removeOne(thread);
        thread->deleteLater();
    });
    threads << thread;
    thread->start();
}

void SetupFlow::finishCancelled() {
    busy = false;
    progress.pause("Cancelled. Choose Continue to pick up where this stopped.");
    appendLog("Cancelled.");
    setPhase("cancelled");
}

void SetupFlow::finishUnpack(const ExtractResult& result, const QString& staging) {
    if (result.cancelled) {
        removeInsideData(staging);
        finishCancelled();
        return;
    }
    if (!result.ok) {
        removeInsideData(staging);
        removeInsideData(dataPath("downloads/" + release->name));
        fail(SetupStep::Unpack, QString("AnyDevKit could not be unpacked. %1 Choose Retry to download it again.").arg(result.problem));
        return;
    }

    // Some archives wrap everything in one top-level folder.
    QString root = staging;
    if (!QFileInfo(staging + "/bin").isDir()) {
        const auto children = QDir(staging).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (children.size() == 1 && QFileInfo(staging + '/' + children.first() + "/bin").isDir()) root = staging + '/' + children.first();
    }
    if (const auto problem = validateSdkRoot(root); !problem.isEmpty()) {
        removeInsideData(staging);
        removeInsideData(dataPath("downloads/" + release->name));
        fail(SetupStep::Unpack, QString("The download does not look like AnyDevKit: it has no working adk program. Choose Retry to download it again, or choose Use existing folders."));
        return;
    }

    const auto destination = sdkFolderFor(release->tag);
    removeInsideData(destination);
    if (!QDir().rename(root, destination)) {
        fail(SetupStep::Unpack, QString("Studio could not move AnyDevKit into %1. Check that your disk has room, then choose Retry.").arg(destination));
        return;
    }
    if (root != staging) removeInsideData(staging);
    chosenSdkRoot = destination;
    progress.complete(SetupStep::Unpack, QString("%1 files unpacked.").arg(result.fileCount));
    appendLog(QString("Unpacked %1 files into %2").arg(result.fileCount).arg(destination));
    emit changed();
    startDependencies();
}

bool SetupFlow::prepareDependencyRetry(QString& problem) {
    if (!QDir().mkpath(dataPath("dependencies"))) {
        problem = QString("Studio could not create %1. Check that your disk has room, then choose Retry.").arg(dataPath("dependencies"));
        return false;
    }
    // adk setup refuses to run over the leftovers of a cancelled or failed attempt, and over
    // an unfinished download, so those are cleared. Finished downloads in the cache are kept.
    for (const auto& name : QDir(dataPath("dependencies")).entryList({"adk-setup-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) removeInsideData(dataPath("dependencies/" + name));
    for (const auto& name : QDir(dataPath("adk-cache")).entryList({"*.partial"}, QDir::Files)) removeInsideData(dataPath("adk-cache/" + name));
    return true;
}

QProcessEnvironment SetupFlow::dependencyEnvironment(QString& problem) {
    auto directories = config.toolDirectories;
    for (const auto& check : std::as_const(toolChecks)) {
        if (check.id != "python" || check.executable.isEmpty()) continue;
        const auto name = QFileInfo(check.executable).fileName();
        if (name == "python3" || name == "python" || name == "python.exe" || name == "python3.exe" || name == "py.exe") {
            directories.prepend(QFileInfo(check.executable).absolutePath());
        } else {
            // adk setup only looks for python3 and python on PATH, so a versioned install gets a plain name.
            const auto folder = dataPath("python-link");
            const auto link = folder + "/python3";
            removeInsideData(link);
            QDir().mkpath(folder);
            if (!QFile::link(check.executable, link)) {
                problem = QString("Studio could not prepare Python in %1. Check that your disk has room, then choose Retry.").arg(folder);
                return {};
            }
            directories.prepend(folder);
        }
    }
    return withToolDirectories(config.environment, directories);
}

void SetupFlow::startDependencies() {
    const auto tag = release ? release->tag : QStringLiteral("current");
    const auto destination = dependencyFolderFor(tag);
    if (validateDependencyRoot(config.existingDependencyRoot).isEmpty()) {
        chosenDependencyRoot = config.existingDependencyRoot;
        progress.complete(SetupStep::Dependencies, "Using your dependency folder.");
        finishFlow();
        return;
    }
    if (validateDependencyRoot(destination).isEmpty()) {
        chosenDependencyRoot = destination;
        progress.complete(SetupStep::Dependencies, "Already installed.");
        finishFlow();
        return;
    }

    progress.begin(SetupStep::Dependencies, "Starting adk setup...");
    emit changed();
    QString problem;
    if (!prepareDependencyRetry(problem)) {
        fail(SetupStep::Dependencies, problem);
        return;
    }
    removeInsideData(destination);
    QDir().mkpath(dataPath("adk-cache"));
    const auto environment = dependencyEnvironment(problem);
    if (!problem.isEmpty()) {
        fail(SetupStep::Dependencies, problem);
        return;
    }
    chosenDependencyRoot = destination;
    dependencyOutput.clear();
    const BuildStep step{"setup", "Install PS5 libraries", adkExecutable(chosenSdkRoot), {"setup", destination, "--target", "ps5", "--cache", dataPath("adk-cache")}};
    appendLog("$ " + describeBuildStep(step));
    dependencyRunner.start({step}, config.dataDirectory, environment);
}

void SetupFlow::finishDependencies(bool success, const QString& runnerMessage) {
    if (cancelledByUser) {
        finishCancelled();
        return;
    }
    if (!success) {
        fail(SetupStep::Dependencies, describeAdkSetupFailure(dependencyOutput, runnerMessage));
        return;
    }
    progress.complete(SetupStep::Dependencies, "PS5 libraries installed.");
    finishFlow();
}

void SetupFlow::finishFlow() {
    if (const auto problem = validateSdkRoot(chosenSdkRoot); !problem.isEmpty()) {
        fail(SetupStep::Dependencies, QString("%1 Choose Retry to run setup again.").arg(problem));
        return;
    }
    if (const auto problem = validateDependencyRoot(chosenDependencyRoot); !problem.isEmpty()) {
        fail(SetupStep::Dependencies, QString("%1 Choose Retry to run setup again.").arg(problem));
        return;
    }
    busy = false;
    // Verified and unpacked, so the archives have done their job.
    removeInsideData(dataPath("downloads"));
    removeInsideData(dataPath("adk-cache"));
    setPhase("done");
    emit finished(chosenSdkRoot, chosenDependencyRoot);
}

}
