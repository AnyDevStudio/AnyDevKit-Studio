// Host checks for the 2D platformer physics in runtime/SceneGame. No Qt, SDK or GPU is involved, so this
// file also runs under sanitizers for a long time:
//
//   clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra \
//       -Wpedantic -Werror -Iruntime/SceneGame/include tests/tst_physics2d.cpp -o tst_physics2d
//   ./tst_physics2d --soak 2000000
#include "Scene/Physics2D.hpp.in"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <span>
#include <string>
#include <vector>

// Every heap allocation in the test program goes through here, so a test can prove a step makes none.
static std::size_t allocations = 0;
void* operator new(std::size_t size) {
    ++allocations;
    if (void* memory = std::malloc(size)) return memory;
    std::abort();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

using namespace Physics2D;

int failures = 0;
#define EXPECT(condition) do { if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (false)
#define EXPECT_EQ(actual, expected) do { const auto a_ = (actual); const auto e_ = (expected); if (a_ != e_) { ++failures; std::printf("FAIL %s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #actual, static_cast<long long>(a_), static_cast<long long>(e_)); } } while (false)

constexpr std::int32_t Cell = 64;
constexpr std::uint16_t SolidTile = 1;
constexpr std::int32_t EmptyTile = -1;

std::int64_t U(std::int64_t units) { return units * Scale; }

// A map from rows of text: '#' is a solid tile, anything else empty. Built once and read by the layer.
struct Map {
    std::vector<std::int32_t> cells;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::array<std::uint16_t, 1> solid{SolidTile};

    explicit Map(std::initializer_list<const char*> lines) {
        for (const char* line : lines) {
            const auto width = static_cast<std::uint32_t>(std::strlen(line));
            if (columns == 0) columns = width;
            for (std::uint32_t column = 0; column < width; ++column) cells.push_back(line[column] == '#' ? SolidTile : EmptyTile);
            ++rows;
        }
    }

    TileLayer Layer(std::int32_t cell = Cell, std::int64_t originX = 0, std::int64_t originY = 0) const {
        return {cells.data(), columns, rows, cell, cell, originX, originY, solid.data(), 1};
    }
};

struct Sim {
    std::vector<Body> bodies;
    std::vector<Aabb> solids;
    std::vector<TileLayer> layers;
    std::int64_t width = U(100000);
    std::int64_t height = U(100000);

    WorldView View() { return {bodies, solids, layers, width, height}; }
    void Run(int steps, Input input = {}) {
        for (int step = 0; step < steps; ++step) Step(View(), input);
    }
};

BodyConfig Platformer() { return {100, true, 600, 260}; }
BodyConfig Crate() { return {100, false, 0, 0}; }

Body Make(const BodyConfig& config, std::int64_t x, std::int64_t y, std::int64_t width, std::int64_t height) {
    return MakeBody(config, U(x), U(y), U(width), U(height));
}

bool Overlaps(const Body& body, const Aabb& box) {
    return body.x < box.right && box.left < body.x + body.width && body.y < box.bottom && box.top < body.y + body.height;
}

// No solid tile of any layer overlaps the body's rectangle. Edges that only meet do not count.
bool OverlapsSolidTile(const Body& body, std::span<const TileLayer> layers) {
    for (const auto& layer : layers) {
        for (std::uint32_t row = 0; row < layer.rows; ++row) {
            for (std::uint32_t column = 0; column < layer.columns; ++column) {
                const auto tile = layer.cells[row * layer.columns + column];
                if (tile < 0 || !IsSolidTile(layer, static_cast<std::uint32_t>(tile))) continue;
                const Aabb box{layer.originX + U(column) * layer.cellWidth, layer.originY + U(row) * layer.cellHeight, layer.originX + U(column + 1) * layer.cellWidth, layer.originY + U(row + 1) * layer.cellHeight};
                if (Overlaps(body, box)) return true;
            }
        }
    }
    return false;
}

bool OverlapsAnySolid(const Sim& sim, const Body& body) {
    if (OverlapsSolidTile(body, sim.layers)) return true;
    for (const auto& solid : sim.solids) {
        if (Overlaps(body, solid)) return true;
    }
    return false;
}

// Falling onto tiles lands exactly on their top edge and stops there.
void landsExactlyOnATileAndReportsItOnce() {
    const Map map{"...", "...", "...", "###"};
    Sim sim;
    sim.layers = {map.Layer()};
    sim.bodies = {Make(Crate(), 50, 10, 32, 32)};
    sim.Run(1);
    EXPECT(!sim.bodies[0].landed);
    int landings = 0;
    for (int step = 0; step < 600; ++step) {
        sim.bodies[0].landed = false;
        sim.Run(1);
        if (sim.bodies[0].landed) ++landings;
    }
    EXPECT_EQ(sim.bodies[0].y, U(192 - 32));
    EXPECT_EQ(sim.bodies[0].velocityY, 0);
    EXPECT(sim.bodies[0].grounded);
    EXPECT_EQ(landings, 1);
}

void standingStillStaysStillWithNoNewLanding() {
    const Map map{"...", "###"};
    Sim sim;
    sim.layers = {map.Layer()};
    sim.bodies = {Make(Crate(), 50, 32, 32, 32)};
    for (int step = 0; step < 2400; ++step) {
        sim.bodies[0].landed = false;
        sim.bodies[0].hitSolid = false;
        sim.Run(1);
        if (step > 1) {
            EXPECT(!sim.bodies[0].landed);
            EXPECT(!sim.bodies[0].hitSolid);
        }
    }
    EXPECT_EQ(sim.bodies[0].y, U(32));
}

void solidItemsBlockLikeTiles() {
    Sim sim;
    sim.solids = {{U(0), U(300), U(800), U(340)}, {U(500), U(200), U(540), U(300)}};
    sim.bodies = {Make(Platformer(), 100, 200, 40, 40)};
    sim.Run(300);
    EXPECT_EQ(sim.bodies[0].y, U(260));
    EXPECT(sim.bodies[0].grounded);
    sim.Run(400, {1000});
    EXPECT_EQ(sim.bodies[0].x, U(460));
    EXPECT_EQ(sim.bodies[0].velocityX, 0);
    EXPECT(sim.bodies[0].hitSolid);
    EXPECT(sim.bodies[0].touchingRight);
}

// A body far above a one-tile floor moving at the speed limit lands on it, at any frame rate.
void aVeryFastBodyDoesNotTunnelThroughOneTile() {
    const Map map{".", ".", ".", "#", ".", ".", "."};
    for (int frameDivisor : {1, 4, 13, 30}) {
        Sim sim;
        sim.layers = {map.Layer()};
        auto body = Make(Crate(), 0, 0, 32, 32);
        SetVelocity(body, 0, 100000);
        sim.bodies = {body};
        sim.Run(frameDivisor * 8);
        EXPECT_EQ(sim.bodies[0].y, U(192 - 32));
        EXPECT(sim.bodies[0].grounded);
    }
}

void aVeryFastBodyStopsAtAThinWallSideways() {
    const Map map{"..#...", "..#...", "..#..."};
    Sim sim;
    sim.layers = {map.Layer()};
    sim.bodies = {Make(Crate(), 0, 0, 32, 32)};
    SetVelocity(sim.bodies[0], 100000, 0);
    sim.bodies[0].config.gravityPercent = 0;
    sim.bodies[0].gravityStep = 0;
    sim.Run(40);
    EXPECT_EQ(sim.bodies[0].x, U(128 - 32));
    EXPECT_EQ(sim.bodies[0].velocityX, 0);
}

void cellsFourUnitsWideStillBlockATerminalSpeedBody() {
    const Map map{"....#..."};
    Sim sim;
    sim.layers = {map.Layer(4)};
    sim.bodies = {Make(Crate(), 0, 0, 2, 2)};
    sim.bodies[0].config.gravityPercent = 0;
    sim.bodies[0].gravityStep = 0;
    SetVelocity(sim.bodies[0], 100000, 0);
    sim.Run(80);
    EXPECT_EQ(sim.bodies[0].x, U(16 - 2));
}

void thinSolidItemsDoNotLetABodyThroughAtSpeed() {
    Sim sim;
    sim.solids = {{U(0), U(1000), U(64), U(1001)}};
    sim.bodies = {Make(Crate(), 0, 0, 32, 32)};
    SetVelocity(sim.bodies[0], 0, 100000);
    sim.Run(600);
    EXPECT_EQ(sim.bodies[0].y, U(1000 - 32));
}

// A jump at 2 FPS, so dozens of steps run per displayed frame, still lands where a smooth one does.
void lowFrameRatesTakeTheSameFixedSteps() {
    const Map map{"................", "................", "................", "................", "################"};
    const auto simulate = [&](int stepsPerFrame) {
        Sim sim;
        sim.layers = {map.Layer()};
        sim.bodies = {Make(Platformer(), 100, 100, 40, 40)};
        for (int frame = 0; frame < 4000 / stepsPerFrame; ++frame) {
            for (int step = 0; step < stepsPerFrame; ++step) Step(sim.View(), {});
        }
        return sim.bodies[0].y;
    };
    EXPECT_EQ(simulate(1), simulate(30));
    EXPECT_EQ(simulate(1), U(4 * Cell - 40));
}

void theClockGivesFixedStepsAndCapsLongFrames() {
    FixedClock clock;
    EXPECT_EQ(clock.StepsFor(1.0 / 60.0), 2);
    EXPECT_EQ(clock.StepsFor(1.0 / 120.0), 1);
    EXPECT_EQ(clock.StepsFor(1.0 / 240.0), 0);
    EXPECT_EQ(clock.StepsFor(1.0 / 240.0), 1);
    EXPECT_EQ(clock.StepsFor(10.0), MaximumStepsPerFrame);
    clock = {};
    int total = 0;
    for (int frame = 0; frame < 3000; ++frame) total += clock.StepsFor(1.0 / 144.0);
    EXPECT(total >= 2498 && total <= 2500);
}

void aBodyExactlyOneTileWideFallsIntoAOneTileGap() {
    const Map map{"......", "......", "##.###", "##.###", "##.###", "......", "######"};
    Sim sim;
    sim.layers = {map.Layer()};
    auto body = Make(Crate(), 2 * Cell, 0, Cell, Cell);
    sim.bodies = {body};
    sim.Run(1200);
    EXPECT_EQ(sim.bodies[0].x, U(2 * Cell));
    EXPECT_EQ(sim.bodies[0].y, U(5 * Cell));
    EXPECT(sim.bodies[0].grounded);
    EXPECT(!OverlapsSolidTile(sim.bodies[0], sim.layers));
}

void aBodyExactlyOneTileHighWalksAOneTileTunnel() {
    const Map map{"########", "........", "########"};
    Sim sim;
    sim.layers = {map.Layer()};
    sim.bodies = {Make(Platformer(), 0, Cell, Cell, Cell)};
    sim.Run(60);
    EXPECT_EQ(sim.bodies[0].y, U(Cell));
    sim.Run(70, {1000});
    EXPECT_EQ(sim.bodies[0].y, U(Cell));
    EXPECT(sim.bodies[0].x > U(3 * Cell));
    EXPECT(sim.bodies[0].grounded);
    EXPECT(!OverlapsSolidTile(sim.bodies[0], sim.layers));
}

void aNarrowBodyWalksOverAOneTileGap() {
    const Map map{"..........", "..........", "##.#######"};
    Sim sim;
    sim.layers = {map.Layer()};
    auto body = Make(Platformer(), 0, 64, 100, 40);
    sim.bodies = {body};
    sim.Run(60);
    EXPECT_EQ(sim.bodies[0].y, U(128 - 40));
    sim.Run(70, {1000});
    EXPECT(sim.bodies[0].x > U(200));
    EXPECT_EQ(sim.bodies[0].y, U(128 - 40));
}

void aBodyAgainstAWallKeepsFallingAndDoesNotStick() {
    const Map map{"..#", "..#", "..#", "..#", "..#", "###"};
    Sim sim;
    sim.layers = {map.Layer()};
    sim.bodies = {Make(Platformer(), 40, 0, 64, 64)};
    sim.bodies[0].x = U(2 * Cell - 64) - 1;
    sim.Run(2, {1000});
    int steps = 0;
    while (!sim.bodies[0].grounded && steps++ < 900) sim.Run(1, {1000});
    EXPECT(steps < 900);
    EXPECT_EQ(sim.bodies[0].y, U(5 * Cell - 64));
    EXPECT_EQ(sim.bodies[0].x, U(2 * Cell - 64));
}

void aBodySlidesPastAnExactTileCornerWithoutSnagging() {
    // Falling with the body's right edge exactly on the corner of a tile: edges only meet, nothing blocks.
    const Map map{"..", "..", "#.", "..", "##"};
    Sim sim;
    sim.layers = {map.Layer()};
    sim.bodies = {Make(Crate(), Cell, 0, Cell, Cell)};
    sim.Run(1200);
    EXPECT_EQ(sim.bodies[0].y, U(3 * Cell));
    EXPECT(!sim.bodies[0].touchingLeft);
    EXPECT(!OverlapsSolidTile(sim.bodies[0], sim.layers));
}

void landingOnAStaircaseCornerStaysOnTop() {
    const Map map{
        ".......",
        ".......",
        "......#",
        ".....##",
        "....###",
        "#######",
    };
    Sim sim;
    sim.layers = {map.Layer()};
    sim.bodies = {Make(Platformer(), 4 * Cell + 8, 0, 48, 48)};
    sim.Run(800);
    EXPECT_EQ(sim.bodies[0].y, U(4 * Cell - 48));
    EXPECT(sim.bodies[0].grounded);
}

void stairsBlockWalkingAndAreClimbedByJumpingEachStep() {
    const Map map{
        "..............",
        "..........####",
        "........######",
        "......########",
        "....##########",
        "##############",
    };
    Sim sim;
    sim.layers = {map.Layer()};
    sim.bodies = {Make(Platformer(), 0, 4 * Cell - 48, 48, 48)};
    sim.Run(120);
    EXPECT_EQ(sim.bodies[0].y, U(5 * Cell - 48));
    // Walking into the first step stops the body: stairs are walls until jumped.
    sim.Run(600, {1000});
    EXPECT_EQ(sim.bodies[0].x, U(4 * Cell - 48));
    EXPECT_EQ(sim.bodies[0].y, U(5 * Cell - 48));
    // Each jump clears one step, and the body never overlaps the stairs on the way up.
    for (int stepNumber = 1; stepNumber <= 4; ++stepNumber) {
        const auto previousY = sim.bodies[0].y;
        sim.Run(100, {1000}); // up against the next riser
        Step(sim.View(), {1000, true, true});
        for (int step = 0; step < 28; ++step) {
            Step(sim.View(), {1000, true, false});
            EXPECT(!OverlapsSolidTile(sim.bodies[0], sim.layers));
        }
        for (int step = 0; step < 240 && !sim.bodies[0].grounded; ++step) {
            Step(sim.View(), {0, true, false});
            EXPECT(!OverlapsSolidTile(sim.bodies[0], sim.layers));
        }
        EXPECT(sim.bodies[0].grounded);
        EXPECT_EQ(sim.bodies[0].y, previousY - U(Cell));
    }
    EXPECT_EQ(sim.bodies[0].y, U(Cell * 1 - 48));
}

Sim GroundWithHero(std::int64_t heroX = 100) {
    static const Map map{"....................", "....................", "....................", "....................", "####################"};
    Sim sim;
    sim.layers = {map.Layer()};
    sim.bodies = {Make(Platformer(), heroX, 4 * Cell - 40, 40, 40)};
    sim.Run(30);
    return sim;
}

void walkingReachesTheConfiguredSpeedAndStopsQuickly() {
    auto sim = GroundWithHero();
    const auto before = sim.bodies[0].x;
    sim.Run(120, {1000});
    const auto travelled = sim.bodies[0].x - before;
    // Nearly 600 units in one second, allowing for the start-up from standing.
    EXPECT(travelled > U(560) && travelled <= U(600));
    EXPECT_EQ(sim.bodies[0].velocityX, ToStep(600));
    sim.Run(30);
    EXPECT_EQ(sim.bodies[0].velocityX, 0);
}

void aHalfTiltOfTheStickWalksAtHalfSpeed() {
    auto sim = GroundWithHero();
    sim.Run(200, {500});
    EXPECT_EQ(sim.bodies[0].velocityX, ToStep(300));
}

void aFullHeldJumpReachesTheConfiguredHeight() {
    auto sim = GroundWithHero();
    const auto startY = sim.bodies[0].y;
    Step(sim.View(), {0, true, true});
    std::int64_t top = sim.bodies[0].y;
    for (int step = 0; step < 300 && !(sim.bodies[0].grounded && step > 5); ++step) {
        Step(sim.View(), {0, true, false});
        top = std::min(top, sim.bodies[0].y);
    }
    const auto height = static_cast<double>(startY - top) / Scale;
    EXPECT(height >= 260.0 * 0.97 && height <= 260.0 * 1.03);
}

void aShortTapJumpsMuchLowerThanAFullJump() {
    auto sim = GroundWithHero();
    const auto startY = sim.bodies[0].y;
    Step(sim.View(), {0, true, true});
    sim.Run(6, {0, true});
    std::int64_t top = sim.bodies[0].y;
    for (int step = 0; step < 300 && !(sim.bodies[0].grounded && step > 2); ++step) {
        Step(sim.View(), {0, false, false});
        top = std::min(top, sim.bodies[0].y);
    }
    const auto height = static_cast<double>(startY - top) / Scale;
    EXPECT(height >= 40.0 && height <= 130.0);
}

Sim LedgeRunner() {
    static const Map map{".....................", ".....................", "#####................", "#####................"};
    Sim sim;
    sim.layers = {map.Layer()};
    sim.bodies = {Make(Platformer(), 5 * Cell - 80, 2 * Cell - 40, 40, 40)};
    sim.Run(20);
    return sim;
}

void coyoteTimeAllowsAJumpJustAfterLeavingALedge() {
    auto sim = LedgeRunner();
    EXPECT(sim.bodies[0].grounded);
    int walked = 0;
    while (sim.bodies[0].grounded && walked++ < 300) Step(sim.View(), {1000});
    EXPECT(!sim.bodies[0].grounded);
    sim.Run(CoyoteSteps / 2, {1000});
    EXPECT(!sim.bodies[0].grounded);
    Step(sim.View(), {1000, true, true});
    EXPECT(sim.bodies[0].velocityY < 0);
}

void afterTheCoyoteWindowAJumpIsTooLate() {
    auto sim = LedgeRunner();
    int walked = 0;
    while (sim.bodies[0].grounded && walked++ < 300) Step(sim.View(), {1000});
    sim.Run(CoyoteSteps + 2, {1000});
    const auto velocityBefore = sim.bodies[0].velocityY;
    Step(sim.View(), {1000, true, true});
    EXPECT(sim.bodies[0].velocityY >= velocityBefore);
}

void aJumpPressedJustBeforeLandingHappensOnLanding() {
    auto sim = GroundWithHero();
    Step(sim.View(), {0, true, true});
    // Rising, then falling, until only a few steps from the ground, then press again.
    for (int step = 0; step < 240 && sim.bodies[0].velocityY <= 0; ++step) Step(sim.View(), {0, true, false});
    const auto floorY = U(4 * Cell - 40);
    for (int step = 0; step < 240 && floorY - sim.bodies[0].y > U(30); ++step) Step(sim.View(), {});
    Step(sim.View(), {0, true, true});
    bool jumpedAgain = false;
    for (int step = 0; step < JumpBufferSteps + 10; ++step) {
        Step(sim.View(), {0, true, false});
        if (sim.bodies[0].velocityY < 0) jumpedAgain = true;
    }
    EXPECT(jumpedAgain);
}

void aJumpPressedTooEarlyIsForgotten() {
    auto sim = GroundWithHero();
    Step(sim.View(), {0, true, true});
    for (int step = 0; step < 240 && sim.bodies[0].velocityY <= 0; ++step) Step(sim.View(), {0, true, false});
    Step(sim.View(), {0, true, true}); // far above the ground: too early to be remembered until landing
    int airborne = 0;
    while (!sim.bodies[0].grounded && airborne++ < 400) Step(sim.View(), {0, true, false});
    EXPECT(airborne < 400);
    EXPECT(sim.bodies[0].velocityY >= 0);
    sim.Run(4);
    EXPECT(sim.bodies[0].velocityY >= 0);
}

void theJumpBlockDoesWhatTheButtonDoes() {
    auto sim = GroundWithHero();
    Jump(sim.bodies[0]);
    Step(sim.View(), {});
    EXPECT(sim.bodies[0].velocityY < 0);
    EXPECT(sim.bodies[0].jumping);
}

void theJumpBlockDoesNothingInTheAir() {
    auto sim = GroundWithHero();
    Step(sim.View(), {0, true, true});
    sim.Run(30, {0, true});
    const auto velocity = sim.bodies[0].velocityY;
    Jump(sim.bodies[0]);
    Step(sim.View(), {});
    EXPECT(sim.bodies[0].velocityY >= velocity);
}

void setVelocityIsUnitsPerSecond() {
    Sim sim;
    sim.bodies = {Make(Crate(), 0, 1000, 10, 10)};
    sim.bodies[0].config.gravityPercent = 0;
    sim.bodies[0].gravityStep = 0;
    SetVelocity(sim.bodies[0], 120, -60);
    sim.Run(120);
    EXPECT_EQ(sim.bodies[0].x, U(120));
    EXPECT_EQ(sim.bodies[0].y, U(940));
}

void gravityScaleChangesHowFastBodiesFall() {
    const auto fallenAfterSteps = [](std::int32_t percent) {
        Sim sim;
        BodyConfig config{percent, false, 0, 0};
        sim.bodies = {Make(config, 0, 0, 10, 10)};
        sim.Run(60);
        return sim.bodies[0].y;
    };
    EXPECT_EQ(fallenAfterSteps(0), 0);
    const auto normal = fallenAfterSteps(100);
    const auto heavy = fallenAfterSteps(200);
    EXPECT(normal > U(100));
    EXPECT(heavy >= normal * 2 - U(5) && heavy <= normal * 2 + U(5));
}

void terminalSpeedStopsAccelerationFromBecomingUnbounded() {
    Sim sim;
    sim.bodies = {Make(Crate(), 0, 0, 10, 10)};
    sim.Run(120 * 60);
    EXPECT(sim.bodies[0].velocityY <= MaximumStep);
}

// Hit reports the moment of impact, once per contact, not on every step of pressing against it.
void hittingAWallReportsOnceWhilePressing() {
    const Map map{".....#", ".....#", "#####."};
    Sim sim;
    sim.layers = {map.Layer()};
    sim.bodies = {Make(Platformer(), 100, 88, 40, 40)};
    sim.Run(20);
    int hits = 0;
    for (int step = 0; step < 360; ++step) {
        sim.bodies[0].hitSolid = false;
        Step(sim.View(), {1000});
        if (sim.bodies[0].hitSolid) ++hits;
    }
    EXPECT_EQ(sim.bodies[0].x, U(5 * Cell - 40));
    EXPECT_EQ(hits, 1);
    // Letting go and pushing again is a new hit.
    sim.Run(20);
    sim.bodies[0].hitSolid = false;
    sim.Run(30, {1000});
    EXPECT(sim.bodies[0].hitSolid);
}

void hittingACeilingStopsRisingAndReports() {
    const Map map{"####", "....", "....", "####"};
    Sim sim;
    sim.layers = {map.Layer()};
    sim.bodies = {Make(Platformer(), 10, 2 * Cell, 40, 40)};
    sim.Run(30);
    sim.bodies[0].hitSolid = false;
    Step(sim.View(), {0, true, true});
    bool touchedTop = false;
    for (int step = 0; step < 120; ++step) {
        Step(sim.View(), {0, true, false});
        touchedTop = touchedTop || sim.bodies[0].touchingTop;
    }
    EXPECT(sim.bodies[0].hitSolid);
    EXPECT(touchedTop);
    EXPECT(!OverlapsSolidTile(sim.bodies[0], sim.layers));
}

void fallingOutOfTheBottomOfTheWorldIsReportedAndTheBodyStops() {
    Sim sim;
    sim.height = U(1080);
    sim.width = U(1920);
    sim.bodies = {Make(Crate(), 100, 1000, 40, 40)};
    int reports = 0;
    for (int step = 0; step < 600; ++step) {
        sim.bodies[0].leftWorld = false;
        sim.Run(1);
        if (sim.bodies[0].leftWorld) ++reports;
    }
    EXPECT_EQ(reports, 1);
    EXPECT(sim.bodies[0].outside);
    EXPECT(sim.bodies[0].velocityY == 0);
    EXPECT(sim.bodies[0].y >= U(1080));
    EXPECT(sim.bodies[0].y <= U(1080) + MaximumStep);
}

void comingBackInsideTheWorldReadiesItToLeaveAgain() {
    Sim sim;
    sim.height = U(1080);
    sim.width = U(1920);
    sim.bodies = {Make(Crate(), 100, 1000, 40, 40)};
    sim.Run(300);
    EXPECT(sim.bodies[0].leftWorld);
    sim.bodies[0].leftWorld = false;
    Teleport(sim.bodies[0], U(100), U(10));
    sim.Run(1);
    EXPECT(!sim.bodies[0].outside);
    sim.Run(600);
    EXPECT(sim.bodies[0].leftWorld);
}

void aPlatformerCharacterTreatsTheWorldSidesAsWalls() {
    Sim sim;
    sim.width = U(1920);
    sim.height = U(1080);
    sim.solids = {{U(0), U(500), U(1920), U(540)}};
    sim.bodies = {Make(Platformer(), 1800, 460, 40, 40)};
    sim.Run(30);
    sim.Run(200, {1000});
    EXPECT_EQ(sim.bodies[0].x, U(1880));
    EXPECT(!sim.bodies[0].leftWorld);
    sim.Run(800, {-1000});
    EXPECT_EQ(sim.bodies[0].x, 0);
    EXPECT(!sim.bodies[0].leftWorld);
}

void aPlainBodyLeavesThroughTheSide() {
    Sim sim;
    sim.width = U(1920);
    sim.height = U(1080);
    sim.bodies = {Make(Crate(), 1880, 100, 40, 40)};
    sim.bodies[0].config.gravityPercent = 0;
    sim.bodies[0].gravityStep = 0;
    SetVelocity(sim.bodies[0], 600, 0);
    sim.Run(60);
    EXPECT(sim.bodies[0].leftWorld);
    EXPECT(sim.bodies[0].outside);
}

void aBodyStartingInsideASolidIsPushedOut() {
    const Map map{"...", "###", "###"};
    Sim sim;
    sim.layers = {map.Layer()};
    sim.bodies = {Make(Crate(), 80, 60, 32, 32)};
    sim.Run(10);
    EXPECT(!OverlapsSolidTile(sim.bodies[0], sim.layers));
    EXPECT_EQ(sim.bodies[0].y, U(Cell - 32));
}

void aSolidItemMovedOntoABodyPushesItOut() {
    Sim sim;
    sim.solids = {{U(0), U(500), U(800), U(540)}};
    sim.bodies = {Make(Crate(), 100, 460, 40, 40)};
    sim.Run(30);
    sim.solids[0] = {U(0), U(490), U(800), U(530)};
    sim.Run(2);
    EXPECT_EQ(sim.bodies[0].y, U(450));
    EXPECT(!Overlaps(sim.bodies[0], sim.solids[0]));
}

void aHugeBodyInsideManySmallTilesStillFinishes() {
    // Big body over 4-unit tiles: a bounded amount of work with no crash.
    std::vector<std::int32_t> cells(512 * 512, 0);
    std::array<std::uint16_t, 1> solid{0};
    const TileLayer layer{cells.data(), 512, 512, 4, 4, 0, 0, solid.data(), 1};
    Sim sim;
    sim.layers = {layer};
    sim.bodies = {Make(Platformer(), 256, 256, 2048, 2048)};
    sim.Run(50, {1000});
    EXPECT(true);
}

// Nothing allocates, however long the simulation runs.
void aHundredThousandStepsMakeNoAllocations() {
    static const Map map{"..........", "..........", "...#......", "###.######"};
    Sim sim;
    sim.layers = {map.Layer()};
    sim.solids = {{U(0), U(4000), U(100000), U(4040)}};
    sim.bodies = {Make(Platformer(), 0, 0, 40, 40), Make(Crate(), 200, 0, 30, 30)};
    const auto before = allocations;
    for (int step = 0; step < 100000; ++step) {
        Input input{step % 700 < 350 ? 1000 : -1000, step % 40 < 20, step % 40 == 0};
        Step(sim.View(), input);
    }
    EXPECT_EQ(allocations - before, 0u);
}

// The same inputs always give the same results, step for step: no floats, nothing but the inputs.
void identicalInputsGiveIdenticalTrajectories() {
    const auto run = [] {
        static const Map map{
            "..........................",
            "..#.......##.......#......",
            "...........#..............",
            "......##......##......##..",
            "######..######..######..##",
        };
        Sim sim;
        sim.layers = {map.Layer()};
        sim.bodies = {Make(Platformer(), 0, 0, 44, 44), Make(Crate(), 300, 0, 36, 36)};
        std::uint64_t digest = 14695981039346656037ull;
        const auto mix = [&digest](std::int64_t value) {
            for (int byte = 0; byte < 8; ++byte) digest = (digest ^ ((value >> (byte * 8)) & 0xff)) * 1099511628211ull;
        };
        std::mt19937 random(12345);
        for (int step = 0; step < 20000; ++step) {
            const Input input{static_cast<std::int32_t>(random() % 2001) - 1000, random() % 2 == 0, random() % 8 == 0};
            Step(sim.View(), input);
            for (const auto& body : sim.bodies) {
                mix(body.x);
                mix(body.y);
                mix(body.velocityX);
                mix(body.velocityY);
            }
        }
        return digest;
    };
    EXPECT_EQ(run(), run());
}

// Long randomized runs: no overlap with any solid after any step, positions stay in range, no state that
// sanitizers object to, on random maps and huge random speeds. --soak 2000000 runs this for a long time.
void soakOnRandomWorlds(int rounds, int stepsPerRound, bool quiet) {
    std::mt19937 random(0xC0FFEE);
    const auto roll = [&random](int lowest, int highest) { return lowest + static_cast<int>(random() % static_cast<std::uint32_t>(highest - lowest + 1)); };
    for (int round = 0; round < rounds; ++round) {
        const int columns = roll(4, 40);
        const int rows = roll(4, 24);
        const int cell = std::array{4, 8, 16, 24, 32, 64, 96, 128}[static_cast<std::size_t>(roll(0, 7))];
        const int layout = roll(0, 2);
        std::vector<std::int32_t> cells(static_cast<std::size_t>(columns * rows));
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                bool wanted;
                if (layout == 0) wanted = roll(0, 9) == 0;
                else if (layout == 1) wanted = row == rows - 1 || (roll(0, 99) < 8 && column % 3 != 0);
                else wanted = row == rows - 1 || row > rows - 2 - (column / 3); // stairs
                cells[static_cast<std::size_t>(row * columns + column)] = wanted ? 1 : (roll(0, 1) == 0 ? -1 : 2);
            }
        }
        std::array<std::uint16_t, 1> solid{1};
        const auto originX = U(roll(-300, 300));
        const auto originY = U(roll(-300, 300));
        const TileLayer layer{cells.data(), static_cast<std::uint32_t>(columns), static_cast<std::uint32_t>(rows), cell, cell, originX, originY, solid.data(), 1};

        Sim sim;
        sim.layers = {layer};
        sim.width = U(2000 + roll(0, 6000));
        sim.height = U(1200 + roll(0, 6000));
        const int solidItems = roll(0, 8);
        for (int index = 0; index < solidItems; ++index) {
            const auto left = U(roll(0, 5000));
            const auto top = U(roll(0, 3000));
            sim.solids.push_back({left, top, left + U(roll(1, 300)), top + U(roll(1, 200))});
        }

        const int bodyCount = roll(1, 3);
        for (int index = 0; index < bodyCount; ++index) {
            const BodyConfig config{roll(0, 300), roll(0, 1) == 0, roll(50, 1500), roll(20, 500)};
            auto body = MakeBody(config, U(roll(0, 1900)), U(roll(0, 900)), U(roll(1, 200)), U(roll(1, 200)));
            // Only bodies placed clear of every solid count here; a start inside a solid is a separate test.
            for (int attempt = 0; attempt < 30 && OverlapsAnySolid(sim, body); ++attempt) {
                body.x = U(roll(0, 1900));
                body.y = U(roll(0, 900));
            }
            if (!OverlapsAnySolid(sim, body)) sim.bodies.push_back(body);
        }

        std::uint32_t fingerprintBad = 0;
        for (int step = 0; step < stepsPerRound; ++step) {
            for (auto& body : sim.bodies) {
                if (roll(0, 400) == 0) SetVelocity(body, roll(-30000, 30000), roll(-30000, 30000));
                if (roll(0, 400) == 0) Jump(body);
            }
            const Input input{roll(-1000, 1000), roll(0, 1) == 0, roll(0, 9) == 0};
            Step(sim.View(), input);
            for (auto& body : sim.bodies) {
                if (body.outside) {
                    // Bring it back somewhere clear, so a long run keeps colliding instead of idling outside.
                    auto x = U(roll(0, 1900));
                    auto y = U(roll(0, 900));
                    for (int attempt = 0; attempt < 30; ++attempt) {
                        Body probe = body;
                        Teleport(probe, x, y);
                        if (!OverlapsAnySolid(sim, probe)) {
                            Teleport(body, x, y);
                            break;
                        }
                        x = U(roll(0, 1900));
                        y = U(roll(0, 900));
                    }
                    body.leftWorld = false;
                    continue;
                }
                if (OverlapsAnySolid(sim, body)) ++fingerprintBad;
                EXPECT(body.velocityX >= -MaximumStep && body.velocityX <= MaximumStep);
                EXPECT(body.velocityY >= -MaximumStep && body.velocityY <= MaximumStep);
                EXPECT(body.x >= -U(200000) && body.x <= sim.width + U(200000));
                EXPECT(body.y >= -U(200000) && body.y <= sim.height + U(200000));
            }
        }
        EXPECT_EQ(fingerprintBad, 0u);
        if (failures != 0) break;
    }
    if (!quiet) std::printf("soak finished: %d rounds of %d steps\n", rounds, stepsPerRound);
}

// Whatever speeds and jumps are asked for, a body shut in by a wall and a floor stays on its own side of
// them: an overlap check alone cannot see a body that went straight through in one step.
void soakNeverCrossesAWallOrFloor(int rounds, int stepsPerRound) {
    std::mt19937 random(0xBADC0DE);
    const auto roll = [&random](int lowest, int highest) { return lowest + static_cast<int>(random() % static_cast<std::uint32_t>(highest - lowest + 1)); };
    constexpr int WorldWide = 1280;
    constexpr int WorldTall = 640;
    for (int round = 0; round < rounds; ++round) {
        const int cell = std::array{4, 8, 16, 32, 64}[static_cast<std::size_t>(roll(0, 4))];
        const int columns = WorldWide / cell;
        const int rows = 2 * WorldTall / cell; // the map reaches a whole world above the top, as high as a body can go
        const int wallColumn = roll(columns / 3, 2 * columns / 3);
        std::vector<std::int32_t> cells(static_cast<std::size_t>(columns * rows), EmptyTile);
        for (int row = 0; row < rows; ++row) cells[static_cast<std::size_t>(row * columns + wallColumn)] = SolidTile;
        for (int column = 0; column < columns; ++column) cells[static_cast<std::size_t>((rows - 1) * columns + column)] = SolidTile;
        std::array<std::uint16_t, 1> solid{SolidTile};

        Sim sim;
        sim.width = U(WorldWide);
        sim.height = U(WorldTall);
        const auto wallLeft = U(wallColumn * cell);
        const auto floorTop = U(WorldTall - cell);
        const bool useItems = roll(0, 1) == 0;
        if (useItems) {
            sim.solids = {{wallLeft, -U(WorldTall), wallLeft + U(1), U(WorldTall)}, {0, floorTop, U(WorldWide), floorTop + U(1)}};
        } else {
            sim.layers = {TileLayer{cells.data(), static_cast<std::uint32_t>(columns), static_cast<std::uint32_t>(rows), cell, cell, 0, -U(WorldTall), solid.data(), 1}};
        }
        const BodyConfig config{roll(0, 300), true, roll(60, 3000), roll(16, 2000)};
        const auto size = roll(1, std::min(100, wallColumn * cell - 1));
        sim.bodies = {MakeBody(config, U(roll(0, wallColumn * cell - size)), U(roll(0, WorldTall - cell - size)), U(size), U(size))};

        for (int step = 0; step < stepsPerRound; ++step) {
            auto& body = sim.bodies[0];
            if (roll(0, 40) == 0) SetVelocity(body, roll(-30000, 30000), roll(-30000, 30000));
            if (roll(0, 40) == 0) Jump(body);
            Step(sim.View(), {roll(-1000, 1000), roll(0, 1) == 0, roll(0, 5) == 0});
            if (body.x + body.width > wallLeft || body.y + body.height > floorTop || body.outside) {
                ++failures;
                std::printf("FAIL round %d step %d: cell %d body at %lld,%lld size %d went past the wall or floor\n", round, step, cell, static_cast<long long>(body.x), static_cast<long long>(body.y), size);
                return;
            }
        }
    }
    std::printf("wall soak finished: %d rounds of %d steps\n", rounds, stepsPerRound);
}

}

int main(int argc, char** argv) {
    landsExactlyOnATileAndReportsItOnce();
    standingStillStaysStillWithNoNewLanding();
    solidItemsBlockLikeTiles();
    aVeryFastBodyDoesNotTunnelThroughOneTile();
    aVeryFastBodyStopsAtAThinWallSideways();
    cellsFourUnitsWideStillBlockATerminalSpeedBody();
    thinSolidItemsDoNotLetABodyThroughAtSpeed();
    lowFrameRatesTakeTheSameFixedSteps();
    theClockGivesFixedStepsAndCapsLongFrames();
    aBodyExactlyOneTileWideFallsIntoAOneTileGap();
    aBodyExactlyOneTileHighWalksAOneTileTunnel();
    aNarrowBodyWalksOverAOneTileGap();
    aBodyAgainstAWallKeepsFallingAndDoesNotStick();
    aBodySlidesPastAnExactTileCornerWithoutSnagging();
    landingOnAStaircaseCornerStaysOnTop();
    stairsBlockWalkingAndAreClimbedByJumpingEachStep();
    walkingReachesTheConfiguredSpeedAndStopsQuickly();
    aHalfTiltOfTheStickWalksAtHalfSpeed();
    aFullHeldJumpReachesTheConfiguredHeight();
    aShortTapJumpsMuchLowerThanAFullJump();
    coyoteTimeAllowsAJumpJustAfterLeavingALedge();
    afterTheCoyoteWindowAJumpIsTooLate();
    aJumpPressedJustBeforeLandingHappensOnLanding();
    aJumpPressedTooEarlyIsForgotten();
    theJumpBlockDoesWhatTheButtonDoes();
    theJumpBlockDoesNothingInTheAir();
    setVelocityIsUnitsPerSecond();
    gravityScaleChangesHowFastBodiesFall();
    terminalSpeedStopsAccelerationFromBecomingUnbounded();
    hittingAWallReportsOnceWhilePressing();
    hittingACeilingStopsRisingAndReports();
    fallingOutOfTheBottomOfTheWorldIsReportedAndTheBodyStops();
    comingBackInsideTheWorldReadiesItToLeaveAgain();
    aPlatformerCharacterTreatsTheWorldSidesAsWalls();
    aPlainBodyLeavesThroughTheSide();
    aBodyStartingInsideASolidIsPushedOut();
    aSolidItemMovedOntoABodyPushesItOut();
    aHugeBodyInsideManySmallTilesStillFinishes();
    aHundredThousandStepsMakeNoAllocations();
    identicalInputsGiveIdenticalTrajectories();

    int rounds = 120;
    int steps = 12000;
    if (argc == 3 && std::strcmp(argv[1], "--soak") == 0) {
        steps = std::atoi(argv[2]);
        rounds = 80;
    }
    soakOnRandomWorlds(rounds, steps, false);
    soakNeverCrossesAWallOrFloor(rounds, steps);
    if (failures != 0) {
        std::printf("%d checks failed\n", failures);
        return 1;
    }
    std::printf("all physics checks passed\n");
    return 0;
}
