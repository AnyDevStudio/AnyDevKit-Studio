#include "CodeEditors.h"

#include "Project.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace Studio {

namespace {

struct EditorDefinition {
    const char* id;
    const char* name;
    QStringList commands;
    QStringList macApplications;
    QStringList windowsPatterns;
};

const QList<EditorDefinition>& editorDefinitions() {
    static const QList<EditorDefinition> definitions{
        {"vscode", "VS Code", {"code"}, {"Visual Studio Code.app"}, {"Programs/Microsoft VS Code/Code.exe", "Microsoft VS Code/Code.exe"}},
        {"cursor", "Cursor", {"cursor"}, {"Cursor.app"}, {"Programs/cursor/Cursor.exe"}},
        {"visualstudio", "Visual Studio", {}, {}, {"Microsoft Visual Studio/*/*/Common7/IDE/devenv.exe"}},
        {"xcode", "Xcode", {}, {"Xcode.app"}, {}},
        {"clion", "CLion", {"clion", "clion.sh"}, {"CLion.app"}, {"JetBrains/*/bin/clion64.exe"}},
        {"sublime", "Sublime Text", {"subl"}, {"Sublime Text.app"}, {"Sublime Text/subl.exe"}},
        {"zed", "Zed", {"zed", "zeditor"}, {"Zed.app"}, {"Programs/Zed/Zed.exe"}},
        {"qtcreator", "Qt Creator", {"qtcreator"}, {"Qt Creator.app"}, {}},
    };
    return definitions;
}

// Expands a relative pattern whose segments may be `*`, newest (last sorted) match first.
QString expandPattern(const QString& root, const QStringList& segments, int index = 0) {
    if (index == segments.size()) return QFileInfo(root).isFile() ? root : QString();
    if (segments[index] != "*") return expandPattern(root + '/' + segments[index], segments, index + 1);
    auto children = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const auto& child : children) {
        const auto match = expandPattern(root + '/' + child, segments, index + 1);
        if (!match.isEmpty()) return match;
    }
    return {};
}

}

QList<CodeEditor> detectCodeEditors(const EditorLocations& locations) {
    QList<CodeEditor> editors;
    for (const auto& definition : editorDefinitions()) {
        CodeEditor editor{definition.id, definition.name, {}, {}};

        for (const auto& root : locations.windowsRoots) {
            for (const auto& pattern : definition.windowsPatterns) {
                if (editor.program.isEmpty()) editor.program = expandPattern(root, pattern.split('/'));
            }
        }

        for (const auto& directory : locations.macApplicationDirectories) {
            for (const auto& application : definition.macApplications) {
                const auto path = directory + '/' + application;
                if (editor.program.isEmpty() && QFileInfo(path).isDir()) {
                    editor.program = "open";
                    editor.launcherArguments = {"-a", path};
                }
            }
        }

        // An empty list would make findExecutable fall back to the real PATH.
        if (editor.program.isEmpty() && !locations.pathDirectories.isEmpty()) {
            for (const auto& command : definition.commands) {
                const auto found = QStandardPaths::findExecutable(command, locations.pathDirectories);
#ifdef Q_OS_WIN
                if (!found.endsWith(".exe", Qt::CaseInsensitive)) continue;
#endif
                if (editor.program.isEmpty()) editor.program = found;
            }
        }

        if (!editor.program.isEmpty()) editors.append(editor);
    }
    return editors;
}

EditorLaunch editorLaunch(const CodeEditor& editor, const QString& projectRoot, const QString& file) {
    auto arguments = editor.launcherArguments;
    if (file.isEmpty()) arguments << projectRoot;
    else if (editor.id == "visualstudio") arguments << "/Edit" << file;
    else if (editor.id == "xcode" || editor.id == "clion" || editor.id == "qtcreator") arguments << file;
    else arguments << projectRoot << file;
    return {editor.program, arguments};
}

EditorLocations hostEditorLocations() {
    EditorLocations locations;
    const auto environment = QProcessEnvironment::systemEnvironment();
    locations.pathDirectories = hostToolDirectories() + environment.value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
#if defined(Q_OS_MACOS)
    locations.macApplicationDirectories = {"/Applications", QDir::homePath() + "/Applications"};
#elif defined(Q_OS_WIN)
    for (const auto* variable : {"LOCALAPPDATA", "ProgramFiles", "ProgramFiles(x86)"}) {
        const auto value = environment.value(variable);
        if (!value.isEmpty()) locations.windowsRoots << QDir::fromNativeSeparators(value);
    }
#endif
    return locations;
}

}
