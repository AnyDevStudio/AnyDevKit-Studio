#include "Project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <functional>

namespace Studio {

namespace {

constexpr int MaximumListedFiles = 5000;

ProjectLoadResult failure(const QString& message) {
    return {std::nullopt, message};
}

std::optional<QJsonObject> readObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return std::nullopt;
    return document.object();
}

bool isSkipped(const QString& name) {
    return name == "build" || name == "dist" || name == ".git" || name == ".adk" || name == ".studio-backup" || name == ".DS_Store";
}

}

bool isInsideDirectory(const QString& path, const QString& directory) {
    const auto cleanDirectory = QDir::cleanPath(directory);
    const auto prefix = cleanDirectory.endsWith('/') ? cleanDirectory : cleanDirectory + '/';
    return QDir::cleanPath(path).startsWith(prefix);
}

// A lexical check is not enough: a link inside the project could lead anywhere, so every part of
// the path is inspected. isSymLink also catches links whose target is missing, which exists() does not.
QString checkTargetPath(const QString& root, const QString& relativePath) {
    if (!isInsideDirectory(root + '/' + relativePath, root)) return QString("%1 is outside the project folder, so it was not installed.").arg(relativePath);
    const auto parts = relativePath.split('/');
    auto current = root;
    for (qsizetype index = 0; index < parts.size(); ++index) {
        current += '/' + parts[index];
        const QFileInfo info(current);
        const auto shown = parts.mid(0, index + 1).join('/');
        if (info.isSymLink() || info.isJunction()) return QString("%1 is a link, and Studio will not write through it. Replace it with a regular file or folder and try again.").arg(shown);
        if (!info.exists()) return {};
        const auto isLast = index == parts.size() - 1;
        if (isLast && info.isDir()) return QString("%1 is a folder, but Studio needs to write a file there. Move the folder and try again.").arg(shown);
        if (!isLast && !info.isDir()) return QString("%1 is a file, but Studio needs a folder there. Move the file and try again.").arg(shown);
    }
    return {};
}

ProjectLoadResult loadProject(const QString& directory) {
    const auto root = QDir::cleanPath(QDir(directory).absolutePath());
    const auto config = readObject(root + "/adk.json");
    if (!QFile::exists(root + "/adk.json")) return failure("This folder has no adk.json. Choose a folder created with adk init.");
    if (!config) return failure("adk.json could not be read. Check that it contains valid JSON.");

    const auto schema = config->value("schemaVersion").toInt(-1);
    if (schema != 1) return failure(QString("adk.json uses schemaVersion %1, but this version of Studio supports 1.").arg(schema));
    const auto options = config->value("targets").toObject().value("ps5").toObject();
    if (options.isEmpty()) return failure("adk.json has no ps5 target.");

    Project project;
    project.root = root;
    project.name = config->value("name").toString();
    if (project.name.isEmpty()) return failure("adk.json has no project name.");

    const auto resolve = [&](const char* field, QString& out) {
        const auto value = options.value(field).toString();
        if (value.isEmpty()) return QString("adk.json has no %1 path for the ps5 target.").arg(field);
        out = QDir::cleanPath(root + '/' + value);
        if (!isInsideDirectory(out, root)) return QString("adk.json: the %1 path must stay inside the project folder.").arg(field);
        return QString();
    };

    for (const auto& [field, target] : {std::pair<const char*, QString*>{"input", &project.elfPath}, {"app0", &project.app0Path}, {"param", &project.paramPath}, {"output", &project.outputPath}, {"workspace", &project.workspacePath}}) {
        const auto error = resolve(field, *target);
        if (!error.isEmpty()) return failure(error);
    }

    const auto metadata = readObject(project.paramPath);
    if (!metadata) return failure("The application metadata in app0/sce_sys/param.json could not be read.");
    project.titleId = metadata->value("titleId").toString();
    project.contentVersion = metadata->value("contentVersion").toString();
    const auto localized = metadata->value("localizedParameters").toObject();
    project.titleName = localized.value(localized.value("defaultLanguage").toString()).toObject().value("titleName").toString();

    const auto presets = readObject(root + "/CMakePresets.json");
    if (!presets) return failure("CMakePresets.json is missing or invalid. Create the project again with adk init.");
    for (const auto& preset : presets->value("configurePresets").toArray()) {
        const auto object = preset.toObject();
        if (object.value("name").toString() != "ps5") continue;
        const auto sdkConfig = object.value("cacheVariables").toObject().value("AnyDevKit_DIR").toString();
        if (!sdkConfig.isEmpty()) project.sdkRoot = QDir::cleanPath(sdkConfig + "/../../..");
    }
    if (project.sdkRoot.isEmpty()) return failure("CMakePresets.json has no ps5 preset with an AnyDevKit_DIR.");

    return {project, {}};
}

QString adkExecutable(const Project& project) {
    return adkExecutable(project.sdkRoot);
}

QString adkExecutable(const QString& sdkRoot) {
#ifdef Q_OS_WIN
    return sdkRoot + "/bin/adk.exe";
#else
    return sdkRoot + "/bin/adk";
#endif
}

QList<BuildStep> ps5BuildSteps(const Project& project) {
    return {
        {"configure", "Configure", "cmake", {"--preset", "ps5"}, {}},
        {"compile", "Compile", "cmake", {"--build", "--preset", "ps5"}, {}},
        {"package", "Package", adkExecutable(project), {"package", "--target", "ps5"}, {project.outputPath, project.workspacePath}},
    };
}

QList<FileEntry> listProjectFiles(const QString& root) {
    QList<FileEntry> entries;
    const QDir base(root);
    const std::function<void(const QString&, int)> visit = [&](const QString& path, int depth) {
        const auto children = QDir(path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        for (const auto& child : children) {
            if (entries.size() >= MaximumListedFiles) return;
            if (isSkipped(child.fileName())) continue;
            entries.append({child.fileName(), base.relativeFilePath(child.absoluteFilePath()), depth, child.isDir()});
            if (child.isDir()) visit(child.absoluteFilePath(), depth + 1);
        }
    };
    visit(root, 0);
    return entries;
}

QProcessEnvironment withToolDirectories(QProcessEnvironment environment, const QStringList& directories) {
    auto paths = directories;
    const auto current = environment.value("PATH");
    if (!current.isEmpty()) paths << current;
    environment.insert("PATH", paths.join(QDir::listSeparator()));
    return environment;
}

QStringList hostToolDirectories() {
    QStringList existing;
#ifdef Q_OS_MACOS
    // Apps opened from Finder do not inherit the shell PATH, and the toolchain needs LLVM clang rather than Apple clang.
    for (const auto* directory : {"/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin", "/opt/homebrew/bin", "/usr/local/bin"}) {
        if (QFileInfo(directory).isDir()) existing << directory;
    }
#endif
    return existing;
}

}
