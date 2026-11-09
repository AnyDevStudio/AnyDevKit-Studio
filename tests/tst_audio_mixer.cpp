// Host checks for the game's sound mixer in runtime/SceneGame. No Qt, SDK or sound device is involved
// and the mixer only ever writes to memory, so nothing here makes a sound. The CMake target builds
// this file under AddressSanitizer and UBSan, and again under ThreadSanitizer for the two-thread test.
#include "Audio/Mixer.hpp.in"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <thread>
#include <vector>

namespace {

std::atomic<std::size_t> allocations{0};

int failures = 0;
#define EXPECT(condition) do { if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (false)

using Audio::Clip;
using Audio::Mixer;

// Stereo frames of one constant value, enough to tell which clip a sample came from.
struct Tone {
    Tone(std::int16_t level, std::uint32_t frames) : samples(frames * 2, level) {}
    Clip clip() const { return {samples.data(), static_cast<std::uint32_t>(samples.size() / 2)}; }
    std::vector<std::int16_t> samples;
};

std::vector<std::int16_t> mix(Mixer& mixer, std::uint32_t frames) {
    std::vector<std::int16_t> output(frames * 2, 12345);
    mixer.Mix(output);
    return output;
}

void thereIsAFixedVoiceCountAndADocumentedFormat() {
    EXPECT(Audio::VoiceCount == 16);
    EXPECT(Audio::SampleRate == 48000);
    EXPECT(Audio::Channels == 2);
    EXPECT(Audio::BlockFrames == 512);
}

void silenceWhenNothingPlays() {
    Mixer mixer({});
    const auto output = mix(mixer, 8);
    for (const auto sample : output) EXPECT(sample == 0);
    EXPECT(mixer.ActiveVoices() == 0);
}

void aSoundPlaysOnceThenItsVoiceIsFree() {
    Tone tone(1000, 4);
    const std::array clips{tone.clip()};
    Mixer mixer(clips);
    EXPECT(mixer.PlaySound(0));
    auto output = mix(mixer, 6);
    for (int sample = 0; sample < 8; ++sample) EXPECT(output[sample] == 1000);
    for (int sample = 8; sample < 12; ++sample) EXPECT(output[sample] == 0); // the block after the clip ends is padded
    EXPECT(mixer.ActiveVoices() == 0);
    output = mix(mixer, 4);
    for (const auto sample : output) EXPECT(sample == 0);
}

void soundsAddTogetherAndClampInsteadOfWrapping() {
    Tone quiet(1000, 8);
    Tone loud(30000, 8);
    Tone negative(-30000, 8);
    const std::array clips{quiet.clip(), loud.clip(), negative.clip()};
    {
        Mixer mixer(clips);
        mixer.PlaySound(0);
        mixer.PlaySound(0);
        const auto output = mix(mixer, 2);
        EXPECT(output[0] == 2000);
    }
    {
        Mixer mixer(clips);
        mixer.PlaySound(1);
        mixer.PlaySound(1);
        const auto output = mix(mixer, 2);
        EXPECT(output[0] == std::numeric_limits<std::int16_t>::max());
    }
    {
        Mixer mixer(clips);
        mixer.PlaySound(2);
        mixer.PlaySound(2);
        const auto output = mix(mixer, 2);
        EXPECT(output[0] == std::numeric_limits<std::int16_t>::min());
    }
}

void aNewSoundTakesTheVoiceFurthestAlongWhenAllAreBusy() {
    Tone longTone(100, 1000);
    Tone mark(5000, 1000);
    const std::array clips{longTone.clip(), mark.clip()};
    Mixer mixer(clips);
    for (std::size_t index = 0; index < Audio::VoiceCount - 1; ++index) mixer.PlaySound(0);
    mix(mixer, 10);
    EXPECT(mixer.ActiveVoices() == Audio::VoiceCount - 1);

    // One more sound while all sound voices are busy: it steals one, the count stays fixed.
    mixer.PlaySound(1);
    const auto output = mix(mixer, 1);
    EXPECT(mixer.ActiveVoices() == Audio::VoiceCount - 1);
    // 14 voices of the old sound and the new one: 14 * 100 + 5000.
    EXPECT(output[0] == 14 * 100 + 5000);
}

void musicLoopsSeamlesslyAcrossBlocks() {
    std::vector<std::int16_t> samples;
    for (std::int16_t frame = 1; frame <= 3; ++frame) {
        samples.push_back(frame);
        samples.push_back(frame);
    }
    const std::array clips{Clip{samples.data(), 3}};
    Mixer mixer(clips);
    EXPECT(mixer.PlayMusic(0));
    auto output = mix(mixer, 7);
    const std::array<std::int16_t, 7> expected{1, 2, 3, 1, 2, 3, 1};
    for (std::size_t frame = 0; frame < expected.size(); ++frame) EXPECT(output[frame * 2] == expected[frame]);
    output = mix(mixer, 2);
    EXPECT(output[0] == 2);
    EXPECT(output[2] == 3);
    EXPECT(mixer.ActiveVoices() == 1);
}

void playingTheSameMusicAgainDoesNotRestartIt() {
    std::vector<std::int16_t> samples;
    for (std::int16_t frame = 1; frame <= 8; ++frame) {
        samples.push_back(frame);
        samples.push_back(frame);
    }
    Tone other(7000, 8);
    const std::array clips{Clip{samples.data(), 8}, other.clip()};
    Mixer mixer(clips);
    mixer.PlayMusic(0);
    mix(mixer, 2);
    mixer.PlayMusic(0);
    auto output = mix(mixer, 1);
    EXPECT(output[0] == 3);
    mixer.PlayMusic(1);
    output = mix(mixer, 1);
    EXPECT(output[0] == 7000);
}

void stopMusicLeavesSoundsPlaying() {
    Tone music(100, 64);
    Tone sound(300, 64);
    const std::array clips{music.clip(), sound.clip()};
    Mixer mixer(clips);
    mixer.PlayMusic(0);
    mixer.PlaySound(1);
    auto output = mix(mixer, 1);
    EXPECT(output[0] == 400);
    EXPECT(mixer.ActiveVoices() == 2);
    mixer.StopMusic();
    output = mix(mixer, 1);
    EXPECT(output[0] == 300);
    EXPECT(mixer.ActiveVoices() == 1);
}

void volumeScalesEverythingAndStaysInRange() {
    Tone tone(1000, 64);
    const std::array clips{tone.clip()};
    Mixer mixer(clips);
    mixer.PlaySound(0);
    mixer.SetVolume(50);
    auto output = mix(mixer, 1);
    EXPECT(output[0] == 500);
    mixer.SetVolume(0);
    output = mix(mixer, 1);
    EXPECT(output[0] == 0);
    mixer.SetVolume(150); // clamps to 100
    output = mix(mixer, 1);
    EXPECT(output[0] == 1000);
    mixer.SetVolume(-5); // clamps to 0
    output = mix(mixer, 1);
    EXPECT(output[0] == 0);
}

void anUnknownOrEmptyClipIsIgnored() {
    const Tone tone(1000, 4);
    const std::array clips{tone.clip(), Clip{nullptr, 0}};
    Mixer mixer(clips);
    EXPECT(!mixer.PlaySound(9));
    EXPECT(!mixer.PlayMusic(9));
    EXPECT(!mixer.PlaySound(1));
    EXPECT(!mixer.PlayMusic(1));
    mix(mixer, 4);
    EXPECT(mixer.ActiveVoices() == 0);
}

void aFullQueueDropsCommandsInsteadOfBlocking() {
    Tone tone(100, 8);
    const std::array clips{tone.clip()};
    Mixer mixer(clips);
    std::size_t accepted = 0;
    for (std::size_t attempt = 0; attempt < Audio::CommandCapacity + 50; ++attempt) {
        if (mixer.PlaySound(0)) ++accepted;
    }
    EXPECT(accepted <= Audio::CommandCapacity);
    EXPECT(accepted >= Audio::CommandCapacity - 1);
    mix(mixer, 2);
    EXPECT(mixer.ActiveVoices() <= Audio::VoiceCount - 1); // the music voice is never taken by sounds
    EXPECT(mixer.PlaySound(0));
}

void aLongBlockIsMixedInChunksWithoutOverrunningItsBuffer() {
    Tone tone(777, 5000);
    const std::array clips{tone.clip()};
    Mixer mixer(clips);
    mixer.PlaySound(0);
    const auto output = mix(mixer, 4096);
    for (const auto sample : output) EXPECT(sample == 777);
}

void anEmptyBlockIsFine() {
    Tone tone(1, 1);
    const std::array clips{tone.clip()};
    Mixer mixer(clips);
    std::vector<std::int16_t> nothing;
    mixer.Mix(nothing);
    EXPECT(true);
}

void mixingNeverAllocates() {
    Tone tone(1000, 96000);
    const std::array clips{tone.clip()};
    Mixer mixer(clips);
    std::array<std::int16_t, Audio::BlockFrames * 2> block{};
    for (int frame = 0; frame < 200; ++frame) {
        if (frame % 3 == 0) mixer.PlaySound(0);
        if (frame % 7 == 0) mixer.PlayMusic(0);
        if (frame % 5 == 0) mixer.SetVolume(frame % 101);
        if (frame % 11 == 0) mixer.StopMusic();
    }
    const auto before = allocations.load();
    for (int block_ = 0; block_ < 1000; ++block_) mixer.Mix(block);
    EXPECT(allocations.load() == before);
}

// The frame thread only ever calls the Play and Set functions, and the audio thread only calls Mix, so
// that one queue is the whole of what they share. ThreadSanitizer checks that it really is a clean handoff.
void twoThreadsShareOnlyTheQueue() {
    Tone tone(10, 4096);
    const std::array clips{tone.clip()};
    Mixer mixer(clips);
    std::atomic<bool> stop{false};
    std::thread audioThread([&] {
        std::array<std::int16_t, Audio::BlockFrames * 2> block{};
        while (!stop.load(std::memory_order_relaxed)) mixer.Mix(block);
    });
    for (int round = 0; round < 20000; ++round) {
        mixer.PlaySound(0);
        if (round % 17 == 0) mixer.PlayMusic(0);
        if (round % 23 == 0) mixer.StopMusic();
        if (round % 31 == 0) mixer.SetVolume(round % 101);
    }
    stop.store(true);
    audioThread.join();
    EXPECT(mixer.ActiveVoices() <= Audio::VoiceCount);
}

}

// Counts heap use so mixingNeverAllocates can prove the audio path never touches it.
void* operator new(std::size_t size) {
    allocations.fetch_add(1);
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    std::abort();
}
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main() {
    thereIsAFixedVoiceCountAndADocumentedFormat();
    silenceWhenNothingPlays();
    aSoundPlaysOnceThenItsVoiceIsFree();
    soundsAddTogetherAndClampInsteadOfWrapping();
    aNewSoundTakesTheVoiceFurthestAlongWhenAllAreBusy();
    musicLoopsSeamlesslyAcrossBlocks();
    playingTheSameMusicAgainDoesNotRestartIt();
    stopMusicLeavesSoundsPlaying();
    volumeScalesEverythingAndStaysInRange();
    anUnknownOrEmptyClipIsIgnored();
    aFullQueueDropsCommandsInsteadOfBlocking();
    aLongBlockIsMixedInChunksWithoutOverrunningItsBuffer();
    anEmptyBlockIsFine();
    mixingNeverAllocates();
    twoThreadsShareOnlyTheQueue();
    if (failures != 0) {
        std::printf("%d checks failed\n", failures);
        return 1;
    }
    std::printf("all mixer checks passed\n");
    return 0;
}
