#pragma once

#include "Project.h"
#include "Scene.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace Studio {

enum class ProjectType { Game2D, App, CodeOnly, Game3D };

// What Studio does, in order, to create a project of a type.
enum class NewGameStep { AdkInit, AddSceneRuntime };

struct ProjectTypeInfo {
    ProjectType type;
    QString id; // what the form sends back
    QString title;
    QString description; // one line of what the person gets
    QString noun; // what the form calls the thing being made
    bool available;
    QString note; // why an unavailable type cannot be picked
};

QList<ProjectTypeInfo> projectTypes(); // in the order the form offers them
ProjectType defaultProjectType();
std::optional<ProjectType> projectTypeFromId(const QString& id);
QString projectTypeNoun(ProjectType type);
QList<NewGameStep> newGameSteps(ProjectType type); // empty for a type Studio cannot make yet
std::optional<Scene> starterSceneFor(ProjectType type); // only for a type that adds the scene runtime
QString openingTab(ProjectType type); // the ProjectView mode a new project opens on

struct NewGameRequest {
    QString name;
    QString parentDirectory;
    QString sdkRoot;
    QString dependencyRoot;
    ProjectType type = ProjectType::Game2D;
};

// Each check returns the message to show the person, or an empty string when the value is fine.
QString defaultProjectsFolder(const QString& homeFolder);
QString suggestProjectName(const QString& parentFolder, const QString& noun);
QString validateGameName(const QString& name, const QString& noun = "game");
QString validateSdkRoot(const QString& sdkRoot);
QString validateDependencyRoot(const QString& dependencyRoot);
QString validateParentDirectory(const QString& parentDirectory, const QString& noun = "game");
QString validateNewGame(const NewGameRequest& request);

QString newGameDirectory(const NewGameRequest& request);
BuildStep adkInitStep(const NewGameRequest& request);
QString describeAdkInitFailure(const QStringList& output, const QString& runnerMessage, const QString& noun = "game");

}
