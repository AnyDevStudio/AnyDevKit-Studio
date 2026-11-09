#pragma once

#include "HostTools.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <atomic>
#include <optional>

namespace Studio {

// Where AnyDevKit for this computer is published.
struct ReleaseSource {
    QString repository;
    QString tagPrefix;   // empty to use the repository's latest release
    QString assetPrefix = "AnyDevKit-";
    QString assetSuffix; // the part of the asset name that names the platform
};

std::optional<ReleaseSource> releaseSourceFor(const HostPlatform& platform); // empty for a computer with no download
QString releaseQueryPath(const ReleaseSource& source);

struct ReleaseAsset {
    QString tag;
    QString name;
    QString url;
    QString sha256;
    qint64 size = 0;
};

enum class ReleaseProblem { None, Malformed, NotPublished, NoChecksum, Ambiguous, BadUrl };

struct ReleaseSelection {
    std::optional<ReleaseAsset> asset;
    ReleaseProblem problem = ReleaseProblem::None;
};

// `allowLoopback` lets a test fixture on 127.0.0.1 serve plain http; GitHub always needs https.
ReleaseSelection selectReleaseAsset(const QByteArray& json, const ReleaseSource& source, bool allowLoopback = false);
bool isAllowedDownloadUrl(const QUrl& url, bool allowLoopback);
std::optional<QString> parseAssetDigest(const QString& digest); // the hex digest of "sha256:<hex>"

// Lower-case hex SHA-256 of the file, or nothing if it cannot be read or `cancelled` was set.
std::optional<QString> sha256OfFile(const QString& path, const std::atomic<bool>* cancelled = nullptr);

}
