#include "HostTools.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>

#include <array>

namespace Studio {

namespace {

constexpr int NewestPythonMinor = 15;
constexpr int OldestPythonMinor = 11;
constexpr int ProbeTimeoutMilliseconds = 10000;

QString versionPatternFor(const QString& toolId) {
    if (toolId == "cmake") return R"(cmake version\s+(\d+(?:\.\d+)+))";
    if (toolId == "ninja") return R"(^\s*(\d+(?:\.\d+)+))";
    if (toolId == "lld") return R"(LLD\s+(\d+(?:\.\d+)+))";
    if (toolId == "clang") return R"(clang version\s+(\d+(?:\.\d+)+))";
    if (toolId == "python") return R"(Python\s+(\d+(?:\.\d+)+))";
    return {};
}

QString packageName(const QString& toolId, LinuxFlavor flavor) {
    if (toolId == "ninja") return flavor == LinuxFlavor::Arch ? "ninja" : "ninja-build";
    if (toolId == "python") return flavor == LinuxFlavor::Arch ? "python" : "python3";
    return toolId;
}

}

LinuxFlavor linuxFlavorFromProductType(const QString& productType) {
    for (const auto* id : {"ubuntu", "debian", "linuxmint", "pop", "kali", "raspbian", "neon", "elementary", "zorin"}) {
        if (productType == QLatin1String(id)) return LinuxFlavor::Debian;
    }
    for (const auto* id : {"fedora", "rhel", "centos", "rocky", "almalinux"}) {
        if (productType == QLatin1String(id)) return LinuxFlavor::Fedora;
    }
    for (const auto* id : {"arch", "manjaro", "endeavouros", "garuda"}) {
        if (productType == QLatin1String(id)) return LinuxFlavor::Arch;
    }
    return LinuxFlavor::Other;
}

HostPlatform currentHostPlatform() {
    HostPlatform platform;
#if defined(Q_OS_WIN)
    platform.os = HostOs::Windows;
#elif defined(Q_OS_MACOS)
    platform.os = HostOs::MacOs;
#else
    platform.os = HostOs::Linux;
#endif
    platform.architecture = QSysInfo::currentCpuArchitecture();
    platform.flavor = linuxFlavorFromProductType(QSysInfo::productType());
    return platform;
}

QList<HostTool> requiredHostTools() {
    return {
        {"cmake", "CMake", "cmake", QVersionNumber(3, 22)},
        {"ninja", "Ninja", "ninja", {}},
        {"lld", "LLD", "ld.lld", {}},
        {"clang", "Clang", "clang", QVersionNumber(18)},
        {"python", "Python", "python3", QVersionNumber(3, 11)},
    };
}

std::optional<QVersionNumber> parseToolVersion(const QString& toolId, const QString& output) {
    const auto pattern = versionPatternFor(toolId);
    if (pattern.isEmpty()) return std::nullopt;
    const auto match = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption).match(output);
    if (!match.hasMatch()) return std::nullopt;
    return QVersionNumber::fromString(match.captured(1));
}

bool isAppleClang(const QString& output) {
    return output.contains("Apple clang", Qt::CaseInsensitive) || output.contains("Apple LLVM", Qt::CaseInsensitive);
}

QString installCommand(const QString& toolId, const HostPlatform& platform) {
    switch (platform.os) {
    case HostOs::MacOs:
        if (toolId == "clang") return "brew install llvm";
        if (toolId == "lld") return "brew install lld";
        if (toolId == "python") return "brew install python@3.13";
        return "brew install " + toolId;
    case HostOs::Windows:
        if (toolId == "cmake") return "winget install --id Kitware.CMake -e";
        if (toolId == "ninja") return "winget install --id Ninja-build.Ninja -e";
        if (toolId == "clang" || toolId == "lld") return "winget install --id LLVM.LLVM -e";
        if (toolId == "python") return "winget install --id Python.Python.3.13 -e";
        return {};
    case HostOs::Linux:
        break;
    }
    const auto package = packageName(toolId, platform.flavor);
    if (platform.flavor == LinuxFlavor::Fedora) return "sudo dnf install -y " + package;
    if (platform.flavor == LinuxFlavor::Arch) return "sudo pacman -S --needed --noconfirm " + package;
    return "sudo apt-get install -y " + package;
}

ToolCheck evaluateTool(const HostTool& tool, const HostPlatform& platform, bool found, int exitCode, const QString& output) {
    ToolCheck check;
    check.id = tool.id;
    check.name = tool.name;
    const auto unusable = [&](const QString& problem) {
        check.problem = problem;
        check.command = installCommand(tool.id, platform);
        return check;
    };

    if (!found) return unusable(QString("%1 is not installed.").arg(tool.name));
    if (exitCode != 0) return unusable(QString("%1 is installed but could not be run.").arg(tool.name));
    if (tool.id == "clang" && isAppleClang(output)) return unusable("This is Apple's clang, and PS5 code needs Homebrew LLVM.");

    const auto version = parseToolVersion(tool.id, output);
    if (version) check.version = version->toString();
    if (!tool.minimum.isNull()) {
        if (!version) return unusable(QString("Studio could not read the version of %1.").arg(tool.name));
        if (*version < tool.minimum) return unusable(QString("%1 %2 is too old. Studio needs %3 or newer.").arg(tool.name, version->toString(), tool.minimum.toString()));
    }
    check.ok = true;
    return check;
}

QStringList pythonCandidates(const HostPlatform& platform, const QStringList& pathDirectories, const QString& homeDirectory) {
    QStringList candidates;
    const auto add = [&](const QString& path) {
        if (!candidates.contains(path)) candidates << path;
    };

    if (platform.os == HostOs::Windows) {
        for (const auto& directory : pathDirectories) {
            for (const auto* name : {"python.exe", "python3.exe", "py.exe"}) add(directory + '/' + name);
        }
        for (int minor = NewestPythonMinor; minor >= OldestPythonMinor; --minor) {
            const auto folder = QString("Python3%1").arg(minor);
            add(homeDirectory + "/AppData/Local/Programs/Python/" + folder + "/python.exe");
            add("C:/Program Files/" + folder + "/python.exe");
        }
        return candidates;
    }

    // adk setup finds Python by these names on PATH. On macOS, /usr/bin/python3 is Apple's stub:
    // too old to use, and running it can pop up the Xcode tools installer.
    QStringList names{"python3", "python"};
    for (int minor = NewestPythonMinor; minor >= OldestPythonMinor; --minor) names << QString("python3.%1").arg(minor);
    for (const auto& directory : pathDirectories) {
        if (platform.os == HostOs::MacOs && directory == "/usr/bin") continue;
        for (const auto& name : names) add(directory + '/' + name);
    }
    if (platform.os == HostOs::MacOs) {
        // Homebrew keeps the plain python3 name inside the formula's libexec folder.
        for (int minor = NewestPythonMinor; minor >= OldestPythonMinor; --minor) {
            for (const auto* prefix : {"/opt/homebrew/opt", "/usr/local/opt"}) add(QString("%1/python@3.%2/libexec/bin/python3").arg(QLatin1String(prefix)).arg(minor));
            add(QString("/Library/Frameworks/Python.framework/Versions/3.%1/bin/python3").arg(minor));
        }
    }
    return candidates;
}

SystemToolProbe::SystemToolProbe(QStringList extraPathDirectories) : extraDirectories(std::move(extraPathDirectories)) {}

QString SystemToolProbe::findExecutable(const QString& program, const QStringList& directories) const {
    return QStandardPaths::findExecutable(program, directories);
}

bool SystemToolProbe::isExecutable(const QString& path) const {
    const QFileInfo info(path);
    return info.isFile() && info.isExecutable();
}

ProbeResult SystemToolProbe::run(const QString& executable, const QStringList& arguments) const {
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    auto environment = QProcessEnvironment::systemEnvironment();
    if (!extraDirectories.isEmpty()) environment.insert("PATH", extraDirectories.join(QDir::listSeparator()) + QDir::listSeparator() + environment.value("PATH"));
    process.setProcessEnvironment(environment);
    process.start(executable, arguments);
    if (!process.waitForStarted(ProbeTimeoutMilliseconds)) return {};
    if (!process.waitForFinished(ProbeTimeoutMilliseconds)) {
        process.kill();
        process.waitForFinished(1000);
        return {true, -1, {}};
    }
    return {true, process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1, QString::fromLocal8Bit(process.readAllStandardOutput())};
}

ToolCheck checkHostTool(const HostTool& tool, const HostPlatform& platform, const ToolProbe& probe, const QStringList& directories, const QString& homeDirectory) {
    if (tool.id != "python") {
        const auto executable = probe.findExecutable(tool.program, directories);
        if (executable.isEmpty()) return evaluateTool(tool, platform, false, -1, {});
        // Without Xcode tools, /usr/bin/clang offers to install them; it is Apple's clang either way.
        if (platform.os == HostOs::MacOs && tool.id == "clang" && executable == "/usr/bin/clang") return evaluateTool(tool, platform, true, 0, "Apple clang");
        const auto result = probe.run(executable, {"--version"});
        auto check = evaluateTool(tool, platform, true, result.exitCode, result.output);
        check.executable = executable;
        return check;
    }

    // Keeps the newest Python seen, so a message can name the old version that was found.
    std::optional<ToolCheck> best;
    for (const auto& candidate : pythonCandidates(platform, directories, homeDirectory)) {
        if (!probe.isExecutable(candidate)) continue;
        const auto result = probe.run(candidate, {"--version"});
        auto check = evaluateTool(tool, platform, true, result.exitCode, result.output);
        check.executable = candidate;
        if (check.ok) return check;
        if (!best || !check.version.isEmpty()) best = check;
    }
    return best ? *best : evaluateTool(tool, platform, false, -1, {});
}

std::optional<double> parseAdkSetupProgress(const QString& line) {
    // The order setup.py fetches the libraries for --target ps5.
    struct Milestone {
        QLatin1StringView marker;
        double fraction;
    };
    static constexpr std::array<Milestone, 6> Milestones{{
        {QLatin1StringView("prx-headers"), 0.1},
        {QLatin1StringView("prx-linux-unpatched"), 0.3},
        {QLatin1StringView("llvm-cxx-ps"), 0.5},
        {QLatin1StringView("libc6-dev"), 0.7},
        {QLatin1StringView("linux-libc-dev"), 0.8},
        {QLatin1StringView("Prepared ps5"), 0.92},
    }};
    if (line.startsWith("Using AnyPS5")) return 0.04;
    for (const auto& milestone : Milestones) {
        if (line.contains(milestone.marker)) return milestone.fraction;
    }
    return std::nullopt;
}

QString describeAdkSetupFailure(const QStringList& output, const QString& runnerMessage) {
    const auto all = output.join('\n');
    // The server answered but could not deliver, so the person's connection is not the problem.
    if (QRegularExpression(R"(HTTP Error 5\d\d)").match(all).hasMatch()) return "The server with the PS5 libraries is having trouble right now. Finished downloads are kept. Wait a minute, then choose Retry.";
    for (const auto* needle : {"URLError", "HTTPError", "timed out", "Temporary failure", "Network is unreachable", "No address associated", "nodename nor servname", "ConnectionError", "SSLError", "CERTIFICATE_VERIFY_FAILED"}) {
        if (all.contains(QLatin1String(needle))) return "Studio could not download the PS5 libraries. Check your internet connection, then choose Retry.";
    }
    // adk ends with "setup.py exited with code 1"; the Python error before that says what went wrong.
    QString reason;
    for (auto line = output.crbegin(); line != output.crend() && reason.isEmpty(); ++line) {
        const auto text = line->trimmed();
        if (!text.startsWith("adk: setup.py exited")) reason = text;
    }
    if (reason.startsWith("adk: ")) reason = reason.mid(5);
    if (reason.isEmpty()) reason = runnerMessage;
    if (!reason.endsWith('.') && !reason.endsWith('!')) reason += '.';
    return QString("Installing the PS5 libraries stopped: %1 Choose Retry to run it again. If it keeps failing, the log has the details.").arg(reason);
}

}
