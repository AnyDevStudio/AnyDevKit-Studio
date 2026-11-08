#pragma once

#include "Project.h"

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>

#include <memory>

namespace Studio {

class BuildRunner : public QObject {
    Q_OBJECT

public:
    explicit BuildRunner(QObject* parent = nullptr);
    ~BuildRunner() override;

    void start(const QList<BuildStep>& steps, const QString& workingDirectory, const QProcessEnvironment& environment);
    void cancel();
    bool isRunning() const;

signals:
    void stepStarted(int index);
    void stepFinished(int index, bool success);
    void outputLine(const QString& line);
    void finished(bool success, const QString& message);

private:
    void runNext();
    void readOutput();
    void flushOutput();
    void finish(bool success, const QString& message);
    bool removeStaleOutputs(const BuildStep& step, QString& error);

    QList<BuildStep> steps;
    QString workingDirectory;
    QProcessEnvironment environment;
    std::unique_ptr<QProcess> process;
    QByteArray pending;
    int current = -1;
    bool running = false;
    bool cancelled = false;
};

}
