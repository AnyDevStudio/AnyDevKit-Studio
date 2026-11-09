// Host checks for the particle simulation in runtime/SceneGame. No Qt, SDK or GPU is involved, so this
// file can also run under sanitizers for a long time:
//
//   clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra \
//       -Wpedantic -Werror -Iruntime/SceneGame/include tests/tst_particles.cpp -o tst_particles
//   ./tst_particles --soak 600
#include "Scene/Particles.hpp.in"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {

std::size_t allocations = 0;

int failures = 0;
#define EXPECT(condition) do { if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (false)

using Particles::Emitter;
using Particles::Effect;
using Particles::Palette;
using Particles::Particle;
using Particles::Preset;
using Particles::Quad;

constexpr Particles::Color Background{0.08f, 0.1f, 0.2f};
constexpr Particles::Color White{1.0f, 1.0f, 1.0f};
constexpr std::array AllPresets{Preset::Fireworks, Preset::Fountain, Preset::Sparks, Preset::Snow, Preset::Rain};

Effect effectFor(Preset preset, std::uint32_t amount = 900, bool looping = true, std::uint32_t seed = 1234) {
    return {preset, Palette::Festival, amount, seed, 0.5f, 1.0f, 1.0f, 12.0f, looping};
}

// One emitter with its own storage, the way both the game and the preview use it.
struct Rig {
    explicit Rig(const Effect& effect, Particles::Area area = {400.0f, 200.0f, 400.0f, 400.0f}) : pool(effect.amount), quads(effect.amount) {
        emitter.Reset(effect, area, White, Background, pool, quads);
    }
    std::vector<Particle> pool;
    std::vector<Quad> quads;
    Emitter emitter;
};

std::uint64_t hashQuads(const Emitter& emitter, std::uint64_t hash = 1469598103934665603ull) {
    const auto quads = emitter.Quads();
    const auto* cursor = reinterpret_cast<const unsigned char*>(quads.data());
    for (std::size_t index = 0; index < quads.size_bytes(); ++index) hash = (hash ^ cursor[index]) * 1099511628211ull;
    return hash;
}

constexpr double Frame = 1.0 / 60.0;

void run(Emitter& emitter, double seconds) {
    for (double elapsed = 0.0; elapsed < seconds; elapsed += Frame) emitter.Update(Frame);
}

void sameSeedGivesTheSameShow() {
    for (const auto preset : AllPresets) {
        Rig first(effectFor(preset));
        Rig second(effectFor(preset));
        std::uint64_t hashA = 0, hashB = 0;
        for (int frame = 0; frame < 1200; ++frame) {
            first.emitter.Update(Frame);
            second.emitter.Update(Frame);
            hashA = hashQuads(first.emitter, hashA);
            hashB = hashQuads(second.emitter, hashB);
        }
        EXPECT(hashA == hashB);
        EXPECT(first.emitter.LiveCount() == second.emitter.LiveCount());

        Rig other(effectFor(preset, 900, true, 99));
        std::uint64_t hashC = 0;
        for (int frame = 0; frame < 1200; ++frame) {
            other.emitter.Update(Frame);
            hashC = hashQuads(other.emitter, hashC);
        }
        EXPECT(hashC != hashA);
    }
}

void motionFollowsElapsedTimeNotFrameRate() {
    for (const auto preset : AllPresets) {
        Rig sixty(effectFor(preset));
        Rig oneTwenty(effectFor(preset));
        for (int frame = 0; frame < 600; ++frame) sixty.emitter.Update(1.0 / 60.0);
        for (int frame = 0; frame < 1200; ++frame) oneTwenty.emitter.Update(1.0 / 120.0);
        EXPECT(hashQuads(sixty.emitter) == hashQuads(oneTwenty.emitter));
        EXPECT(sixty.emitter.Steps() == 1200);
    }
}

void aHitchIsNotReplayed() {
    Rig rig(effectFor(Preset::Fountain));
    rig.emitter.Update(Particles::MaxFrameSeconds);
    const auto before = rig.emitter.Steps();
    EXPECT(before == 12);
    rig.emitter.Update(30.0);
    EXPECT(rig.emitter.Steps() - before == 12);
}

void neverExceedsItsBudget() {
    for (const auto preset : AllPresets) {
        for (const std::uint32_t amount : {1u, 2u, 7u, 16u, 200u, 1400u}) {
            Rig rig(effectFor(preset, amount));
            for (int frame = 0; frame < 3600; ++frame) {
                if (frame % 13 == 0) rig.emitter.Burst();
                rig.emitter.Update(Frame);
                EXPECT(rig.emitter.LiveCount() <= amount);
                EXPECT(rig.emitter.Quads().size() <= amount);
            }
        }
    }
}

void theFrameLoopDoesNotAllocate() {
    Rig rig(effectFor(Preset::Fireworks, 1200));
    rig.emitter.Update(Frame);
    allocations = 0;
    for (int frame = 0; frame < 3600; ++frame) {
        if (frame % 40 == 0) rig.emitter.Burst();
        rig.emitter.SetPosition(static_cast<float>(frame), 100.0f);
        rig.emitter.Update(Frame);
    }
    EXPECT(allocations == 0);
}

void quadsStayOnThePixelGrid() {
    const float cell = 8.0f;
    for (const auto preset : AllPresets) {
        auto effect = effectFor(preset);
        effect.pixelSize = cell;
        Rig rig(effect);
        run(rig.emitter, 4.0);
        EXPECT(!rig.emitter.Quads().empty());
        for (const auto& quad : rig.emitter.Quads()) {
            const auto inCells = [cell](float value) { return static_cast<float>(static_cast<int>(value / cell)) * cell == value; };
            EXPECT(inCells(quad.x));
            EXPECT(inCells(quad.y));
            EXPECT(inCells(quad.width));
            EXPECT(inCells(quad.height));
            EXPECT(quad.width >= cell && quad.height >= cell);
            EXPECT(quad.x + quad.width > 0.0f && quad.y + quad.height > 0.0f);
            EXPECT(quad.x < Particles::StageWidth && quad.y < Particles::StageHeight);
        }
    }
}

// The fade is a handful of flat colors per particle, never a smooth blend.
void colorsFadeInSteps() {
    auto effect = effectFor(Preset::Fountain, 1500);
    effect.palette = Palette::Color;
    Rig rig(effect);
    std::vector<Particles::Color> seen;
    for (int frame = 0; frame < 600; ++frame) {
        rig.emitter.Update(Frame);
        for (const auto& quad : rig.emitter.Quads()) {
            bool known = false;
            for (const auto& color : seen) known = known || std::memcmp(&color, &quad.color, sizeof(color)) == 0;
            if (!known) seen.push_back(quad.color);
        }
    }
    EXPECT(seen.size() >= 3);
    EXPECT(seen.size() <= Particles::RampSteps + 1);
}

void oneShotPlaysOnceAndEnds() {
    for (const auto preset : AllPresets) {
        Rig rig(effectFor(preset, 400, false));
        run(rig.emitter, 3.0);
        EXPECT(rig.emitter.PeakLive() > 0);
        run(rig.emitter, 30.0);
        EXPECT(rig.emitter.LiveCount() == 0);
        EXPECT(rig.emitter.Quads().empty());
    }
}

void loopingNeverRunsDry() {
    for (const auto preset : AllPresets) {
        Rig rig(effectFor(preset));
        run(rig.emitter, 6.0);
        std::size_t lowest = rig.emitter.LiveCount();
        for (int frame = 0; frame < 600; ++frame) {
            rig.emitter.Update(Frame);
            lowest = std::min(lowest, rig.emitter.LiveCount());
        }
        EXPECT(lowest > 0);
    }
}

void aHeldOneShotWaitsForItsBurst() {
    for (const auto preset : AllPresets) {
        Rig rig(effectFor(preset, 300, false));
        rig.emitter.AwaitBurst();
        run(rig.emitter, 3.0);
        EXPECT(rig.emitter.PeakLive() == 0);
        EXPECT(rig.emitter.Quads().empty());
        rig.emitter.Burst();
        run(rig.emitter, 1.0);
        EXPECT(rig.emitter.PeakLive() > 0);
    }
}

void burstsAddToALoopingEffect() {
    Rig plain(effectFor(Preset::Sparks, 1000));
    Rig burst(effectFor(Preset::Sparks, 1000));
    burst.emitter.Burst();
    burst.emitter.Update(1.0 / 120.0);
    plain.emitter.Update(1.0 / 120.0);
    EXPECT(burst.emitter.LiveCount() > plain.emitter.LiveCount());
}

void aHiddenEffectStopsSpawningButAgesOut() {
    Rig rig(effectFor(Preset::Fountain, 600));
    run(rig.emitter, 2.0);
    rig.emitter.SetVisible(false);
    EXPECT(rig.emitter.Quads().empty());
    run(rig.emitter, 10.0);
    EXPECT(rig.emitter.LiveCount() == 0);
    rig.emitter.SetVisible(true);
    run(rig.emitter, 1.0);
    EXPECT(rig.emitter.LiveCount() > 0);
}

void movingTheEmitterMovesWhereParticlesStart() {
    auto effect = effectFor(Preset::Sparks, 400);
    Rig rig(effect, {100.0f, 100.0f, 100.0f, 100.0f});
    rig.emitter.SetPosition(1600.0f, 800.0f);
    run(rig.emitter, 1.0);
    float meanX = 0.0f;
    const auto quads = rig.emitter.Quads();
    for (const auto& quad : quads) meanX += quad.x;
    EXPECT(!quads.empty());
    EXPECT(meanX / static_cast<float>(quads.size()) > 1200.0f);
}

void colorChangesApplyToLiveParticles() {
    auto effect = effectFor(Preset::Fountain, 400);
    effect.palette = Palette::Color;
    Rig rig(effect);
    run(rig.emitter, 1.0);
    rig.emitter.SetBaseColor({1.0f, 0.0f, 0.0f});
    rig.emitter.Update(Frame);
    bool anyRed = false;
    for (const auto& quad : rig.emitter.Quads()) anyRed = anyRed || (quad.color.red > quad.color.green + 0.4f);
    EXPECT(anyRed);
}

void paletteFixesColorsRegardlessOfBase() {
    auto effect = effectFor(Preset::Fountain, 400);
    effect.palette = Palette::Fire;
    Rig a(effect);
    Rig b(effect);
    b.emitter.SetBaseColor({0.0f, 1.0f, 0.0f});
    run(a.emitter, 1.0);
    run(b.emitter, 1.0);
    EXPECT(hashQuads(a.emitter) == hashQuads(b.emitter));
}

void aResetRestartsTheEffect() {
    Rig rig(effectFor(Preset::Fireworks, 600));
    run(rig.emitter, 5.0);
    const auto first = hashQuads(rig.emitter);
    rig.emitter.Reset(effectFor(Preset::Fireworks, 600), {400.0f, 200.0f, 400.0f, 400.0f}, White, Background, rig.pool, rig.quads);
    EXPECT(rig.emitter.LiveCount() == 0);
    EXPECT(rig.emitter.Steps() == 0);
    run(rig.emitter, 5.0);
    EXPECT(hashQuads(rig.emitter) == first);
}

void soak(int simulatedSeconds) {
    std::printf("soak: %d simulated seconds per preset\n", simulatedSeconds);
    for (const auto preset : AllPresets) {
        for (const std::uint32_t amount : {16u, 16000u}) {
            auto effect = effectFor(preset, amount);
            effect.spread = 1.0f;
            effect.speed = 3.0f;
            effect.gravity = preset == Preset::Snow ? -2.0f : 3.0f;
            effect.pixelSize = 2.0f;
            Rig rig(effect, {0.0f, 0.0f, 1920.0f, 1080.0f});
            rig.emitter.Update(Frame);
            allocations = 0;
            std::uint64_t dropped = 0;
            for (int frame = 0; frame < simulatedSeconds * 60; ++frame) {
                if (frame % 45 == 0) rig.emitter.Burst();
                if (frame % 971 == 0) rig.emitter.SetBaseColor({0.3f, 0.5f, 0.9f});
                if (frame % 1373 == 0) rig.emitter.SetVisible(false);
                if (frame % 1373 == 80) rig.emitter.SetVisible(true);
                rig.emitter.SetPosition(static_cast<float>(frame % 1920), static_cast<float>(frame % 1080));
                rig.emitter.Update(frame % 7 == 0 ? Frame * 3 : Frame);
                EXPECT(rig.emitter.LiveCount() <= amount);
                EXPECT(rig.emitter.Quads().size() <= amount);
                dropped = rig.emitter.Dropped();
            }
            EXPECT(allocations == 0);
            std::printf("soak: preset %d amount %u live %zu dropped %llu\n", static_cast<int>(preset), amount, rig.emitter.LiveCount(), static_cast<unsigned long long>(dropped));
        }
    }
}

}

void* operator new(std::size_t bytes) {
    ++allocations;
    if (void* memory = std::malloc(bytes)) return memory;
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

int main(int argc, char** argv) {
    sameSeedGivesTheSameShow();
    motionFollowsElapsedTimeNotFrameRate();
    aHitchIsNotReplayed();
    neverExceedsItsBudget();
    theFrameLoopDoesNotAllocate();
    quadsStayOnThePixelGrid();
    colorsFadeInSteps();
    oneShotPlaysOnceAndEnds();
    loopingNeverRunsDry();
    aHeldOneShotWaitsForItsBurst();
    burstsAddToALoopingEffect();
    aHiddenEffectStopsSpawningButAgesOut();
    movingTheEmitterMovesWhereParticlesStart();
    colorChangesApplyToLiveParticles();
    paletteFixesColorsRegardlessOfBase();
    aResetRestartsTheEffect();
    if (argc == 3 && std::strcmp(argv[1], "--soak") == 0) soak(std::atoi(argv[2]));
    if (failures != 0) {
        std::printf("%d checks failed\n", failures);
        return 1;
    }
    std::printf("all particle checks passed\n");
    return 0;
}
