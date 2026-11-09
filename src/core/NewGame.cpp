#include "NewGame.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <array>

namespace Studio {

namespace {

constexpr int MaximumNameLength = 64;

// `adk init` refuses these because CMake keeps them as target names. The match is exact: "ALL" is allowed.
constexpr std::array<QLatin1StringView, 10> ReservedNames{
    QLatin1StringView("all"), QLatin1StringView("ALL_BUILD"), QLatin1StringView("help"), QLatin1StringView("install"), QLatin1StringView("INSTALL"),
    QLatin1StringView("preinstall"), QLatin1StringView("clean"), QLatin1StringView("edit_cache"), QLatin1StringView("rebuild_cache"), QLatin1StringView("ZERO_CHECK"),
};

bool isAsciiDigit(QChar character) {
    return character >= QLatin1Char('0') && character <= QLatin1Char('9');
}

bool isNameCharacter(QChar character) {
    return (character >= QLatin1Char('a') && character <= QLatin1Char('z')) || (character >= QLatin1Char('A') && character <= QLatin1Char('Z')) || isAsciiDigit(character) || character == QLatin1Char('_');
}

bool pathIsTaken(const QString& path) {
    const QFileInfo info(path);
    return info.exists() || info.isSymLink(); // adk init refuses a dangling link too
}

QString withFullStop(QString text) {
    text = text.trimmed();
    if (!text.isEmpty() && !text.endsWith('.') && !text.endsWith('!') && !text.endsWith('?')) text += '.';
    return text;
}

}

QList<ProjectTypeInfo> projectTypes() {
    return {
        {ProjectType::Game2D, "2d", "2D game", "A starter scene with a square the controller moves.", "game", true, {}},
        {ProjectType::App, "app", "App", "A starter screen with a title and button shapes.", "app", true, {}},
        {ProjectType::CodeOnly, "code", "Code only", "A plain adk init project you write in C++.", "project", true, {}},
        {ProjectType::Game3D, "3d", "3D game", "Studio has no 3D editor or 3D runtime yet.", "game", false, "Coming later"},
    };
}

ProjectType defaultProjectType() {
    return ProjectType::Game2D;
}

std::optional<ProjectType> projectTypeFromId(const QString& id) {
    for (const auto& type : projectTypes()) {
        if (type.id == id) return type.type;
    }
    return std::nullopt;
}

QString projectTypeNoun(ProjectType type) {
    for (const auto& known : projectTypes()) {
        if (known.type == type) return known.noun;
    }
    return "game";
}

QList<NewGameStep> newGameSteps(ProjectType type) {
    switch (type) {
    case ProjectType::Game2D:
    case ProjectType::App:
        return {NewGameStep::AdkInit, NewGameStep::AddSceneRuntime};
    case ProjectType::CodeOnly:
        return {NewGameStep::AdkInit};
    case ProjectType::Game3D:
        break;
    }
    return {};
}

std::optional<Scene> starterSceneFor(ProjectType type) {
    switch (type) {
    case ProjectType::Game2D:
        return starterScene();
    case ProjectType::App:
        return appStarterScene();
    case ProjectType::CodeOnly:
    case ProjectType::Game3D:
        break;
    }
    return std::nullopt;
}

QString openingTab(ProjectType type) {
    return type == ProjectType::CodeOnly ? "build" : "scene";
}

QString defaultProjectsFolder(const QString& homeFolder) {
    return homeFolder + "/Documents/AnyDevKit Projects";
}

QString suggestProjectName(const QString& parentFolder, const QString& noun) {
    auto capitalized = noun;
    if (!capitalized.isEmpty()) capitalized[0] = capitalized[0].toUpper();
    const auto base = "MyFirst" + capitalized;
    const QDir parent(parentFolder);
    if (!parent.exists(base)) return base;
    for (int number = 1;; ++number) {
        const auto name = base + QString::number(number);
        if (!parent.exists(name)) return name;
    }
}

QString validateGameName(const QString& name, const QString& noun) {
    if (name.isEmpty()) return QString("Give your %1 a name.").arg(noun);
    if (name.size() > MaximumNameLength) return QString("That name is %1 characters long. Use %2 or fewer.").arg(name.size()).arg(MaximumNameLength);
    if (!std::ranges::all_of(name, isNameCharacter)) return "Use only English letters, digits and underscores in the name, with no spaces.";
    if (isAsciiDigit(name.front())) return "A name cannot start with a digit. Start it with a letter.";
    if (std::ranges::find(ReservedNames, name) != ReservedNames.end()) return QString("CMake reserves the name %1 for itself. Choose another name.").arg(name);
    return {};
}

QString validateSdkRoot(const QString& sdkRoot) {
    if (sdkRoot.isEmpty()) return "Choose the AnyDevKit SDK folder, the one that contains bin/adk.";
    if (!QFileInfo(sdkRoot).isDir()) return QString("The SDK folder %1 does not exist. Choose the folder that contains bin/adk.").arg(sdkRoot);
    const QFileInfo adk(adkExecutable(QDir::cleanPath(sdkRoot)));
    if (!adk.isFile()) return QString("%1 has no bin/adk. Choose the AnyDevKit SDK folder that contains it.").arg(sdkRoot);
    if (!adk.isExecutable()) return QString("%1 cannot be run. Make it executable or reinstall the SDK.").arg(adk.filePath());
    if (!QFileInfo(QDir::cleanPath(sdkRoot) + "/lib/cmake/AnyDevKit").isDir()) return QString("%1 has no lib/cmake/AnyDevKit, so the SDK looks incomplete. Reinstall it or choose a complete copy.").arg(sdkRoot);
    return {};
}

QString validateDependencyRoot(const QString& dependencyRoot) {
    if (dependencyRoot.isEmpty()) return "Choose the dependency folder made by adk setup.";
    if (!QFileInfo(dependencyRoot).isDir()) return QString("The dependency folder %1 does not exist. Choose the folder made by adk setup.").arg(dependencyRoot);
    const auto root = QDir::cleanPath(dependencyRoot);
    if (QFileInfo(root + "/ps5/sysroot").isDir()) return {};
    if (QFileInfo(root).fileName() == "ps5" && QFileInfo(root + "/sysroot").isDir()) return "This is the ps5 folder inside the dependency folder. Choose its parent folder instead.";
    return QString("%1 has no ps5/sysroot. Choose the folder made by adk setup, or run adk setup with --target ps5 first.").arg(dependencyRoot);
}

QString validateParentDirectory(const QString& parentDirectory, const QString& noun) {
    if (parentDirectory.isEmpty()) return QString("Choose the folder to create the %1 in.").arg(noun);
    if (QDir::isRelativePath(parentDirectory)) return QString("%1 is not a full path. Choose a folder or type its full path.").arg(parentDirectory);
    const QFileInfo info(parentDirectory);
    if (!info.exists()) return QString("%1 does not exist. Create it or choose another folder.").arg(parentDirectory);
    if (!info.isDir()) return QString("%1 is not a folder. Choose a folder to create the %2 in.").arg(parentDirectory, noun);
    if (!info.isWritable()) return QString("You cannot write to %1. Choose a folder you can write to.").arg(parentDirectory);
    return {};
}

QString validateNewGame(const NewGameRequest& request) {
    // Refused here too, so nothing but the form's disabled card stands between a type and adk init.
    if (newGameSteps(request.type).isEmpty()) return "3D games are coming later, so Studio cannot make one yet. Choose another project type.";
    const auto noun = projectTypeNoun(request.type);
    for (const auto& problem : {validateGameName(request.name, noun), validateParentDirectory(request.parentDirectory, noun), validateSdkRoot(request.sdkRoot), validateDependencyRoot(request.dependencyRoot)}) {
        if (!problem.isEmpty()) return problem;
    }
    const auto directory = newGameDirectory(request);
    if (pathIsTaken(directory)) return QString("%1 already exists. Choose another name or parent folder.").arg(directory);
    return {};
}

QString newGameDirectory(const NewGameRequest& request) {
    return QDir::cleanPath(request.parentDirectory + '/' + request.name);
}

BuildStep adkInitStep(const NewGameRequest& request) {
    return {
        "init",
        "Create game",
        adkExecutable(QDir::cleanPath(request.sdkRoot)),
        {"init", request.name, "--deps", QDir::cleanPath(request.dependencyRoot), "--directory", newGameDirectory(request)},
        {},
    };
}

QString describeAdkInitFailure(const QStringList& output, const QString& runnerMessage, const QString& noun) {
    QString reason;
    for (auto line = output.crbegin(); line != output.crend() && reason.isEmpty(); ++line) reason = line->trimmed();
    if (reason.startsWith("adk: ")) reason = reason.mid(5);
    if (reason.isEmpty()) {
        reason = runnerMessage;
        reason.remove(" The log has the details.");
    }
    return QString("The %1 could not be created. %2 Check the name and folders, then try again.").arg(noun, withFullStop(reason));
}

}
