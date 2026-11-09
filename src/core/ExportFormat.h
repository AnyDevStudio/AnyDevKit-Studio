#pragma once

#include "Project.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace Studio {

enum class ExportFormat { Ffpfsc, Zip, Pkg };

struct ExportFormatInfo {
    ExportFormat format = ExportFormat::Ffpfsc;
    QString id;
    QString label;
    QString buildLabel;
    QString buildAgainLabel;
};

// Provided by the user and kept per user. Passcode and image seed are secrets, so a key set must
// never be written into the project or the build log.
struct PkgKeys {
    QString passcodeModuliPath;
    QString mountModulusPath;
    QString passcode;
    QString imageSeed;
};

QList<ExportFormatInfo> exportFormats();
ExportFormatInfo exportFormatInfo(ExportFormat format);
std::optional<ExportFormat> exportFormatFromId(const QString& id);
QString exportOutputPath(const Project& project, ExportFormat format);
QList<BuildStep> exportBuildSteps(const Project& project, ExportFormat format, const PkgKeys& keys = {});
// Why this export must not run: a path would leave the project or pass through a link. Empty when safe.
QString checkExportPaths(const Project& project, ExportFormat format);
QStringList pkgKeyProblems(const PkgKeys& keys);
QString describePkgKeyProblems(const QStringList& problems);
QString redactSecrets(QString text, const QStringList& secrets);
QString describeBuildStep(const BuildStep& step);

}
