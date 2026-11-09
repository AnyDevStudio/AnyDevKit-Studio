#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Sounds in a project. Each sound is a named entry in logic.json that points at a WAV file under
// assets/sounds. At build time a sound becomes raw PCM embedded in the game; the format is
// documented in docs/blocks-format.md. Everything here uses only Qt Core and plays nothing.
namespace Studio {

struct ProjectSound {
    QString name;   // 1 to MaximumSoundNameLength letters, digits, - or _; unique regardless of case
    QString source; // assets/sounds/<file>.wav
};

inline constexpr int MaximumSoundNameLength = 40;
inline constexpr int MaximumProjectSounds = 64;
inline constexpr qint64 MaximumSoundFileBytes = 512 * 1024 * 1024; // refused before reading
inline constexpr qint64 MaximumTotalPcmBytes = 256 * 1024 * 1024;  // about 23 minutes of stereo
inline constexpr int GamePcmSampleRate = 48000;
inline constexpr int GamePcmChannels = 2;
inline constexpr int GamePcmBytesPerFrame = 4;

bool isSoundName(const QString& name);
bool isSoundSource(const QString& source); // a plain .wav file directly in assets/sounds
QString validateSounds(const QList<ProjectSound>& sounds); // empty when valid

// Decoded samples, interleaved, 16-bit.
struct DecodedSound {
    int sampleRate = 0;
    int channels = 0;
    QList<qint16> samples;
};

bool parseWav(const QByteArray& bytes, DecodedSound& sound, QString& why); // `why` finishes "… because …"
QByteArray encodeWav(const DecodedSound& sound);                         // a 16-bit PCM WAV file

// To the format the game plays: 48 kHz, stereo, signed 16-bit little-endian, interleaved, no header.
// Mono is copied to both channels. Other rates use linear interpolation.
QByteArray convertToGamePcm(const DecodedSound& sound);

// One sound converted for the game, with the WAV source it came from.
struct SoundPcm {
    QString source;
    QByteArray pcm;
};

QString soundFileProblem(const QString& projectRoot, const QString& source); // empty when safe to read
QByteArray generateSoundsHeader(int soundCount);            // src/GeneratedSounds.hpp
QByteArray generateSoundSource(const QList<SoundPcm>& pcms); // src/GeneratedSounds.cpp

// Decodes every sound and writes src/GeneratedSounds/, GeneratedSounds.hpp and .cpp. Unchanged files are
// left alone, and files from sounds that are gone are removed. An empty list writes an empty table.
bool exportSounds(const QString& projectRoot, const QList<ProjectSound>& sounds, QString& error);

struct ImportedSound {
    QString name;
    QString source;
};

// Copies a WAV file into assets/sounds after checking that it can be used.
bool importWavSound(const QString& projectRoot, const QList<ProjectSound>& existing, const QString& filePath, ImportedSound& imported, QString& error);

// Stores already decoded audio as a WAV in assets/sounds, named after `baseName`.
bool storeImportedSound(const QString& projectRoot, const QList<ProjectSound>& existing, const QString& baseName, const QByteArray& wavBytes, ImportedSound& imported, QString& error);

// What the Sounds area shows for a sound.
struct SoundPreviewData {
    QString error;
    double seconds = 0;
    int sampleRate = 0;
    int channels = 0;
    QList<double> peaks; // each 0 to 1, one per column, loudest sample in that stretch
};

SoundPreviewData soundPreview(const QString& projectRoot, const QString& source, int columns);

// True when a project's game code is from before sound and saving were added, so it needs reinstalling.
bool soundRuntimeNeedsUpdate(const QString& projectRoot);

}
