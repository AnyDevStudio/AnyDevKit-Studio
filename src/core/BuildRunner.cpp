#include "BuildRunner.h"

#include "ExportFormat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

namespace Studio {

namespace {

constexpr int KillDelayMilliseconds = 2000;

// Stops the whole tool tree: `cmake --build` starts Ninja and compilers of its own.
void stopProcessTree(QProcess& process) {
    const auto id = process.processId();
    if (id <= 0) return;
#ifdef Q_OS_UNIX
    ::kill(-static_cast<pid_t>(id), SIGTERM);
#elif defined(Q_OS_WIN)
    QProcess::execute("taskkill", {"/PID", QString::number(id), "/T", "/F"});
#else
    process.terminate();
#endif
}

QString resolveProgram(const QString& program, const QProcessEnvironment& environment) {
    if (QFileInfo(program).isAbsolute()) return QFileInfo(program).isExecutable() ? program : QString();
    return QStandardPaths::findExecutable(program, environment.value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts));
}

}

BuildRunner::BuildRunner(QObject* parent) : QObject(parent) {}

BuildRunner::~BuildRunner() {
    if (!process) return;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        stopProcessTree(*process);
        process->kill();
        process->waitForFinished(KillDelayMilliseconds);
    }
}

void BuildRunner::start(const QList<BuildStep>& newSteps, const QString& directory, const QProcessEnvironment& newEnvironment) {
    if (running) return;
    steps = newSteps;
    workingDirectory = directory;
    environment = newEnvironment;
    secrets.clear();
    for (const auto& step : steps) secrets << step.secrets;
    pending.clear();
    current = -1;
    cancelled = false;
    running = true;
    QMetaObject::invokeMethod(this, &BuildRunner::runNext, Qt::QueuedConnection);
}

void BuildRunner::cancel() {
    if (!running) return;
    cancelled = true;
    if (!process || process->state() == QProcess::NotRunning) return;
    stopProcessTree(*process);
    // Bound to this process, so a build started during the delay does not get killed in its place.
    auto* target = process.get();
    QTimer::singleShot(KillDelayMilliseconds, target, [target] {
        if (target->state() != QProcess::NotRunning) target->kill();
    });
}

bool BuildRunner::isRunning() const {
    return running;
}

bool BuildRunner::prepareStep(const BuildStep& step, QString& error) {
    if (!step.workingDirectory.isEmpty() && !isInsideDirectory(step.workingDirectory, workingDirectory)) {
        error = QString("Refusing to run in %1 because it is outside the project folder.").arg(step.workingDirectory);
        return false;
    }
    for (const auto& path : step.removeFirst) {
        if (!isInsideDirectory(path, workingDirectory)) {
            error = QString("Refusing to remove %1 because it is outside the project folder.").arg(path);
            return false;
        }
        const QFileInfo info(path);
        if (!info.exists() && !info.isSymLink()) continue;
        const auto removed = info.isDir() && !info.isSymLink() ? QDir(path).removeRecursively() : QFile::remove(path);
        if (!removed) {
            error = QString("Could not remove the previous output at %1.").arg(path);
            return false;
        }
    }
    for (const auto& path : step.createFirst) {
        if (!isInsideDirectory(path, workingDirectory)) {
            error = QString("Refusing to create %1 because it is outside the project folder.").arg(path);
            return false;
        }
        if (!QDir().mkpath(path)) {
            error = QString("Could not create the folder %1.").arg(path);
            return false;
        }
    }
    return true;
}

void BuildRunner::runNext() {
    if (cancelled) {
        finish(false, "Cancelled.");
        return;
    }
    ++current;
    if (current >= steps.size()) {
        finish(true, "Build finished.");
        return;
    }

    const auto step = steps[current];
    emit stepStarted(current);

    QString error;
    if (!prepareStep(step, error)) {
        emit stepFinished(current, false);
        finish(false, error);
        return;
    }

    const auto program = resolveProgram(step.program, environment);
    if (program.isEmpty()) {
        emit stepFinished(current, false);
        finish(false, QString("%1 was not found. Install it or add it to PATH, then build again.").arg(step.program));
        return;
    }

    if (process) process.release()->deleteLater();
    process = std::make_unique<QProcess>();
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setWorkingDirectory(step.workingDirectory.isEmpty() ? workingDirectory : step.workingDirectory);
    process->setProcessEnvironment(environment);
#ifdef Q_OS_UNIX
    process->setChildProcessModifier([] { ::setpgid(0, 0); });
#endif
    connect(process.get(), &QProcess::readyRead, this, &BuildRunner::readOutput);
    connect(process.get(), &QProcess::errorOccurred, this, [this, title = step.title](QProcess::ProcessError failure) {
        if (failure != QProcess::FailedToStart) return;
        emit stepFinished(current, false);
        finish(false, QString("%1 could not be started: %2").arg(title, process->errorString()));
    });
    connect(process.get(), &QProcess::finished, this, [this, title = step.title](int code, QProcess::ExitStatus status) {
        readOutput();
        flushOutput();
        const auto succeeded = status == QProcess::NormalExit && code == 0;
        emit stepFinished(current, succeeded);
        if (cancelled) finish(false, "Cancelled.");
        else if (!succeeded) finish(false, QString("%1 failed with exit code %2. The log has the details.").arg(title).arg(code));
        else QMetaObject::invokeMethod(this, &BuildRunner::runNext, Qt::QueuedConnection);
    });
    process->start(program, step.arguments);
}

void BuildRunner::readOutput() {
    if (!process) return;
    pending += process->readAll();
    qsizetype newline = 0;
    while ((newline = pending.indexOf('\n')) >= 0) {
        auto line = pending.first(newline);
        pending.remove(0, newline + 1);
        if (line.endsWith('\r')) line.chop(1);
        emit outputLine(redactSecrets(QString::fromUtf8(line), secrets));
    }
}

void BuildRunner::flushOutput() {
    if (pending.isEmpty()) return;
    emit outputLine(redactSecrets(QString::fromUtf8(pending), secrets));
    pending.clear();
}

void BuildRunner::finish(bool success, const QString& message) {
    if (!running) return;
    running = false;
    emit finished(success, redactSecrets(message, secrets));
}

}
