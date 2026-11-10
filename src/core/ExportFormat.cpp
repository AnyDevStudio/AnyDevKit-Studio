#include "ExportFormat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

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
        {ExportFormat::Windows, "windows", "Windows", "Build for Windows", "Build for Windows again"},
        {ExportFormat::Linux, "linux", "Linux", "Build for Linux", "Build for Linux again"},
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

bool isDesktopFormat(ExportFormat format) { return format == ExportFormat::Windows || format == ExportFormat::Linux; }
QString desktopTarget(ExportFormat format) { return format == ExportFormat::Windows ? "windows" : "linux"; }

namespace {

QString helperFolder(const Project& project) { return project.root + "/build/.studio"; }

// Moves a freshly set up target into the dependency folder and lists it there, as adk setup would have.
constexpr auto PlaceDependenciesScript = R"cmake(# Written by AnyDevKit Studio. Moves a target set up on its own into the dependency folder.
file(MAKE_DIRECTORY "${DEPENDENCY_ROOT}")
if(EXISTS "${DEPENDENCY_ROOT}/${TARGET}")
    file(REMOVE_RECURSE "${DEPENDENCY_ROOT}/${TARGET}")
endif()
file(RENAME "${STAGING}/${TARGET}" "${DEPENDENCY_ROOT}/${TARGET}")
if(NOT EXISTS "${DEPENDENCY_ROOT}/include" AND EXISTS "${STAGING}/include")
    file(RENAME "${STAGING}/include" "${DEPENDENCY_ROOT}/include")
endif()
set(manifest "${DEPENDENCY_ROOT}/dependencies.json")
if(EXISTS "${manifest}")
    file(READ "${manifest}" json)
    string(JSON count LENGTH "${json}" targets)
    set(listed FALSE)
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(index RANGE ${last})
            string(JSON name GET "${json}" targets ${index})
            if(name STREQUAL "${TARGET}")
                set(listed TRUE)
            endif()
        endforeach()
    endif()
    if(NOT listed)
        string(JSON json SET "${json}" targets ${count} "\"${TARGET}\"")
        file(WRITE "${manifest}" "${json}")
    endif()
else()
    file(COPY "${STAGING}/dependencies.json" DESTINATION "${DEPENDENCY_ROOT}")
endif()
file(REMOVE_RECURSE "${STAGING}")
)cmake";

// An SDK built on a Mac or Linux has no Windows startup library; the published Windows SDK of the same release does.
constexpr auto WindowsStartupScript = R"cmake(# Written by AnyDevKit Studio. Borrows the Windows startup library from the published Windows SDK.
set(zip "${WORK}/AnyDevKit-windows.zip")
file(DOWNLOAD "https://github.com/boykopovar/AnyDevKit/releases/download/v${RELEASE}/AnyDevKit-${RELEASE}-windows-x86_64.zip" "${zip}" STATUS status TLS_VERIFY ON)
list(GET status 0 code)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "Could not download the Windows SDK of AnyDevKit ${RELEASE}: ${status}")
endif()
file(ARCHIVE_EXTRACT INPUT "${zip}" DESTINATION "${WORK}/sdk" PATTERNS "lib/libAnyDevKitWindowsStartup.a")
if(NOT EXISTS "${WORK}/sdk/lib/libAnyDevKitWindowsStartup.a")
    message(FATAL_ERROR "The Windows SDK of AnyDevKit ${RELEASE} has no Windows startup library.")
endif()
file(COPY "${WORK}/sdk/lib/libAnyDevKitWindowsStartup.a" DESTINATION "${SDK_ROOT}/lib")
file(REMOVE_RECURSE "${WORK}")
)cmake";

// Makes the program runnable and puts the folder into one file to hand out.
constexpr auto PackageDesktopScript = R"cmake(# Written by AnyDevKit Studio. Packs a desktop build into one file.
if(EXISTS "${SOURCE}/${NAME}")
    file(CHMOD "${SOURCE}/${NAME}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()
file(REMOVE "${OUTPUT}")
file(GLOB entries RELATIVE "${SOURCE}" "${SOURCE}/*")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar ${MODE} "${OUTPUT}" ${FORMAT} -- ${entries} WORKING_DIRECTORY "${SOURCE}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Could not create ${OUTPUT}")
endif()
)cmake";

// llvm-rc on a Mac or Linux reads a path that starts with / as one of its options; this hands it relative ones.
constexpr auto ResourceCompilerWrapper = R"sh(#!/bin/sh
# Written by AnyDevKit Studio. llvm-rc takes /Users/... and /home/... for options, so paths go to it relative.
# This folder comes first on PATH, so the real llvm-rc is the next one along it.
here=$(cd "$(dirname "$0")" && pwd)
rc=
old_ifs=$IFS; IFS=:
for dir in $PATH; do
    [ "$(cd "$dir" 2>/dev/null && pwd)" = "$here" ] && continue
    if [ -x "$dir/llvm-rc" ]; then rc="$dir/llvm-rc"; break; fi
done
IFS=$old_ifs
[ -n "$rc" ] || { echo "llvm-rc was not found. Install LLVM (on a Mac: brew install llvm lld) and try again." >&2; exit 1; }
set -- "$@" --
while [ "$1" != "--" ]; do
    arg=$1; shift
    case "$arg" in
        /FO|/fo) set -- "$@" -FO ;;
        /*) if [ -e "$arg" ] || [ -d "$(dirname "$arg")" ]; then set -- "$@" "$(python3 -c 'import os,sys; print(os.path.relpath(sys.argv[1]))' "$arg" 2>/dev/null || echo "$arg")"; else set -- "$@" "$arg"; fi ;;
        *) set -- "$@" "$arg" ;;
    esac
done
shift
exec "$rc" "$@"
)sh";

}

QString desktopToolPath(const Project& project, ExportFormat format, bool hostIsWindows) {
    return format == ExportFormat::Windows && !hostIsWindows ? helperFolder(project) : QString();
}

bool writeDesktopHelpers(const Project& project, QString& error) {
    const auto folder = helperFolder(project);
    if (!QDir().mkpath(folder)) {
        error = QString("%1 could not be created. Check that the project folder is writable.").arg(QDir(project.root).relativeFilePath(folder));
        return false;
    }
    const QList<std::pair<QString, const char*>> files{{"place-dependencies.cmake", PlaceDependenciesScript}, {"windows-startup.cmake", WindowsStartupScript},
                                                       {"package-desktop.cmake", PackageDesktopScript}, {"llvm-rc", ResourceCompilerWrapper}};
    for (const auto& [name, text] : files) {
        QSaveFile file(folder + '/' + name);
        const QByteArray contents(text);
        if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
            error = QString("build/.studio/%1 could not be written: %2.").arg(name, file.errorString());
            return false;
        }
    }
    QFile::setPermissions(folder + "/llvm-rc", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
    return true;
}

QString exportOutputPath(const Project& project, ExportFormat format) {
    if (format == ExportFormat::Ffpfsc) return project.outputPath;
    if (format == ExportFormat::Windows) return project.root + "/dist/" + project.name + "-windows-x64.zip";
    if (format == ExportFormat::Linux) return project.root + "/dist/" + project.name + "-linux-x64.tar.gz";
    return project.root + "/dist/ps5/" + project.name + (format == ExportFormat::Zip ? ".zip" : ".pkg");
}

QList<BuildStep> exportBuildSteps(const Project& project, ExportFormat format, const PkgKeys& keys, const DesktopBuildContext& desktop) {
    if (isDesktopFormat(format)) {
        const auto target = desktopTarget(format);
        const auto helpers = helperFolder(project);
        const auto label = format == ExportFormat::Windows ? QString("Windows") : QString("Linux");
        QList<BuildStep> steps;
        if (desktop.needsDependencies) {
            const auto staging = project.root + "/build/.adk-" + target;
            QStringList setup{"setup", staging, "--target", target};
            if (!desktop.cacheDirectory.isEmpty()) setup << "--cache" << desktop.cacheDirectory;
            steps.append({"libraries", "Install " + label + " libraries", adkExecutable(project), setup, {staging}, {}, {}, {}});
            steps.append({"place", "Add them to your libraries", "cmake", {"-DSTAGING=" + staging, "-DDEPENDENCY_ROOT=" + desktop.dependencyRoot, "-DTARGET=" + target, "-P", helpers + "/place-dependencies.cmake"}, {}, {}, {}, {}});
        }
        if (format == ExportFormat::Windows && desktop.needsWindowsStartup) {
            steps.append({"startup", "Get the Windows startup library", "cmake",
                          {"-DRELEASE=" + QString(WindowsStartupRelease), "-DWORK=" + project.root + "/build/.studio/windows-sdk", "-DSDK_ROOT=" + project.sdkRoot, "-P", helpers + "/windows-startup.cmake"}, {}, {}, {}, {}});
        }
        // Away from Windows the SDK finds llvm-rc on PATH, where Studio puts build/.studio first (desktopToolPath).
        steps.append({"configure", "Configure", "cmake", {"--preset", target}, {}, {}, {}, {}});
        steps.append({"compile", "Compile", "cmake", {"--build", "--preset", target}, {}, {}, {}, {}});
        const auto output = exportOutputPath(project, format);
        const auto source = project.root + "/dist/" + target;
        QStringList pack{"-DSOURCE=" + source, "-DOUTPUT=" + output, "-DNAME=" + project.name};
        if (format == ExportFormat::Windows) pack << "-DMODE=cf" << "-DFORMAT=--format=zip";
        else pack << "-DMODE=czf" << "-DFORMAT=";
        pack << "-P" << helpers + "/package-desktop.cmake";
        steps.append({"pack", "Pack for sharing", "cmake", pack, {output}, {}, {}, {}});
        return steps;
    }
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
    if (isDesktopFormat(format) && !isPlainFileName(project.name)) {
        return QString("The project name \"%1\" cannot be used for a file name, so nothing was built. Give the project a plain name in adk.json and try again.").arg(project.name);
    }
    if (isDesktopFormat(format)) return {};
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
