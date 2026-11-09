#include "SoundImport.h"

#include <QFile>
#include <QFileInfo>

#ifdef STUDIO_HAS_MULTIMEDIA
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>

#include <span>
#endif

namespace Studio {

namespace {

bool looksLikeWav(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto header = file.read(12);
    return header.startsWith("RIFF") && header.mid(8, 4) == "WAVE";
}

#ifdef STUDIO_HAS_MULTIMEDIA
bool decodeWithQt(const QString& path, DecodedSound& sound, QString& why) {
    QAudioFormat format;
    format.setSampleRate(GamePcmSampleRate);
    format.setChannelCount(GamePcmChannels);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioDecoder decoder;
    decoder.setAudioFormat(format);
    decoder.setSource(QUrl::fromLocalFile(path));

    sound.sampleRate = GamePcmSampleRate;
    sound.channels = GamePcmChannels;
    QEventLoop loop;
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&] {
        const auto buffer = decoder.read();
        if (!buffer.isValid() || buffer.format().sampleFormat() != QAudioFormat::Int16 || buffer.format().channelCount() != GamePcmChannels) return;
        const auto samples = std::span(buffer.constData<qint16>(), buffer.frameCount() * GamePcmChannels);
        sound.samples.append(QList<qint16>(samples.begin(), samples.end()));
    });
    QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop, [&] {
        why = decoder.errorString();
        loop.quit();
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(120000, &loop, [&] {
        why = "decoding took too long";
        loop.quit();
    });
    decoder.start();
    loop.exec();
    decoder.stop();
    if (!why.isEmpty()) return false;
    if (sound.samples.isEmpty()) {
        why = "it holds no sound that could be decoded";
        return false;
    }
    return true;
}
#endif

}

bool canDecodeCompressedSound() {
#ifdef STUDIO_HAS_MULTIMEDIA
    return true;
#else
    return false;
#endif
}

bool importSoundFile(const QString& projectRoot, const QList<ProjectSound>& existing, const QString& filePath, ImportedSound& imported, QString& error) {
    const QFileInfo info(filePath);
    if (info.suffix().compare("wav", Qt::CaseInsensitive) == 0 || looksLikeWav(filePath)) return importWavSound(projectRoot, existing, filePath, imported, error);
    if (!info.exists()) return importWavSound(projectRoot, existing, filePath, imported, error); // the shared "could not be found" message

#ifdef STUDIO_HAS_MULTIMEDIA
    if (info.size() > MaximumSoundFileBytes) {
        error = QString("%1 is too large to import. Export a shorter sound and try again.").arg(info.fileName());
        return false;
    }
    DecodedSound decoded;
    QString why;
    if (!decodeWithQt(filePath, decoded, why)) {
        error = QString("%1 could not be decoded: %2. Export it as a WAV file and try again.").arg(info.fileName(), why);
        return false;
    }
    return storeImportedSound(projectRoot, existing, info.completeBaseName(), encodeWav(decoded), imported, error);
#else
    error = QString("%1 is not a WAV file, and this copy of Studio was built without Qt Multimedia, so it cannot decode MP3 or OGG. Export it as a WAV file and try again.").arg(info.fileName());
    return false;
#endif
}

}
