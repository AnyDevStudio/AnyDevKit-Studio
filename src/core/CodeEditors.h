#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Studio {

struct CodeEditor {
    QString id;
    QString name;
    QString program;
    QStringList launcherArguments;
};

struct EditorLaunch {
    QString program;
    QStringList arguments;
};

struct EditorLocations {
    QStringList pathDirectories;
    QStringList macApplicationDirectories;
    QStringList windowsRoots;
};

QList<CodeEditor> detectCodeEditors(const EditorLocations& locations);
EditorLaunch editorLaunch(const CodeEditor& editor, const QString& projectRoot, const QString& file = {});
EditorLocations hostEditorLocations();

}
