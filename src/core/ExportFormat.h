#pragma once

#include "Project.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace Studio {

enum class ExportFormat { Ffpfsc, Zip, Pkg, Windows, Linux };

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

// What a desktop build needs to know about this computer. The caller looks; the steps only follow.
struct DesktopBuildContext {
    QString dependencyRoot;          // the folder adk setup filled; a target's libraries live in a folder named after it
    QString cacheDirectory;          // where adk setup may keep downloads
    bool needsDependencies = false;  // the target's libraries are not installed yet
    bool needsWindowsStartup = false; // the SDK has no Windows startup library, as an SDK built on a Mac or Linux
    bool hostIsWindows = false;
};

// The AnyDevKit release whose Windows startup library a Windows build from a Mac or Linux borrows.
inline constexpr auto WindowsStartupRelease = "0.1.7";

bool isDesktopFormat(ExportFormat format);
QString desktopTarget(ExportFormat format); // "windows" or "linux"
// The helper scripts a desktop build runs, written under build/.studio before it starts. Fails with a message.
bool writeDesktopHelpers(const Project& project, QString& error);
// A folder to put first on PATH for the build, or nothing: away from Windows, the wrapped llvm-rc for a Windows build.
QString desktopToolPath(const Project& project, ExportFormat format, bool hostIsWindows);

QList<ExportFormatInfo> exportFormats();
ExportFormatInfo exportFormatInfo(ExportFormat format);
std::optional<ExportFormat> exportFormatFromId(const QString& id);
QString exportOutputPath(const Project& project, ExportFormat format);
QList<BuildStep> exportBuildSteps(const Project& project, ExportFormat format, const PkgKeys& keys = {}, const DesktopBuildContext& desktop = {});
// Why this export must not run: a path would leave the project or pass through a link. Empty when safe.
QString checkExportPaths(const Project& project, ExportFormat format);
QStringList pkgKeyProblems(const PkgKeys& keys);
QString describePkgKeyProblems(const QStringList& problems);
QString redactSecrets(QString text, const QStringList& secrets);
QString describeBuildStep(const BuildStep& step);

}
