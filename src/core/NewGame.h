#pragma once

#include "Project.h"

#include <QString>
#include <QStringList>

namespace Studio {

struct NewGameRequest {
    QString name;
    QString parentDirectory;
    QString sdkRoot;
    QString dependencyRoot;
};

// Each check returns the message to show the person, or an empty string when the value is fine.
QString validateGameName(const QString& name);
QString validateSdkRoot(const QString& sdkRoot);
QString validateDependencyRoot(const QString& dependencyRoot);
QString validateParentDirectory(const QString& parentDirectory);
QString validateNewGame(const NewGameRequest& request);

QString newGameDirectory(const NewGameRequest& request);
BuildStep adkInitStep(const NewGameRequest& request);
QString describeAdkInitFailure(const QStringList& output, const QString& runnerMessage);

}
