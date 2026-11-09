#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace Studio {

// Plays a sound once while editing. Only built with sound when Qt Multimedia is available; otherwise
// available() is false and the Sounds area shows the waveform instead of playing anything.
class SoundPreviewPlayer : public QObject {
    Q_OBJECT

public:
    explicit SoundPreviewPlayer(QObject* parent = nullptr);
    ~SoundPreviewPlayer() override;

    static bool available();
    void play(const QString& path);
    void stop();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}
