#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <atomic>
#include <functional>

namespace Studio {

enum class ArchiveEntryType { File, Directory, SymbolicLink, HardLink };

struct ArchiveEntry {
    QString path;
    ArchiveEntryType type = ArchiveEntryType::File;
    QString linkTarget; // symbolic: as stored; hard: another entry's path from the archive root
    quint32 mode = 0;
    qint64 size = 0;
};

// Why these entries must not be unpacked, or an empty string when every entry stays inside the folder.
QString findUnsafeEntry(const QList<ArchiveEntry>& entries);

// Undoes gzip around a tar. Empty when the data is damaged or would exceed `limit` bytes; `error` says which.
QByteArray gunzipTar(const QByteArray& data, qint64 limit, QString& error, const std::atomic<bool>* cancelled = nullptr);

struct ExtractOptions {
    const std::atomic<bool>* cancelled = nullptr;
    std::function<void(int done, int total)> progress;
    qint64 maxExtractedBytes = 1024LL * 1024 * 1024;
};

struct ExtractResult {
    bool ok = false;
    bool cancelled = false;
    QString problem;
    int fileCount = 0;
};

// Unpacks a .tar.gz or .zip into `destination`, which must not exist yet. Every entry is checked
// before anything is written, so an unsafe archive leaves nothing behind.
ExtractResult extractArchive(const QString& archivePath, const QString& destination, const ExtractOptions& options = {});

}
