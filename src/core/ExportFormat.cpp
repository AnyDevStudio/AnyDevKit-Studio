#include "ExportFormat.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace Studio {

namespace {

constexpr qint64 PasscodeModuliBytes = 7 * 384;
constexpr qint64 MountModulusBytes = 384;
constexpr qsizetype KeyValueCharacters = 32;
constexpr auto HiddenValue = "<hidden>";

bool isPlainFileName(const QString& name) {
    return !name.isEmpty() && name != "." && name != ".." && !name.contains('/') && !name.contains('\\');
}

bool isKeyValue(const QString& value) {
    return value.size() == KeyValueCharacters && std::all_of(value.begin(), value.end(), [](QChar c) { return c.isDigit() || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); });
}

QString intermediateArchive(const Project& project) {
    return project.root + "/build/export/" + project.name + ".ffpfsc";
}

// A link anywhere along the way could lead outside the project even when the path looks inside it.
QString firstLinkUnder(const QString& root, const QString& path) {
    auto current = root;
    QStringList shown;
    for (const auto& part : QDir(root).relativeFilePath(path).split('/')) {
        current += '/' + part;
        shown << part;
        const QFileInfo info(current);
        if (info.isSymLink() || info.isJunction()) return shown.join('/');
        if (!info.exists()) return {};
    }
    return {};
}

}

QList<ExportFormatInfo> exportFormats() {
    return {
        {ExportFormat::Ffpfsc, "ffpfsc", "FFPFSC", "Build PS5 package", "Build again"},
        {ExportFormat::Zip, "zip", "ZIP", "Export ZIP", "Export ZIP again"},
        {ExportFormat::Pkg, "pkg", "PKG", "Build PKG", "Build PKG again"},
    };
}

ExportFormatInfo exportFormatInfo(ExportFormat format) {
    const auto formats = exportFormats();
    const auto found = std::ranges::find(formats, format, &ExportFormatInfo::format);
    return found == formats.end() ? ExportFormatInfo{} : *found;
}

std::optional<ExportFormat> exportFormatFromId(const QString& id) {
    const auto formats = exportFormats();
    const auto found = std::ranges::find(formats, id, &ExportFormatInfo::id);
    if (found == formats.end()) return std::nullopt;
    return found->format;
}

QString exportOutputPath(const Project& project, ExportFormat format) {
    if (format == ExportFormat::Ffpfsc) return project.outputPath;
    return project.root + "/dist/ps5/" + project.name + (format == ExportFormat::Zip ? ".zip" : ".pkg");
}

QList<BuildStep> exportBuildSteps(const Project& project, ExportFormat format, const PkgKeys& keys) {
    auto steps = ps5BuildSteps(project);
    if (format == ExportFormat::Ffpfsc) return steps;

    auto& package = steps.last();
    const auto output = exportOutputPath(project, format);
    if (format == ExportFormat::Zip) {
        // adk still has to prepare app0, so its archive goes to the build folder rather than next to the ZIP.
        const auto archive = intermediateArchive(project);
        package.arguments << "--output" << archive;
        package.removeFirst = {output, archive, project.workspacePath};
        steps.append(BuildStep{"zip", "Create ZIP", "cmake", {"-E", "tar", "cf", output, "--format=zip", "."}, {}, project.workspacePath + "/app0", {QFileInfo(output).absolutePath()}, {}});
        return steps;
    }

    package.arguments << "--format" << "pkg" << "--output" << output
                      << "--passcode-moduli" << keys.passcodeModuliPath << "--mount-modulus" << keys.mountModulusPath
                      << "--passcode" << keys.passcode << "--image-seed" << keys.imageSeed;
    package.removeFirst = {output, project.workspacePath};
    package.secrets = {keys.passcode, keys.imageSeed};
    return steps;
}

QString checkExportPaths(const Project& project, ExportFormat format) {
    if (format == ExportFormat::Ffpfsc) return {};
    if (!isPlainFileName(project.name)) {
        return QString("The project name \"%1\" cannot be used for a file name, so nothing was exported. Give the project a plain name in adk.json and try again.").arg(project.name);
    }

    const auto output = exportOutputPath(project, format);
    QStringList paths{output, project.workspacePath};
    if (format == ExportFormat::Zip) paths << intermediateArchive(project);
    for (const auto& path : paths) {
        if (!isInsideDirectory(path, project.root)) return QString("%1 is outside the project folder, so nothing was exported. Check the paths in adk.json and try again.").arg(path);
        const auto link = firstLinkUnder(project.root, path);
        if (!link.isEmpty()) return QString("%1 is a link, and Studio will not export through it. Replace it with a regular folder and try again.").arg(link);
    }

    if (QFileInfo(output).isDir()) return QString("%1 is a folder, but Studio needs to write a file there. Move the folder and try again.").arg(QDir(project.root).relativeFilePath(output));
    return {};
}

QStringList pkgKeyProblems(const PkgKeys& keys) {
    QStringList problems;
    const auto checkFile = [&](const QString& path, const QString& name, qint64 bytes, const QString& sizeText) {
        const QFileInfo info(path);
        if (path.isEmpty()) problems << QString("Choose the %1 file (%2).").arg(name, sizeText);
        else if (!info.isFile()) problems << QString("The %1 file was not found: %2. Choose it again.").arg(name, path);
        else if (info.size() != bytes) problems << QString("The %1 file must be exactly %2 (%3), but it is %4 bytes.").arg(name, QString("%1 bytes").arg(bytes), sizeText).arg(info.size());
    };
    const auto checkValue = [&](const QString& value, const QString& name) {
        if (value.isEmpty()) problems << QString("Enter the %1 (32 hexadecimal characters).").arg(name);
        else if (!isKeyValue(value)) problems << QString("The %1 must be 32 hexadecimal characters.").arg(name);
    };
    checkFile(keys.passcodeModuliPath, "passcode moduli", PasscodeModuliBytes, "7 x 384 bytes");
    checkFile(keys.mountModulusPath, "mount modulus", MountModulusBytes, "384 bytes");
    checkValue(keys.passcode, "passcode");
    checkValue(keys.imageSeed, "image seed");
    return problems;
}

QString describePkgKeyProblems(const QStringList& problems) {
    if (problems.isEmpty()) return {};
    return QString("A PKG needs your own keys, and Studio does not include or create any. %1 Add them under Code and build, then try again.").arg(problems.join(' '));
}

QString redactSecrets(QString text, const QStringList& secrets) {
    for (const auto& secret : secrets) {
        if (!secret.isEmpty()) text.replace(secret, QString::fromLatin1(HiddenValue), Qt::CaseInsensitive);
    }
    return text;
}

QString describeBuildStep(const BuildStep& step) {
    return redactSecrets(QFileInfo(step.program).fileName() + ' ' + step.arguments.join(' '), step.secrets);
}

}
