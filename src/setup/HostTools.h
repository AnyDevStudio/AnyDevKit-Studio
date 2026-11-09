#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVersionNumber>

#include <optional>

namespace Studio {

enum class HostOs { Windows, Linux, MacOs };
enum class LinuxFlavor { Debian, Fedora, Arch, Other };

struct HostPlatform {
    HostOs os = HostOs::Linux;
    QString architecture = "x86_64"; // QSysInfo names: x86_64, arm64
    LinuxFlavor flavor = LinuxFlavor::Debian;
};

HostPlatform currentHostPlatform();
LinuxFlavor linuxFlavorFromProductType(const QString& productType);

struct HostTool {
    QString id;      // cmake, ninja, lld, clang, python
    QString name;    // shown to the person
    QString program; // looked up on PATH; Python is searched for in more places
    QVersionNumber minimum; // null when any version works
};

QList<HostTool> requiredHostTools();
std::optional<QVersionNumber> parseToolVersion(const QString& toolId, const QString& output);
bool isAppleClang(const QString& output);
// The one line a person runs to install the tool on this computer.
QString installCommand(const QString& toolId, const HostPlatform& platform);

struct ToolCheck {
    QString id;
    QString name;
    bool checking = false;
    bool ok = false;
    QString version;
    QString executable;
    QString problem; // why it cannot be used, empty when it can
    QString command; // how to install it, set whenever the tool is not usable
};

// `found` is whether the program exists at all; `exitCode` and `output` come from asking its version.
ToolCheck evaluateTool(const HostTool& tool, const HostPlatform& platform, bool found, int exitCode, const QString& output);

// Everywhere Python 3.11+ might be, most likely first: PATH names, then the usual install folders.
QStringList pythonCandidates(const HostPlatform& platform, const QStringList& pathDirectories, const QString& homeDirectory);

// How the checks reach the outside world; tests provide their own.
struct ProbeResult {
    bool started = false;
    int exitCode = -1;
    QString output;
};

class ToolProbe {
public:
    virtual ~ToolProbe() = default;
    virtual QString findExecutable(const QString& program, const QStringList& directories) const = 0;
    virtual bool isExecutable(const QString& path) const = 0;
    virtual ProbeResult run(const QString& executable, const QStringList& arguments) const = 0;
};

class SystemToolProbe final : public ToolProbe {
public:
    explicit SystemToolProbe(QStringList extraPathDirectories);
    QString findExecutable(const QString& program, const QStringList& directories) const override;
    bool isExecutable(const QString& path) const override;
    ProbeResult run(const QString& executable, const QStringList& arguments) const override;

private:
    QStringList extraDirectories;
};

// Runs the check for one tool. Python tries every candidate and keeps the first that is new enough.
ToolCheck checkHostTool(const HostTool& tool, const HostPlatform& platform, const ToolProbe& probe, const QStringList& directories, const QString& homeDirectory);

// Reports the fraction of a `adk setup` line, or nothing when the line carries no progress.
std::optional<double> parseAdkSetupProgress(const QString& line);
QString describeAdkSetupFailure(const QStringList& output, const QString& runnerMessage);

}
