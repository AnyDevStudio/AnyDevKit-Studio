#include "Audio/AudioEngine.hpp"
#include "prx/libSceAudioOut/libSceAudioOut.hpp"
#include "prx/libkernel/Time/include/Time.hpp"
#include "prx/libkernel/include/Api.hpp"

namespace {

// The console's system user and its main speaker port, as AnyPS5's libSceAudio3d does it.
constexpr int SystemUser = 0xFF;
constexpr int MainPort = 0;
constexpr std::uint32_t FormatS16Stereo = 1;
constexpr int OutputFailureLimit = 8;
constexpr KernelUseconds RetryDelayMicroseconds = 10000;

}

void* APS5_VABI AudioEngine::threadEntry(void* argument) {
    static_cast<AudioEngine*>(argument)->runThread();
    return nullptr;
}

AudioEngine::AudioEngine() : mixer(clips) {
    for (std::size_t index = 0; index < GeneratedSounds::SoundCount; ++index) {
        clips[index] = {GeneratedSounds::Sounds[index].frames, GeneratedSounds::Sounds[index].frameCount};
    }
    if (GeneratedSounds::SoundCount != 0) started = start();
}

AudioEngine::~AudioEngine() {
    if (!started) return;
    stopRequested.store(true, std::memory_order_release);
    scePthreadJoin(thread, nullptr);
}

Audio::Mixer* AudioEngine::Mixer() { return started ? &mixer : nullptr; }

bool AudioEngine::start() {
    sceAudioOutInit(); // can report that it already ran; whether a port opens is what counts
    const int opened = sceAudioOutOpen(SystemUser, MainPort, 0, Audio::BlockFrames, Audio::SampleRate, FormatS16Stereo);
    if (opened < 0) return false;
    port = opened;
    if (scePthreadCreate(&thread, nullptr, threadEntry, this, "StudioAudio") != 0) {
        sceAudioOutClose(port);
        port = -1;
        return false;
    }
    return true;
}

void AudioEngine::runThread() {
    int failures = 0;
    while (!stopRequested.load(std::memory_order_acquire)) {
        mixer.Mix(output);
        if (sceAudioOutOutput(port, output.data()) < 0) {
            if (++failures >= OutputFailureLimit) break;
            sceKernelUsleep_nid_postfix(RetryDelayMicroseconds);
            continue;
        }
        failures = 0;
    }
    sceAudioOutOutput(port, nullptr); // blocks until what was queued has played out
    sceAudioOutClose(port);
}
