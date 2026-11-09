#include "SafeArchive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace Studio {

namespace {

constexpr int CancelCheckInterval = 1 << 16;
constexpr int MaximumEntries = 200000;

quint32 crc32Of(const uchar* data, qsizetype size) {
    static const auto table = [] {
        std::array<quint32, 256> entries{};
        for (quint32 i = 0; i < 256; ++i) {
            auto value = i;
            for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xEDB88320u & (0u - (value & 1u)));
            entries[i] = value;
        }
        return entries;
    }();
    quint32 crc = 0xFFFFFFFFu;
    for (qsizetype i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

quint32 readLittle(const uchar* data, int bytes) {
    quint32 value = 0;
    for (int i = 0; i < bytes; ++i) value |= static_cast<quint32>(data[i]) << (8 * i);
    return value;
}

// DEFLATE (RFC 1951). Qt keeps its zlib private, and Studio should not need another library for this.
class Inflater {
public:
    Inflater(const uchar* input, qsizetype inputSize, qint64 limit, const std::atomic<bool>* cancelled)
        : in(input), size(inputSize), maximum(limit), cancel(cancelled) {}

    enum class Result { Ok, Damaged, TooLarge, Cancelled };

    Result run(QByteArray& out) {
        output = &out;
        for (;;) {
            const auto last = bits(1);
            const auto type = bits(2);
            if (failed != Result::Ok) return failed;
            bool ok = false;
            if (type == 0) ok = stored();
            else if (type == 1) ok = fixedBlock();
            else if (type == 2) ok = dynamicBlock();
            if (failed != Result::Ok) return failed;
            if (!ok) return Result::Damaged;
            if (last) return Result::Ok;
        }
    }

    qsizetype consumed() const { return position; }

private:
    static constexpr int MaximumBits = 15;

    struct Huffman {
        std::array<quint16, MaximumBits + 1> count{};
        std::array<quint16, 288 + 32> symbol{};
    };

    int bits(int need) {
        qint64 value = bitBuffer;
        while (bitCount < need) {
            if (position >= size) {
                failed = Result::Damaged;
                return 0;
            }
            value |= static_cast<qint64>(in[position++]) << bitCount;
            bitCount += 8;
        }
        bitBuffer = static_cast<quint32>(value >> need);
        bitCount -= need;
        return static_cast<int>(value & ((1LL << need) - 1));
    }

    bool append(char byte) {
        if (output->size() >= maximum) {
            failed = Result::TooLarge;
            return false;
        }
        if (output->size() == output->capacity()) output->reserve(std::min<qint64>(maximum, std::max<qint64>(64 * 1024, output->capacity() * 2)));
        output->append(byte);
        if (cancel && (output->size() % CancelCheckInterval) == 0 && cancel->load()) {
            failed = Result::Cancelled;
            return false;
        }
        return true;
    }

    bool stored() {
        bitBuffer = 0;
        bitCount = 0;
        if (position + 4 > size) return false;
        const auto length = readLittle(in + position, 2);
        const auto complement = readLittle(in + position + 2, 2);
        position += 4;
        if (length != (~complement & 0xFFFF) || position + length > size) return false;
        for (quint32 i = 0; i < length; ++i) {
            if (!append(static_cast<char>(in[position + i]))) return false;
        }
        position += length;
        return true;
    }

    static int build(Huffman& code, const quint8* lengths, int count) {
        code.count.fill(0);
        for (int symbol = 0; symbol < count; ++symbol) code.count[lengths[symbol]]++;
        if (code.count[0] == count) return 0;
        int left = 1;
        for (int length = 1; length <= MaximumBits; ++length) {
            left = left * 2 - code.count[length];
            if (left < 0) return left;
        }
        std::array<quint16, MaximumBits + 1> offsets{};
        for (int length = 1; length < MaximumBits; ++length) offsets[length + 1] = offsets[length] + code.count[length];
        for (int symbol = 0; symbol < count; ++symbol) {
            if (lengths[symbol] != 0) code.symbol[offsets[lengths[symbol]]++] = static_cast<quint16>(symbol);
        }
        return left;
    }

    int decode(const Huffman& code) {
        int value = 0;
        int first = 0;
        int index = 0;
        for (int length = 1; length <= MaximumBits; ++length) {
            value |= bits(1);
            if (failed != Result::Ok) return -1;
            const int count = code.count[length];
            if (value - count < first) return code.symbol[index + (value - first)];
            index += count;
            first += count;
            first <<= 1;
            value <<= 1;
        }
        return -1;
    }

    bool codes(const Huffman& literals, const Huffman& distances) {
        static constexpr std::array<quint16, 29> LengthBase{3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static constexpr std::array<quint16, 29> LengthExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static constexpr std::array<quint16, 30> DistanceBase{1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static constexpr std::array<quint16, 30> DistanceExtra{0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            const auto symbol = decode(literals);
            if (symbol < 0) return false;
            if (symbol < 256) {
                if (!append(static_cast<char>(symbol))) return false;
            } else if (symbol == 256) {
                return true;
            } else {
                const auto lengthCode = symbol - 257;
                if (lengthCode >= 29) return false;
                const auto length = LengthBase[lengthCode] + bits(LengthExtra[lengthCode]);
                const auto distanceCode = decode(distances);
                if (distanceCode < 0 || distanceCode >= 30) return false;
                const auto distance = DistanceBase[distanceCode] + bits(DistanceExtra[distanceCode]);
                if (failed != Result::Ok) return false;
                if (distance > output->size()) return false;
                for (int i = 0; i < length; ++i) {
                    if (!append(output->at(output->size() - distance))) return false;
                }
            }
        }
    }

    bool fixedBlock() {
        static const auto tables = [] {
            std::pair<Huffman, Huffman> built;
            std::array<quint8, 320> lengths{};
            for (int i = 0; i < 144; ++i) lengths[i] = 8;
            for (int i = 144; i < 256; ++i) lengths[i] = 9;
            for (int i = 256; i < 280; ++i) lengths[i] = 7;
            for (int i = 280; i < 288; ++i) lengths[i] = 8;
            build(built.first, lengths.data(), 288);
            lengths.fill(5);
            build(built.second, lengths.data(), 30);
            return built;
        }();
        return codes(tables.first, tables.second);
    }

    bool dynamicBlock() {
        static constexpr std::array<quint8, 19> Order{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const auto literalCount = bits(5) + 257;
        const auto distanceCount = bits(5) + 1;
        const auto codeCount = bits(4) + 4;
        if (failed != Result::Ok || literalCount > 286 || distanceCount > 30) return false;

        std::array<quint8, 320> lengths{};
        for (int i = 0; i < codeCount; ++i) lengths[Order[i]] = static_cast<quint8>(bits(3));
        for (int i = codeCount; i < 19; ++i) lengths[Order[i]] = 0;
        Huffman lengthCode;
        if (build(lengthCode, lengths.data(), 19) != 0) return false;

        int index = 0;
        while (index < literalCount + distanceCount) {
            auto symbol = decode(lengthCode);
            if (symbol < 0) return false;
            if (symbol < 16) {
                lengths[index++] = static_cast<quint8>(symbol);
                continue;
            }
            int previous = 0;
            int repeat = 0;
            if (symbol == 16) {
                if (index == 0) return false;
                previous = lengths[index - 1];
                repeat = 3 + bits(2);
            } else if (symbol == 17) {
                repeat = 3 + bits(3);
            } else {
                repeat = 11 + bits(7);
            }
            if (index + repeat > literalCount + distanceCount) return false;
            while (repeat-- > 0) lengths[index++] = static_cast<quint8>(previous);
        }
        if (failed != Result::Ok || lengths[256] == 0) return false;

        Huffman literals;
        Huffman distances;
        const auto literalsLeft = build(literals, lengths.data(), literalCount);
        if (literalsLeft != 0 && (literalsLeft < 0 || literalCount - literals.count[0] != 1)) return false;
        const auto distancesLeft = build(distances, lengths.data() + literalCount, distanceCount);
        if (distancesLeft != 0 && (distancesLeft < 0 || distanceCount - distances.count[0] != 1)) return false;
        return codes(literals, distances);
    }

    const uchar* in;
    qsizetype size;
    qint64 maximum;
    const std::atomic<bool>* cancel;
    qsizetype position = 0;
    quint32 bitBuffer = 0;
    int bitCount = 0;
    QByteArray* output = nullptr;
    Result failed = Result::Ok;
};

bool isWindowsDeviceName(const QString& part) {
    static const QRegularExpression pattern("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(\\..*)?$", QRegularExpression::CaseInsensitiveOption);
    return pattern.match(part).hasMatch();
}

bool splitEntryPath(const QString& path, QStringList& parts) {
    if (path.contains(QChar(0)) || path.contains('\\') || path.contains(':') || path.startsWith('/')) return false;
    parts.clear();
    for (const auto& part : path.split('/', Qt::SkipEmptyParts)) {
        if (part == ".") continue;
        if (part == ".." || isWindowsDeviceName(part)) return false;
        parts << part;
    }
    return true;
}

struct PlannedEntry {
    ArchiveEntry entry;
    qsizetype offset = 0;       // into the tar data or zip file
    qint64 compressedSize = 0;
    quint16 method = 0;
    quint32 crc = 0;
};

// Pulls entries out of a tar without trusting its names; the checks run once the list is complete.
bool parseTar(const QByteArray& tar, QList<PlannedEntry>& entries, QString& problem) {
    const auto octal = [&](const char* field, int length, bool& ok) {
        qint64 value = 0;
        ok = true;
        for (int i = 0; i < length; ++i) {
            const auto digit = static_cast<unsigned char>(field[i]);
            if (digit == 0 || digit == ' ') {
                if (value > 0 || i > 0) break;
                continue;
            }
            if (digit < '0' || digit > '7') {
                ok = false;
                return value;
            }
            value = value * 8 + (digit - '0');
        }
        return value;
    };

    QString pendingName;
    QString pendingLink;
    qint64 pendingSize = -1;
    qsizetype position = 0;
    while (position + 512 <= tar.size()) {
        const auto* header = tar.constData() + position;
        if (std::all_of(header, header + 512, [](char byte) { return byte == 0; })) return true;

        bool ok = false;
        const auto stored = octal(header + 148, 8, ok);
        qint64 sum = 8 * 32; // the checksum field counts as spaces
        for (int i = 0; i < 512; ++i) {
            if (i >= 148 && i < 156) continue;
            sum += static_cast<unsigned char>(header[i]);
        }
        if (!ok || stored != sum) {
            problem = "The archive is damaged: a file header failed its check.";
            return false;
        }

        auto size = octal(header + 124, 12, ok);
        if (!ok || size < 0) {
            problem = "The archive is damaged: a file has an impossible size.";
            return false;
        }
        const auto type = header[156];
        if (pendingSize >= 0 && (type == '0' || type == 0 || type == '7')) size = pendingSize;
        if (position + 512 + size > tar.size()) {
            problem = "The archive is damaged: it ends in the middle of a file.";
            return false;
        }
        const auto bodyStart = position + 512;
        const auto next = bodyStart + (size + 511) / 512 * 512;

        const auto body = [&](qint64 length) { return QByteArray(tar.constData() + bodyStart, length); };

        if (type == 'L' || type == 'K') {
            auto value = body(size);
            while (value.endsWith('\0')) value.chop(1);
            (type == 'L' ? pendingName : pendingLink) = QString::fromUtf8(value);
            position = next;
            continue;
        }
        if (type == 'x' || type == 'g') {
            if (type == 'x') {
                const auto records = body(size);
                qsizetype cursor = 0;
                while (cursor < records.size()) {
                    const auto space = records.indexOf(' ', cursor);
                    if (space < 0) break;
                    const auto length = records.mid(cursor, space - cursor).toLongLong();
                    if (length <= 0 || cursor + length > records.size()) break;
                    const auto record = records.mid(space + 1, cursor + length - space - 2);
                    cursor += length;
                    const auto equals = record.indexOf('=');
                    if (equals < 0) continue;
                    const auto key = record.left(equals);
                    const auto value = QString::fromUtf8(record.mid(equals + 1));
                    if (key == "path") pendingName = value;
                    else if (key == "linkpath") pendingLink = value;
                    else if (key == "size") pendingSize = value.toLongLong();
                }
            }
            position = next;
            continue;
        }

        const auto stringField = [](const char* field, int length) {
            const auto* end = std::find(field, field + length, '\0');
            return QString::fromUtf8(field, end - field);
        };

        PlannedEntry planned;
        auto name = stringField(header, 100);
        if (QByteArrayView(header + 257, 5) == "ustar") {
            const auto prefix = stringField(header + 345, 155);
            if (!prefix.isEmpty()) name = prefix + '/' + name;
        }
        if (!pendingName.isEmpty()) name = pendingName;
        planned.entry.path = name;
        planned.entry.linkTarget = pendingLink.isEmpty() ? stringField(header + 157, 100) : pendingLink;
        planned.entry.mode = static_cast<quint32>(octal(header + 100, 8, ok));
        planned.entry.size = size;
        planned.offset = bodyStart;
        pendingName.clear();
        pendingLink.clear();
        pendingSize = -1;

        switch (type) {
        case '0':
        case '\0':
        case '7':
            planned.entry.type = ArchiveEntryType::File;
            break;
        case '5':
            planned.entry.type = ArchiveEntryType::Directory;
            break;
        case '2':
            planned.entry.type = ArchiveEntryType::SymbolicLink;
            break;
        case '1':
            planned.entry.type = ArchiveEntryType::HardLink;
            break;
        default:
            problem = QString("The archive is unsafe: %1 is a kind of file an SDK has no reason to contain.").arg(name);
            return false;
        }
        if (planned.entry.type == ArchiveEntryType::File && name.endsWith('/')) planned.entry.type = ArchiveEntryType::Directory;
        entries.append(planned);
        if (entries.size() > MaximumEntries) {
            problem = "The archive is unsafe: it lists far more files than an SDK has.";
            return false;
        }
        position = next;
    }
    problem = "The archive is damaged: it ended unexpectedly.";
    return false;
}

bool parseZip(const QByteArray& zip, QList<PlannedEntry>& entries, QString& problem) {
    const auto* data = reinterpret_cast<const uchar*>(zip.constData());
    const auto total = zip.size();
    const auto damaged = [&] {
        problem = "The archive is damaged: its file list could not be read.";
        return false;
    };

    qsizetype end = -1;
    for (qsizetype i = total - 22; i >= 0 && i >= total - 22 - 65535; --i) {
        if (readLittle(data + i, 4) == 0x06054b50) {
            end = i;
            break;
        }
    }
    if (end < 0) return damaged();
    const auto count = readLittle(data + end + 10, 2);
    const auto directorySize = readLittle(data + end + 12, 4);
    const auto directoryOffset = readLittle(data + end + 16, 4);
    if (count == 0xFFFF || directoryOffset == 0xFFFFFFFFu) {
        problem = "The archive is too large for Studio to read.";
        return false;
    }
    if (static_cast<qint64>(directoryOffset) + directorySize > end || count > MaximumEntries) return damaged();

    qsizetype cursor = directoryOffset;
    for (quint32 index = 0; index < count; ++index) {
        if (cursor + 46 > total || readLittle(data + cursor, 4) != 0x02014b50) return damaged();
        const auto madeBy = readLittle(data + cursor + 4, 2);
        const auto flags = readLittle(data + cursor + 8, 2);
        const auto method = readLittle(data + cursor + 10, 2);
        const auto crc = readLittle(data + cursor + 16, 4);
        const auto compressed = readLittle(data + cursor + 20, 4);
        const auto uncompressed = readLittle(data + cursor + 24, 4);
        const auto nameLength = readLittle(data + cursor + 28, 2);
        const auto extraLength = readLittle(data + cursor + 30, 2);
        const auto commentLength = readLittle(data + cursor + 32, 2);
        const auto attributes = readLittle(data + cursor + 38, 4);
        const auto localOffset = readLittle(data + cursor + 42, 4);
        if (cursor + 46 + nameLength + extraLength + commentLength > total) return damaged();
        const QByteArray nameBytes(reinterpret_cast<const char*>(data + cursor + 46), nameLength);
        cursor += 46 + nameLength + extraLength + commentLength;

        if (flags & 1) {
            problem = "The archive is encrypted, so Studio cannot open it.";
            return false;
        }
        if (compressed == 0xFFFFFFFFu || uncompressed == 0xFFFFFFFFu || localOffset == 0xFFFFFFFFu) {
            problem = "The archive is too large for Studio to read.";
            return false;
        }
        if (method != 0 && method != 8) {
            problem = "The archive uses a compression method Studio cannot read.";
            return false;
        }

        PlannedEntry planned;
        planned.entry.path = QString::fromUtf8(nameBytes);
        planned.entry.size = uncompressed;
        planned.method = static_cast<quint16>(method);
        planned.crc = crc;
        planned.compressedSize = compressed;
        planned.entry.mode = (madeBy >> 8) == 3 ? (attributes >> 16) : 0;
        if ((planned.entry.mode & 0170000) == 0120000) {
            problem = QString("The archive is unsafe: %1 is a link, which Studio does not unpack from a zip.").arg(planned.entry.path);
            return false;
        }
        planned.entry.type = nameBytes.endsWith('/') ? ArchiveEntryType::Directory : ArchiveEntryType::File;

        // The local header must agree with the central directory, or the two can name different files.
        if (localOffset + 30ll > total || readLittle(data + localOffset, 4) != 0x04034b50) return damaged();
        const auto localNameLength = readLittle(data + localOffset + 26, 2);
        const auto localExtraLength = readLittle(data + localOffset + 28, 2);
        if (localOffset + 30ll + localNameLength + localExtraLength + compressed > total) return damaged();
        if (localNameLength != nameLength || std::memcmp(data + localOffset + 30, nameBytes.constData(), nameLength) != 0) return damaged();
        planned.offset = static_cast<qsizetype>(localOffset) + 30 + localNameLength + localExtraLength;
        entries.append(planned);
    }
    return true;
}

// The folder bits of a tar or zip mode, always readable and writable by the person.
QFile::Permissions permissionsFor(quint32 mode) {
    QFile::Permissions permissions = QFile::ReadOwner | QFile::WriteOwner;
    if (mode & 0100) permissions |= QFile::ExeOwner;
    if (mode & 0040) permissions |= QFile::ReadGroup;
    if (mode & 0010) permissions |= QFile::ExeGroup;
    if (mode & 0004) permissions |= QFile::ReadOther;
    if (mode & 0001) permissions |= QFile::ExeOther;
    return permissions;
}

}

QString findUnsafeEntry(const QList<ArchiveEntry>& entries) {
    const auto escapes = [](const QString& path) { return QString("%1 would land outside the folder, so the archive is unsafe.").arg(path); };

    QHash<QString, ArchiveEntryType> kinds;
    QList<QStringList> paths;
    paths.reserve(entries.size());
    for (const auto& entry : entries) {
        QStringList parts;
        if (!splitEntryPath(entry.path, parts)) return escapes(entry.path);
        paths << parts;
        const auto path = parts.join('/');
        if (path.isEmpty()) continue;
        const auto found = kinds.constFind(path);
        if (found != kinds.constEnd() && !(*found == ArchiveEntryType::Directory && entry.type == ArchiveEntryType::Directory)) {
            return QString("The archive lists %1 twice, so it is unsafe.").arg(path);
        }
        kinds[path] = entry.type;
    }

    for (qsizetype index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        const auto& parts = paths[index];
        if (parts.isEmpty()) continue;
        const auto path = parts.join('/');

        for (int depth = 1; depth < parts.size(); ++depth) {
            const auto parent = kinds.constFind(parts.first(depth).join('/'));
            if (parent != kinds.constEnd() && *parent != ArchiveEntryType::Directory) return QString("%1 goes through a link or file, so the archive is unsafe.").arg(path);
        }

        if (entry.type == ArchiveEntryType::SymbolicLink) {
            const auto& target = entry.linkTarget;
            if (target.isEmpty() || target.startsWith('/') || target.contains('\\') || target.contains(':') || target.contains(QChar(0))) {
                return QString("The link %1 points at %2, outside the folder, so the archive is unsafe.").arg(path, target);
            }
            auto stack = parts.first(parts.size() - 1);
            const auto segments = target.split('/', Qt::SkipEmptyParts);
            for (int segment = 0; segment < segments.size(); ++segment) {
                if (segments[segment] == ".") continue;
                if (segments[segment] == "..") {
                    if (stack.isEmpty()) return QString("The link %1 points at %2, outside the folder, so the archive is unsafe.").arg(path, target);
                    stack.removeLast();
                    continue;
                }
                stack << segments[segment];
                const auto through = kinds.constFind(stack.join('/'));
                if (segment + 1 < segments.size() && through != kinds.constEnd() && *through == ArchiveEntryType::SymbolicLink) {
                    return QString("The link %1 goes through another link, so the archive is unsafe.").arg(path);
                }
            }
        } else if (entry.type == ArchiveEntryType::HardLink) {
            QStringList targetParts;
            if (!splitEntryPath(entry.linkTarget, targetParts) || targetParts.isEmpty()) return escapes(entry.linkTarget);
            const auto target = kinds.constFind(targetParts.join('/'));
            if (target == kinds.constEnd() || *target != ArchiveEntryType::File) return QString("The link %1 does not point at a file in the archive, so the archive is unsafe.").arg(path);
        }
    }
    return {};
}

QByteArray gunzipTar(const QByteArray& data, qint64 limit, QString& error, const std::atomic<bool>* cancelled) {
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData());
    if (data.size() < 18 || bytes[0] != 0x1f || bytes[1] != 0x8b || bytes[2] != 8) {
        error = "The archive is damaged: it is not a gzip file.";
        return {};
    }
    const auto flags = bytes[3];
    qsizetype position = 10;
    if (flags & 0x04) {
        if (position + 2 > data.size()) { error = "The archive is damaged."; return {}; }
        position += 2 + readLittle(bytes + position, 2);
    }
    const auto skipString = [&] {
        while (position < data.size() && bytes[position] != 0) ++position;
        ++position;
    };
    if (flags & 0x08) skipString();
    if (flags & 0x10) skipString();
    if (flags & 0x02) position += 2;
    if (position >= data.size()) { error = "The archive is damaged: it ended early."; return {}; }

    Inflater inflater(bytes + position, data.size() - position, limit, cancelled);
    QByteArray output;
    const auto result = inflater.run(output);
    if (result == Inflater::Result::TooLarge) {
        error = "The archive is too large to unpack safely.";
        return {};
    }
    if (result == Inflater::Result::Cancelled) {
        error = "Cancelled.";
        return {};
    }
    if (result != Inflater::Result::Ok) {
        error = "The archive is damaged: it could not be decompressed.";
        return {};
    }
    const auto trailer = position + inflater.consumed();
    if (trailer + 8 > data.size()) {
        error = "The archive is damaged: it ended early.";
        return {};
    }
    if (readLittle(bytes + trailer, 4) != crc32Of(reinterpret_cast<const uchar*>(output.constData()), output.size()) || readLittle(bytes + trailer + 4, 4) != static_cast<quint32>(output.size())) {
        error = "The archive is damaged: its checksum does not match.";
        return {};
    }
    return output;
}

ExtractResult extractArchive(const QString& archivePath, const QString& destination, const ExtractOptions& options) {
    ExtractResult result;
    const auto isCancelled = [&] { return options.cancelled && options.cancelled->load(); };
    const auto cancelledResult = [&] {
        result.cancelled = true;
        result.problem = "Cancelled.";
        return result;
    };
    const auto problemResult = [&](const QString& problem) {
        result.problem = problem;
        return result;
    };

    if (QFileInfo::exists(destination) || QFileInfo(destination).isSymLink()) return problemResult(QString("%1 already exists, so Studio cannot unpack into it.").arg(destination));
    if (isCancelled()) return cancelledResult();

    QFile input(archivePath);
    if (!input.open(QIODevice::ReadOnly)) return problemResult(QString("The archive %1 could not be read: %2").arg(archivePath, input.errorString()));
    const auto archive = input.readAll();
    input.close();

    const auto isZip = archivePath.endsWith(".zip", Qt::CaseInsensitive);
    QByteArray tar;
    QList<PlannedEntry> planned;
    QString problem;
    if (isZip) {
        if (!parseZip(archive, planned, problem)) return problemResult(problem);
    } else {
        tar = gunzipTar(archive, options.maxExtractedBytes, problem, options.cancelled);
        if (!problem.isEmpty()) return problem == "Cancelled." ? cancelledResult() : problemResult(problem);
        if (!parseTar(tar, planned, problem)) return problemResult(problem);
    }
    if (isCancelled()) return cancelledResult();

    QList<ArchiveEntry> entries;
    entries.reserve(planned.size());
    qint64 announced = 0;
    for (const auto& item : planned) {
        entries << item.entry;
        announced += item.entry.size;
    }
    if (announced > options.maxExtractedBytes) return problemResult("The archive is too large to unpack safely.");
    if (const auto unsafe = findUnsafeEntry(entries); !unsafe.isEmpty()) return problemResult(unsafe);

    if (!QDir().mkpath(destination)) return problemResult(QString("Studio could not create the folder %1.").arg(destination));
    const QString root = QDir::cleanPath(QFileInfo(destination).absoluteFilePath());
    const auto failed = [&](const QString& why) {
        QDir(root).removeRecursively();
        return problemResult(why);
    };

    const auto absolute = [&](const QString& path) {
        QStringList parts;
        splitEntryPath(path, parts);
        return root + '/' + parts.join('/');
    };

    auto writeFile = [&](const QString& target, const QByteArray& bytes, quint32 mode) -> QString {
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return QString("%1 could not be written: %2").arg(target, file.errorString());
        if (file.write(bytes) != bytes.size()) return QString("%1 could not be written: %2").arg(target, file.errorString());
        file.close();
        if (!QFile::setPermissions(target, permissionsFor(mode))) return QString("%1 could not be set up: %2").arg(target, file.errorString());
        return {};
    };

    auto entryData = [&](const PlannedEntry& item, QByteArray& out) -> QString {
        if (!isZip) {
            out = QByteArray(tar.constData() + item.offset, item.entry.size);
            return {};
        }
        const auto* data = reinterpret_cast<const uchar*>(archive.constData()) + item.offset;
        if (item.method == 0) {
            out = QByteArray(reinterpret_cast<const char*>(data), item.compressedSize);
        } else {
            Inflater inflater(data, item.compressedSize, std::min<qint64>(item.entry.size + 1, options.maxExtractedBytes), options.cancelled);
            const auto result = inflater.run(out);
            if (result == Inflater::Result::Cancelled) return "Cancelled.";
            if (result == Inflater::Result::TooLarge) return "The archive is too large to unpack safely.";
            if (result != Inflater::Result::Ok) return QString("The archive is damaged: %1 could not be decompressed.").arg(item.entry.path);
        }
        if (out.size() != item.entry.size || crc32Of(reinterpret_cast<const uchar*>(out.constData()), out.size()) != item.crc) return QString("The archive is damaged: %1 failed its check.").arg(item.entry.path);
        return {};
    };

    const auto total = static_cast<int>(planned.size());
    int done = 0;
    const auto report = [&] {
        ++done;
        if (options.progress) options.progress(done, total);
    };

    // Directories and regular files first, so hard links have something to copy.
    for (const auto& item : planned) {
        if (isCancelled()) {
            QDir(root).removeRecursively();
            return cancelledResult();
        }
        const auto target = absolute(item.entry.path);
        if (item.entry.type == ArchiveEntryType::Directory) {
            if (!QDir().mkpath(target)) return failed(QString("Studio could not create the folder %1.").arg(target));
        } else if (item.entry.type == ArchiveEntryType::File) {
            QByteArray bytes;
            const auto why = entryData(item, bytes);
            if (why == "Cancelled.") {
                QDir(root).removeRecursively();
                return cancelledResult();
            }
            if (!why.isEmpty()) return failed(why);
            if (const auto written = writeFile(target, bytes, item.entry.mode); !written.isEmpty()) return failed(written);
            ++result.fileCount;
        }
        report();
    }
    for (const auto& item : planned) {
        const auto target = absolute(item.entry.path);
        if (item.entry.type == ArchiveEntryType::HardLink) {
            QFile source(absolute(item.entry.linkTarget));
            if (!source.open(QIODevice::ReadOnly)) return failed(QString("%1 could not be copied.").arg(item.entry.path));
            if (const auto written = writeFile(target, source.readAll(), item.entry.mode); !written.isEmpty()) return failed(written);
            report();
        } else if (item.entry.type == ArchiveEntryType::SymbolicLink) {
#ifdef Q_OS_UNIX
            QDir().mkpath(QFileInfo(target).absolutePath());
            if (::symlink(item.entry.linkTarget.toUtf8().constData(), target.toUtf8().constData()) != 0) return failed(QString("The link %1 could not be created.").arg(item.entry.path));
            report();
#else
            return failed("Studio cannot create links in this archive on Windows.");
#endif
        }
    }

    result.ok = true;
    return result;
}

}
