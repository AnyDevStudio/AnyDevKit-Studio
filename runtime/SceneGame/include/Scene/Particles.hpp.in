#pragma once
// The particle effects. Studio's stage preview compiles this same file, so what the stage shows is
// what the game runs. It uses no heap, no libm and nothing from the rest of the runtime.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Particles {

inline constexpr float StageWidth = 1920.0f;
inline constexpr float StageHeight = 1080.0f;

inline constexpr int StepsPerSecond = 120;
inline constexpr double StepSeconds = 1.0 / StepsPerSecond;
// A longer gap than this (a hitch, a debugger) is dropped rather than replayed in one frame.
inline constexpr double MaxFrameSeconds = 0.1;
inline constexpr std::size_t RampSteps = 6;

enum class Preset : std::uint8_t { Fireworks, Fountain, Sparks, Snow, Rain };
enum class Palette : std::uint8_t { Color, Festival, Fire, Ice };

inline constexpr std::array<std::string_view, 5> PresetNames{"fireworks", "fountain", "sparks", "snow", "rain"};
inline constexpr std::array<std::string_view, 4> PaletteNames{"color", "festival", "fire", "ice"};

struct Color {
    float red, green, blue;
};

// spread is 0 to 1. speed and gravity scale the preset's natural motion, 1 being natural.
struct Effect {
    Preset preset;
    Palette palette;
    std::uint32_t amount;
    std::uint32_t seed;
    float spread;
    float speed;
    float gravity;
    float pixelSize;
    bool looping;
};

// The rectangle the effect happens in, in stage units.
struct Area {
    float x, y, width, height;
};

// One flat rectangle, in stage units, on the effect's pixel grid.
struct Quad {
    float x, y, width, height;
    Color color;
};

struct Particle {
    float x, y;
    float velocityX, velocityY;
    float age, lifetime;
    float gravity;
    float sway; // Snow only: how far it drifts from side to side, in stage units per second
    std::uint8_t ramp;
    std::uint8_t size; // starting edge length in grid cells
    bool rocket;
};

// PCG32: the sequence depends only on the seed, so the same scene always plays the same way.
class Random final {
public:
    explicit Random(std::uint64_t seed = 0) : state(seed + Increment) { Next(); }

    std::uint32_t Next() {
        const auto previous = state;
        state = previous * 6364136223846793005ull + Increment;
        const auto shifted = static_cast<std::uint32_t>(((previous >> 18) ^ previous) >> 27);
        const auto rotation = static_cast<std::uint32_t>(previous >> 59);
        return (shifted >> rotation) | (shifted << ((0u - rotation) & 31u));
    }

    // Inclusive on both ends.
    int Range(int minimum, int maximum) {
        const auto span = static_cast<std::uint32_t>(maximum - minimum) + 1u;
        return minimum + static_cast<int>(Next() % span);
    }

    float Range(float minimum, float maximum) {
        const auto unit = static_cast<float>(Next() >> 8) / 16777216.0f;
        return minimum + (maximum - minimum) * unit;
    }

private:
    static constexpr std::uint64_t Increment = 0x14057b7ef767814full;
    std::uint64_t state;
};

namespace Detail {

// Sine over a 256-step circle: a table keeps this off libm and gives the same answer on every compiler.
consteval std::array<float, 256> buildSine() {
    constexpr double Pi = 3.14159265358979323846;
    std::array<float, 256> table{};
    for (std::size_t index = 0; index < table.size(); ++index) {
        double angle = 2.0 * Pi * static_cast<double>(index) / 256.0;
        if (angle > Pi) angle -= 2.0 * Pi;
        double term = angle;
        double sum = 0.0;
        for (int power = 1; power < 22; power += 2) {
            sum += term;
            term *= -angle * angle / (static_cast<double>(power + 1) * static_cast<double>(power + 2));
        }
        table[index] = static_cast<float>(sum);
    }
    return table;
}

inline constexpr std::array<float, 256> SineTable = buildSine();

constexpr float sine(int step) { return SineTable[static_cast<std::size_t>(step & 255)]; }
constexpr float cosine(int step) { return sine(step + 64); }
constexpr int Upward = 192; // the table's step for straight up, since y grows downwards

constexpr float absolute(float value) { return value < 0.0f ? -value : value; }

constexpr float squareRoot(float value) {
    if (value <= 0.0f) return 0.0f;
    float guess = value > 1.0f ? value : 1.0f;
    for (int iteration = 0; iteration < 18; ++iteration) guess = 0.5f * (guess + value / guess);
    return guess;
}

constexpr int floorToCell(float value, float cell) {
    const auto scaled = value / cell;
    const auto truncated = static_cast<int>(scaled);
    return static_cast<float>(truncated) > scaled ? truncated - 1 : truncated;
}

constexpr Color rgb(std::uint32_t hex) {
    return {static_cast<float>((hex >> 16) & 0xFFu) / 255.0f, static_cast<float>((hex >> 8) & 0xFFu) / 255.0f, static_cast<float>(hex & 0xFFu) / 255.0f};
}

constexpr Color mix(Color from, Color to, float amount) {
    return {from.red + (to.red - from.red) * amount, from.green + (to.green - from.green) * amount, from.blue + (to.blue - from.blue) * amount};
}

// Step 0 is a bright head, step 1 the color itself, and the rest sink towards the background.
constexpr std::array<Color, RampSteps> ramp(Color base, Color background) {
    const Color white{1.0f, 1.0f, 1.0f};
    return {mix(base, white, 0.7f), base, mix(base, background, 0.25f), mix(base, background, 0.5f), mix(base, background, 0.72f), mix(base, background, 0.88f)};
}

constexpr std::array<std::uint32_t, 8> FestivalColors{0xFFD23F, 0x67E3C8, 0xFF6B9A, 0x6FB6FF, 0xFF9A3C, 0xC28BFF, 0xC4F250, 0xFF6F59};
constexpr std::array<std::uint32_t, 3> FireColors{0xFFD23F, 0xFF9A3C, 0xFF6F59};
constexpr std::array<std::uint32_t, 3> IceColors{0xE8F2FF, 0x6FB6FF, 0x67E3C8};

constexpr std::uint32_t TrailColor = 0xFFB347;
constexpr float RocketGravity = 480.0f;
constexpr float RocketBurstSpeed = -90.0f;
constexpr int EmberEverySteps = 2;
constexpr float CullMargin = 96.0f;

}

// One particles item. The caller owns the pool and the quad list, each with effect.amount entries,
// so nothing here allocates. Reset is the only call that does more than a small fixed amount of work
// per particle, and it runs before the first frame.
class Emitter final {
public:
    void Reset(const Effect& newEffect, Area newArea, Color newBaseColor, Color newBackground, std::span<Particle> newPool, std::span<Quad> newQuads) {
        effect = newEffect;
        area = newArea;
        base = newBaseColor;
        background = newBackground;
        pool = newPool;
        quads = newQuads;
        random = Random(effect.seed);
        rebuildRamps();
        live = 0;
        quadCount = 0;
        peak = 0;
        steps = 0;
        dropped = 0;
        accumulator = 0.0;
        spawnCarry = 0.0f;
        launchCountdown = 0.0f;
        pendingBurst = 0;
        pendingRocket = false;
        burstShell = false;
        visible = true;
        if (!effect.looping) queuePlay();
        // Rain and snow fill the area before the first frame, so they do not start as an empty sky.
        if (effect.looping && (effect.preset == Preset::Snow || effect.preset == Preset::Rain)) {
            const int warmupSteps = static_cast<int>(longestFallSeconds() * StepsPerSecond);
            for (int step = 0; step < warmupSteps; ++step) stepOnce();
            steps = 0;
        }
        rebuildQuads();
    }

    void SetPosition(float x, float y) {
        area.x = x;
        area.y = y;
    }

    void SetBaseColor(Color color) {
        base = color;
        rebuildRamps();
    }

    void SetVisible(bool value) {
        visible = value;
        if (!visible) quadCount = 0;
    }

    // Plays the whole effect once, on top of anything already running.
    void Burst() {
        if (effect.preset == Preset::Fireworks) {
            burstShell = true;
            return;
        }
        pendingBurst = effect.amount;
    }

    // A one-shot effect started by a block should not also play when the game starts.
    void AwaitBurst() {
        pendingBurst = 0;
        pendingRocket = false;
        burstShell = false;
    }

    void Update(double seconds) {
        if (seconds > 0.0) {
            accumulator += std::min(seconds, MaxFrameSeconds);
            while (accumulator + 1e-9 >= StepSeconds) {
                accumulator -= StepSeconds;
                stepOnce();
            }
        }
        if (visible) rebuildQuads();
    }

    std::span<const Quad> Quads() const { return quads.first(quadCount); }
    std::size_t LiveCount() const { return live; }
    std::size_t PeakLive() const { return peak; }
    std::uint64_t Steps() const { return steps; }
    std::uint64_t Dropped() const { return dropped; }

private:
    static constexpr std::size_t MaximumRamps = Detail::FestivalColors.size() + 1;
    static constexpr std::size_t PendingExplosions = 16;

    void queuePlay() {
        if (effect.preset == Preset::Fireworks) pendingRocket = true;
        else pendingBurst = effect.amount;
    }

    void rebuildRamps() {
        const auto fill = [this](auto&& colors) {
            rampCount = 0;
            for (const auto color : colors) ramps[rampCount++] = Detail::ramp(Detail::rgb(color), background);
        };
        if (effect.palette == Palette::Festival) fill(Detail::FestivalColors);
        else if (effect.palette == Palette::Fire) fill(Detail::FireColors);
        else if (effect.palette == Palette::Ice) fill(Detail::IceColors);
        else {
            rampCount = 1;
            ramps[0] = Detail::ramp(base, background);
        }
        trailRamp = rampCount;
        ramps[trailRamp] = Detail::ramp(Detail::rgb(Detail::TrailColor), background);
    }

    float fallDirection() const { return effect.gravity < 0.0f ? -1.0f : 1.0f; }

    float fallSpeed(float natural) const {
        return std::max(Detail::absolute(natural * effect.speed * effect.gravity), 30.0f);
    }

    float naturalLifetime() const {
        switch (effect.preset) {
        case Preset::Fountain: return 1.5f;
        case Preset::Sparks: return 0.55f;
        case Preset::Snow: return area.height / fallSpeed(115.0f);
        case Preset::Rain: return area.height / fallSpeed(1100.0f);
        case Preset::Fireworks: return 1.5f;
        }
        return 1.0f;
    }

    float longestFallSeconds() const { return std::min(naturalLifetime() * 1.15f, 12.0f); }

    void stepOnce() {
        ++steps;
        if (visible) emitNewParticles();
        launchEmbers();
        integrate();
        peak = std::max(peak, live);
    }

    void emitNewParticles() {
        if (effect.preset == Preset::Fireworks) emitFireworks();
        else emitStream();
        if (pendingBurst > 0) {
            const auto window = burstWindowSteps();
            const auto batch = std::min(pendingBurst, std::max<std::uint32_t>(effect.amount / window, 1u));
            for (std::uint32_t count = 0; count < batch; ++count) spawnStreamParticle();
            pendingBurst -= batch;
        }
    }

    std::uint32_t burstWindowSteps() const {
        switch (effect.preset) {
        case Preset::Fountain: return StepsPerSecond * 4 / 5;
        case Preset::Snow: return StepsPerSecond * 3;
        case Preset::Rain: return StepsPerSecond * 3 / 2;
        case Preset::Sparks:
        case Preset::Fireworks: return 1;
        }
        return 1;
    }

    void emitStream() {
        if (!effect.looping) return;
        // Below the pool's size on purpose, so a Burst block still has room when the effect is steady.
        const float rate = static_cast<float>(effect.amount) * 0.7f / naturalLifetime();
        spawnCarry += rate * static_cast<float>(StepSeconds);
        while (spawnCarry >= 1.0f) {
            spawnCarry -= 1.0f;
            spawnStreamParticle();
        }
    }

    void emitFireworks() {
        if (burstShell) {
            burstShell = false;
            shell(area.x + area.width * 0.5f, area.y + area.height * 0.45f, 0.0f, wholeShell());
        }
        if (pendingRocket) {
            pendingRocket = false;
            launchRocket();
        }
        if (!effect.looping) return;
        launchCountdown -= static_cast<float>(StepSeconds);
        if (launchCountdown <= 0.0f) {
            launchRocket();
            launchCountdown = random.Range(0.45f, 1.05f);
        }
    }

    std::uint32_t wholeShell() const { return std::max<std::uint32_t>(effect.amount * 3 / 4, 1u); }
    std::uint32_t loopShell() const { return std::max<std::uint32_t>(effect.amount / 4, 1u); }

    void launchRocket() {
        Particle rocket{};
        rocket.rocket = true;
        rocket.x = area.x + area.width * random.Range(0.1f, 0.9f);
        rocket.y = area.y + area.height;
        rocket.velocityX = random.Range(-30.0f, 30.0f);
        const float rise = area.height * random.Range(0.55f, 0.92f);
        rocket.velocityY = -Detail::squareRoot(2.0f * Detail::RocketGravity * rise) * effect.speed;
        rocket.lifetime = 1.0f;
        rocket.ramp = static_cast<std::uint8_t>(random.Range(0, static_cast<int>(rampCount) - 1));
        rocket.size = 1;
        spawn(rocket);
    }

    // Embers are added before integrate runs, so they never appear in the list it is compacting.
    void launchEmbers() {
        if (effect.preset != Preset::Fireworks || steps % Detail::EmberEverySteps != 0) return;
        for (std::size_t index = 0, count = live; index < count; ++index) {
            const auto& rocket = pool[index];
            if (!rocket.rocket) continue;
            Particle ember{};
            ember.x = rocket.x;
            ember.y = rocket.y;
            ember.velocityX = random.Range(-10.0f, 10.0f);
            ember.velocityY = random.Range(20.0f, 60.0f);
            ember.lifetime = random.Range(0.28f, 0.42f);
            ember.gravity = 120.0f;
            ember.ramp = static_cast<std::uint8_t>(trailRamp);
            ember.size = 1;
            if (!spawn(ember)) return;
        }
    }

    // A filled ball, a double ring or a slow willow, with the same three shapes as PixelFireworks.
    void shell(float x, float y, float driftX, std::uint32_t sparks) {
        const auto colorRamp = static_cast<std::uint8_t>(random.Range(0, static_cast<int>(rampCount) - 1));
        const float scale = effect.speed * (0.25f + 1.5f * effect.spread);
        const auto add = [&](int angle, float speed, float life, float gravity, std::uint8_t size, std::uint8_t paletteRamp) {
            Particle spark{};
            spark.x = x;
            spark.y = y;
            spark.velocityX = Detail::cosine(angle) * speed + driftX;
            spark.velocityY = Detail::sine(angle) * speed;
            spark.lifetime = life;
            spark.gravity = gravity * effect.gravity;
            spark.ramp = paletteRamp;
            spark.size = size;
            return spawn(spark);
        };

        const int kind = random.Range(0, 4);
        if (kind <= 1) {
            for (std::uint32_t index = 0; index < sparks; ++index) {
                const auto size = static_cast<std::uint8_t>(random.Range(0, 4) == 0 ? 3 : random.Range(1, 2));
                if (!add(random.Range(0, 255), random.Range(110.0f, 360.0f) * scale, random.Range(1.1f, 1.9f), 190.0f, size, colorRamp)) return;
            }
        } else if (kind <= 3) {
            const auto innerRamp = static_cast<std::uint8_t>(random.Range(0, static_cast<int>(rampCount) - 1));
            const auto ring = std::max<std::uint32_t>(sparks * 2 / 3, 1u);
            for (std::uint32_t index = 0; index < ring; ++index) {
                const int angle = static_cast<int>(index * 256 / ring);
                const float speed = random.Range(300.0f, 320.0f) * scale;
                if (!add(angle, speed, random.Range(1.2f, 1.6f), 170.0f, 2, colorRamp)) return;
                if (index % 2 == 0 && !add(angle, speed * 0.5f, random.Range(1.0f, 1.4f), 170.0f, 1, innerRamp)) return;
            }
        } else {
            for (std::uint32_t index = 0; index < sparks; ++index) {
                if (!add(random.Range(0, 255), random.Range(50.0f, 240.0f) * scale, random.Range(2.0f, 3.2f), 110.0f, 2, colorRamp)) return;
            }
        }
    }

    void spawnStreamParticle() {
        Particle particle{};
        particle.ramp = static_cast<std::uint8_t>(random.Range(0, static_cast<int>(rampCount) - 1));
        particle.size = 1;
        const int cone = static_cast<int>(effect.spread * 128.0f);
        switch (effect.preset) {
        case Preset::Fountain: {
            particle.x = area.x + area.width * 0.5f;
            particle.y = area.y + area.height;
            const int angle = Detail::Upward + random.Range(-cone, cone);
            const float speed = 720.0f * effect.speed * random.Range(0.85f, 1.12f);
            particle.velocityX = Detail::cosine(angle) * speed;
            particle.velocityY = Detail::sine(angle) * speed;
            particle.gravity = 720.0f * effect.gravity;
            particle.lifetime = random.Range(1.2f, 1.9f);
            particle.size = static_cast<std::uint8_t>(random.Range(1, 2));
            break;
        }
        case Preset::Sparks: {
            particle.x = area.x + area.width * 0.5f;
            particle.y = area.y + area.height * 0.5f;
            const int angle = Detail::Upward + random.Range(-cone, cone);
            const float speed = 470.0f * effect.speed * random.Range(0.3f, 1.0f);
            particle.velocityX = Detail::cosine(angle) * speed;
            particle.velocityY = Detail::sine(angle) * speed;
            particle.gravity = 340.0f * effect.gravity;
            particle.lifetime = random.Range(0.32f, 0.8f);
            particle.size = static_cast<std::uint8_t>(random.Range(1, 2));
            break;
        }
        case Preset::Snow: {
            const float fall = fallSpeed(115.0f) * random.Range(0.85f, 1.15f);
            particle.x = area.x + area.width * random.Range(0.0f, 1.0f);
            particle.y = effect.gravity < 0.0f ? area.y + area.height : area.y;
            particle.velocityY = fall * fallDirection();
            particle.sway = 40.0f * effect.spread * random.Range(0.5f, 1.5f);
            particle.lifetime = (area.height / fall) * random.Range(0.92f, 1.08f);
            particle.size = static_cast<std::uint8_t>(random.Range(1, 2));
            break;
        }
        case Preset::Rain: {
            const float fall = fallSpeed(1100.0f) * random.Range(0.9f, 1.1f);
            particle.x = area.x + area.width * random.Range(0.0f, 1.0f);
            particle.y = effect.gravity < 0.0f ? area.y + area.height : area.y;
            particle.velocityX = fall * effect.spread * 0.45f;
            particle.velocityY = fall * fallDirection();
            particle.lifetime = (area.height / fall) * random.Range(0.92f, 1.05f);
            break;
        }
        case Preset::Fireworks:
            return;
        }
        spawn(particle);
    }

    bool spawn(const Particle& particle) {
        if (live == pool.size()) {
            ++dropped;
            return false;
        }
        pool[live++] = particle;
        return true;
    }

    void integrate() {
        const auto step = static_cast<float>(StepSeconds);
        std::array<Particle, PendingExplosions> explosions{};
        std::size_t explosionCount = 0;

        std::size_t write = 0;
        const bool fireworksLoop = effect.preset == Preset::Fireworks && effect.looping;
        const auto shellSparks = fireworksLoop ? loopShell() : wholeShell();
        for (std::size_t read = 0; read < live; ++read) {
            auto& particle = pool[read];
            if (particle.rocket) {
                particle.velocityY += Detail::RocketGravity * step;
                particle.x += particle.velocityX * step;
                particle.y += particle.velocityY * step;
                if (particle.velocityY >= Detail::RocketBurstSpeed) {
                    if (explosionCount < PendingExplosions) explosions[explosionCount++] = particle;
                    continue;
                }
                if (write != read) pool[write] = particle;
                ++write;
                continue;
            }

            particle.age += step;
            const float drag = effect.preset == Preset::Snow || effect.preset == Preset::Rain ? 1.0f : 0.99f;
            particle.velocityX *= drag;
            particle.velocityY = particle.velocityY * drag + particle.gravity * step;
            if (particle.sway != 0.0f) {
                const auto wobble = Detail::sine(static_cast<int>(particle.age * 90.0f) + static_cast<int>(particle.ramp));
                particle.x += wobble * particle.sway * step;
            }
            particle.x += particle.velocityX * step;
            particle.y += particle.velocityY * step;

            const bool below = particle.y > StageHeight + Detail::CullMargin;
            const bool above = particle.y < -Detail::CullMargin && particle.velocityY < 0.0f && particle.gravity <= 0.0f;
            const bool beside = particle.x < -Detail::CullMargin || particle.x > StageWidth + Detail::CullMargin;
            if (particle.age >= particle.lifetime || below || above || beside) continue;
            if (write != read) pool[write] = particle;
            ++write;
        }
        live = write;

        for (std::size_t index = 0; index < explosionCount; ++index) {
            const auto& rocket = explosions[index];
            shell(rocket.x, rocket.y, rocket.velocityX, shellSparks);
        }
    }

    // The stepped fade: a particle holds one flat color at a time, and shrinks in whole cells so
    // it never leaves the grid.
    void rebuildQuads() {
        quadCount = 0;
        const float cell = effect.pixelSize;
        for (std::size_t index = 0; index < live; ++index) {
            const auto& particle = pool[index];
            int size = particle.size;
            int heightCells = size;
            std::size_t colorStep = 0;
            std::size_t rampIndex = particle.ramp;

            if (particle.rocket) {
                heightCells = 2;
                rampIndex = trailRamp;
            } else {
                const float life = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
                if (effect.preset == Preset::Snow || effect.preset == Preset::Rain) {
                    colorStep = life < 0.75f ? 1 : life < 0.9f ? 2 : 4;
                } else {
                    colorStep = std::min(static_cast<std::size_t>(life * RampSteps), RampSteps - 1);
                    if (life >= 0.75f) size = 1;
                    else if (life >= 0.4f) size = std::max(size - 1, 1);
                    heightCells = size;
                }
                if (effect.preset == Preset::Rain) heightCells = 3;
            }

            const int column = Detail::floorToCell(particle.x, cell) - (size - 1) / 2;
            const int row = Detail::floorToCell(particle.y, cell) - (heightCells - 1) / 2;
            const float left = static_cast<float>(column) * cell;
            const float top = static_cast<float>(row) * cell;
            const float width = static_cast<float>(size) * cell;
            const float height = static_cast<float>(heightCells) * cell;
            if (left + width <= 0.0f || top + height <= 0.0f || left >= StageWidth || top >= StageHeight) continue;
            quads[quadCount++] = {left, top, width, height, ramps[rampIndex][colorStep]};
        }
    }

    Effect effect{};
    Area area{};
    Color base{};
    Color background{};
    std::span<Particle> pool{};
    std::span<Quad> quads{};
    std::array<std::array<Color, RampSteps>, MaximumRamps> ramps{};
    std::size_t rampCount = 1;
    std::size_t trailRamp = 1;
    Random random{};
    std::size_t live = 0;
    std::size_t quadCount = 0;
    std::size_t peak = 0;
    std::uint64_t steps = 0;
    std::uint64_t dropped = 0;
    double accumulator = 0.0;
    float spawnCarry = 0.0f;
    float launchCountdown = 0.0f;
    std::uint32_t pendingBurst = 0;
    bool pendingRocket = false;
    bool burstShell = false;
    bool visible = true;
};

}
