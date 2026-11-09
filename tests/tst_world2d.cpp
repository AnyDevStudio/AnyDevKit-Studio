// Host checks for the camera, sprite animation and tile culling in runtime/SceneGame. No Qt, SDK or
// GPU is involved, so this file can also run under sanitizers for a long time:
//
//   clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra \
//       -Wpedantic -Werror -Iruntime/SceneGame/include tests/tst_world2d.cpp -o tst_world2d
//   ./tst_world2d --soak 100000
#include "Scene/World2D.hpp.in"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {

std::size_t allocations = 0;
int failures = 0;
#define EXPECT(condition) do { if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (false)

using namespace World2D;

constexpr float WorldWide = 7680.0f;
constexpr float WorldTall = 2160.0f;

struct TileRecord {
    int column, row;
    TileQuad quad;
};

// The small reference version of culling: look at every cell.
std::size_t countVisibleByHand(const TilemapInfo& map, const std::vector<std::int32_t>& cells, Point origin, Point camera) {
    const float snapX = std::round(origin.x - camera.x);
    const float snapY = std::round(origin.y - camera.y);
    std::size_t visible = 0;
    for (std::uint32_t row = 0; row < map.rows; ++row) {
        for (std::uint32_t column = 0; column < map.columns; ++column) {
            if (cells[row * map.columns + column] < 0) continue;
            const float left = snapX + float(column * map.cellWidth);
            const float top = snapY + float(row * map.cellHeight);
            if (left < ScreenWidth && left + float(map.cellWidth) > 0.0f && top < ScreenHeight && top + float(map.cellHeight) > 0.0f) ++visible;
        }
    }
    return visible;
}

TilemapInfo smallMap() {
    return {4, 3, 16, 16, 64, 64, 4, 64, 64};
}

void cameraStaysInTheWorld() {
    EXPECT((ClampCamera({100.0f, 100.0f}, WorldWide, WorldTall).x == 100.0f));
    EXPECT((ClampCamera({-5.0f, -5.0f}, WorldWide, WorldTall).x == 0.0f));
    EXPECT((ClampCamera({-5.0f, -5.0f}, WorldWide, WorldTall).y == 0.0f));
    EXPECT((ClampCamera({9000.0f, 9000.0f}, WorldWide, WorldTall).x == WorldWide - ScreenWidth));
    EXPECT((ClampCamera({9000.0f, 9000.0f}, WorldWide, WorldTall).y == WorldTall - ScreenHeight));
    // The stage-sized world of an older scene never moves.
    EXPECT((ClampCamera({500.0f, 500.0f}, ScreenWidth, ScreenHeight).x == 0.0f));
    EXPECT((ClampCamera({500.0f, 500.0f}, ScreenWidth, ScreenHeight).y == 0.0f));
}

void followingCentersAnItem() {
    const auto camera = FollowCamera({1000.0f, 400.0f, 64.0f, 64.0f}, WorldWide, WorldTall);
    EXPECT(camera.x == 1000.0f + 32.0f - ScreenWidth / 2.0f);
    EXPECT(camera.y == 0.0f); // clamped: near the top of the world
    const auto corner = FollowCamera({WorldWide, WorldTall, 64.0f, 64.0f}, WorldWide, WorldTall);
    EXPECT(corner.x == WorldWide - ScreenWidth);
    EXPECT(corner.y == WorldTall - ScreenHeight);
}

void animationAdvancesAtItsSpeedAndLoops() {
    AnimationPlayer player;
    constexpr Clip clip{10, 4, 10}; // 4 frames at 10 per second: 0.1 seconds each
    player.Play(clip);
    EXPECT(player.Frame() == 10);
    player.Advance(0.05);
    EXPECT(player.Frame() == 10);
    player.Advance(0.05);
    EXPECT(player.Frame() == 11);
    player.Advance(0.2);
    EXPECT(player.Frame() == 13);
    player.Advance(0.1);
    EXPECT(player.Frame() == 10); // loops
    player.Advance(0.1);
    player.Play(clip); // playing the current clip again keeps its place
    EXPECT(player.Frame() == 11);
    player.Play({20, 2, 4});
    EXPECT(player.Frame() == 20); // a different one starts from its first frame
}

void animationIsSafeWithoutAClipAndWithHugeSteps() {
    AnimationPlayer player;
    EXPECT(player.Frame() == 0);
    player.Advance(1.0); // no clip yet
    EXPECT(player.Frame() == 0);
    player.Play({3, 1, 60});
    player.Advance(1000000.0);
    EXPECT(player.Frame() == 3);
    player.Play({0, 5, 30});
    player.Advance(1e15);
    EXPECT(player.Frame() < 5);
    player.Advance(-1.0); // ignored
    player.Advance(std::nan(""));
    EXPECT(player.Frame() < 5);
}

void frameCoordinatesFollowTheSheet() {
    constexpr auto uv = SheetFrameUv(5, 16, 16, 4, 64, 32, false);
    EXPECT(uv.u0 == 16.0f / 64.0f);
    EXPECT(uv.v0 == 16.0f / 32.0f);
    EXPECT(uv.u1 == 32.0f / 64.0f);
    EXPECT(uv.v1 == 32.0f / 32.0f);
    constexpr auto flipped = SheetFrameUv(5, 16, 16, 4, 64, 32, true);
    EXPECT(flipped.u0 == uv.u1);
    EXPECT(flipped.u1 == uv.u0);
    EXPECT(flipped.v0 == uv.v0);

    constexpr auto whole = FullImageUv(false);
    EXPECT(whole.u0 == 0.0f && whole.u1 == 1.0f && whole.v1 == 1.0f);
    EXPECT(FullImageUv(true).u0 == 1.0f);
}

void reservesWhatTheScreenCanShow() {
    EXPECT(ReservedTileQuads(500, 500, 64, 64) == 31u * 18u);
    EXPECT(ReservedTileQuads(10, 4, 64, 64) == 40u);
    EXPECT(ReservedTileQuads(500, 500, 16, 16) == 121u * 69u); // 120 columns fit exactly, 67.5 rows do not
}

void emptyAndFarAwayMapsDrawNothing() {
    const auto map = smallMap();
    const std::vector<std::int32_t> empty(12, -1);
    EXPECT(EmitVisibleTiles(map, empty.data(), {0.0f, 0.0f}, {0.0f, 0.0f}, 1000, [](const TileQuad&) {}) == 0);

    const std::vector<std::int32_t> full(12, 0);
    EXPECT(EmitVisibleTiles(map, full.data(), {5000.0f, 0.0f}, {0.0f, 0.0f}, 1000, [](const TileQuad&) {}) == 0);
    EXPECT(EmitVisibleTiles(map, full.data(), {0.0f, 0.0f}, {5000.0f, 0.0f}, 1000, [](const TileQuad&) {}) == 0);
    EXPECT(EmitVisibleTiles(map, full.data(), {-5000.0f, 0.0f}, {0.0f, 0.0f}, 1000, [](const TileQuad&) {}) == 0);
    EXPECT(EmitVisibleTiles(map, full.data(), {0.0f, -5000.0f}, {0.0f, 0.0f}, 1000, [](const TileQuad&) {}) == 0);
}

void tilesAreCulledToTheScreen() {
    TilemapInfo map{100, 100, 16, 16, 64, 64, 4, 64, 64};
    std::vector<std::int32_t> cells(10000, 2);
    std::vector<TileQuad> quads;

    // Aligned to the grid: exactly the 30 by 17 cells on screen.
    auto count = EmitVisibleTiles(map, cells.data(), {0.0f, 0.0f}, {0.0f, 0.0f}, 1 << 16, [&](const TileQuad& quad) { quads.push_back(quad); });
    EXPECT(count == 30u * 17u);
    EXPECT(quads.size() == count);
    EXPECT(quads[0].x == 0.0f && quads[0].y == 0.0f && quads[0].width == 64.0f && quads[0].height == 64.0f);
    EXPECT(quads[0].uv.u0 == 32.0f / 64.0f && quads[0].uv.v0 == 0.0f);

    // Between grid lines: one more column and row, and each tile's edge meets the next tile's edge.
    quads.clear();
    count = EmitVisibleTiles(map, cells.data(), {0.0f, 0.0f}, {32.0f, 32.0f}, 1 << 16, [&](const TileQuad& quad) { quads.push_back(quad); });
    EXPECT(count == 31u * 18u);
    EXPECT(quads[0].x == -32.0f && quads[0].y == -32.0f);
    EXPECT(quads[1].x == quads[0].x + quads[0].width); // whole-unit, so no seams appear between tiles

    // A fractional camera snaps to a whole unit.
    quads.clear();
    EmitVisibleTiles(map, cells.data(), {0.0f, 0.0f}, {64.4f, 0.4f}, 1 << 16, [&](const TileQuad& quad) { quads.push_back(quad); });
    EXPECT(quads[0].x == 0.0f); // the -64 column is fully off screen, so the first drawn tile starts at 0
    EXPECT(quads[0].x == std::floor(quads[0].x));
    EXPECT(std::floor(quads[0].y) == quads[0].y);
}

void capacityIsNeverExceeded() {
    TilemapInfo map{100, 100, 16, 16, 64, 64, 4, 64, 64};
    std::vector<std::int32_t> cells(10000, 0);
    std::size_t written = 0;
    const auto count = EmitVisibleTiles(map, cells.data(), {0.0f, 0.0f}, {0.0f, 0.0f}, 12, [&](const TileQuad&) { ++written; });
    EXPECT(count == 12);
    EXPECT(written == 12);
}

void skipsTilesTheTilesetDoesNotHave() {
    TilemapInfo map = smallMap();
    std::vector<std::int32_t> cells(12, 0);
    cells[1] = 1000;
    std::size_t written = 0;
    EmitVisibleTiles(map, cells.data(), {0.0f, 0.0f}, {0.0f, 0.0f}, 100, [&](const TileQuad& quad) {
        ++written;
        EXPECT(quad.uv.u0 >= 0.0f && quad.uv.u1 <= 1.0f);
    });
    EXPECT(written == 11);
}

void matchesTheReferenceEverywhereWithoutAllocating(int rounds, bool quiet) {
    std::srand(7);
    TilemapInfo map{200, 150, 16, 16, 64, 64, 8, 128, 128};
    std::vector<std::int32_t> cells(200u * 150u, -1);
    for (auto& cell : cells) cell = (std::rand() % 10 == 0) ? -1 : std::rand() % 64;

    for (int round = 0; round < rounds; ++round) {
        const Point camera{static_cast<float>(std::rand() % 20000 - 5000) + static_cast<float>(std::rand() % 100) / 100.0f, static_cast<float>(std::rand() % 12000 - 3000)};
        const Point origin{static_cast<float>(std::rand() % 2000 - 1000), static_cast<float>(std::rand() % 2000 - 1000)};
        const auto reserved = ReservedTileQuads(map.columns, map.rows, map.cellWidth, map.cellHeight);

        const auto before = allocations;
        std::size_t onScreen = 0;
        const auto count = EmitVisibleTiles(map, cells.data(), origin, camera, reserved, [&](const TileQuad& quad) {
            onScreen += quad.x < ScreenWidth && quad.x + quad.width > 0.0f && quad.y < ScreenHeight && quad.y + quad.height > 0.0f ? 1 : 0;
        });
        EXPECT(allocations == before); // no allocation while drawing
        EXPECT(count == onScreen);    // nothing off-screen was emitted
        EXPECT(count <= reserved);
        EXPECT(count == countVisibleByHand(map, cells, origin, camera));
    }
    if (!quiet) std::printf("checked %d cameras\n", rounds);
}

void animationAndCameraDoNotAllocate() {
    AnimationPlayer player;
    const auto before = allocations;
    Point camera{0.0f, 0.0f};
    for (int frame = 0; frame < 2000; ++frame) {
        player.Play({0, 4, 12});
        player.Advance(1.0 / 60.0);
        camera = FollowCamera({float(frame), 0.0f, 16.0f, 16.0f}, WorldWide, WorldTall);
    }
    EXPECT(allocations == before);
    EXPECT(camera.x >= 0.0f);
}

}

void* operator new(std::size_t size) {
    ++allocations;
    if (void* memory = std::malloc(size)) return memory;
    throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

int main(int argc, char** argv) {
    cameraStaysInTheWorld();
    followingCentersAnItem();
    animationAdvancesAtItsSpeedAndLoops();
    animationIsSafeWithoutAClipAndWithHugeSteps();
    frameCoordinatesFollowTheSheet();
    reservesWhatTheScreenCanShow();
    emptyAndFarAwayMapsDrawNothing();
    tilesAreCulledToTheScreen();
    capacityIsNeverExceeded();
    skipsTilesTheTilesetDoesNotHave();
    animationAndCameraDoNotAllocate();
    int rounds = 200;
    if (argc == 3 && std::strcmp(argv[1], "--soak") == 0) rounds = std::atoi(argv[2]);
    matchesTheReferenceEverywhereWithoutAllocating(rounds, argc != 3);
    if (failures != 0) {
        std::printf("%d checks failed\n", failures);
        return 1;
    }
    std::printf("all world checks passed\n");
    return 0;
}
