#include "Graphics/Renderer.hpp"
#include "Graphics/Font.hpp"
#include "Graphics/Textures.hpp"
#include "Scene/SceneState.hpp"
#include "Scene/World2D.hpp"
#include "Error.hpp"
#include "GeneratedScene.hpp"
#include "ScreenVertex.hpp"
#include "ScreenFragment.hpp"
#include "ScreenImageFragment.hpp"
#include "prx/libkernel/Time/include/Time.hpp"
#include "prx/libkernel/include/Api.hpp"
#include "prx/libSceAgc/libSceAgc.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include <atomic>
#include <cstring>

namespace {

constexpr float GlyphMode = 1.0f;
constexpr float SolidMode = 2.0f;
constexpr float TexturedMode = 3.0f;
// Every quad is always in the draw, so a hidden item's quads are given no area and rasterize to nothing.
constexpr std::array<float, 4> HiddenRectangle{0.0f, 0.0f, 0.0f, 0.0f};

// CB_BLEND0_CONTROL: output = source + destination * (1 - source alpha), for color and alpha alike,
// because the image pixels already have alpha applied. Bit 30 turns the blender on.
constexpr std::uint32_t PremultipliedBlendControl = 1u | (5u << 8) | (1u << 16) | (5u << 24) | (1u << 30);
constexpr std::uint32_t BlendControlRegister = 0x1e0;

// One draw call: a stretch of quads that share a shader and a texture. Solid quads are grouped so a
// scene without images is still one draw. Consecutive items that use the same image with the same
// sampling share one draw, so tile layers on one tileset and the sprites over them cost one bind.
struct Run {
    std::size_t first;
    std::size_t count;
    std::int32_t image;
    bool nearest;
    bool upright = false; // one standing item, drawn in depth order rather than list order
};

constexpr bool isTextured(const GeneratedScene::Item& item) {
    return item.kind == GeneratedScene::Kind::Image || item.kind == GeneratedScene::Kind::Tilemap;
}

constexpr bool usesNearest(const GeneratedScene::Item& item) {
    return item.kind == GeneratedScene::Kind::Tilemap || item.pixelated;
}

// A particles item reserves one quad per particle it can ever show, so the budget is fixed before
// the first frame.
constexpr std::size_t itemFrames(const GeneratedScene::Item& item, bool tilted) {
    if (tilted && item.kind == GeneratedScene::Kind::Image && !item.screenFixed && !item.upright) return World2D::TiltStrips;
    if (item.kind == GeneratedScene::Kind::Text) return item.text.size();
    if (item.kind == GeneratedScene::Kind::Particles) return GeneratedScene::ParticleEffects[static_cast<std::size_t>(item.particles)].amount;
    if (item.kind == GeneratedScene::Kind::Tilemap) return GeneratedScene::Tilemaps[static_cast<std::size_t>(item.tilemap)].reservedQuads;
    return 1;
}

// Quads are laid out scene after scene: each scene starts with its own background quad. Only the showing
// scene's quads and runs are read each frame, and each scene's static colors and glyphs are set up once.
consteval std::size_t countFrames() {
    std::size_t count = 0;
    for (const auto& scene : GeneratedScene::Scenes) {
        count += 1;
        for (std::size_t index = scene.firstItem; index < scene.firstItem + scene.itemCount; ++index) count += itemFrames(GeneratedScene::Items[index], scene.tilt > 0.0f);
    }
    return count;
}

constexpr std::size_t FrameCount = countFrames();

consteval std::size_t countRuns() {
    std::size_t runs = 0;
    for (const auto& scene : GeneratedScene::Scenes) {
        ++runs; // the scene's background
        std::int32_t image = -1;
        bool nearest = false;
        bool afterUpright = false;
        for (std::size_t index = scene.firstItem; index < scene.firstItem + scene.itemCount; ++index) {
            const auto& item = GeneratedScene::Items[index];
            const std::int32_t wanted = isTextured(item) ? item.image : -1;
            const bool wantedNearest = isTextured(item) && usesNearest(item);
            if (wanted != image || wantedNearest != nearest || item.upright || afterUpright) {
                ++runs;
                image = wanted;
                nearest = wantedNearest;
            }
            afterUpright = item.upright;
        }
    }
    return runs;
}

constexpr std::size_t RunCount = countRuns();

consteval std::size_t countMostUpright() {
    std::size_t most = 1;
    for (const auto& scene : GeneratedScene::Scenes) {
        std::size_t here = 0;
        for (std::size_t index = scene.firstItem; index < scene.firstItem + scene.itemCount; ++index) here += GeneratedScene::Items[index].upright ? 1 : 0;
        most = std::max(most, here);
    }
    return most;
}

constexpr std::size_t MostUpright = countMostUpright();

struct SceneLayout {
    std::size_t firstFrame = 0;
    std::size_t firstRun = 0;
    std::size_t runCount = 0;
};

struct Layout {
    std::array<Run, RunCount> runs{};
    std::array<std::size_t, GeneratedScene::Items.size()> itemRuns{};
    std::array<SceneLayout, GeneratedScene::SceneCount> scenes{};
};

consteval Layout buildLayout() {
    Layout layout{};
    std::size_t frame = 0;
    std::size_t run = 0;
    for (std::size_t sceneIndex = 0; sceneIndex < GeneratedScene::SceneCount; ++sceneIndex) {
        const auto& scene = GeneratedScene::Scenes[sceneIndex];
        auto& sceneLayout = layout.scenes[sceneIndex];
        sceneLayout.firstFrame = frame;
        sceneLayout.firstRun = run;
        Run open{frame, 1, -1, false};
        frame += 1;
        for (std::size_t index = scene.firstItem; index < scene.firstItem + scene.itemCount; ++index) {
            const auto& item = GeneratedScene::Items[index];
            const std::int32_t wanted = isTextured(item) ? item.image : -1;
            const bool wantedNearest = isTextured(item) && usesNearest(item);
            const std::size_t frames = itemFrames(item, scene.tilt > 0.0f);
            if (wanted != open.image || wantedNearest != open.nearest || item.upright || open.upright) {
                layout.runs[run++] = open;
                open = {frame, 0, wanted, wantedNearest, item.upright};
            }
            layout.itemRuns[index] = run;
            open.count += frames;
            frame += frames;
        }
        layout.runs[run++] = open;
        sceneLayout.runCount = run - sceneLayout.firstRun;
    }
    return layout;
}

constexpr Layout SceneLayouts = buildLayout();
constexpr const auto& Runs = SceneLayouts.runs;
constexpr const auto& ItemRuns = SceneLayouts.itemRuns;

// Worst case per draw: both shaders' user data (16 registers each), a pipeline switch and the draw.
constexpr std::size_t CommandWords = 4096 + RunCount * 200;

// The size the game draws at, also what culling and whole-pixel snapping work against.
constexpr int FrameWidth = 1920;
constexpr int FrameHeight = 1080;

bool APS5_VABI exhausted(CommandBuffer*, std::uint32_t, void*) { throw Error{"AGC command buffer exhausted"}; }
void bindShader(CommandBuffer& buffer, std::uint32_t base, const std::array<std::uint32_t, 16>& values, std::uint32_t count, int slot, const void* descriptors) {
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto value = static_cast<int>(index) == slot ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(descriptors)) : values[index];
        sceAgcDcbSetShRegisterDirect(&buffer, {base + index, value});
    }
}

}

// Walks the same allocations, in the same order, as the constructor and Pipeline.
std::size_t Renderer::requiredMemory() {
    std::size_t used = 0;
    const auto reserve = [&used](std::size_t bytes, std::size_t alignment) { used = ((used + alignment - 1) & ~(alignment - 1)) + bytes; };

    for (std::size_t slot = 0; slot < std::tuple_size_v<decltype(slots)>; ++slot) {
        reserve(CommandWords * sizeof(std::uint32_t), 256);
        reserve(sizeof(std::uint64_t), 64);
        reserve(sizeof(Frame) * FrameCount, alignof(Frame));
        reserve(16 * RunCount, 16);
        reserve(Pipeline::ContextCapacity * sizeof(ShaderRegister), 256);
        if (GeneratedScene::ImageCount != 0) reserve(Pipeline::ContextCapacity * sizeof(ShaderRegister), 256);
    }

    for (const auto& image : GeneratedScene::Images) reserve(Textures::SurfaceBytes(image.width, image.height), Textures::BaseAlignment);
    if (GeneratedScene::ImageCount != 0) reserve(Textures::TableBytes * Textures::TablesPerImage * GeneratedScene::ImageCount, 16);

    reserve(sizeof(AdkShaders::ScreenVertex::Header), 8);
    reserve(sizeof(AdkShaders::ScreenVertex::Code), 256);
    reserve(sizeof(AdkShaders::ScreenFragment::Header), 8);
    reserve(sizeof(AdkShaders::ScreenFragment::Code), 256);
    reserve(Pipeline::ContextCapacity * sizeof(ShaderRegister), 256);
    reserve(3 * sizeof(ShaderRegister), 256);
    if (GeneratedScene::ImageCount != 0) {
        reserve(sizeof(AdkShaders::ScreenImageFragment::Header), 8);
        reserve(sizeof(AdkShaders::ScreenImageFragment::Code), 256);
        reserve(Pipeline::ContextCapacity * sizeof(ShaderRegister), 256);
        reserve(3 * sizeof(ShaderRegister), 256);
    }
    return (used + Memory::Alignment - 1) & ~(Memory::Alignment - 1);
}

Renderer::Renderer(int width, int height) : memory(requiredMemory(), 0x240000000ull) {
    const auto address = reinterpret_cast<std::uintptr_t>(memory.Data());
    if (address >> 32 != 2 || (address + memory.Size() - 1) >> 32 != 2) throw Error{"GPU memory is outside shader aperture"};
    Check(sceAgcInit(11), "sceAgcInit");
    static_assert(sizeof(Frame) == 64);
    static_assert(AdkShaders::ScreenVertex::PushConstantSize == 0);
    static_assert(AdkShaders::ScreenFragment::PushConstantSize == 0);
    static_assert(AdkShaders::ScreenVertex::DescriptorSetSlots[0] >= 0);
    static_assert(AdkShaders::ScreenImageFragment::PushConstantSize == 0);
    static_assert(AdkShaders::ScreenImageFragment::DescriptorSetSlots[0] >= 0);
    const auto stageWidth = static_cast<float>(width);
    const auto stageHeight = static_cast<float>(height);

    for (auto& slot : slots) {
        commands = static_cast<std::uint32_t*>(memory.Allocate(CommandWords * sizeof(std::uint32_t), 256));
        fence = static_cast<std::uint64_t*>(memory.Allocate(sizeof(std::uint64_t), 64));
        std::construct_at(fence, 0);
        frames = static_cast<Frame*>(memory.Allocate(sizeof(Frame) * FrameCount, alignof(Frame)));

        for (std::size_t index = 0; index < FrameCount; ++index) {
            std::construct_at(frames + index);
            frames[index].viewport = {stageWidth, stageHeight, SolidMode, 0};
        }

        for (std::size_t sceneIndex = 0; sceneIndex < GeneratedScene::SceneCount; ++sceneIndex) {
            const auto& scene = GeneratedScene::Scenes[sceneIndex];
            const auto& background = scene.background;
            const auto backgroundFrame = SceneLayouts.scenes[sceneIndex].firstFrame;
            frames[backgroundFrame].rectangle = {0, 0, stageWidth, stageHeight};
            frames[backgroundFrame].color = {background[0], background[1], background[2], 1};
            std::size_t next = backgroundFrame + 1;

            for (std::size_t itemIndex = scene.firstItem; itemIndex < scene.firstItem + scene.itemCount; ++itemIndex) {
                const auto& item = GeneratedScene::Items[itemIndex];
                const std::array color{item.red, item.green, item.blue, 1.0f};
                if (item.kind != GeneratedScene::Kind::Text) {
                    // The same count the layout reserved, or every glyph after this item lands in the wrong quad.
                    const auto reserved = itemFrames(item, scene.tilt > 0.0f);
                    for (std::size_t quad = 0; quad < reserved; ++quad) frames[next++].color = color;
                    continue;
                }

                for (const char character : item.text) {
                    frames[next].color = color;
                    frames[next].viewport[2] = GlyphMode;
                    frames[next++].glyph = EncodeGlyph(character);
                }
            }
        }

        for (const auto& run : Runs) {
            if (run.image < 0) continue;
            for (std::size_t index = 0; index < run.count; ++index) frames[run.first + index].viewport[2] = TexturedMode;
        }

        // Each draw reads its quads through its own buffer descriptor, because gl_VertexIndex restarts at 0.
        descriptor = static_cast<std::uint32_t*>(memory.Allocate(16 * RunCount, 16));
        for (std::size_t run = 0; run < RunCount; ++run) {
            const auto frameAddress = reinterpret_cast<std::uintptr_t>(frames + Runs[run].first);
            auto* words = descriptor + run * 4;
            words[0] = static_cast<std::uint32_t>(frameAddress);
            words[1] = static_cast<std::uint32_t>(frameAddress >> 32);
            words[2] = static_cast<std::uint32_t>(sizeof(Frame) * Runs[run].count);
            words[3] = 0x31016fac;
        }
        contexts = static_cast<ShaderRegister*>(memory.Allocate(Pipeline::ContextCapacity * sizeof(ShaderRegister), 256));
        imageContexts = GeneratedScene::ImageCount != 0 ? static_cast<ShaderRegister*>(memory.Allocate(Pipeline::ContextCapacity * sizeof(ShaderRegister), 256)) : nullptr;
        const auto end = imageContexts != nullptr ? imageContexts + Pipeline::ContextCapacity : contexts + Pipeline::ContextCapacity;
        const auto bytes = reinterpret_cast<std::byte*>(end) - reinterpret_cast<std::byte*>(commands);
        slot = {commands, fence, frames, descriptor, contexts, imageContexts, static_cast<std::size_t>(bytes), false};
    }

    uploadImages();

    vertex = createShader(AdkShaders::ScreenVertex::Header, AdkShaders::ScreenVertex::Code);
    fragment = createShader(AdkShaders::ScreenFragment::Header, AdkShaders::ScreenFragment::Code);
    pipeline = std::make_unique<Pipeline>(memory, width, height, vertex, fragment);
    if (GeneratedScene::ImageCount != 0) {
        imageFragment = createShader(AdkShaders::ScreenImageFragment::Header, AdkShaders::ScreenImageFragment::Code);
        imagePipeline = std::make_unique<Pipeline>(memory, width, height, vertex, imageFragment);
    }
}

// Copies each image into GPU memory as linear rows and writes its S#, T# table for ScreenImage.frag.
void Renderer::uploadImages() {
    if (GeneratedScene::ImageCount == 0) return;
    static_assert(Textures::TextureDescriptor(0x240001000ull, 1024, 512)[0] == 0x02400010u);
    static_assert(Textures::TextureDescriptor(0x240001000ull, 1024, 512)[1] == ((56u << 20) | (3u << 30)));
    static_assert(Textures::TextureDescriptor(0x240001000ull, 1024, 512)[2] == (0xffu | (511u << 14) | (1u << 31)));
    static_assert(Textures::PitchBytes(1024) == 4096 && Textures::PitchBytes(1) == 256 && Textures::PitchBytes(65) == 512);

    std::array<std::byte*, GeneratedScene::ImageCount> surfaces{};
    for (std::size_t index = 0; index < GeneratedScene::ImageCount; ++index) {
        const auto& image = GeneratedScene::Images[index];
        auto* surface = static_cast<std::byte*>(memory.Allocate(Textures::SurfaceBytes(image.width, image.height), Textures::BaseAlignment));
        const auto pitch = Textures::PitchBytes(image.width);
        const auto rowBytes = static_cast<std::size_t>(image.width) * Textures::BytesPerTexel;
        for (std::uint32_t row = 0; row < image.height; ++row) {
            std::memcpy(surface + row * pitch, image.pixels + row * rowBytes, rowBytes);
        }
        surfaces[index] = surface;
    }

    imageTables = static_cast<std::uint32_t*>(memory.Allocate(Textures::TableBytes * Textures::TablesPerImage * GeneratedScene::ImageCount, 16));
    for (std::size_t index = 0; index < GeneratedScene::ImageCount; ++index) {
        const auto& image = GeneratedScene::Images[index];
        const auto texture = Textures::TextureDescriptor(reinterpret_cast<std::uintptr_t>(surfaces[index]), image.width, image.height);
        // The bilinear table comes first and the nearest one right after it, see imageTable().
        for (std::size_t nearest = 0; nearest < Textures::TablesPerImage; ++nearest) {
            auto* table = imageTables + (index * Textures::TablesPerImage + nearest) * Textures::TableBytes / sizeof(std::uint32_t);
            const auto sampler = nearest != 0 ? Textures::NearestClampSampler() : Textures::BilinearClampSampler();
            std::memcpy(table, sampler.data(), sizeof(sampler));
            std::memcpy(table + 4, texture.data(), sizeof(texture));
        }
    }
}

Renderer::~Renderer() noexcept(false) { for (auto& slot : slots) waitSlot(slot); }
void Renderer::waitSlot(DrawSlot& slot) {
    if (!slot.Pending) return;
    const auto start = sceKernelGetProcessTime();
    std::atomic_ref completion(*slot.Fence);

    while (completion.load(std::memory_order_acquire) != 1) {
        if (sceKernelGetProcessTime() - start > 5000000) throw Error{"GPU rendering timed out"};
        Check(sceKernelUsleep_nid_postfix(100), "sceKernelUsleep");
    }

    slot.Pending = false;
}

void Renderer::selectSlot(std::size_t index) {
    auto& slot = slots.at(index);
    waitSlot(slot);
    commands = slot.Commands;
    fence = slot.Fence;
    frames = slot.Frames;
    descriptor = slot.Descriptor;
    contexts = slot.Contexts;
    imageContexts = slot.ImageContexts;
}

Shader* Renderer::createShader(std::span<const std::uint8_t> header, std::span<const std::uint8_t> code) {
    void* headerMemory = memory.Allocate(header.size(), 8);
    void* codeMemory = memory.Allocate(code.size(), 256);
    std::memcpy(headerMemory, header.data(), header.size());
    std::memcpy(codeMemory, code.data(), code.size());
    Shader* shader = nullptr;
    Check(sceAgcCreateShader(&shader, headerMemory, codeMemory), "sceAgcCreateShader");
    if (!shader || !shader->cx_registers || !shader->sh_registers) throw Error{"Incomplete shader"};
    return shader;
}

void Renderer::Draw(Display& display, const SceneState& scene) {
    selectSlot(slotIndex);
    const auto sceneIndex = scene.CurrentScene();
    const auto& sceneInfo = GeneratedScene::Scenes[sceneIndex];
    const auto& sceneLayout = SceneLayouts.scenes[sceneIndex];
    std::size_t next = sceneLayout.firstFrame + 1;
    const auto& currentPositions = scene.Current();
    const auto& currentColors = scene.CurrentColors();
    const auto& currentVisibility = scene.CurrentVisibility();
    const auto camera = scene.Camera();
    // Quads written so far into each textured run. A run's quads are not all used every frame:
    // off-screen tiles and sprites never reach it, and an empty run makes no draw call.
    std::array<std::size_t, RunCount> runUsed{};
    const float screenWidth = static_cast<float>(FrameWidth);
    const float screenHeight = static_cast<float>(FrameHeight);
    const bool tilted = sceneInfo.tilt > 0.0f;
    const auto tilt = World2D::MakeTiltView(tilted ? sceneInfo.tilt : 1.0f, camera);
    struct Standing {
        float foot;
        std::size_t run;
    };
    std::array<Standing, MostUpright> standing{};
    std::size_t standingCount = 0;

    for (std::size_t itemIndex = sceneInfo.firstItem; itemIndex < sceneInfo.firstItem + sceneInfo.itemCount; ++itemIndex) {
        const auto& item = GeneratedScene::Items[itemIndex];
        const auto [x, y] = currentPositions[itemIndex];
        const auto [red, green, blue] = currentColors[itemIndex];
        const std::array color{red, green, blue, 1.0f};
        const bool visible = currentVisibility[itemIndex];
        // Items that stay on the screen do not move with the camera.
        const float offsetX = item.screenFixed ? 0.0f : camera.x;
        const float offsetY = item.screenFixed ? 0.0f : camera.y;

        if (isTextured(item)) {
            const std::size_t runIndex = ItemRuns[itemIndex];
            const auto& run = Runs[runIndex];
            if (item.kind == GeneratedScene::Kind::Tilemap) {
                const auto mapIndex = static_cast<std::size_t>(item.tilemap);
                const auto& map = GeneratedScene::Tilemaps[mapIndex];
                const auto& image = GeneratedScene::Images[static_cast<std::size_t>(map.image)];
                if (visible) {
                    const World2D::TilemapInfo info{map.columns, map.rows, map.tileWidth, map.tileHeight, map.cellWidth, map.cellHeight, map.tilesetColumns, image.width, image.height};
                    auto* out = frames + run.first + runUsed[runIndex];
                    runUsed[runIndex] += World2D::EmitVisibleTiles(info, GeneratedScene::TilemapCells[mapIndex], {x, y}, {offsetX, offsetY}, map.reservedQuads, [&](const World2D::TileQuad& quad) {
                        if (tilted && !item.screenFixed) {
                            const auto corner = World2D::TiltProject(tilt, quad.x + offsetX, quad.y + offsetY);
                            const auto scale = World2D::TiltScale(tilt, quad.y + offsetY + quad.height / 2.0f);
                            out->rectangle = {corner.x, corner.y, quad.width * scale, quad.height * scale * tilt.squash};
                        } else {
                            out->rectangle = {quad.x, quad.y, quad.width, quad.height};
                        }
                        out->color = {quad.uv.u0, quad.uv.v0, quad.uv.u1, quad.uv.v1};
                        ++out;
                    });
                }
                next += map.reservedQuads;
                continue;
            }

            if (tilted && !item.screenFixed) {
                const auto& image = GeneratedScene::Images[static_cast<std::size_t>(item.image)];
                const bool flipped = scene.Flipped(itemIndex);
                const World2D::UvRect uv = item.sprite >= 0
                    ? World2D::SheetFrameUv(scene.SpriteFrame(itemIndex), GeneratedScene::Sprites[static_cast<std::size_t>(item.sprite)].frameWidth, GeneratedScene::Sprites[static_cast<std::size_t>(item.sprite)].frameHeight, GeneratedScene::Sprites[static_cast<std::size_t>(item.sprite)].columns, image.width, image.height, flipped)
                    : World2D::FullImageUv(flipped);
                if (item.upright) {
                    const float footY = y + item.height;
                    const auto box = World2D::TiltUpright(tilt, x + item.width / 2.0f, footY, item.drawWidth > 0.0f ? item.drawWidth : item.width, item.drawHeight > 0.0f ? item.drawHeight : item.height);
                    if (visible && box.x < screenWidth && box.y < screenHeight && box.x + box.width > 0.0f && box.y + box.height > 0.0f) {
                        auto& quad = frames[run.first + runUsed[runIndex]++];
                        quad.rectangle = {box.x, box.y, box.width, box.height};
                        quad.color = {uv.u0, uv.v0, uv.u1, uv.v1};
                        standing[standingCount++] = {footY, runIndex};
                    }
                    next += 1;
                } else {
                    if (visible) {
                        auto* out = frames + run.first + runUsed[runIndex];
                        runUsed[runIndex] += World2D::EmitTiltedGround(tilt, x, y, item.width, item.height, [&](const World2D::GroundStrip& strip) {
                            out->rectangle = {strip.x, strip.y, strip.width, strip.height};
                            out->color = {uv.u0, uv.v0 + (uv.v1 - uv.v0) * strip.v0, uv.u1, uv.v0 + (uv.v1 - uv.v0) * strip.v1};
                            ++out;
                        });
                    }
                    next += World2D::TiltStrips;
                }
                continue;
            }

            float left = x - offsetX;
            float top = y - offsetY;
            if (item.pixelated) { // pixel art lands on whole screen pixels, so it never shimmers
                left = World2D::RoundToUnit(left);
                top = World2D::RoundToUnit(top);
            }
            if (visible && left < screenWidth && top < screenHeight && left + item.width > 0.0f && top + item.height > 0.0f) {
                const auto& image = GeneratedScene::Images[static_cast<std::size_t>(item.image)];
                const bool flipped = scene.Flipped(itemIndex);
                const World2D::UvRect uv = item.sprite >= 0
                    ? World2D::SheetFrameUv(scene.SpriteFrame(itemIndex), GeneratedScene::Sprites[static_cast<std::size_t>(item.sprite)].frameWidth, GeneratedScene::Sprites[static_cast<std::size_t>(item.sprite)].frameHeight, GeneratedScene::Sprites[static_cast<std::size_t>(item.sprite)].columns, image.width, image.height, flipped)
                    : World2D::FullImageUv(flipped);
                auto& quad = frames[run.first + runUsed[runIndex]++];
                quad.rectangle = {left, top, item.width, item.height};
                quad.color = {uv.u0, uv.v0, uv.u1, uv.v1};
            }
            next += 1;
            continue;
        }

        if (item.kind == GeneratedScene::Kind::Particles) {
            // The solid shader draws these quads like any other item's, in list order. The unused
            // part of the budget is given no area, as a hidden item is.
            const auto quads = scene.ParticleQuads(itemIndex);
            const auto capacity = GeneratedScene::ParticleEffects[static_cast<std::size_t>(item.particles)].amount;
            for (std::size_t index = 0; index < capacity; ++index, ++next) {
                if (index < quads.size()) {
                    const auto& quad = quads[index];
                    if (tilted && !item.screenFixed) {
                        const auto box = World2D::TiltUpright(tilt, quad.x, quad.y, quad.width, quad.height);
                        frames[next].rectangle = {box.x, box.y, box.width, box.height};
                    } else {
                        frames[next].rectangle = {quad.x - offsetX, quad.y - offsetY, quad.width, quad.height};
                    }
                    frames[next].color = {quad.color.red, quad.color.green, quad.color.blue, 1.0f};
                } else {
                    frames[next].rectangle = HiddenRectangle;
                }
            }
            continue;
        }
        if (item.kind != GeneratedScene::Kind::Text) {
            frames[next].color = color;
            if (visible && tilted && !item.screenFixed) {
                const auto corner = World2D::TiltProject(tilt, x, y);
                const auto scale = World2D::TiltScale(tilt, y + item.height / 2.0f);
                frames[next++].rectangle = {corner.x, corner.y, item.width * scale, item.height * scale * tilt.squash};
                continue;
            }
            frames[next++].rectangle = visible ? std::array{x - offsetX, y - offsetY, item.width, item.height} : HiddenRectangle;
            continue;
        }

        // item.text reserves a quad for every character the item can ever show. A text that shows
        // variables is usually shorter than that, and each frame its glyphs follow its current value.
        const auto text = scene.CurrentText(itemIndex);
        const bool changes = text.data() != item.text.data();
        const bool textTilted = tilted && !item.screenFixed;
        const auto textStart = textTilted ? World2D::TiltProject(tilt, x, y) : World2D::Point{x - offsetX, y - offsetY};
        const float textScale = item.textScale * (textTilted ? World2D::TiltScale(tilt, y) : 1.0f);
        for (std::size_t index = 0; index < item.text.size(); ++index) {
            frames[next].color = color;
            if (visible && index < text.size()) {
                frames[next].rectangle = {textStart.x + static_cast<float>(index) * 6 * textScale, textStart.y, 5 * textScale, 7 * textScale};
                if (changes) frames[next].glyph = EncodeGlyph(text[index]);
            } else {
                frames[next].rectangle = HiddenRectangle;
            }
            ++next;
        }
    }

    std::atomic_ref completion(*fence);
    completion.store(0, std::memory_order_release);
    CommandBuffer buffer{commands, commands + CommandWords, commands, commands + CommandWords, exhausted, nullptr, 0};
    sceAgcDcbContextStateOp(&buffer, 0);
    sceAgcDcbAcquireMem(&buffer, 1, 0, 0x4381, shaderMemoryPending ? memory.Data() : commands, shaderMemoryPending ? memory.Size() : slots[slotIndex].Bytes, 40);
    auto* const target = display.Acquire();

    // Standing items go back to front; a list that barely changes between frames sorts in about one pass.
    for (std::size_t sorted = 1; sorted < standingCount; ++sorted) {
        const auto entry = standing[sorted];
        std::size_t place = sorted;
        while (place > 0 && standing[place - 1].foot > entry.foot) {
            standing[place] = standing[place - 1];
            --place;
        }
        standing[place] = entry;
    }

    const Pipeline* bound = nullptr;
    const auto drawRun = [&](std::size_t index) {
        const auto& run = Runs[index];
        const bool isImage = run.image >= 0;
        const std::size_t quadCount = isImage ? runUsed[index] : run.count;
        if (quadCount == 0) return; // nothing of this run is on the screen this frame
        auto& wanted = isImage ? *imagePipeline : *pipeline;

        if (&wanted != bound) {
            // Bind resets every blend register from the pipeline's defaults, so only images switch it on.
            wanted.Bind(buffer, target, {isImage ? imageContexts : contexts, Pipeline::ContextCapacity});
            if (isImage) sceAgcDcbSetCxRegisterDirect(&buffer, {BlendControlRegister, PremultipliedBlendControl});
            for (const auto* shader : {vertex, isImage ? imageFragment : fragment}) sceAgcDcbSetShRegistersIndirect(&buffer, shader->sh_registers, shader->num_sh_registers);
            sceAgcDcbSetNumInstances(&buffer, 1);
            bound = &wanted;
        }

        bindShader(buffer, 0x8c, AdkShaders::ScreenVertex::UserData, AdkShaders::ScreenVertex::UserDataCount, AdkShaders::ScreenVertex::DescriptorSetSlots[0], descriptor + index * 4);
        if (isImage) {
            const auto* table = imageTables + (static_cast<std::size_t>(run.image) * Textures::TablesPerImage + (run.nearest ? 1u : 0u)) * Textures::TableBytes / sizeof(std::uint32_t);
            bindShader(buffer, 0xc, AdkShaders::ScreenImageFragment::UserData, AdkShaders::ScreenImageFragment::UserDataCount, AdkShaders::ScreenImageFragment::DescriptorSetSlots[0], table);
        } else {
            bindShader(buffer, 0xc, AdkShaders::ScreenFragment::UserData, AdkShaders::ScreenFragment::UserDataCount, -1, nullptr);
        }
        sceAgcDcbDrawIndexAuto(&buffer, static_cast<std::uint32_t>(quadCount * 6), 0);
    };

    bool standingDrawn = false;
    for (std::size_t index = sceneLayout.firstRun; index < sceneLayout.firstRun + sceneLayout.runCount; ++index) {
        if (Runs[index].upright) {
            if (!standingDrawn) {
                standingDrawn = true;
                for (std::size_t entry = 0; entry < standingCount; ++entry) drawRun(standing[entry].run);
            }
            continue;
        }
        drawRun(index);
    }

    const auto flip = display.PrepareFlip();
    sceAgcDcbSetFlip(&buffer, flip.Handle, flip.BufferIndex, 1, flip.Argument);
    sceAgcCbReleaseMem(&buffer, 0x2d, 0x70f, 0, 0, reinterpret_cast<Label*>(fence), 2, 1, 0, 0, 3, 0);
    const Packet packet{commands, static_cast<std::uint32_t>(buffer.cursor_up - commands), 0, {}};
    std::atomic_thread_fence(std::memory_order_seq_cst);
    Check(sceAgcDriverSubmitDcb(&packet), "sceAgcDriverSubmitDcb");
    slots[slotIndex].Pending = true;
    shaderMemoryPending = false;
    slotIndex = (slotIndex + 1) % slots.size();
    display.CompleteFlip();
}
