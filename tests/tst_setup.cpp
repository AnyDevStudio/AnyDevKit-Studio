#include "HostTools.h"
#include "HttpClient.h"
#include "ReleaseAssets.h"
#include "SafeArchive.h"
#include "SetupFlow.h"
#include "SetupProgress.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace Studio;

namespace {

// Real `gzip -9` output (dynamic Huffman) of a tar with bin/adk and a long CMake file.
constexpr auto RealTarGz =
    "H4sIAAAAAAAC/+2aQWsUQRBG95xfseJFL8lUV1dV91HUgwgKCl4l0VUXNYKJgv/eJbD1RsGTKIj1XdJUN7s1tfNeX3Kxvzw7f/1+8yezHBJmN3"
    "8P+fnvsjRlfVMPCdtsl81fyJer6/PPh6/c/J+5fevs4vAKXL072b1692l7eBVONpX/Jx/2F2evPp6/353du/z2YPf18f6a1f1Pl2/2b09v9n+T"
    "f+/9F/w39ZAf+RfruhT/fyNXu+s79x48fvni4bPnj54+2S6nchp3T6pc5SpXucpVrnKVq1zlKle5ylWucpWrXOUqV7nKVa5ylatc5X+mfHv7YX"
    "+52y7HxTA/LmPR49IsD+iM47L1flxOyVXP/Zi57z0P9DHys9SOSxktP3ZmMyP3Q3PfIg/0lt22yG6lZbcz8sBoecAjD5hku+rZbpNs1wbPmPvh"
    "ue9LHuiW7eqS7UrPdmVZjEHnCZ95wrozaGXQ/D7BoJVB575rMOjOoLNfiey30czI/WiDQRuD5udxnkecQeeBIXnA3Rm0Muhsty3BozPo3A8LBt"
    "0ZtPCyDAZtq0HnidEHgza+LfvVkf025XXJdqfmfgxn0MqgoaMFg169Tvx80NE6g4YO4edxnkey2+mDQRuDBo8l21VgbiuYoWMBdoOOJQ90YG7A"
    "LMA8scHABo4OTKEdmhs0A/PEBoENHB10YFZgFmCG5YENAhuYAwcwN2AWYJ7YYGADRwcGywrLAsugPLFBYAObzpxhHZYFlicyGMjAsYEpsANzA2"
    "ZYnsggkIFjgw7LCssCy6A8kEEgA3PYgOUGywLLExkMZODYwEBZQVnmyk35AcggkIHNPNBBuY3V48A6Lhi4wJGBKazDcoNlUJ64IHCBI4MOygrK"
    "Asr0ggoCFZjxrkFyg2SB5IkKBipwXNAhWSFZIBmQByqIvroouQghuUGyQPJEBQMVOC4wLnYF5QbKkDxRQaACxwUdkhWSZUUyY4YMTGBc6x2QGy"
    "DD8bSV17gFMUGHY4VjGavZM2VuQURgXOodjhscCxxPRDAQgWMC41ZXQG6yuuS4kwEdETgm6HCscCxwTC94IPCAcacrGDcwhuKJBgINOB7oUKxQ"
    "LFAMxAMNBBowbvQOxQ2KBYonGhhowPGAcaUrHLcVx7xJaCDQgHOjdzBWMBYwPiAP6JggMIFxpysgN0CG44kIAhE4JuhwrHAscAzGAxEEIjCu9A"
    "7HbXWlw/FEBGMlgoMJ6l9XK5VKpVKpVCqVSqVSqVQqlUqlUqlUKpVKpVKpVCqHfAeAH3jOAFAAAA==";

// Real Python zipfile output: two deflated members and one stored, as a Windows zip has.
constexpr auto RealZip =
    "UEsDBBQAAAAIAI08aV0+1DGlGQAAAIACAAALAAAAYmluL2Fkay5leGXzjVJIS8xOVSgoyk8vSszl8h3lj/LpyAcAUEsDBBQAAAAIAI08aV09EN"
    "dXDAMAALknAAApAAAAbGliL2NtYWtlL0FueURldktpdC9BbnlEZXZLaXRDb25maWcuY21ha2XtlbuqFUEQRXO/4oKJJjL17g4FDURQUDA1MhDE"
    "5Pr/aHL2ml8QOmuqipk9j7Xq+cefV2/fffz+7f2Xrx8+f3q63tibef3i+ZRP+ZRP+ZRP+ZRP+ZRP+ZRP+ZRP+ZRP+ZRP+ZRP+ZRP+ZRP+ZRP+X"
    "8pv3z69fP3j6frcVjVj+Nc8ThWaSD2PI6e+Thu0ynVn61+pwZyLV0r6nG05brsVpil/oT6NRpIV1ofpTVX2j0aWK6BHg2UKW604ropbi2eUf1p"
    "9fvSQJbixqW4lopr16WJVZrorYlK5Y2tvJ58H8Xdqf5s9Ts0kEt5I5TXRnmdMEv9cfVrNJDO52mex5R2jwaWaaBbA2WKG624fg2PzotWf0r9vj"
    "SQpbi+FdeSv+m6NLFSE72LuylvLOX14HdR3B3qz1K/QwO5oMOV1+b2O/H5oMPVr4EO4/M0z2NKu1sDyzTQDR6X4gYw+w1m6LiAvaDj0kACswOz"
    "AfPGBgsbNDqogHZodmgG5o0NBhs0OkhgDmA2YIblhQ0GG1QDBzA7MBswb2ywsEGjg4LlgGWDZVDe2GCwQW0NJCw7LBssb2SwkEFjgwpgB2YHZl"
    "jeyGCQQWODhOWAZYNlUF7IYJBBNWzAssOywfJGBgsZNDYoUA5Qtn1zky6ADAYZ1NZAgrKv2+PAOi5YuKCRQQWsw7LDMihvXDC4oJFBgnKAsoEy"
    "WVDBoIIq/jVIdkg2SN6oYKGCxgUJyQHJBsmAvFDB5G1Rsggh2SHZIHmjgoUKGhcUiz1A2UEZkjcqGFTQuCAhOSDZbiTzmiEDExRrPQHZARmOd9"
    "28xhbEBAnHAce2bu+et8wWRATFUk84djg2ON6IYCGCxgTFVg9AdrstOV0LEQwiaEyQcBxwbHBMFjwweKDY6QHGDsZQvNHAoIHGAwnFAcUGxUC8"
    "0MCggWKjJxQ7FBsUbzSw0EDjgWKlBxz7jWP+JDQwaKDZ6AnGAcYGxv+QB3RMMJig2OkByA7IcLwRwSCCxgQJxwHHBsdgvBDBIIJipScc+22lw/"
    "FGBOsmgn8m+AtQSwMEFAAAAAAAAAAhAHe905EMAAAADAAAAA8AAABkb2NzL3JlYWRtZS50eHRzdG9yZWQgZmlsZQpQSwECFAMUAAAACACNPGld"
    "PtQxpRkAAACAAgAACwAAAAAAAAAAAAAAgAEAAAAAYmluL2Fkay5leGVQSwECFAMUAAAACACNPGldPRDXVwwDAAC5JwAAKQAAAAAAAAAAAAAAgA"
    "FCAAAAbGliL2NtYWtlL0FueURldktpdC9BbnlEZXZLaXRDb25maWcuY21ha2VQSwECFAMUAAAAAAAAACEAd73TkQwAAAAMAAAADwAAAAAAAAAA"
    "AAAAgAGVAwAAZG9jcy9yZWFkbWUudHh0UEsFBgAAAAADAAMAzQAAAM4DAAAAAA==";

quint32 crc32Of(const QByteArray& data) {
    quint32 crc = 0xFFFFFFFFu;
    for (const auto byte : data) {
        crc ^= static_cast<quint8>(byte);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void appendLittle(QByteArray& out, quint32 value, int bytes) {
    for (int shift = 0; shift < bytes * 8; shift += 8) out.append(static_cast<char>((value >> shift) & 0xFF));
}

// A gzip of stored deflate blocks, so tests can build any archive without a compressor.
QByteArray gzipStored(const QByteArray& data) {
    QByteArray out = QByteArray::fromHex("1f8b08000000000002ff"); // the fixed 10 byte header
    qsizetype offset = 0;
    do {
        const auto length = std::min<qsizetype>(65535, data.size() - offset);
        const bool last = offset + length >= data.size();
        out.append(last ? char(1) : char(0));
        appendLittle(out, static_cast<quint32>(length), 2);
        appendLittle(out, static_cast<quint32>(~length & 0xFFFF), 2);
        out.append(data.mid(offset, length));
        offset += length;
    } while (offset < data.size());
    appendLittle(out, crc32Of(data), 4);
    appendLittle(out, static_cast<quint32>(data.size()), 4);
    return out;
}

struct TarMember {
    QByteArray name;
    QByteArray body;
    char type = '0';
    QByteArray linkName;
    quint32 mode = 0644;
};

QByteArray tarHeader(const TarMember& member) {
    QByteArray header(512, '\0');
    const auto put = [&](int offset, const QByteArray& value) { header.replace(offset, value.size(), value); };
    put(0, member.name.left(100));
    put(100, QByteArray::number(member.mode, 8).rightJustified(7, '0'));
    put(108, "0000000");
    put(116, "0000000");
    put(124, QByteArray::number(member.body.size(), 8).rightJustified(11, '0'));
    put(136, QByteArray("00000000000"));
    put(148, "        ");
    header[156] = member.type;
    put(157, member.linkName.left(100));
    put(257, QByteArray("ustar\0" "00", 8));
    int sum = 0;
    for (const auto byte : std::as_const(header)) sum += static_cast<quint8>(byte);
    put(148, QByteArray::number(sum, 8).rightJustified(6, '0') + QByteArray("\0 ", 2));
    return header;
}

QByteArray tarOf(const QList<TarMember>& members) {
    QByteArray out;
    for (const auto& member : members) {
        out += tarHeader(member);
        out += member.body;
        out += QByteArray((512 - member.body.size() % 512) % 512, '\0');
    }
    out += QByteArray(1024, '\0');
    return out;
}

void writeBytes(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

// Unpacks one tar's members into a fresh folder; returns the result and leaves the folder for checks.
ExtractResult extractTar(const QTemporaryDir& workspace, const QList<TarMember>& members, const QString& name = "out") {
    const auto archive = workspace.filePath(name + ".tar.gz");
    writeBytes(archive, gzipStored(tarOf(members)));
    return extractArchive(archive, workspace.filePath(name));
}

QJsonObject assetJson(const QString& name, const QString& digest = QString(64, 'a'), const QString& url = {}, qint64 size = 1234) {
    QJsonObject asset{{"name", name}, {"size", size}, {"browser_download_url", url.isEmpty() ? "https://github.com/o/r/releases/download/x/" + name : url}};
    if (!digest.isNull()) asset["digest"] = digest.isEmpty() ? QString() : "sha256:" + digest;
    return asset;
}

QJsonObject releaseJson(const QString& tag, const QJsonArray& assets, const QString& published = "2026-11-08T20:05:51Z", bool draft = false, bool prerelease = false) {
    return {{"tag_name", tag}, {"draft", draft}, {"prerelease", prerelease}, {"published_at", published}, {"assets", assets}};
}

QByteArray json(const QJsonObject& object) { return QJsonDocument(object).toJson(); }
QByteArray json(const QJsonArray& array) { return QJsonDocument(array).toJson(); }

HostPlatform macArm() { return {HostOs::MacOs, "arm64", LinuxFlavor::Other}; }
HostPlatform linuxX86() { return {HostOs::Linux, "x86_64", LinuxFlavor::Debian}; }
HostPlatform windowsX86() { return {HostOs::Windows, "x86_64", LinuxFlavor::Other}; }

HostTool toolById(const QString& id) {
    for (const auto& tool : requiredHostTools()) {
        if (tool.id == id) return tool;
    }
    return {};
}

class FakeTransfer final : public HttpTransfer {
public:
    using HttpTransfer::HttpTransfer;
    int httpStatus = 0;
    qint64 partialStart = 0;
    QString transportError;
    QByteArray buffered;
    bool aborted = false;

    int status() const override { return httpStatus; }
    qint64 rangeStart() const override { return partialStart; }
    QString errorMessage() const override { return transportError; }
    QByteArray takeData() override { return std::exchange(buffered, {}); }
    void abort() override {
        aborted = true;
        transportError = "Operation canceled";
        emit finished();
    }
};

struct FakeReply {
    int status = 200;
    QByteArray body;
    QString error;      // a transport failure
    qint64 rangeStart = 0;
    bool hold = false;  // delivers the body, then never finishes until abort()
};

class FakeHttp final : public HttpClient {
public:
    QHash<QString, QList<FakeReply>> replies; // keyed by URL; the last reply for a URL repeats
    QList<QPair<QString, qint64>> requests;
    QPointer<FakeTransfer> heldTransfer;

    HttpTransfer* get(const QUrl& url, const QString& accept, qint64 rangeStart, QObject* parent) override {
        Q_UNUSED(accept);
        const auto key = url.toString();
        requests.append({key, rangeStart});
        auto* transfer = new FakeTransfer(parent);
        auto& queue = replies[key];
        if (queue.isEmpty()) {
            QTimer::singleShot(0, transfer, [transfer] {
                transfer->transportError = "Host not found";
                emit transfer->finished();
            });
            return transfer;
        }
        const auto reply = queue.size() > 1 ? queue.takeFirst() : queue.first();
        QTimer::singleShot(0, transfer, [this, transfer, reply] {
            transfer->httpStatus = reply.status;
            transfer->partialStart = reply.rangeStart;
            transfer->transportError = reply.error;
            transfer->buffered = reply.body;
            if (!reply.body.isEmpty()) emit transfer->dataReady();
            if (reply.hold) heldTransfer = transfer;
            else emit transfer->finished();
        });
        return transfer;
    }
};

class FakeProbe final : public ToolProbe {
public:
    // Tools are found under /fake with versions new enough unless a test changes these.
    QHash<QString, QString> output{
        {"/fake/cmake", "cmake version 3.31.6\nCMake suite maintained by Kitware"},
        {"/fake/ninja", "1.13.2"},
        {"/fake/ld.lld", "Homebrew LLD 23.1.3 (compatible with GNU linkers)"},
        {"/fake/clang", "Homebrew clang version 23.1.3\nTarget: arm64-apple-darwin25.6.0\nThread model: posix"},
        {"/fake/python3", "Python 3.13.13"},
    };
    QSet<QString> missing;
    QHash<QString, QString> foundAt; // a program found outside /fake
    mutable QStringList ran;

    QString findExecutable(const QString& program, const QStringList& directories) const override {
        Q_UNUSED(directories);
        if (foundAt.contains(program)) return foundAt[program];
        const auto path = "/fake/" + program;
        return output.contains(path) && !missing.contains(program) ? path : QString();
    }
    bool isExecutable(const QString& path) const override { return output.contains(path) && !missing.contains(QFileInfo(path).fileName()); }
    ProbeResult run(const QString& executable, const QStringList&) const override {
        ran << executable;
        if (!output.contains(executable)) return {};
        return {true, 0, output[executable]};
    }
};

// The adk the flow unpacks: records how it was called, and fails once if a marker file exists.
constexpr auto FakeAdkScript =
    "#!/bin/sh\n"
    "echo \"$@\" > \"$FAKE_ADK_ARGS\"\n"
    "echo \"$PATH\" > \"$FAKE_ADK_ARGS.path\"\n"
    "echo \"Using AnyPS5 v0.1.4 and llvm-cxx-ps v0.1.9\"\n"
    "echo \"Downloading prx-headers-v0.1.4.zip\"\n"
    "if [ -f \"$FAKE_ADK_FAIL_ONCE\" ]; then rm -f \"$FAKE_ADK_FAIL_ONCE\"; echo \"ValueError: simulated setup failure\"; exit 1; fi\n"
    "mkdir -p \"$2/ps5/sysroot/usr/include\"\n"
    "echo \"Prepared ps5\"\n"
    "echo \"Dependencies installed: $2\"\n";

QByteArray fakeSdkArchive() {
    return gzipStored(tarOf({
        {"bin/adk", FakeAdkScript, '0', {}, 0755},
        {"lib/cmake/AnyDevKit/AnyDevKitConfig.cmake", "set(ADK 1)\n"},
        {"tools/setup.py", "# setup\n"},
    }));
}

const QString LatestReleaseUrl = "https://api.github.com/repos/boykopovar/AnyDevKit/releases/latest";
const QString LinuxAssetName = "AnyDevKit-0.1.7-linux-x86_64.tar.gz";
const QString LinuxAssetUrl = "https://github.com/boykopovar/AnyDevKit/releases/download/v0.1.7/" + LinuxAssetName;

QString sha256Hex(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

struct FlowFixture {
    QTemporaryDir workspace;
    FakeHttp http;
    FakeProbe probe;
    QString dataDirectory = workspace.filePath("app data"); // the real location has spaces in it
    QString argsFile = workspace.filePath("args.txt");
    QString failOnceFile = workspace.filePath("fail-once");

    SetupFlowConfig config(const HostPlatform& platform = linuxX86()) {
        SetupFlowConfig config;
        config.dataDirectory = dataDirectory;
        config.platform = platform;
        config.toolDirectories = {"/fake"};
        config.http = &http;
        config.probe = &probe;
        config.environment = QProcessEnvironment::systemEnvironment();
        config.environment.insert("FAKE_ADK_ARGS", argsFile);
        config.environment.insert("FAKE_ADK_FAIL_ONCE", failOnceFile);
        config.homeDirectory = workspace.filePath("home");
        return config;
    }

    void serveRelease(const QByteArray& archive, const QString& digest = {}) {
        http.replies[LatestReleaseUrl] = {FakeReply{200, json(releaseJson("v0.1.7", QJsonArray{assetJson(LinuxAssetName, digest.isEmpty() ? sha256Hex(archive) : digest, LinuxAssetUrl, archive.size())}))}};
        http.replies[LinuxAssetUrl] = {FakeReply{200, archive}};
    }
};

}

class TestSetup : public QObject {
    Q_OBJECT

private slots:
    // ---- Tool version parsing ----

    void parsesToolVersions_data() {
        QTest::addColumn<QString>("tool");
        QTest::addColumn<QString>("output");
        QTest::addColumn<QString>("expected");
        QTest::newRow("cmake") << "cmake" << "cmake version 3.31.6\n\nCMake suite maintained" << "3.31.6";
        QTest::newRow("cmake rc") << "cmake" << "cmake version 4.0.0-rc2" << "4.0.0";
        QTest::newRow("ninja") << "ninja" << "1.13.2\n" << "1.13.2";
        QTest::newRow("ninja kitware") << "ninja" << "1.11.1.git.kitware.jobserver-1" << "1.11.1";
        QTest::newRow("lld homebrew") << "lld" << "Homebrew LLD 23.1.3 (compatible with GNU linkers)" << "23.1.3";
        QTest::newRow("lld ubuntu") << "lld" << "Ubuntu LLD 18.1.3 (compatible with GNU linkers)" << "18.1.3";
        QTest::newRow("clang homebrew") << "clang" << "Homebrew clang version 23.1.3\nTarget: arm64" << "23.1.3";
        QTest::newRow("clang ubuntu") << "clang" << "Ubuntu clang version 18.1.3 (1ubuntu1)" << "18.1.3";
        QTest::newRow("clang windows") << "clang" << "clang version 18.1.8\nTarget: x86_64-pc-windows-msvc" << "18.1.8";
        QTest::newRow("python") << "python" << "Python 3.13.13\n" << "3.13.13";
        QTest::newRow("no version") << "cmake" << "Python was not found; install from the Store" << "";
        QTest::newRow("empty") << "clang" << "" << "";
    }

    void parsesToolVersions() {
        QFETCH(QString, tool);
        QFETCH(QString, output);
        QFETCH(QString, expected);
        const auto version = parseToolVersion(tool, output);
        QCOMPARE(version ? version->toString() : QString(), expected);
    }

    void recognizesAppleClang() {
        QVERIFY(isAppleClang("Apple clang version 21.0.0 (clang-2100.3.34.2)"));
        QVERIFY(!isAppleClang("Homebrew clang version 23.1.3"));
    }

    void acceptsToolsThatMeetMinimums_data() {
        QTest::addColumn<QString>("tool");
        QTest::addColumn<QString>("output");
        QTest::addColumn<bool>("ok");
        QTest::newRow("cmake exact") << "cmake" << "cmake version 3.22.0" << true;
        QTest::newRow("cmake old") << "cmake" << "cmake version 3.21.9" << false;
        QTest::newRow("clang 18") << "clang" << "clang version 18.1.0" << true;
        QTest::newRow("clang 17") << "clang" << "clang version 17.0.6" << false;
        QTest::newRow("python 3.11") << "python" << "Python 3.11.0" << true;
        QTest::newRow("python 3.9") << "python" << "Python 3.9.6" << false;
        QTest::newRow("any ninja") << "ninja" << "1.9.0" << true;
        QTest::newRow("ninja version unknown") << "ninja" << "weird" << true;
    }

    void acceptsToolsThatMeetMinimums() {
        QFETCH(QString, tool);
        QFETCH(QString, output);
        QFETCH(bool, ok);
        const auto check = evaluateTool(toolById(tool), linuxX86(), true, 0, output);
        QCOMPARE(check.ok, ok);
        QCOMPARE(check.problem.isEmpty(), ok);
        QCOMPARE(check.command.isEmpty(), ok);
    }

    void reportsMissingAndUnusableTools() {
        const auto missing = evaluateTool(toolById("cmake"), linuxX86(), false, -1, {});
        QVERIFY(!missing.ok);
        QVERIFY(missing.problem.contains("not installed"));
        QCOMPARE(missing.command, QString("sudo apt-get install -y cmake"));

        const auto crashed = evaluateTool(toolById("cmake"), linuxX86(), true, 1, "cmake version 3.31.6");
        QVERIFY(!crashed.ok);
        QVERIFY(crashed.problem.contains("could not be run"));

        const auto old = evaluateTool(toolById("clang"), linuxX86(), true, 0, "clang version 14.0.6");
        QVERIFY(old.problem.contains("14.0.6"));
        QVERIFY(old.problem.contains("18"));
    }

    void rejectsAppleClangOnMac() {
        const auto check = evaluateTool(toolById("clang"), macArm(), true, 0, "Apple clang version 21.0.0 (clang-2100.3.34.2)");
        QVERIFY(!check.ok);
        QVERIFY(check.problem.contains("Apple"));
        QCOMPARE(check.command, QString("brew install llvm"));
    }

    void installCommandsPerOperatingSystem() {
        QCOMPARE(installCommand("cmake", macArm()), QString("brew install cmake"));
        QCOMPARE(installCommand("lld", macArm()), QString("brew install lld"));
        QCOMPARE(installCommand("ninja", windowsX86()), QString("winget install --id Ninja-build.Ninja -e"));
        QCOMPARE(installCommand("clang", windowsX86()), QString("winget install --id LLVM.LLVM -e"));
        QCOMPARE(installCommand("lld", windowsX86()), QString("winget install --id LLVM.LLVM -e"));
        QCOMPARE(installCommand("ninja", linuxX86()), QString("sudo apt-get install -y ninja-build"));
        QCOMPARE(installCommand("python", {HostOs::Linux, "x86_64", LinuxFlavor::Fedora}), QString("sudo dnf install -y python3"));
        QCOMPARE(installCommand("ninja", {HostOs::Linux, "x86_64", LinuxFlavor::Arch}), QString("sudo pacman -S --needed --noconfirm ninja"));
    }

    void pythonIsSearchedBeyondPath() {
        const auto mac = pythonCandidates(macArm(), {"/usr/bin", "/opt/homebrew/bin"}, "/Users/me");
        QVERIFY2(!mac.contains("/usr/bin/python3"), "Apple's stub must not be run: it can open the Xcode installer");
        QVERIFY(mac.contains("/opt/homebrew/bin/python3.13"));
        QVERIFY(mac.contains("/opt/homebrew/opt/python@3.13/libexec/bin/python3"));
        const auto windows = pythonCandidates(windowsX86(), {"C:/Tools"}, "C:/Users/me");
        QVERIFY(windows.contains("C:/Tools/python.exe"));
        QVERIFY(windows.contains("C:/Users/me/AppData/Local/Programs/Python/Python313/python.exe"));
    }

    void pythonCheckKeepsTheFirstNewEnoughOne() {
        FakeProbe probe;
        probe.output.remove("/fake/python3");
        probe.output.insert("/old/python3", "Python 3.9.6");
        probe.output.insert("/opt/homebrew/opt/python@3.13/libexec/bin/python3", "Python 3.13.13");
        const auto check = checkHostTool(toolById("python"), macArm(), probe, {"/old"}, "/Users/me");
        QVERIFY(check.ok);
        QCOMPARE(check.version, QString("3.13.13"));
        QCOMPARE(check.executable, QString("/opt/homebrew/opt/python@3.13/libexec/bin/python3"));
    }

    void macDoesNotRunTheSystemClangStub() {
        FakeProbe probe;
        probe.foundAt.insert("clang", "/usr/bin/clang");
        const auto check = checkHostTool(toolById("clang"), macArm(), probe, {"/usr/bin"}, "/Users/me");
        QVERIFY(!check.ok);
        QVERIFY(check.problem.contains("Apple"));
        QCOMPARE(check.command, QString("brew install llvm"));
        QVERIFY2(probe.ran.isEmpty(), "the stub would open the Xcode tools installer on a Mac without them");
    }

    void pythonCheckExplainsAnOldOne() {
        FakeProbe probe;
        probe.output.remove("/fake/python3");
        probe.output.insert("/old/python3", "Python 3.9.6");
        const auto check = checkHostTool(toolById("python"), macArm(), probe, {"/old"}, "/Users/me");
        QVERIFY(!check.ok);
        QVERIFY(check.problem.contains("3.9.6"));
        QCOMPARE(check.command, QString("brew install python@3.13"));
    }

    void parsesAdkSetupProgress() {
        QVERIFY(!parseAdkSetupProgress("Some other line").has_value());
        QVERIFY(parseAdkSetupProgress("Downloading prx-headers-v0.1.4.zip").has_value());
        const auto later = parseAdkSetupProgress("Prepared ps5");
        QVERIFY(later.has_value());
        QVERIFY(*later > 0.8 && *later < 1.0);
    }

    void describesAdkSetupFailures() {
        const auto network = describeAdkSetupFailure({"Using AnyPS5", "urllib.error.URLError: <urlopen error [Errno 8] nodename nor servname provided>"}, "Install PS5 libraries failed with exit code 1. The log has the details.");
        QVERIFY(network.contains("internet"));
        const auto serverTrouble = describeAdkSetupFailure({"urllib.error.HTTPError: HTTP Error 504: Gateway Time-out", "adk: setup.py exited with code 1"}, "x");
        QVERIFY2(serverTrouble.contains("server") && serverTrouble.contains("Retry"), qPrintable(serverTrouble));
        QVERIFY(!serverTrouble.contains("internet"));
        QVERIFY(describeAdkSetupFailure({"urllib.error.HTTPError: HTTP Error 403: Forbidden"}, "x").contains("internet"));
        QVERIFY(describeAdkSetupFailure({"ValueError: Cached download does not match SHA-256", "adk: setup.py exited with code 1"}, "x").contains("Cached download does not match"));
        const auto other = describeAdkSetupFailure({"ValueError: Unpatched release is missing libc.prx"}, "x failed with exit code 1. The log has the details.");
        QVERIFY(other.contains("Unpatched release is missing libc.prx"));
        QVERIFY(other.contains("Retry"));
    }

    // ---- Release asset selection ----

    void choosesAssetPerPlatform() {
        const auto source = releaseSourceFor(linuxX86());
        QVERIFY(source);
        QCOMPARE(source->repository, QString("boykopovar/AnyDevKit"));
        QCOMPARE(releaseQueryPath(*source), QString("/repos/boykopovar/AnyDevKit/releases/latest"));
        const auto selection = selectReleaseAsset(json(releaseJson("v0.1.7", {
            assetJson("AnyDevKit-0.1.7-linux-x86_64.tar.gz"),
            assetJson("AnyDevKit-0.1.7-windows-x86_64.zip"),
            assetJson("Calculator3D-0.1.7-linux-x86_64.tar.gz"),
        })), *source);
        QVERIFY(selection.asset);
        QCOMPARE(selection.asset->name, QString("AnyDevKit-0.1.7-linux-x86_64.tar.gz"));
        QCOMPARE(selection.asset->tag, QString("v0.1.7"));
        QCOMPARE(selection.asset->sha256, QString(64, 'a'));

        const auto windows = releaseSourceFor(windowsX86());
        QVERIFY(windows);
        const auto chosen = selectReleaseAsset(json(releaseJson("v0.1.7", {
            assetJson("AnyDevKit-0.1.7-windows-x86_64.zip"),
            assetJson("AnyDevKit-0.1.7-linux-x86_64.tar.gz"),
        })), *windows);
        QVERIFY(chosen.asset);
        QCOMPARE(chosen.asset->name, QString("AnyDevKit-0.1.7-windows-x86_64.zip"));
    }

    void macUsesTheTaggedReleaseOfTheFork() {
        const auto source = releaseSourceFor(macArm());
        QVERIFY(source);
        QCOMPARE(source->repository, QString("utkuhalis/AnyDevKit"));
        QVERIFY(releaseQueryPath(*source).startsWith("/repos/utkuhalis/AnyDevKit/releases"));
        const QJsonArray releases{
            releaseJson("v9.9.9", {assetJson("AnyDevKit-9.9.9-macos-arm64.tar.gz")}, "2026-11-08T20:05:51Z"),
            releaseJson("mac-arm64-0.1.7", {assetJson("AnyDevKit-0.1.7-macos-arm64.tar.gz")}, "2026-11-07T10:00:00Z"),
            releaseJson("mac-arm64-0.1.8", {assetJson("AnyDevKit-0.1.8-macos-arm64.tar.gz")}, "2026-11-08T10:00:00Z"),
            releaseJson("mac-arm64-0.2.0", {assetJson("AnyDevKit-0.2.0-macos-arm64.tar.gz")}, "2026-11-09T10:00:00Z", true),
        };
        const auto selection = selectReleaseAsset(json(releases), *source);
        QVERIFY(selection.asset);
        QCOMPARE(selection.asset->tag, QString("mac-arm64-0.1.8"));
        QCOMPARE(selection.asset->name, QString("AnyDevKit-0.1.8-macos-arm64.tar.gz"));
    }

    void reportsWhenTheMacAssetIsAbsent() {
        const auto source = releaseSourceFor(macArm());
        QVERIFY(source);
        QCOMPARE(selectReleaseAsset(json(QJsonArray{}), *source).problem, ReleaseProblem::NotPublished);
        const QJsonArray otherTags{releaseJson("v0.1.7", {assetJson("AnyDevKit-0.1.7-macos-arm64.tar.gz")})};
        QCOMPARE(selectReleaseAsset(json(otherTags), *source).problem, ReleaseProblem::NotPublished);
        const QJsonArray noAsset{releaseJson("mac-arm64-0.1.7", {assetJson("Calculator-0.1.7-macos-arm64.tar.gz")})};
        QCOMPARE(selectReleaseAsset(json(noAsset), *source).problem, ReleaseProblem::NotPublished);
    }

    void unsupportedComputersHaveNoSource() {
        QVERIFY(!releaseSourceFor({HostOs::MacOs, "x86_64", LinuxFlavor::Other}));
        QVERIFY(!releaseSourceFor({HostOs::Linux, "arm64", LinuxFlavor::Debian}));
    }

    void releaseSelectionRejectsBadData() {
        const auto source = *releaseSourceFor(linuxX86());
        const auto name = QString("AnyDevKit-0.1.7-linux-x86_64.tar.gz");
        QCOMPARE(selectReleaseAsset("not json", source).problem, ReleaseProblem::Malformed);
        QCOMPARE(selectReleaseAsset(json(releaseJson("v1", {assetJson(name, QString())})), source).problem, ReleaseProblem::NoChecksum);
        QCOMPARE(selectReleaseAsset(json(releaseJson("v1", {assetJson(name), assetJson("AnyDevKit-0.1.7-debug-linux-x86_64.tar.gz")})), source).problem, ReleaseProblem::Ambiguous);
        QCOMPARE(selectReleaseAsset(json(releaseJson("v1", {assetJson(name, QString(64, 'a'), "http://github.com/x/a.tar.gz")})), source).problem, ReleaseProblem::BadUrl);
        QCOMPARE(selectReleaseAsset(json(releaseJson("v1", {assetJson("AnyDevKit-../../evil-linux-x86_64.tar.gz")})), source).problem, ReleaseProblem::NotPublished);
        // A loopback address is only trusted when the caller allows it, for the test fixture.
        const auto loopback = assetJson(name, QString(64, 'a'), "http://127.0.0.1:8123/a.tar.gz");
        QCOMPARE(selectReleaseAsset(json(releaseJson("v1", {loopback})), source).problem, ReleaseProblem::BadUrl);
        QVERIFY(selectReleaseAsset(json(releaseJson("v1", {loopback})), source, true).asset);
    }

    // ---- Digest checks ----

    void parsesAssetDigests() {
        QCOMPARE(parseAssetDigest("sha256:" + QString(64, 'b')).value_or(QString()), QString(64, 'b'));
        QVERIFY(!parseAssetDigest(QString(64, 'b')).has_value());
        QVERIFY(!parseAssetDigest("sha256:abc").has_value());
        QVERIFY(!parseAssetDigest("sha1:" + QString(40, 'b')).has_value());
        QVERIFY(!parseAssetDigest("sha256:" + QString(64, 'Z')).has_value());
    }

    void hashesFiles() {
        QTemporaryDir workspace;
        const auto file = workspace.filePath("data.bin");
        writeBytes(file, "hello");
        QCOMPARE(sha256OfFile(file).value_or(QString()), QString("2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"));
        QVERIFY(!sha256OfFile(workspace.filePath("missing")).has_value());
        std::atomic<bool> cancelled{true};
        QVERIFY(!sha256OfFile(file, &cancelled).has_value());
    }

    void onlyHttpsOrAllowedLoopbackIsDownloaded() {
        QVERIFY(isAllowedDownloadUrl(QUrl("https://github.com/x/a.tgz"), false));
        QVERIFY(!isAllowedDownloadUrl(QUrl("http://github.com/x/a.tgz"), false));
        QVERIFY(!isAllowedDownloadUrl(QUrl("ftp://github.com/x/a.tgz"), false));
        QVERIFY(!isAllowedDownloadUrl(QUrl("http://127.0.0.1:9/a.tgz"), false));
        QVERIFY(isAllowedDownloadUrl(QUrl("http://127.0.0.1:9/a.tgz"), true));
        QVERIFY(!isAllowedDownloadUrl(QUrl("http://evil.example/a.tgz"), true));
    }

    // ---- Safe extraction ----

    void extractsARealGzipWithDynamicHuffmanBlocks() {
        QTemporaryDir workspace;
        const auto archive = workspace.filePath("real.tar.gz");
        writeBytes(archive, QByteArray::fromBase64(RealTarGz));
        const auto result = extractArchive(archive, workspace.filePath("out"));
        QVERIFY2(result.ok, qPrintable(result.problem));
        QCOMPARE(result.fileCount, 2);
        QCOMPARE(readBytes(workspace.filePath("out/bin/adk")), QByteArray("#!/bin/sh\necho adk\n"));
        const auto config = readBytes(workspace.filePath("out/lib/cmake/AnyDevKit/AnyDevKitConfig.cmake"));
        QCOMPARE(sha256Hex(config), QString("81cd73bc81d25762d50eca4efb0230e85e6fe788ab42e1f6cd0ffae4f03da2ab"));
#ifdef Q_OS_UNIX
        QVERIFY(QFileInfo(workspace.filePath("out/bin/adk")).isExecutable());
#endif
    }

    void extractsARealZip() {
        QTemporaryDir workspace;
        const auto archive = workspace.filePath("real.zip");
        writeBytes(archive, QByteArray::fromBase64(RealZip));
        const auto result = extractArchive(archive, workspace.filePath("out"));
        QVERIFY2(result.ok, qPrintable(result.problem));
        QCOMPARE(result.fileCount, 3);
        QCOMPARE(readBytes(workspace.filePath("out/docs/readme.txt")), QByteArray("stored file\n"));
        QCOMPARE(sha256Hex(readBytes(workspace.filePath("out/lib/cmake/AnyDevKit/AnyDevKitConfig.cmake"))), QString("81cd73bc81d25762d50eca4efb0230e85e6fe788ab42e1f6cd0ffae4f03da2ab"));
        QCOMPARE(readBytes(workspace.filePath("out/bin/adk.exe")), QByteArray("MZ fake program\n").repeated(40));
    }

    void extractsATarBuiltHere() {
        QTemporaryDir workspace;
        const auto result = extractTar(workspace, {{"dir/", {}, '5'}, {"dir/a.txt", "alpha"}, {"./b.txt", "beta"}});
        QVERIFY2(result.ok, qPrintable(result.problem));
        QCOMPARE(readBytes(workspace.filePath("out/dir/a.txt")), QByteArray("alpha"));
        QCOMPARE(readBytes(workspace.filePath("out/b.txt")), QByteArray("beta"));
    }

    void leavesOutMacMetadataFiles() {
        QTemporaryDir workspace;
        const auto result = extractTar(workspace, {{"templates/._include", "metadata"}, {"templates/include/a.h", "header"}, {"._LICENSE", "metadata"}});
        QVERIFY2(result.ok, qPrintable(result.problem));
        QCOMPARE(result.fileCount, 1);
        QVERIFY(!QFileInfo::exists(workspace.filePath("out/templates/._include")));
        QVERIFY(!QFileInfo::exists(workspace.filePath("out/._LICENSE")));
        QCOMPARE(readBytes(workspace.filePath("out/templates/include/a.h")), QByteArray("header"));
    }

    void stillRefusesAnUnsafeMetadataPath() {
        QTemporaryDir workspace;
        QVERIFY(!extractTar(workspace, {{"../._evil", "x"}}).ok);
    }

    void handlesLongNames() {
        const QByteArray longName = "deep/" + QByteArray(120, 'x') + "/file.txt";
        // GNU long name: an "L" entry whose body is the real path, with a terminating NUL.
        QByteArray gnuBody = longName + '\0';
        // PAX: a header entry whose body declares the real path; the length counts the whole record.
        const auto record = "path=" + longName + "\n";
        auto length = record.size() + 1;
        length += QByteArray::number(length).size();
        length = record.size() + 1 + QByteArray::number(length).size();
        const auto paxRecord = QByteArray::number(length) + " " + record;
        QTemporaryDir workspace;
        auto result = extractTar(workspace, {{"././@LongLink", gnuBody, 'L'}, {QByteArray("placeholder"), "gnu body"}}, "gnu");
        QVERIFY2(result.ok, qPrintable(result.problem));
        QCOMPARE(readBytes(workspace.filePath("gnu/" + QString::fromLatin1(longName))), QByteArray("gnu body"));
        result = extractTar(workspace, {{"PaxHeaders/file", paxRecord, 'x'}, {QByteArray("placeholder"), "pax body"}}, "pax");
        QVERIFY2(result.ok, qPrintable(result.problem));
        QCOMPARE(readBytes(workspace.filePath("pax/" + QString::fromLatin1(longName))), QByteArray("pax body"));
    }

    void rejectsTraversalAndWritesNothing_data() {
        QTest::addColumn<QByteArray>("name");
        QTest::newRow("parent") << QByteArray("../evil.txt");
        QTest::newRow("nested parent") << QByteArray("a/../../evil.txt");
        QTest::newRow("absolute") << QByteArray("/tmp/evil.txt");
        QTest::newRow("backslash") << QByteArray("a\\..\\evil.txt");
        QTest::newRow("drive") << QByteArray("C:/evil.txt");
        QTest::newRow("windows device") << QByteArray("bin/NUL.txt");
    }

    void rejectsTraversalAndWritesNothing() {
        QFETCH(QByteArray, name);
        QTemporaryDir workspace;
        const auto result = extractTar(workspace, {{"good.txt", "fine"}, {name, "bad"}});
        QVERIFY(!result.ok);
        QVERIFY(!result.problem.isEmpty());
        QVERIFY(!QFileInfo::exists(workspace.filePath("out/good.txt")));
        QVERIFY(!QFileInfo::exists(workspace.filePath("evil.txt")));
    }

    void rejectsLinksEscapingTheFolder_data() {
        QTest::addColumn<QList<ArchiveEntry>>("entries");
        using E = ArchiveEntry;
        using T = ArchiveEntryType;
        QTest::newRow("absolute") << QList<E>{{"link", T::SymbolicLink, "/etc/passwd"}};
        QTest::newRow("up and out") << QList<E>{{"link", T::SymbolicLink, "../outside"}};
        QTest::newRow("nested up and out") << QList<E>{{"a/link", T::SymbolicLink, "../../outside"}};
        QTest::newRow("hard link out") << QList<E>{{"link", T::HardLink, "../outside"}};
        QTest::newRow("hard link to nothing") << QList<E>{{"link", T::HardLink, "nowhere"}};
        QTest::newRow("hard link to a link") << QList<E>{{"a", T::SymbolicLink, "b"}, {"b", T::File, {}}, {"c", T::HardLink, "a"}};
        QTest::newRow("file through a symlink") << QList<E>{{"link", T::SymbolicLink, "sub"}, {"sub", T::Directory}, {"link/file.txt", T::File}};
        // dir/s points at the root, so "dir/s/.." resolves outside it even though it looks inside.
        QTest::newRow("link through a link") << QList<E>{{"dir", T::Directory}, {"dir/s", T::SymbolicLink, ".."}, {"t", T::SymbolicLink, "dir/s/.."}};
        QTest::newRow("symlink over a file") << QList<E>{{"a", T::File}, {"a", T::SymbolicLink, "b"}};
        QTest::newRow("entry through a file") << QList<E>{{"a", T::File}, {"a/b", T::File}};
    }

    void rejectsLinksEscapingTheFolder() {
        QFETCH(QList<ArchiveEntry>, entries);
        QVERIFY(!findUnsafeEntry(entries).isEmpty());
    }

    void allowsLinksInsideTheFolder() {
        using E = ArchiveEntry;
        using T = ArchiveEntryType;
        QVERIFY2(findUnsafeEntry({{"lib", T::Directory}, {"lib/libfoo.so.1", T::File}, {"lib/libfoo.so", T::SymbolicLink, "libfoo.so.1"}, {"lib/copy", T::HardLink, "lib/libfoo.so.1"}}).isEmpty(), "ordinary shared library links must be allowed");
        QVERIFY2(findUnsafeEntry({{"a/b", T::SymbolicLink, "../c"}, {"c", T::File}}).isEmpty(), "a link inside the folder must be allowed");
    }

#ifdef Q_OS_UNIX
    void createsSymlinksAndHardLinks() {
        QTemporaryDir workspace;
        const auto result = extractTar(workspace, {
            {"lib/libfoo.so.1", "library"},
            {"lib/libfoo.so", {}, '2', "libfoo.so.1"},
            {"lib/hard", {}, '1', "lib/libfoo.so.1"},
        });
        QVERIFY2(result.ok, qPrintable(result.problem));
        const auto root = QDir(workspace.path()).canonicalPath() + "/out";
        QCOMPARE(QFileInfo(root + "/lib/libfoo.so").symLinkTarget(), root + "/lib/libfoo.so.1");
        QCOMPARE(readBytes(root + "/lib/libfoo.so"), QByteArray("library"));
        QCOMPARE(readBytes(root + "/lib/hard"), QByteArray("library"));
    }
#endif

    void refusesLinkEscapesDuringExtraction() {
        QTemporaryDir workspace;
        auto result = extractTar(workspace, {{"link", {}, '2', "../outside"}}, "symlink");
        QVERIFY(!result.ok);
        result = extractTar(workspace, {{"dir/", {}, '5'}, {"dir/s", {}, '2', ".."}, {"t", {}, '2', "dir/s/.."}, {"t/file", "x"}}, "chain");
        QVERIFY(!result.ok);
        QVERIFY(!QFileInfo::exists(workspace.filePath("file")));
    }

    void rejectsDamagedAndUnsupportedArchives() {
        QTemporaryDir workspace;
        // A checksum byte flipped: the tar header is damaged.
        auto tar = tarOf({{"a.txt", "alpha"}});
        tar[148] = '9';
        writeBytes(workspace.filePath("bad.tar.gz"), gzipStored(tar));
        QVERIFY(!extractArchive(workspace.filePath("bad.tar.gz"), workspace.filePath("out1")).ok);
        // Cut off mid body.
        auto cut = tarOf({{"a.txt", QByteArray(2000, 'x')}}).left(800);
        writeBytes(workspace.filePath("cut.tar.gz"), gzipStored(cut));
        QVERIFY(!extractArchive(workspace.filePath("cut.tar.gz"), workspace.filePath("out2")).ok);
        // Not a gzip at all.
        writeBytes(workspace.filePath("garbage.tar.gz"), "this is not gzip data");
        QVERIFY(!extractArchive(workspace.filePath("garbage.tar.gz"), workspace.filePath("out3")).ok);
        // A bad CRC at the end of the gzip stream.
        auto badCrc = gzipStored(tarOf({{"a.txt", "alpha"}}));
        badCrc[badCrc.size() - 8] = char(badCrc[badCrc.size() - 8] ^ 0x55);
        writeBytes(workspace.filePath("crc.tar.gz"), badCrc);
        QVERIFY(!extractArchive(workspace.filePath("crc.tar.gz"), workspace.filePath("out4")).ok);
        // Devices and pipes are not something an SDK ships.
        QVERIFY(!extractTar(workspace, {{"pipe", {}, '6'}}, "fifo").ok);
        // Destination already exists.
        QDir().mkpath(workspace.filePath("taken"));
        writeBytes(workspace.filePath("ok.tar.gz"), gzipStored(tarOf({{"a.txt", "alpha"}})));
        const auto taken = extractArchive(workspace.filePath("ok.tar.gz"), workspace.filePath("taken"));
        QVERIFY(!taken.ok);
    }

    void rejectsHostileZips() {
        QByteArray zip;
        const QByteArray name = "../evil.txt";
        const auto localOffset = zip.size();
        appendLittle(zip, 0x04034b50, 4);
        appendLittle(zip, 20, 2);
        appendLittle(zip, 0, 2);
        appendLittle(zip, 0, 2);
        appendLittle(zip, 0, 2);
        appendLittle(zip, 0, 2);
        appendLittle(zip, crc32Of("x"), 4);
        appendLittle(zip, 1, 4);
        appendLittle(zip, 1, 4);
        appendLittle(zip, static_cast<quint32>(name.size()), 2);
        appendLittle(zip, 0, 2);
        zip += name;
        zip += 'x';
        const auto centralOffset = zip.size();
        appendLittle(zip, 0x02014b50, 4);
        appendLittle(zip, 20, 2);
        appendLittle(zip, 20, 2);
        appendLittle(zip, 0, 2);
        appendLittle(zip, 0, 2);
        appendLittle(zip, 0, 2);
        appendLittle(zip, 0, 2);
        appendLittle(zip, crc32Of("x"), 4);
        appendLittle(zip, 1, 4);
        appendLittle(zip, 1, 4);
        appendLittle(zip, static_cast<quint32>(name.size()), 2);
        appendLittle(zip, 0, 2);
        appendLittle(zip, 0, 2);
        appendLittle(zip, 0, 2);
        appendLittle(zip, 0, 2);
        appendLittle(zip, 0, 4);
        appendLittle(zip, static_cast<quint32>(localOffset), 4);
        zip += name;
        const auto centralSize = zip.size() - centralOffset;
        appendLittle(zip, 0x06054b50, 4);
        appendLittle(zip, 0, 2);
        appendLittle(zip, 0, 2);
        appendLittle(zip, 1, 2);
        appendLittle(zip, 1, 2);
        appendLittle(zip, static_cast<quint32>(centralSize), 4);
        appendLittle(zip, static_cast<quint32>(centralOffset), 4);
        appendLittle(zip, 0, 2);
        QTemporaryDir workspace;
        writeBytes(workspace.filePath("bad.zip"), zip);
        const auto result = extractArchive(workspace.filePath("bad.zip"), workspace.filePath("out"));
        QVERIFY(!result.ok);
        QVERIFY(!QFileInfo::exists(workspace.filePath("evil.txt")));
        writeBytes(workspace.filePath("noise.zip"), "PK\x03\x04 but not really a zip");
        QVERIFY(!extractArchive(workspace.filePath("noise.zip"), workspace.filePath("out2")).ok);
    }

    void refusesOversizedOutputs() {
        QTemporaryDir workspace;
        writeBytes(workspace.filePath("big.tar.gz"), gzipStored(tarOf({{"big.bin", QByteArray(10000, 'x')}})));
        ExtractOptions options;
        options.maxExtractedBytes = 2000;
        const auto result = extractArchive(workspace.filePath("big.tar.gz"), workspace.filePath("out"), options);
        QVERIFY(!result.ok);
        QVERIFY2(result.problem.contains("too large"), qPrintable(result.problem));
    }

    void stopsWhenCancelled() {
        QTemporaryDir workspace;
        writeBytes(workspace.filePath("a.tar.gz"), gzipStored(tarOf({{"a.txt", "alpha"}})));
        std::atomic<bool> cancelled{true};
        ExtractOptions options;
        options.cancelled = &cancelled;
        const auto result = extractArchive(workspace.filePath("a.tar.gz"), workspace.filePath("out"), options);
        QVERIFY(result.cancelled);
        QVERIFY(!result.ok);
    }

    // ---- Step state ----

    void stepsRunInOrder() {
        SetupProgress progress;
        QVERIFY(!progress.begin(SetupStep::Download));
        QVERIFY(progress.begin(SetupStep::Tools));
        QCOMPARE(progress.running().value(), SetupStep::Tools);
        QVERIFY(!progress.begin(SetupStep::Download));
        progress.complete(SetupStep::Tools, "All found");
        QCOMPARE(progress.status(SetupStep::Tools).state, SetupStepState::Done);
        QVERIFY(progress.begin(SetupStep::Download));
        progress.report(SetupStep::Download, "1 of 2 MB", 0.5);
        QCOMPARE(progress.status(SetupStep::Download).progress, 0.5);
        progress.fail(SetupStep::Download, "No internet");
        QCOMPARE(progress.failed().value(), SetupStep::Download);
        QCOMPARE(progress.status(SetupStep::Download).detail, QString("No internet"));
        QVERIFY(!progress.running());
        progress.clearFailure();
        QVERIFY(!progress.failed());
        QVERIFY(progress.begin(SetupStep::Download));
        progress.pause("Paused");
        QCOMPARE(progress.status(SetupStep::Download).state, SetupStepState::Waiting);
        QVERIFY(progress.begin(SetupStep::Download));
        progress.complete(SetupStep::Download);
        QVERIFY(progress.begin(SetupStep::Unpack));
        progress.fail(SetupStep::Unpack, "Bad archive");
        progress.clearFailure();
        // Retrying a bad archive downloads it again, so the checklist reopens from Download.
        progress.reopen(SetupStep::Download);
        QCOMPARE(progress.status(SetupStep::Download).state, SetupStepState::Waiting);
        QVERIFY(progress.begin(SetupStep::Download));
        progress.complete(SetupStep::Download);
        QVERIFY(progress.begin(SetupStep::Unpack));
        progress.complete(SetupStep::Unpack);
        QVERIFY(progress.begin(SetupStep::Dependencies));
        progress.complete(SetupStep::Dependencies);
        QVERIFY(progress.allDone());
        progress.restart();
        QCOMPARE(progress.status(SetupStep::Tools).state, SetupStepState::Waiting);
        QVERIFY(!progress.allDone());
    }

    // ---- The whole flow, with fake HTTP and fake tools ----

    void flowEndsReadyWhenToolsAreThere() {
        FlowFixture fixture;
        SetupFlow flow(fixture.config());
        flow.checkTools();
        QTRY_COMPARE(flow.phase(), QString("ready"));
        const auto tools = flow.tools();
        QCOMPARE(tools.size(), 5);
        for (const auto& tool : tools) QVERIFY(tool.toMap()["ok"].toBool());
    }

    void flowAsksForMissingTools() {
        FlowFixture fixture;
        fixture.probe.missing.insert("cmake");
        SetupFlow flow(fixture.config());
        flow.checkTools();
        QTRY_COMPARE(flow.phase(), QString("needsTools"));
        QVariantMap cmake;
        for (const auto& tool : flow.tools()) {
            if (tool.toMap()["id"] == "cmake") cmake = tool.toMap();
        }
        QVERIFY(!cmake["ok"].toBool());
        QCOMPARE(cmake["command"].toString(), QString("sudo apt-get install -y cmake"));

        flow.start();
        QVERIFY2(flow.phase() != "running", "must not start while a tool is missing");

        // Installing the tool and pressing Check again moves on.
        fixture.probe.missing.remove("cmake");
        flow.checkTool("cmake");
        QTRY_COMPARE(flow.phase(), QString("ready"));
    }

    void flowHintsAtHomebrewWhenItIsMissing() {
        FlowFixture fixture;
        fixture.probe.missing.insert("cmake");
        SetupFlow flow(fixture.config(macArm()));
        flow.checkTools();
        QTRY_COMPARE(flow.phase(), QString("needsTools"));
        QVERIFY2(flow.toolsHint().contains("Homebrew"), qPrintable(flow.toolsHint()));
    }

#ifdef Q_OS_UNIX
    void flowRunsToTheEndAndReportsThePaths() {
        FlowFixture fixture;
        fixture.serveRelease(fakeSdkArchive());
        SetupFlow flow(fixture.config());
        QSignalSpy finished(&flow, &SetupFlow::finished);
        flow.checkTools();
        QTRY_COMPARE(flow.phase(), QString("ready"));
        flow.start();
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 15000);
        QCOMPARE(flow.phase(), QString("done"));
        const auto sdk = finished.at(0).at(0).toString();
        const auto deps = finished.at(0).at(1).toString();
        QCOMPARE(sdk, fixture.dataDirectory + "/sdk/v0.1.7");
        QCOMPARE(deps, fixture.dataDirectory + "/dependencies/v0.1.7");
        QVERIFY(QFileInfo(sdk + "/bin/adk").isExecutable());
        QVERIFY(QFileInfo(deps + "/ps5/sysroot").isDir());
        const auto args = readBytes(fixture.argsFile).trimmed();
        QCOMPARE(QString::fromUtf8(args), QString("setup %1 --target ps5 --cache %2/adk-cache").arg(deps, fixture.dataDirectory));
        QVERIFY2(!QFileInfo::exists(fixture.dataDirectory + "/downloads"), "finished runs tidy up the archive");
        QVERIFY(flow.steps().at(3).toMap()["state"] == "done");
    }

    void flowUsesAFolderForAPythonWithAVersionedName() {
        FlowFixture fixture;
        fixture.probe.output.remove("/fake/python3");
        // A Python reachable only under a versioned name, the way Linux installs alongside an old one.
        fixture.probe.output.insert("/usr/bin/python3.13", "Python 3.13.13");
        auto config = fixture.config();
        config.toolDirectories = QStringList{"/usr/bin"};
        fixture.serveRelease(fakeSdkArchive());
        SetupFlow flow(config);
        QSignalSpy finished(&flow, &SetupFlow::finished);
        flow.checkTools();
        QTRY_COMPARE(flow.phase(), QString("ready"));
        flow.start();
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 15000);
        const auto path = QString::fromUtf8(readBytes(fixture.argsFile + ".path").trimmed());
        const auto shim = fixture.dataDirectory + "/python-link";
        QVERIFY2(path.startsWith(shim + ":"), qPrintable(path));
        QCOMPARE(QFileInfo(shim + "/python3").symLinkTarget(), QString("/usr/bin/python3.13"));
    }

    void flowReusesFoldersThatAreAlreadyValid() {
        FlowFixture fixture;
        writeBytes(fixture.workspace.filePath("existing-sdk/lib/cmake/AnyDevKit/AnyDevKitConfig.cmake"), "x");
        writeBytes(fixture.workspace.filePath("existing-sdk/bin/adk"), FakeAdkScript);
        QVERIFY(QFile::setPermissions(fixture.workspace.filePath("existing-sdk/bin/adk"), QFile::ExeOwner | QFile::ReadOwner | QFile::WriteOwner));
        auto config = fixture.config();
        config.existingSdkRoot = fixture.workspace.filePath("existing-sdk");
        SetupFlow flow(config);
        QSignalSpy finished(&flow, &SetupFlow::finished);
        flow.checkTools();
        QTRY_COMPARE(flow.phase(), QString("ready"));
        flow.start();
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 15000);
        QVERIFY2(fixture.http.requests.isEmpty(), "an existing SDK needs no download");
        QCOMPARE(finished.at(0).at(0).toString(), config.existingSdkRoot);
    }

    void flowRetriesAfterAdkSetupFailsAndClearsStaleFiles() {
        FlowFixture fixture;
        writeBytes(fixture.failOnceFile, "1");
        fixture.serveRelease(fakeSdkArchive());
        SetupFlow flow(fixture.config());
        QSignalSpy finished(&flow, &SetupFlow::finished);
        flow.checkTools();
        QTRY_COMPARE(flow.phase(), QString("ready"));
        flow.start();
        QTRY_COMPARE_WITH_TIMEOUT(flow.phase(), QString("failed"), 15000);
        QVERIFY2(flow.problem().contains("simulated setup failure"), qPrintable(flow.problem()));
        QVERIFY(flow.problem().contains("Retry"));
        // A cancelled adk setup leaves these behind and would refuse to run again.
        writeBytes(fixture.dataDirectory + "/adk-cache/prx.zip.partial", "half");
        QDir().mkpath(fixture.dataDirectory + "/dependencies/adk-setup-stale");
        const auto downloads = fixture.http.requests.size();
        flow.start();
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 15000);
        QCOMPARE(fixture.http.requests.size(), downloads);
        QVERIFY(!QFileInfo::exists(fixture.dataDirectory + "/adk-cache/prx.zip.partial"));
        QVERIFY(!QFileInfo::exists(fixture.dataDirectory + "/dependencies/adk-setup-stale"));
    }
#endif

    void flowExplainsTheMissingMacRelease() {
        FlowFixture fixture;
        fixture.http.replies["https://api.github.com/repos/utkuhalis/AnyDevKit/releases?per_page=100"] = {FakeReply{200, json(QJsonArray{})}};
        SetupFlow flow(fixture.config(macArm()));
        flow.checkTools();
        QTRY_COMPARE(flow.phase(), QString("ready"));
        flow.start();
        QTRY_COMPARE(flow.phase(), QString("failed"));
        QVERIFY2(flow.problem().contains("not published"), qPrintable(flow.problem()));
    }

    void flowOffersRetryAfterANetworkFailure() {
        FlowFixture fixture;
        fixture.http.replies[LatestReleaseUrl] = {FakeReply{0, {}, "Host not found"}, FakeReply{200, json(releaseJson("v0.1.7", {assetJson(LinuxAssetName, QString(64, 'a'), LinuxAssetUrl)}))}};
        fixture.http.replies[LinuxAssetUrl] = {FakeReply{0, {}, "Connection closed"}};
        SetupFlow flow(fixture.config());
        flow.checkTools();
        QTRY_COMPARE(flow.phase(), QString("ready"));
        flow.start();
        QTRY_COMPARE(flow.phase(), QString("failed"));
        QVERIFY2(flow.problem().contains("internet"), qPrintable(flow.problem()));
        QVERIFY2(flow.problem().contains("Retry"), qPrintable(flow.problem()));
        flow.start();
        // The release lookup succeeded on the second try, so the failure is now the download's.
        QTRY_COMPARE(flow.phase(), QString("failed"));
        QVERIFY(fixture.http.requests.size() >= 3);
    }

    void flowExplainsRateLimits() {
        FlowFixture fixture;
        fixture.http.replies[LatestReleaseUrl] = {FakeReply{403, "{\"message\":\"API rate limit exceeded\"}"}};
        SetupFlow flow(fixture.config());
        flow.checkTools();
        QTRY_COMPARE(flow.phase(), QString("ready"));
        flow.start();
        QTRY_COMPARE(flow.phase(), QString("failed"));
        QVERIFY2(flow.problem().contains("limit"), qPrintable(flow.problem()));
    }

    void flowRejectsADownloadWithTheWrongDigest() {
        FlowFixture fixture;
        fixture.serveRelease(fakeSdkArchive(), QString(64, 'a'));
        SetupFlow flow(fixture.config());
        flow.checkTools();
        QTRY_COMPARE(flow.phase(), QString("ready"));
        flow.start();
        QTRY_COMPARE_WITH_TIMEOUT(flow.phase(), QString("failed"), 15000);
        QVERIFY2(flow.problem().contains("checksum"), qPrintable(flow.problem()));
        QVERIFY(!QFileInfo::exists(fixture.dataDirectory + "/downloads/" + LinuxAssetName));
        QVERIFY(!QFileInfo::exists(fixture.dataDirectory + "/sdk/v0.1.7"));
    }

    void flowRejectsAnUnsafeArchive() {
        FlowFixture fixture;
        const auto evil = gzipStored(tarOf({{"bin/adk", "x"}, {"../../evil.txt", "bad"}}));
        fixture.serveRelease(evil);
        SetupFlow flow(fixture.config());
        flow.checkTools();
        QTRY_COMPARE(flow.phase(), QString("ready"));
        flow.start();
        QTRY_COMPARE_WITH_TIMEOUT(flow.phase(), QString("failed"), 15000);
        QVERIFY2(flow.problem().contains("unsafe"), qPrintable(flow.problem()));
        QVERIFY(!QFileInfo::exists(fixture.dataDirectory + "/sdk/v0.1.7"));
    }

    void flowCancelsAndResumesTheDownload() {
        FlowFixture fixture;
        const auto archive = fakeSdkArchive();
        const auto digest = sha256Hex(archive);
        fixture.http.replies[LatestReleaseUrl] = {FakeReply{200, json(releaseJson("v0.1.7", {assetJson(LinuxAssetName, digest, LinuxAssetUrl, archive.size())}))}};
        const auto firstPart = archive.left(archive.size() / 2);
        fixture.http.replies[LinuxAssetUrl] = {
            FakeReply{200, firstPart, {}, 0, true},
            FakeReply{206, archive.mid(firstPart.size()), {}, firstPart.size()},
        };
        SetupFlow flow(fixture.config());
        flow.checkTools();
        QTRY_COMPARE(flow.phase(), QString("ready"));
        flow.start();
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.http.heldTransfer.isNull(), 5000);
        QCOMPARE(flow.phase(), QString("running"));
        flow.cancel();
        QTRY_COMPARE(flow.phase(), QString("cancelled"));
        QCOMPARE(QFileInfo(fixture.dataDirectory + "/downloads/" + LinuxAssetName + ".part").size(), firstPart.size());

        QSignalSpy finished(&flow, &SetupFlow::finished);
        flow.start();
#ifdef Q_OS_UNIX
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 15000);
        QCOMPARE(fixture.http.requests.last().second, firstPart.size());
#else
        QTRY_COMPARE_WITH_TIMEOUT(fixture.http.requests.last().second, firstPart.size(), 5000);
#endif
    }

    void flowKeepsThePartialFileWhenARetryFails() {
        FlowFixture fixture;
        const auto archive = fakeSdkArchive();
        fixture.http.replies[LatestReleaseUrl] = {FakeReply{200, json(releaseJson("v0.1.7", {assetJson(LinuxAssetName, sha256Hex(archive), LinuxAssetUrl, archive.size())}))}};
        const auto firstPart = archive.left(archive.size() / 2);
        fixture.http.replies[LinuxAssetUrl] = {
            FakeReply{200, firstPart, {}, 0, true},
            FakeReply{0, {}, "Network unreachable"},
            FakeReply{206, archive.mid(firstPart.size()), {}, firstPart.size()},
        };
        SetupFlow flow(fixture.config());
        flow.checkTools();
        QTRY_COMPARE(flow.phase(), QString("ready"));
        flow.start();
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.http.heldTransfer.isNull(), 5000);
        flow.cancel();
        QTRY_COMPARE(flow.phase(), QString("cancelled"));
        flow.start();
        QTRY_COMPARE(flow.phase(), QString("failed"));
        QVERIFY2(flow.problem().contains("internet"), qPrintable(flow.problem()));
        const auto partFile = fixture.dataDirectory + "/downloads/" + LinuxAssetName + ".part";
        QCOMPARE(QFileInfo(partFile).size(), firstPart.size());
#ifdef Q_OS_UNIX
        QSignalSpy finished(&flow, &SetupFlow::finished);
        flow.start();
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 15000);
        QCOMPARE(fixture.http.requests.last().second, firstPart.size());
#endif
    }

    void flowStartsOverWhenTheServerIgnoresTheRange() {
        FlowFixture fixture;
        const auto archive = fakeSdkArchive();
        fixture.http.replies[LatestReleaseUrl] = {FakeReply{200, json(releaseJson("v0.1.7", {assetJson(LinuxAssetName, sha256Hex(archive), LinuxAssetUrl, archive.size())}))}};
        fixture.http.replies[LinuxAssetUrl] = {
            FakeReply{200, archive.left(100), {}, 0, true},
            FakeReply{200, archive},
        };
        SetupFlow flow(fixture.config());
        flow.checkTools();
        QTRY_COMPARE(flow.phase(), QString("ready"));
        flow.start();
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.http.heldTransfer.isNull(), 5000);
        flow.cancel();
        QTRY_COMPARE(flow.phase(), QString("cancelled"));
        flow.start();
        // The whole body arrived with status 200, so the file must equal the archive, not double it.
        QTRY_VERIFY_WITH_TIMEOUT(flow.phase() == "failed" || flow.phase() == "done", 15000);
#ifdef Q_OS_UNIX
        QCOMPARE(flow.phase(), QString("done"));
#endif
    }
};

QTEST_GUILESS_MAIN(TestSetup)

#include "tst_setup.moc"
