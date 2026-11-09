#include "ReleaseAssets.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace Studio {

namespace {

// Keeps a hostile asset name from ever becoming a path with folders in it.
bool isSafeAssetName(const QString& name) {
    static const QRegularExpression pattern(R"(^[A-Za-z0-9._+-]+$)");
    return pattern.match(name).hasMatch();
}

bool matchesAsset(const QString& name, const ReleaseSource& source) {
    return isSafeAssetName(name) && name.startsWith(source.assetPrefix) && name.endsWith(source.assetSuffix) && name.size() > source.assetPrefix.size() + source.assetSuffix.size();
}

bool isLoopbackHost(const QString& host) {
    if (host == "localhost") return true;
    const QHostAddress address(host);
    return !address.isNull() && address.isLoopback();
}

}

std::optional<ReleaseSource> releaseSourceFor(const HostPlatform& platform) {
    if (platform.os == HostOs::Windows && platform.architecture == "x86_64") return ReleaseSource{"boykopovar/AnyDevKit", {}, "AnyDevKit-", "-windows-x86_64.zip"};
    if (platform.os == HostOs::Linux && platform.architecture == "x86_64") return ReleaseSource{"boykopovar/AnyDevKit", {}, "AnyDevKit-", "-linux-x86_64.tar.gz"};
    if (platform.os == HostOs::MacOs && platform.architecture == "arm64") return ReleaseSource{"utkuhalis/AnyDevKit", "mac-arm64-", "AnyDevKit-", "-macos-arm64.tar.gz"};
    return std::nullopt;
}

QString releaseQueryPath(const ReleaseSource& source) {
    const auto base = "/repos/" + source.repository + "/releases";
    return source.tagPrefix.isEmpty() ? base + "/latest" : base + "?per_page=100";
}

bool isAllowedDownloadUrl(const QUrl& url, bool allowLoopback) {
    if (!url.isValid()) return false;
    if (url.scheme() == "https") return true;
    return allowLoopback && url.scheme() == "http" && isLoopbackHost(url.host());
}

std::optional<QString> parseAssetDigest(const QString& digest) {
    static const QRegularExpression pattern("^sha256:([0-9a-f]{64})$");
    const auto match = pattern.match(digest);
    if (!match.hasMatch()) return std::nullopt;
    return match.captured(1);
}

ReleaseSelection selectReleaseAsset(const QByteArray& json, const ReleaseSource& source, bool allowLoopback) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || (!document.isObject() && !document.isArray())) return {std::nullopt, ReleaseProblem::Malformed};

    QList<QJsonObject> releases;
    if (document.isObject()) {
        releases.append(document.object());
    } else {
        for (const auto& value : document.array()) releases.append(value.toObject());
    }
    // Newest first, so the first release that carries the asset is the one to use.
    std::ranges::stable_sort(releases, std::greater<>(), [](const QJsonObject& release) { return QDateTime::fromString(release["published_at"].toString(), Qt::ISODate); });

    for (const auto& release : releases) {
        const auto tag = release["tag_name"].toString();
        if (release["draft"].toBool() || tag.isEmpty() || !tag.startsWith(source.tagPrefix)) continue;

        QList<QJsonObject> matches;
        for (const auto& value : release["assets"].toArray()) {
            if (matchesAsset(value.toObject()["name"].toString(), source)) matches.append(value.toObject());
        }
        if (matches.isEmpty()) continue;
        if (matches.size() > 1) return {std::nullopt, ReleaseProblem::Ambiguous};

        const auto& asset = matches.first();
        const auto digest = parseAssetDigest(asset["digest"].toString());
        if (!digest) return {std::nullopt, ReleaseProblem::NoChecksum};
        const auto url = asset["browser_download_url"].toString();
        if (!isAllowedDownloadUrl(QUrl(url), allowLoopback)) return {std::nullopt, ReleaseProblem::BadUrl};
        return {ReleaseAsset{tag, asset["name"].toString(), url, *digest, asset["size"].toInteger()}, ReleaseProblem::None};
    }
    return {std::nullopt, ReleaseProblem::NotPublished};
}

std::optional<QString> sha256OfFile(const QString& path, const std::atomic<bool>* cancelled) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    do {
        if (cancelled && cancelled->load()) return std::nullopt;
        const auto read = file.read(buffer.data(), buffer.size());
        if (read < 0) return std::nullopt;
        hash.addData(QByteArrayView(buffer.constData(), read));
    } while (!file.atEnd());
    return QString::fromLatin1(hash.result().toHex());
}

}
