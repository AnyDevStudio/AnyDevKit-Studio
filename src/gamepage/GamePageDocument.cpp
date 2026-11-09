#include "GamePageDocument.h"

#include "Project.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QThread>
#include <QUrlQuery>

#include <algorithm>
#include <memory>

namespace Studio {

GamePageDocument::GamePageDocument(QObject* parent) : QObject(parent) {}

GamePageDocument::~GamePageDocument() {
    waitForWorker();
}

bool GamePageDocument::isLoaded() const { return !root.isEmpty(); }
bool GamePageDocument::isBusy() const { return worker != nullptr; }
QString GamePageDocument::title() const { return currentTitle; }
QUrl GamePageDocument::iconUrl() const { return icon; }
QUrl GamePageDocument::backgroundUrl() const { return background; }
QUrl GamePageDocument::splashUrl() const { return splash; }
QString GamePageDocument::status() const { return currentStatus; }
QString GamePageDocument::error() const { return currentError; }

QString GamePageDocument::systemFolder() const {
    return app0 + "/sce_sys";
}

QUrl GamePageDocument::imageUrl(GameImageKind kind) const {
    const auto path = systemFolder() + '/' + gameImageTarget(kind).fileName;
    if (!QFile::exists(path)) return {};
    auto url = QUrl::fromLocalFile(path);
    // The preview caches by URL, so a replaced picture needs a different one.
    QUrlQuery query;
    query.addQueryItem("v", QString::number(revision));
    url.setQuery(query);
    return url;
}

void GamePageDocument::refreshUrls() {
    icon = imageUrl(GameImageKind::Icon);
    background = imageUrl(GameImageKind::Background);
    splash = imageUrl(GameImageKind::Splash);
}

void GamePageDocument::report(const QString& newStatus, const QString& newError) {
    currentStatus = newStatus;
    currentError = newError;
    emit changed();
}

void GamePageDocument::waitForWorker() {
    if (!worker) return;
    worker->wait();
    worker = nullptr;
    ++generation;
}

void GamePageDocument::open(const QString& projectRoot, const QString& app0Path, const QString& paramPath, const QString& titleName) {
    waitForWorker();
    root = projectRoot;
    app0 = app0Path;
    param = paramPath;
    currentTitle = titleName;
    currentStatus.clear();
    currentError.clear();
    ++revision;
    refreshUrls();
    emit changed();
}

void GamePageDocument::close() {
    waitForWorker();
    root.clear();
    app0.clear();
    param.clear();
    currentTitle.clear();
    currentStatus.clear();
    currentError.clear();
    icon.clear();
    background.clear();
    splash.clear();
    emit changed();
}

void GamePageDocument::setBuilding(bool building) {
    isBuilding = building;
}

void GamePageDocument::setTitle(const QString& text) {
    if (!isLoaded()) return;
    if (isBuilding) {
        report({}, "A build is running. Wait for it to finish, then change the name.");
        return;
    }

    QFile file(param);
    if (!file.open(QIODevice::ReadOnly)) {
        report({}, QString("param.json could not be read: %1. Check that the file exists and try again.").arg(file.errorString()));
        return;
    }
    const auto edit = withDefaultTitle(file.readAll(), text);
    file.close();
    if (!edit.error.isEmpty()) {
        report({}, edit.error);
        return;
    }

    if (const auto problem = checkTargetPath(root, QDir(root).relativeFilePath(param)); !problem.isEmpty()) {
        report({}, problem);
        return;
    }
    QSaveFile save(param);
    if (!save.open(QIODevice::WriteOnly) || save.write(edit.json) != edit.json.size() || !save.commit()) {
        report({}, QString("param.json could not be saved: %1. Check that the project folder can be written to.").arg(save.errorString()));
        return;
    }

    currentTitle = text.trimmed();
    report("Name saved. It appears in the next build.", {});
    emit titleSaved(currentTitle);
}

void GamePageDocument::chooseImage(const QString& kind, const QUrl& source) {
    if (!isLoaded() || isBusy()) return;
    if (isBuilding) {
        report({}, "A build is running. Wait for it to finish, then choose the picture again.");
        return;
    }

    const auto targets = gameImageTargets();
    const auto found = std::find_if(targets.begin(), targets.end(), [&](const GameImageTarget& target) { return target.id == kind; });
    if (found == targets.end()) return;
    const GameImageTarget target = *found;

    const auto sourcePath = source.toLocalFile();
    const auto projectRoot = root;
    const auto folder = systemFolder();
    auto outcome = std::make_shared<QString>();
    worker = QThread::create([=] { *outcome = installGameImage(sourcePath, projectRoot, folder, target); });
    worker->setParent(this);
    const auto token = ++generation;
    auto* thread = worker;
    connect(thread, &QThread::finished, this, [this, thread, outcome, target, token] {
        thread->deleteLater();
        if (token != generation) return;
        worker = nullptr;
        if (!outcome->isEmpty()) {
            report({}, *outcome);
            return;
        }
        ++revision;
        refreshUrls();
        report(QString("%1 saved. It appears in the next build.").arg(target.label), {});
        emit filesChanged();
    });
    report(QString("Preparing the %1...").arg(target.label.toLower()), {});
    worker->start();
}

void GamePageDocument::removeSplash() {
    if (!isLoaded() || isBusy()) return;
    if (isBuilding) {
        report({}, "A build is running. Wait for it to finish, then remove the splash.");
        return;
    }
    const auto problem = removeGameImage(root, systemFolder(), gameImageTarget(GameImageKind::Splash));
    if (!problem.isEmpty()) {
        report({}, problem);
        return;
    }
    refreshUrls();
    report("Splash removed. The background is used until you choose another.", {});
    emit filesChanged();
}

}
