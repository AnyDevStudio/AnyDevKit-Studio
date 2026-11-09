#pragma once

#include <QList>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <optional>

namespace Studio {

struct Project {
    QString root;
    QString name;
    QString titleId;
    QString titleName;
    QString contentVersion;
    QString sdkRoot;
    QString elfPath;
    QString app0Path;
    QString paramPath;
    QString outputPath;
    QString workspacePath;
};

struct ProjectLoadResult {
    std::optional<Project> project;
    QString error;
};

struct BuildStep {
    QString id;
    QString title;
    QString program;
    QStringList arguments;
    QStringList removeFirst;
};

struct FileEntry {
    QString name;
    QString relativePath;
    int depth = 0;
    bool isDirectory = false;
};

ProjectLoadResult loadProject(const QString& directory);
QString adkExecutable(const Project& project);
QList<BuildStep> ps5BuildSteps(const Project& project);
QList<FileEntry> listProjectFiles(const QString& root);
bool isInsideDirectory(const QString& path, const QString& directory);
// Why writing to relativePath under root would be unsafe, or an empty string when it is fine.
QString checkTargetPath(const QString& root, const QString& relativePath);
QProcessEnvironment withToolDirectories(QProcessEnvironment environment, const QStringList& directories);
QStringList hostToolDirectories();

}
