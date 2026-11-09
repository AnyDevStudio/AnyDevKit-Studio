#include "Sounds.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <limits>

// No sound is ever played here: these functions only read and write bytes.
using namespace Studio;

namespace {

void appendU16(QByteArray& out, quint16 value) { out.append(char(value & 0xff)).append(char(value >> 8)); }
void appendU32(QByteArray& out, quint32 value) {
    for (int shift = 0; shift < 32; shift += 8) out.append(char((value >> shift) & 0xff));
}

QByteArray wavBytes(quint16 format, int channels, int sampleRate, int bits, const QByteArray& data) {
    QByteArray body;
    body.append("WAVE");
    body.append("fmt ");
    appendU32(body, 16);
    appendU16(body, format);
    appendU16(body, quint16(channels));
    appendU32(body, quint32(sampleRate));
    appendU32(body, quint32(sampleRate * channels * bits / 8));
    appendU16(body, quint16(channels * bits / 8));
    appendU16(body, quint16(bits));
    body.append("data");
    appendU32(body, quint32(data.size()));
    body.append(data);
    if (data.size() % 2) body.append('\0');
    QByteArray out("RIFF");
    appendU32(out, quint32(body.size()));
    out.append(body);
    return out;
}

QByteArray samples16(std::initializer_list<qint16> values) {
    QByteArray out;
    for (const auto value : values) appendU16(out, quint16(value));
    return out;
}

QList<qint16> pcmSamples(const QByteArray& pcm) {
    QList<qint16> out;
    for (qsizetype offset = 0; offset + 1 < pcm.size(); offset += 2) {
        out << qint16(quint8(pcm[offset]) | (quint8(pcm[offset + 1]) << 8));
    }
    return out;
}

DecodedSound parsed(const QByteArray& bytes) {
    DecodedSound sound;
    QString why;
    if (!parseWav(bytes, sound, why)) qFatal("parseWav failed: %s", qPrintable(why));
    return sound;
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeFile(const QString& path, const QByteArray& bytes) {
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

void writeSoundFile(const QString& root, const QString& source, const QByteArray& bytes) {
    writeFile(root + '/' + source, bytes);
}

ProjectSound sound(const QString& name, const QString& source) { return {name, source}; }

}

class SoundTests : public QObject {
    Q_OBJECT

private slots:
    void parsesPcm16() {
        const auto decoded = parsed(wavBytes(1, 2, 44100, 16, samples16({1, -2, 3, -4})));
        QCOMPARE(decoded.sampleRate, 44100);
        QCOMPARE(decoded.channels, 2);
        QCOMPARE(decoded.samples, QList<qint16>({1, -2, 3, -4}));
    }

    void parsesPcm8WithItsOffsetRemoved() {
        QByteArray data;
        data.append(char(128));
        data.append(char(255));
        data.append(char(0));
        const auto decoded = parsed(wavBytes(1, 1, 8000, 8, data));
        QCOMPARE(decoded.samples.size(), 3);
        QCOMPARE(decoded.samples[0], qint16(0));
        QVERIFY(decoded.samples[1] > 32000);
        QVERIFY(decoded.samples[2] < -32000);
    }

    void parsesPcm24() {
        QByteArray data;
        data.append(char(0x00)); data.append(char(0x34)); data.append(char(0x12)); // 0x123400
        const auto decoded = parsed(wavBytes(1, 1, 8000, 24, data));
        QCOMPARE(decoded.samples.size(), 1);
        QCOMPARE(decoded.samples[0], qint16(0x1234));
    }

    void parsesPcm32AndFloat32() {
        QByteArray integers;
        appendU32(integers, 0x12340000);
        QCOMPARE(parsed(wavBytes(1, 1, 8000, 32, integers)).samples[0], qint16(0x1234));

        QByteArray floats;
        const float half = 0.5f;
        quint32 bits;
        memcpy(&bits, &half, 4);
        appendU32(floats, bits);
        const float over = 3.0f;
        memcpy(&bits, &over, 4);
        appendU32(floats, bits);
        const auto decoded = parsed(wavBytes(3, 1, 8000, 32, floats));
        QVERIFY(std::abs(decoded.samples[0] - 16384) <= 1);
        QCOMPARE(decoded.samples[1], std::numeric_limits<qint16>::max()); // out of range is clamped
    }

    void parsesTheExtensibleFormatByItsSubformat() {
        QByteArray body;
        body.append("WAVE");
        body.append("fmt ");
        appendU32(body, 40);
        appendU16(body, 0xfffe);
        appendU16(body, 1);
        appendU32(body, 48000);
        appendU32(body, 96000);
        appendU16(body, 2);
        appendU16(body, 16);
        appendU16(body, 22); // extension size
        appendU16(body, 16);
        appendU32(body, 0);
        appendU16(body, 1); // subformat GUID starts with the integer PCM code
        body.append(QByteArray(14, char(0)));
        body.append("data");
        appendU32(body, 2);
        appendU16(body, 321);
        QByteArray wav("RIFF");
        appendU32(wav, quint32(body.size()));
        wav.append(body);
        const auto decoded = parsed(wav);
        QCOMPARE(decoded.channels, 1);
        QCOMPARE(decoded.samples[0], qint16(321));
    }

    void skipsOtherChunksIncludingOddSizedOnes() {
        QByteArray body;
        body.append("WAVE");
        body.append("LIST");
        appendU32(body, 3);
        body.append("abc");
        body.append('\0'); // pad byte after an odd chunk
        body.append("fmt ");
        appendU32(body, 16);
        appendU16(body, 1);
        appendU16(body, 1);
        appendU32(body, 8000);
        appendU32(body, 16000);
        appendU16(body, 2);
        appendU16(body, 16);
        body.append("data");
        appendU32(body, 2);
        appendU16(body, 99);
        QByteArray wav("RIFF");
        appendU32(wav, quint32(body.size()));
        wav.append(body);
        QCOMPARE(parsed(wav).samples[0], qint16(99));
    }

    void refusesWhatItCannotUse() {
        DecodedSound decoded;
        QString why;
        QVERIFY(!parseWav(QByteArray(), decoded, why));
        QVERIFY(!why.isEmpty());
        QVERIFY(!parseWav("RIFFxxxxNOPE", decoded, why));
        QVERIFY(!parseWav(wavBytes(0x55, 1, 8000, 16, samples16({1})), decoded, why)); // MP3-in-WAV
        QVERIFY(why.contains("compress", Qt::CaseInsensitive));
        QVERIFY(!parseWav(wavBytes(1, 6, 8000, 16, samples16({1, 2, 3, 4, 5, 6})), decoded, why));
        QVERIFY(why.contains("stereo", Qt::CaseInsensitive));
        QVERIFY(!parseWav(wavBytes(1, 1, 0, 16, samples16({1})), decoded, why));
        QVERIFY(!parseWav(wavBytes(1, 1, 8000, 16, {}), decoded, why));

        // A data chunk that claims more than the file holds, and block alignment that lies.
        auto wrongSize = wavBytes(1, 1, 8000, 16, samples16({1}));
        wrongSize[40] = char(0xff);
        wrongSize[41] = char(0xff);
        QVERIFY(!parseWav(wrongSize, decoded, why));
        QVERIFY(!parseWav(wrongSize.left(20), decoded, why));
    }

    void refusesASampleRateThatWouldMakeAnImpossiblyLongSound() {
        DecodedSound decoded;
        QString why;
        QVERIFY(!parseWav(wavBytes(1, 1, 1, 16, samples16({1, 2, 3, 4})), decoded, why));
        QVERIFY2(why.contains("sample rate"), qPrintable(why));
    }

    void aSoundLongerThanTheLimitConvertsToNothing() {
        DecodedSound decoded;
        decoded.sampleRate = 8000;
        decoded.channels = 1;
        decoded.samples = QList<qint16>(qsizetype(MaximumTotalPcmBytes / GamePcmBytesPerFrame / GamePcmSampleRate * 8000 + 8000), 100);
        QVERIFY(gamePcmBytes(decoded) > MaximumTotalPcmBytes);
        QVERIFY(convertToGamePcm(decoded).isEmpty());

        QTemporaryDir directory;
        QTemporaryDir outside;
        writeFile(outside.path() + "/huge.wav", encodeWav(decoded));
        ImportedSound imported;
        QString error;
        QVERIFY(!importWavSound(directory.path(), {}, outside.path() + "/huge.wav", imported, error));
        QVERIFY2(error.contains("too long"), qPrintable(error));
    }

    void pcmIsLittleEndianStereoAt48k() {
        DecodedSound decoded;
        decoded.sampleRate = 48000;
        decoded.channels = 2;
        decoded.samples = {1000, -1000, 258, -258};
        const auto pcm = convertToGamePcm(decoded);
        QCOMPARE(pcm.size(), qsizetype(8));
        QCOMPARE(quint8(pcm[0]), quint8(0xe8));
        QCOMPARE(quint8(pcm[1]), quint8(0x03));
        QCOMPARE(pcmSamples(pcm), decoded.samples);
    }

    void monoIsCopiedToBothChannels() {
        DecodedSound decoded;
        decoded.sampleRate = 48000;
        decoded.channels = 1;
        decoded.samples = {7, -7, 9};
        QCOMPARE(pcmSamples(convertToGamePcm(decoded)), QList<qint16>({7, 7, -7, -7, 9, 9}));
    }

    void lowerRatesAreResampledLinearly() {
        DecodedSound decoded;
        decoded.sampleRate = 24000;
        decoded.channels = 1;
        decoded.samples = {0, 1000};
        const auto out = pcmSamples(convertToGamePcm(decoded));
        // Twice the frames, each frame copied to both channels; midpoints are interpolated.
        QCOMPARE(out.size(), qsizetype(8));
        QCOMPARE(out[0], qint16(0));
        QCOMPARE(out[2], qint16(500));
        QCOMPARE(out[4], qint16(1000));
        QCOMPARE(out[6], qint16(1000)); // holds the last sample past the end
    }

    void higherRatesAreResampledToTheRightLength() {
        DecodedSound decoded;
        decoded.sampleRate = 96000;
        decoded.channels = 1;
        for (int index = 0; index < 96000; ++index) decoded.samples << qint16(index % 100);
        QCOMPARE(convertToGamePcm(decoded).size(), qsizetype(48000 * 4));
    }

    void conversionLengthRoundsAndNeverDropsTheSoundEntirely() {
        DecodedSound decoded;
        decoded.sampleRate = 44100;
        decoded.channels = 2;
        decoded.samples = QList<qint16>(44100 * 2, 100);
        QCOMPARE(convertToGamePcm(decoded).size() / 4, qsizetype(48000));

        DecodedSound tiny;
        tiny.sampleRate = 192000;
        tiny.channels = 1;
        tiny.samples = {42};
        QCOMPARE(convertToGamePcm(tiny).size() / 4, qsizetype(1));
    }

    void namesAndSourcesAreChecked() {
        QVERIFY(isSoundName("coin-pickup_2"));
        QVERIFY(!isSoundName(""));
        QVERIFY(!isSoundName("has space"));
        QVERIFY(!isSoundName(QString(MaximumSoundNameLength + 1, 'a')));
        QVERIFY(isSoundSource("assets/sounds/coin.wav"));
        QVERIFY(!isSoundSource("assets/sounds/coin.mp3"));
        QVERIFY(!isSoundSource("/etc/passwd.wav"));
        QVERIFY(!isSoundSource("assets/sounds/../secret.wav"));
        QVERIFY(!isSoundSource("assets/sounds/a/b.wav"));
    }

    void validateCatchesDuplicatesAndLimits() {
        QVERIFY(validateSounds({sound("a", "assets/sounds/a.wav")}).isEmpty());
        QVERIFY(!validateSounds({sound("a", "assets/sounds/a.wav"), sound("A", "assets/sounds/b.wav")}).isEmpty());
        QVERIFY(!validateSounds({sound("bad name", "assets/sounds/a.wav")}).isEmpty());
        QVERIFY(!validateSounds({sound("a", "elsewhere/a.wav")}).isEmpty());
        QList<ProjectSound> many;
        for (int index = 0; index < MaximumProjectSounds + 1; ++index) many << sound(QString("s%1").arg(index), QString("assets/sounds/s%1.wav").arg(index));
        QVERIFY(!validateSounds(many).isEmpty());
    }

    void theGeneratedHeaderDeclaresTheTable() {
        const auto header = generateSoundsHeader(2);
        QVERIFY(header.contains("namespace GeneratedSounds"));
        QVERIFY(header.contains("SoundCount = 2"));
        QVERIFY(header.contains("extern const std::array<Sound, 2> Sounds"));

        const auto empty = generateSoundsHeader(0);
        QVERIFY(empty.contains("SoundCount = 0"));
        QVERIFY(empty.contains("Sounds"));
        QVERIFY(generateSoundSource({}).contains("std::array<Sound, 0> Sounds{}"));
    }

    void theGeneratedSourceEmbedsEachClipWithIncbin() {
        QList<SoundPcm> clips{{"assets/sounds/a.wav", QByteArray(8, 0)}, {"assets/sounds/b.wav", QByteArray(16, 0)}};
        const auto source = generateSoundSource(clips);
        QVERIFY(source.contains("SCENE_SOUND_DIRECTORY"));
        QVERIFY(source.contains(".incbin"));
        QVERIFY(source.contains("sound-0.pcm"));
        QVERIFY(source.contains("sound-1.pcm"));
        QVERIFY(source.contains("{StudioSound0, 2}"));
        QVERIFY(source.contains("{StudioSound1, 4}"));
        // Names and paths are free text and must not reach generated code.
        QVERIFY(!source.contains("a.wav"));
    }

    void exportWritesRawPcmAndTheTable() {
        QTemporaryDir directory;
        const auto root = directory.path();
        writeSoundFile(root, "assets/sounds/coin.wav", wavBytes(1, 1, 48000, 16, samples16({5, -5})));
        QString error;
        QVERIFY2(exportSounds(root, {sound("coin", "assets/sounds/coin.wav")}, error), qPrintable(error));
        const auto pcm = readFile(root + "/src/GeneratedSounds/sound-0.pcm");
        QCOMPARE(pcm.size(), qsizetype(8)); // 2 frames, 4 bytes each, no header
        QCOMPARE(pcmSamples(pcm), QList<qint16>({5, 5, -5, -5}));
        QVERIFY(readFile(root + "/src/GeneratedSounds.hpp").contains("SoundCount = 1"));
        QVERIFY(readFile(root + "/src/GeneratedSounds.cpp").contains("sound-0.pcm"));
    }

    void exportWithNoSoundsWritesEmptyTable() {
        QTemporaryDir directory;
        QString error;
        QVERIFY2(exportSounds(directory.path(), {}, error), qPrintable(error));
        QVERIFY(readFile(directory.path() + "/src/GeneratedSounds.hpp").contains("SoundCount = 0"));
        QVERIFY(readFile(directory.path() + "/src/GeneratedSounds.cpp").contains("std::array<Sound, 0> Sounds{}"));
    }

    void exportRemovesPcmFilesFromLeftoverSounds() {
        QTemporaryDir directory;
        const auto root = directory.path();
        writeSoundFile(root, "assets/sounds/a.wav", wavBytes(1, 1, 48000, 16, samples16({1})));
        writeSoundFile(root, "assets/sounds/b.wav", wavBytes(1, 1, 48000, 16, samples16({2})));
        QString error;
        QVERIFY(exportSounds(root, {sound("a", "assets/sounds/a.wav"), sound("b", "assets/sounds/b.wav")}, error));
        QVERIFY(QFile::exists(root + "/src/GeneratedSounds/sound-1.pcm"));
        QVERIFY(exportSounds(root, {sound("a", "assets/sounds/a.wav")}, error));
        QVERIFY(QFile::exists(root + "/src/GeneratedSounds/sound-0.pcm"));
        QVERIFY(!QFile::exists(root + "/src/GeneratedSounds/sound-1.pcm"));
    }

    void exportLeavesUnchangedOutputAlone() {
        QTemporaryDir directory;
        const auto root = directory.path();
        writeSoundFile(root, "assets/sounds/a.wav", wavBytes(1, 1, 48000, 16, samples16({1})));
        QString error;
        QVERIFY(exportSounds(root, {sound("a", "assets/sounds/a.wav")}, error));
        const auto pcmPath = root + "/src/GeneratedSounds/sound-0.pcm";
        QFile pcm(pcmPath);
        QVERIFY(pcm.open(QIODevice::ReadOnly));
        const auto firstWrite = QFileInfo(pcmPath).lastModified();
        pcm.close();
        QTest::qSleep(1100);
        QVERIFY(exportSounds(root, {sound("a", "assets/sounds/a.wav")}, error));
        QCOMPARE(QFileInfo(pcmPath).lastModified(), firstWrite);
    }

    void exportReportsAMissingOrWrongFileForThePerson() {
        QTemporaryDir directory;
        QString error;
        QVERIFY(!exportSounds(directory.path(), {sound("gone", "assets/sounds/gone.wav")}, error));
        QVERIFY2(error.contains("gone.wav"), qPrintable(error));
        writeSoundFile(directory.path(), "assets/sounds/bad.wav", "not a wave");
        QVERIFY(!exportSounds(directory.path(), {sound("bad", "assets/sounds/bad.wav")}, error));
        QVERIFY2(error.contains("bad.wav"), qPrintable(error));
    }

    void exportRefusesALinkThatLeavesTheProject() {
        QTemporaryDir directory;
        QTemporaryDir outside;
        const auto root = directory.path();
        const auto target = outside.path() + "/secret.wav";
        QFile::remove(target);
        writeFile(target, wavBytes(1, 1, 48000, 16, samples16({1})));
        QVERIFY(QDir().mkpath(root + "/assets/sounds"));
        if (!QFile::link(target, root + "/assets/sounds/coin.wav")) QSKIP("this system cannot make links");
        QString error;
        QVERIFY(!exportSounds(root, {sound("coin", "assets/sounds/coin.wav")}, error));
        QVERIFY2(error.contains("link", Qt::CaseInsensitive), qPrintable(error));
    }

    void exportAcceptsFilesOfEveryWavFlavour() {
        QTemporaryDir directory;
        const auto root = directory.path();
        writeSoundFile(root, "assets/sounds/a.wav", wavBytes(1, 2, 22050, 8, QByteArray(44, char(128))));
        QString error;
        QVERIFY2(exportSounds(root, {sound("a", "assets/sounds/a.wav")}, error), qPrintable(error));
        QVERIFY(!readFile(root + "/src/GeneratedSounds/sound-0.pcm").isEmpty());
    }

    void importCopiesAWavIntoTheProject() {
        QTemporaryDir directory;
        QTemporaryDir outside;
        const auto wav = wavBytes(1, 1, 8000, 16, samples16({1, 2}));
        writeFile(outside.path() + "/Coin Pickup.WAV", wav);
        ImportedSound imported;
        QString error;
        QVERIFY2(importWavSound(directory.path(), {}, outside.path() + "/Coin Pickup.WAV", imported, error), qPrintable(error));
        QCOMPARE(imported.name, QString("coin-pickup"));
        QCOMPARE(imported.source, QString("assets/sounds/coin-pickup.wav"));
        QCOMPARE(readFile(directory.path() + '/' + imported.source), wav);
    }

    void importAvoidsNamesAndReusesIdenticalFiles() {
        QTemporaryDir directory;
        QTemporaryDir outside;
        const auto wav = wavBytes(1, 1, 8000, 16, samples16({1, 2}));
        const auto other = wavBytes(1, 1, 8000, 16, samples16({3, 4}));
        writeFile(outside.path() + "/one/coin.wav", wav);
        writeFile(outside.path() + "/two/coin.wav", other);
        ImportedSound first;
        QString error;
        QVERIFY(importWavSound(directory.path(), {}, outside.path() + "/one/coin.wav", first, error));
        QCOMPARE(first.name, QString("coin"));
        QList<ProjectSound> list{sound(first.name, first.source)};

        ImportedSound second;
        QVERIFY(importWavSound(directory.path(), list, outside.path() + "/two/coin.wav", second, error));
        QCOMPARE(second.name, QString("coin-2"));
        QCOMPARE(second.source, QString("assets/sounds/coin-2.wav"));
        QVERIFY(readFile(directory.path() + '/' + second.source) == other);

        // The same bytes again need no second copy.
        ImportedSound again;
        QVERIFY(importWavSound(directory.path(), list, outside.path() + "/one/coin.wav", again, error));
        QCOMPARE(again.source, first.source);
        QVERIFY(again.name != first.name || QFileInfo(directory.path() + "/assets/sounds/coin.wav").exists());
    }

    void importRefusesWhatItCannotRead() {
        QTemporaryDir directory;
        QTemporaryDir outside;
        ImportedSound imported;
        QString error;
        QVERIFY(!importWavSound(directory.path(), {}, outside.path() + "/missing.wav", imported, error));
        QVERIFY2(error.contains("missing.wav"), qPrintable(error));
        writeFile(outside.path() + "/not.wav", "garbage");
        QVERIFY(!importWavSound(directory.path(), {}, outside.path() + "/not.wav", imported, error));
        QVERIFY2(error.contains("not.wav"), qPrintable(error));
        QVERIFY(!QDir(directory.path() + "/assets/sounds").exists() || QDir(directory.path() + "/assets/sounds").isEmpty());
    }

    void storeWritesDecodedWavBytes() {
        QTemporaryDir directory;
        ImportedSound imported;
        QString error;
        DecodedSound decoded;
        decoded.sampleRate = 48000;
        decoded.channels = 2;
        decoded.samples = {1, 2, 3, 4};
        QVERIFY2(storeImportedSound(directory.path(), {}, "Theme Song", encodeWav(decoded), imported, error), qPrintable(error));
        QCOMPARE(imported.name, QString("theme-song"));
        const auto reread = parsed(readFile(directory.path() + '/' + imported.source));
        QCOMPARE(reread.samples, decoded.samples);
    }

    void aProjectFromBeforeSoundIsDetected() {
        QTemporaryDir project;
        QVERIFY(!soundRuntimeNeedsUpdate(project.path())); // no game code to replace
        writeFile(project.path() + "/include/Scene/SceneState.hpp", "#include \"GeneratedVariables.hpp\"\n");
        QVERIFY(soundRuntimeNeedsUpdate(project.path()));
        writeFile(project.path() + "/include/Scene/SceneState.hpp", "#include \"Audio/Mixer.hpp\"\n");
        QVERIFY(!soundRuntimeNeedsUpdate(project.path()));
    }

    void previewDescribesAndSummarisesASound() {
        QTemporaryDir directory;
        const auto root = directory.path();
        QByteArray data;
        for (int index = 0; index < 8000; ++index) appendU16(data, quint16(qint16(index < 4000 ? 16384 : 0)));
        writeSoundFile(root, "assets/sounds/a.wav", wavBytes(1, 1, 8000, 16, data));
        const auto preview = soundPreview(root, "assets/sounds/a.wav", 10);
        QVERIFY2(preview.error.isEmpty(), qPrintable(preview.error));
        QCOMPARE(preview.sampleRate, 8000);
        QCOMPARE(preview.channels, 1);
        QVERIFY(std::abs(preview.seconds - 1.0) < 0.001);
        QCOMPARE(preview.peaks.size(), qsizetype(10));
        QVERIFY(preview.peaks.first() > 0.4);
        QCOMPARE(preview.peaks.last(), 0.0);
        QVERIFY(soundPreview(root, "assets/sounds/missing.wav", 10).error.contains("missing.wav"));
    }
};

QTEST_GUILESS_MAIN(SoundTests)
#include "tst_sounds.moc"
