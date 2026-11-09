#include "SoundPreviewPlayer.h"

#ifdef STUDIO_HAS_MULTIMEDIA
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>
#endif

namespace Studio {

#ifdef STUDIO_HAS_MULTIMEDIA

struct SoundPreviewPlayer::Private {
    QMediaPlayer player;
    QAudioOutput output;
};

SoundPreviewPlayer::SoundPreviewPlayer(QObject* parent) : QObject(parent), d(std::make_unique<Private>()) {
    d->player.setAudioOutput(&d->output);
}

bool SoundPreviewPlayer::available() { return true; }

void SoundPreviewPlayer::play(const QString& path) {
    d->player.stop();
    d->player.setSource(QUrl::fromLocalFile(path));
    d->player.play();
}

void SoundPreviewPlayer::stop() { d->player.stop(); }

#else

struct SoundPreviewPlayer::Private {};

SoundPreviewPlayer::SoundPreviewPlayer(QObject* parent) : QObject(parent) {}

bool SoundPreviewPlayer::available() { return false; }

void SoundPreviewPlayer::play(const QString&) {}

void SoundPreviewPlayer::stop() {}

#endif

SoundPreviewPlayer::~SoundPreviewPlayer() = default;

}
