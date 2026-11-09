#include "Sounds.h"

#include "Project.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Studio {

namespace {

constexpr auto SoundFolder = "assets/sounds";
constexpr auto SoundSubfolder = "sounds";
constexpr auto GeneratedFolder = "src/GeneratedSounds";
constexpr auto GeneratedHeader = "src/GeneratedSounds.hpp";
constexpr auto GeneratedSource = "src/GeneratedSounds.cpp";

constexpr quint16 WaveFormatPcm = 1;
constexpr quint16 WaveFormatFloat = 3;
constexpr quint16 WaveFormatExtensible = 0xfffe;
constexpr int MaximumNameLengthOnDisk = 40;

quint16 u16At(const QByteArray& bytes, qsizetype offset) {
    return quint16(quint8(bytes[offset]) | (quint8(bytes[offset + 1]) << 8));
}

quint32 u32At(const QByteArray& bytes, qsizetype offset) {
    return quint32(quint8(bytes[offset])) | quint32(quint8(bytes[offset + 1])) << 8 | quint32(quint8(bytes[offset + 2])) << 16 | quint32(quint8(bytes[offset + 3])) << 24;
}

qint16 clampToInt16(qint64 value) {
    return qint16(std::clamp<qint64>(value, std::numeric_limits<qint16>::min(), std::numeric_limits<qint16>::max()));
}

// Reduces one sample of the source format to 16-bit, keeping the most significant bits.
qint16 sampleAt(const QByteArray& data, qsizetype offset, quint16 format, int bytesPerSample) {
    if (format == WaveFormatFloat) {
        // Bytes are assembled by hand so a big-endian host reads the file the same way.
        const auto bits = u32At(data, offset);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        if (!std::isfinite(value)) return 0;
        return clampToInt16(std::llround(double(value) * 32768.0));
    }
    switch (bytesPerSample) {
    case 1:
        return qint16((int(quint8(data[offset])) - 128) * 256);
    case 2:
        return qint16(u16At(data, offset));
    case 3:
        return qint16(u16At(data, offset + 1)); // the top two of three bytes
    default:
        return qint16(u16At(data, offset + 2)); // the top two of four bytes
    }
}

bool failParse(QString& why, const QString& text) {
    why = text;
    return false;
}

QString safeBaseName(const QString& original) {
    QString safe;
    for (const auto character : original.toLower()) {
        const auto code = character.unicode();
        const auto keep = (code >= 'a' && code <= 'z') || (code >= '0' && code <= '9') || code == '_';
        if (keep) safe += character;
        else if (!safe.isEmpty() && !safe.endsWith('-')) safe += '-';
    }
    safe = safe.left(MaximumNameLengthOnDisk);
    while (safe.endsWith('-')) safe.chop(1);
    if (safe.isEmpty()) return "sound";
    static const QStringList reserved{"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    return reserved.contains(safe) ? "sound-" + safe : safe;
}

bool nameTaken(const QList<ProjectSound>& sounds, const QString& name) {
    return std::ranges::any_of(sounds, [&](const ProjectSound& sound) { return sound.name.toCaseFolded() == name.toCaseFolded(); });
}

QString uniqueName(const QList<ProjectSound>& sounds, const QString& base) {
    if (!nameTaken(sounds, base)) return base;
    for (int number = 2;; ++number) {
        const auto candidate = QString("%1-%2").arg(base).arg(number);
        if (!nameTaken(sounds, candidate)) return candidate;
    }
}

bool writeIfChanged(const QString& path, const QByteArray& contents, QString& error) {
    if (QFile existing(path); existing.size() == contents.size() && existing.open(QIODevice::ReadOnly) && existing.readAll() == contents) return true;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}

// Fails for a link at any level inside the project, so a build cannot write through one.
QString linkInsideProject(const QString& root, const QString& relativePath) {
    auto current = root;
    for (const auto& part : relativePath.split('/')) {
        current += '/' + part;
        const QFileInfo info(current);
        if (info.isSymLink() || info.isJunction()) return QString("%1 is a link, and Studio will not write through it. Replace it with a regular file or folder and try again.").arg(QDir(root).relativeFilePath(current));
    }
    return {};
}

}

bool isSoundName(const QString& name) {
    if (name.isEmpty() || name.size() > MaximumSoundNameLength) return false;
    for (const auto character : name) {
        const auto code = character.unicode();
        const auto keep = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9') || code == '-' || code == '_';
        if (!keep) return false;
    }
    return true;
}

bool isSoundSource(const QString& source) {
    if (!source.startsWith(QStringLiteral("assets/sounds/")) || !source.endsWith(QStringLiteral(".wav"))) return false;
    const auto file = source.mid(qstrlen("assets/sounds/"));
    if (file.size() <= 4 || file.contains('/') || file.contains('\\') || file.contains(QStringLiteral(".."))) return false;
    return true;
}

QString validateSounds(const QList<ProjectSound>& sounds) {
    if (sounds.size() > MaximumProjectSounds) return QString("This project has %1 sounds, and a project can hold at most %2. Remove some sounds and try again.").arg(sounds.size()).arg(MaximumProjectSounds);
    QSet<QString> seen;
    for (const auto& sound : sounds) {
        if (!isSoundName(sound.name)) return QString("The sound name \"%1\" is not allowed. A name is 1 to %2 letters, digits, - or _ characters.").arg(sound.name).arg(MaximumSoundNameLength);
        if (!isSoundSource(sound.source)) return QString("The sound \"%1\" must be a .wav file directly inside %2.").arg(sound.name, SoundFolder);
        const auto folded = sound.name.toCaseFolded();
        if (seen.contains(folded)) return QString("There is more than one sound named \"%1\". Give each sound its own name.").arg(sound.name);
        seen.insert(folded);
    }
    return {};
}

bool parseWav(const QByteArray& bytes, DecodedSound& sound, QString& why) {
    if (bytes.size() < 12 || bytes.left(4) != "RIFF" || bytes.mid(8, 4) != "WAVE") return failParse(why, "it is not a WAV file");

    quint16 format = 0;
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
    bool haveFormat = false;
    QByteArray data;
    bool haveData = false;

    qsizetype offset = 12;
    while (offset + 8 <= bytes.size()) {
        const auto chunk = bytes.mid(offset, 4);
        const auto size = qsizetype(u32At(bytes, offset + 4));
        const auto body = offset + 8;
        if (size > bytes.size() - body) return failParse(why, "it is cut off in the middle");

        if (chunk == "fmt ") {
            if (size < 16) return failParse(why, "its format chunk is too short");
            format = u16At(bytes, body);
            channels = u16At(bytes, body + 2);
            sampleRate = int(u32At(bytes, body + 4));
            bitsPerSample = u16At(bytes, body + 14);
            if (format == WaveFormatExtensible) {
                if (size < 26) return failParse(why, "its format chunk is too short");
                format = u16At(bytes, body + 24); // the first two bytes of the sub-format GUID
            }
            haveFormat = true;
        } else if (chunk == "data") {
            data = bytes.mid(body, size);
            haveData = true;
        }
        offset = body + size + (size & 1); // chunks are padded to even lengths
    }

    if (!haveFormat) return failParse(why, "it has no format chunk");
    if (!haveData) return failParse(why, "it holds no sound data");
    if (format != WaveFormatPcm && format != WaveFormatFloat) return failParse(why, "it is compressed. Export it again as uncompressed PCM");
    if (channels != 1 && channels != 2) return failParse(why, QString("it has %1 channels, and only mono or stereo can be used").arg(channels));
    if (sampleRate <= 0) return failParse(why, "its sample rate is not valid");
    const auto bytesPerSample = bitsPerSample / 8;
    const auto supported = format == WaveFormatFloat ? bitsPerSample == 32 : (bitsPerSample == 8 || bitsPerSample == 16 || bitsPerSample == 24 || bitsPerSample == 32);
    if (!supported) return failParse(why, QString("it uses %1-bit samples, which cannot be used. Use 8, 16, 24 or 32-bit PCM").arg(bitsPerSample));
    const auto frameBytes = bytesPerSample * channels;
    const auto frames = data.size() / frameBytes;
    if (frames == 0) return failParse(why, "it holds no sound data");

    sound.sampleRate = sampleRate;
    sound.channels = channels;
    sound.samples.resize(frames * channels);
    for (qsizetype frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            sound.samples[frame * channels + channel] = sampleAt(data, frame * frameBytes + channel * bytesPerSample, format, bytesPerSample);
        }
    }
    return true;
}

QByteArray encodeWav(const DecodedSound& sound) {
    QByteArray body("WAVE");
    const auto appendU32 = [](QByteArray& out, quint32 value) { for (int shift = 0; shift < 32; shift += 8) out.append(char((value >> shift) & 0xff)); };
    const auto appendU16 = [](QByteArray& out, quint16 value) { out.append(char(value & 0xff)).append(char(value >> 8)); };
    body.append("fmt ");
    appendU32(body, 16);
    appendU16(body, WaveFormatPcm);
    appendU16(body, quint16(sound.channels));
    appendU32(body, quint32(sound.sampleRate));
    appendU32(body, quint32(sound.sampleRate * sound.channels * 2));
    appendU16(body, quint16(sound.channels * 2));
    appendU16(body, 16);
    body.append("data");
    appendU32(body, quint32(sound.samples.size() * 2));
    body.reserve(body.size() + sound.samples.size() * 2);
    for (const auto sample : sound.samples) appendU16(body, quint16(sample));
    QByteArray wav("RIFF");
    appendU32(wav, quint32(body.size()));
    wav.append(body);
    return wav;
}

QByteArray convertToGamePcm(const DecodedSound& sound) {
    if (sound.channels < 1 || sound.channels > 2 || sound.sampleRate <= 0) return {};
    const auto inputFrames = sound.samples.size() / sound.channels;
    if (inputFrames == 0) return {};

    const auto channelAt = [&](qsizetype frame, int channel) {
        const auto source = sound.channels == 1 ? 0 : channel;
        return int(sound.samples[qBound<qsizetype>(0, frame, inputFrames - 1) * sound.channels + source]);
    };

    const auto outputFrames = std::max<qint64>(1, qint64(std::llround(double(inputFrames) * GamePcmSampleRate / double(sound.sampleRate))));
    QByteArray pcm;
    pcm.resize(outputFrames * GamePcmBytesPerFrame);
    auto* out = reinterpret_cast<uchar*>(pcm.data());
    const auto step = double(sound.sampleRate) / double(GamePcmSampleRate);
    for (qint64 frame = 0; frame < outputFrames; ++frame) {
        const auto position = double(frame) * step;
        const auto whole = qint64(position);
        const auto fraction = position - double(whole);
        for (int channel = 0; channel < GamePcmChannels; ++channel) {
            const auto a = double(channelAt(whole, channel));
            const auto b = double(channelAt(whole + 1, channel));
            const auto value = clampToInt16(std::llround(a + (b - a) * fraction));
            *out++ = uchar(quint16(value) & 0xff);
            *out++ = uchar(quint16(value) >> 8);
        }
    }
    return pcm;
}

QString soundFileProblem(const QString& projectRoot, const QString& source) {
    const auto root = QDir::cleanPath(QDir(projectRoot).absolutePath());
    if (!isSoundSource(source) || !isInsideDirectory(root + '/' + source, root)) return QString("%1 is not a .wav file inside the project's %2 folder.").arg(source, SoundFolder);
    auto current = root;
    const auto parts = source.split('/');
    for (qsizetype index = 0; index < parts.size(); ++index) {
        current += '/' + parts[index];
        const QFileInfo info(current);
        const auto shown = parts.mid(0, index + 1).join('/');
        if (info.isSymLink() || info.isJunction()) return QString("%1 is a link, so the sound %2 was not read. Replace the link with a regular file or folder and try again.").arg(shown, source);
        if (!info.exists()) return QString("%1 could not be found. Add the file to the project or remove the sound in the Sounds area.").arg(source);
        const auto isLast = index == parts.size() - 1;
        if (isLast && !info.isFile()) return QString("%1 is a folder, not a sound file.").arg(source);
        if (!isLast && !info.isDir()) return QString("%1 is a file, but the sound %2 needs a folder there.").arg(shown, source);
    }
    return {};
}

QByteArray generateSoundsHeader(int soundCount) {
    QByteArray header = "// Generated by AnyDevKit Studio from logic.json. Do not edit.\n#pragma once\n\n#include <array>\n#include <cstddef>\n#include <cstdint>\n\nnamespace GeneratedSounds {\n\n";
    header += "// Interleaved stereo, 48 kHz, signed 16-bit. One frame is two samples.\n";
    header += "struct Sound {\n    const std::int16_t* frames;\n    std::uint32_t frameCount;\n};\n\n";
    header += "inline constexpr std::size_t SoundCount = " + QByteArray::number(soundCount) + ";\n";
    header += "extern const std::array<Sound, " + QByteArray::number(soundCount) + "> Sounds; // defined in src/GeneratedSounds.cpp\n\n";
    header += "}\n";
    return header;
}

QByteArray generateSoundSource(const QList<SoundPcm>& pcms) {
    QByteArray source = "// Generated by AnyDevKit Studio from logic.json. Do not edit.\n#include \"GeneratedSounds.hpp\"\n\n";
    if (!pcms.isEmpty()) {
        // PCM is pulled in by the assembler, so the compiler never parses it and a build stays fast.
        source += "// SCENE_SOUND_DIRECTORY comes from CMakeLists.txt and points at src/GeneratedSounds.\n";
    }
    for (qsizetype index = 0; index < pcms.size(); ++index) {
        const auto number = QByteArray::number(index);
        const auto symbol = "StudioSound" + number;
        source += "__asm__(\".pushsection .rodata\\n.balign 16\\n.hidden " + symbol + "\\n" + symbol + ":\\n.incbin \\\"\" SCENE_SOUND_DIRECTORY \"/sound-" + number + ".pcm\\\"\\n.popsection\\n\");\n";
        source += "extern \"C\" const std::int16_t " + symbol + "[];\n";
    }
    source += "\nnamespace GeneratedSounds {\n\n";
    if (pcms.isEmpty()) {
        source += "const std::array<Sound, 0> Sounds{};\n";
    } else {
        source += "const std::array<Sound, " + QByteArray::number(pcms.size()) + "> Sounds{{\n";
        for (qsizetype index = 0; index < pcms.size(); ++index) {
            source += "    {StudioSound" + QByteArray::number(index) + ", " + QByteArray::number(pcms[index].pcm.size() / GamePcmBytesPerFrame) + "},\n";
        }
        source += "}};\n";
    }
    source += "\n}\n";
    return source;
}

bool exportSounds(const QString& projectRoot, const QList<ProjectSound>& sounds, QString& error) {
    error.clear();
    const auto fail = [&](const QString& message) {
        error = message;
        return false;
    };
    if (const auto problem = validateSounds(sounds); !problem.isEmpty()) return fail(problem);

    const auto root = QDir::cleanPath(QDir(projectRoot).absolutePath());
    QList<SoundPcm> pcms;
    qint64 total = 0;
    for (const auto& sound : sounds) {
        if (const auto problem = soundFileProblem(root, sound.source); !problem.isEmpty()) return fail(problem);
        const auto path = root + '/' + sound.source;
        if (QFileInfo(path).size() > MaximumSoundFileBytes) return fail(QString("%1 is too large to use. Export a shorter sound and try again.").arg(sound.source));
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return fail(QString("%1 could not be read: %2.").arg(sound.source, file.errorString()));
        DecodedSound decoded;
        QString why;
        if (!parseWav(file.readAll(), decoded, why)) return fail(QString("The sound %1 could not be used because %2.").arg(sound.source, why));
        auto pcm = convertToGamePcm(decoded);
        total += pcm.size();
        if (total > MaximumTotalPcmBytes) return fail(QString("The project's sounds add up to more than %1 MB of game memory. Shorten or remove some sounds and try again.").arg(MaximumTotalPcmBytes / (1024 * 1024)));
        pcms.append({sound.source, std::move(pcm)});
    }

    for (const auto* relative : {GeneratedFolder, GeneratedHeader, GeneratedSource}) {
        if (const auto problem = linkInsideProject(root, relative); !problem.isEmpty()) return fail(problem);
    }
    const auto folder = root + '/' + GeneratedFolder;
    if (!QDir().mkpath(folder)) return fail(QString("%1 could not be created. Check that the project folder is writable.").arg(GeneratedFolder));

    QSet<QString> keep;
    for (qsizetype index = 0; index < pcms.size(); ++index) {
        const auto name = QString("sound-%1.pcm").arg(index);
        keep.insert(name);
        QString problem;
        if (!writeIfChanged(folder + '/' + name, pcms[index].pcm, problem)) return fail(QString("%1/%2 could not be written: %3. Check that the folder is writable.").arg(GeneratedFolder, name, problem));
    }
    QDirIterator stale(folder, {"sound-*.pcm"}, QDir::Files);
    while (stale.hasNext()) {
        stale.next();
        if (!keep.contains(stale.fileName())) QFile::remove(stale.filePath());
    }

    for (const auto& [relative, contents] : {std::pair(QString(GeneratedHeader), generateSoundsHeader(int(pcms.size()))), std::pair(QString(GeneratedSource), generateSoundSource(pcms))}) {
        QString problem;
        if (!writeIfChanged(root + '/' + relative, contents, problem)) return fail(QString("%1 could not be written: %2. Check that the folder is writable.").arg(relative, problem));
    }
    return true;
}

bool storeImportedSound(const QString& projectRoot, const QList<ProjectSound>& existing, const QString& baseName, const QByteArray& wavBytes, ImportedSound& imported, QString& error) {
    error.clear();
    const auto root = QDir::cleanPath(QDir(projectRoot).absolutePath());
    const auto base = safeBaseName(baseName);

    // A file holding exactly these bytes is reused, so importing the same sound twice costs no disk or game memory.
    QString source;
    for (int number = 1;; ++number) {
        source = QString("%1/%2.wav").arg(SoundFolder, number == 1 ? base : QString("%1-%2").arg(base).arg(number));
        error = checkTargetPath(root, source);
        if (!error.isEmpty()) return false;
        const auto path = root + '/' + source;
        if (!QFileInfo::exists(path)) break;
        QFile on_disk(path);
        if (on_disk.size() == wavBytes.size() && on_disk.open(QIODevice::ReadOnly) && on_disk.readAll() == wavBytes) {
            imported.source = source;
            imported.name = uniqueName(existing, base);
            return true;
        }
    }

    const auto target = root + '/' + source;
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        error = QString("The %1 folder could not be created. Check that the project folder is writable.").arg(SoundFolder);
        return false;
    }
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(wavBytes) != wavBytes.size() || !file.commit()) {
        error = QString("%1 could not be saved in the %2 folder: %3. Check that the project folder is writable.").arg(baseName, SoundFolder, file.errorString());
        return false;
    }
    imported.source = source;
    imported.name = uniqueName(existing, base);
    return true;
}

bool importWavSound(const QString& projectRoot, const QList<ProjectSound>& existing, const QString& filePath, ImportedSound& imported, QString& error) {
    error.clear();
    if (filePath.isEmpty()) {
        error = "That is not a file on this computer. Save the sound to a folder first, then import it.";
        return false;
    }
    const QFileInfo info(filePath);
    if (!info.exists()) {
        error = QString("%1 could not be found. Check that it still exists and try again.").arg(info.fileName());
        return false;
    }
    if (!info.isFile()) {
        error = QString("%1 is not a file. Choose a WAV file and try again.").arg(info.fileName());
        return false;
    }
    if (info.size() > MaximumSoundFileBytes) {
        error = QString("%1 is too large to import. Export a shorter sound and try again.").arg(info.fileName());
        return false;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QString("%1 could not be opened: %2.").arg(info.fileName(), file.errorString());
        return false;
    }
    const auto bytes = file.readAll();
    DecodedSound decoded;
    QString why;
    if (!parseWav(bytes, decoded, why)) {
        error = QString("%1 could not be imported because %2.").arg(info.fileName(), why);
        return false;
    }
    return storeImportedSound(projectRoot, existing, info.completeBaseName(), bytes, imported, error);
}

SoundPreviewData soundPreview(const QString& projectRoot, const QString& source, int columns) {
    SoundPreviewData preview;
    if (const auto problem = soundFileProblem(projectRoot, source); !problem.isEmpty()) {
        preview.error = problem;
        return preview;
    }
    QFile file(QDir::cleanPath(QDir(projectRoot).absolutePath()) + '/' + source);
    if (!file.open(QIODevice::ReadOnly)) {
        preview.error = QString("%1 could not be read: %2.").arg(source, file.errorString());
        return preview;
    }
    DecodedSound decoded;
    QString why;
    if (!parseWav(file.readAll(), decoded, why)) {
        preview.error = QString("The sound %1 could not be used because %2.").arg(source, why);
        return preview;
    }
    const auto frames = decoded.samples.size() / decoded.channels;
    preview.sampleRate = decoded.sampleRate;
    preview.channels = decoded.channels;
    preview.seconds = double(frames) / decoded.sampleRate;
    columns = std::max(columns, 1);
    preview.peaks.reserve(columns);
    for (int column = 0; column < columns; ++column) {
        const auto first = qint64(frames) * column / columns;
        const auto last = std::max(first + 1, qint64(frames) * (column + 1) / columns);
        int loudest = 0;
        for (auto frame = first; frame < last && frame < frames; ++frame) {
            for (int channel = 0; channel < decoded.channels; ++channel) {
                loudest = std::max(loudest, std::abs(int(decoded.samples[frame * decoded.channels + channel])));
            }
        }
        preview.peaks.append(double(loudest) / 32768.0);
    }
    return preview;
}

}
