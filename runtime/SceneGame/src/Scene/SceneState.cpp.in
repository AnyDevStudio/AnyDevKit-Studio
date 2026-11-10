#include "Scene/SceneState.hpp"
#include "Input/Buttons.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace {

constexpr double UnitsPerSecond = 600.0;
constexpr float LogicWorldLimit = 1000000.0f;
constexpr float DirectionThreshold = 0.25f;

std::uint64_t DirectionButtons(float x, float y) {
    std::uint64_t buttons = 0;
    if (y < -DirectionThreshold) buttons |= Buttons::MoveUp;
    if (y > DirectionThreshold) buttons |= Buttons::MoveDown;
    if (x < -DirectionThreshold) buttons |= Buttons::MoveLeft;
    if (x > DirectionThreshold) buttons |= Buttons::MoveRight;
    return buttons;
}

// For each item, its place in GeneratedVariables::BoundTexts, or -1 for an item that shows no variables.
constexpr auto BindingOfItem = [] {
    std::array<std::int32_t, GeneratedScene::Items.size()> bindings{};
    bindings.fill(-1);
    std::int32_t next = 0;
    for (const auto& text : GeneratedVariables::BoundTexts) bindings[text.item] = next++;
    return bindings;
}();

// For each bound text, where its characters start in the shared buffer.
constexpr auto BoundOffsets = [] {
    std::array<std::size_t, GeneratedVariables::BoundTexts.size()> offsets{};
    std::size_t index = 0;
    std::size_t next = 0;
    for (const auto& text : GeneratedVariables::BoundTexts) {
        offsets[index++] = next;
        next += text.quads;
    }
    return offsets;
}();

// Writes the number's digits at `out` and returns the position after them.
char* writeNumber(char* out, std::int32_t value) {
    auto magnitude = static_cast<std::uint32_t>(value < 0 ? -static_cast<std::int64_t>(value) : static_cast<std::int64_t>(value));
    if (value < 0) *out++ = '-';
    std::array<char, 10> digits{};
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    while (count > 0) *out++ = digits[--count];
    return out;
}

// Animation number `animation` of a sprite, in the sprite's own list.
[[maybe_unused]] World2D::Clip ClipOf(const GeneratedScene::Sprite& sprite, std::size_t animation) {
    const auto& clip = GeneratedScene::Animations[sprite.firstAnimation + animation];
    return {clip.firstFrame, clip.frameCount, clip.framesPerSecond};
}

// For each scene, the first body of its slice of Bodies, and one past the last at the end.
constexpr auto SceneFirstBody = [] {
    std::array<std::size_t, GeneratedScene::SceneCount + 1> first{};
    for (std::size_t scene = 0; scene < GeneratedScene::SceneCount; ++scene) {
        std::size_t count = 0;
        const auto& info = GeneratedScene::Scenes[scene];
        for (std::size_t item = info.firstItem; item < info.firstItem + info.itemCount; ++item) {
            if (GeneratedScene::Items[item].body >= 0) ++count;
        }
        first[scene + 1] = first[scene] + count;
    }
    return first;
}();

// Where each effect's particles start in the pool shared by all scenes, sized before the first frame.
constexpr auto ParticleOffsets = [] {
    std::array<std::size_t, GeneratedScene::ParticleEffectCount> offsets{};
    std::size_t next = 0;
    for (std::size_t index = 0; index < offsets.size(); ++index) {
        offsets[index] = next;
        next += GeneratedScene::ParticleEffects[index].amount;
    }
    return offsets;
}();

[[maybe_unused]] std::int64_t FixedOf(float units) {
    return static_cast<std::int64_t>(static_cast<double>(units) * static_cast<double>(Physics2D::Scale) + (units >= 0.0f ? 0.5 : -0.5));
}

constexpr std::size_t NoItemIndex = GeneratedScene::Items.size();

// For each item, the copy group it is a copy in, or -1 for an ordinary item.
constexpr auto CopyGroupOf = [] {
    std::array<std::int32_t, GeneratedScene::Items.size()> groups{};
    groups.fill(-1);
    for (std::size_t group = 0; group < GeneratedScene::CopyGroups.size(); ++group) {
        const auto& range = GeneratedScene::CopyGroups[group];
        for (std::size_t item = range.firstItem; item < range.firstItem + range.count; ++item) groups[item] = static_cast<std::int32_t>(group);
    }
    return groups;
}();

// For each item, the item it follows, or NoItemIndex.
constexpr auto TargetOf = [] {
    std::array<std::size_t, GeneratedScene::Items.size()> targets{};
    targets.fill(NoItemIndex);
    for (const auto& follow : GeneratedScene::Follows) targets[follow.item] = follow.target;
    return targets;
}();

// Whose copy variables an item shows in its text: its own as a copy, its target's as a follower of a copy.
constexpr auto CopyContextOf = [] {
    std::array<std::size_t, GeneratedScene::Items.size()> contexts{};
    contexts.fill(NoItemIndex);
    for (std::size_t item = 0; item < contexts.size(); ++item) {
        if (CopyGroupOf[item] >= 0) contexts[item] = item;
        else if (TargetOf[item] != NoItemIndex && CopyGroupOf[TargetOf[item]] >= 0) contexts[item] = TargetOf[item];
    }
    return contexts;
}();

// For each scene and player, that player's character: the moving body with controls that belongs to them.
constexpr auto HeroOf = [] {
    std::array<std::array<std::size_t, GeneratedVariables::PlayerSlots>, GeneratedScene::SceneCount> heroes{};
    for (auto& scene : heroes) scene.fill(NoItemIndex);
    if constexpr (GeneratedScene::BodyCount != 0) {
        for (std::size_t scene = 0; scene < GeneratedScene::SceneCount; ++scene) {
            const auto& info = GeneratedScene::Scenes[scene];
            for (std::size_t item = info.firstItem; item < info.firstItem + info.itemCount; ++item) {
                const auto body = GeneratedScene::Items[item].body;
                if (body < 0) continue;
                const auto& settings = GeneratedScene::Bodies[static_cast<std::size_t>(body)];
                if (!settings.platformer && !settings.topDown) continue;
                const auto player = static_cast<std::size_t>(settings.player - 1);
                if (player < GeneratedVariables::PlayerSlots && heroes[scene][player] == NoItemIndex) heroes[scene][player] = item;
            }
        }
    }
    return heroes;
}();

// An item larger than the stage cannot fit, so it may only slide while still covering the stage.
float clampAxis(float value, float stage, float size) {
    const float limit = stage - size;
    return std::clamp(value, std::min(0.0f, limit), std::max(0.0f, limit));
}

}

SceneState::SceneState(int width, int height, Audio::Mixer* mixer, SaveMailbox* saves) : audioMixer(mixer), saveMailbox(saves), stageWidth(static_cast<float>(width)), stageHeight(static_cast<float>(height)) {
    startParticles();
    visible.fill(true);
    playerPresent[0] = true; // player 1 is here from the start; their "joins" event runs on the first frame
    enterScene(GeneratedScene::StartScene);
    rebuildText();
}

void SceneState::startParticles() {
    if constexpr (GeneratedScene::ParticleEffectCount != 0) {
        particleWorld = std::make_unique<ParticleWorld>();
    }
}

void SceneState::resetParticlesOf([[maybe_unused]] std::size_t itemIndex) {
    if constexpr (GeneratedScene::ParticleEffectCount != 0) {
        const auto& item = GeneratedScene::Items[itemIndex];
        if (item.kind != GeneratedScene::Kind::Particles) return;
        const auto effectIndex = static_cast<std::size_t>(item.particles);
        const auto& effect = GeneratedScene::ParticleEffects[effectIndex];
        const auto offset = ParticleOffsets[effectIndex];
        const Particles::Effect settings{static_cast<Particles::Preset>(effect.preset), static_cast<Particles::Palette>(effect.palette), effect.amount, effect.seed, effect.spread, effect.speed, effect.gravity, effect.pixelSize, effect.looping};
        const auto pool = std::span(particleWorld->pool).subspan(offset, effect.amount);
        const auto quads = std::span(particleWorld->quads).subspan(offset, effect.amount);
        const auto& background = CurrentSceneInfo().background;
        particleWorld->emitters[effectIndex].Reset(settings, {item.x, item.y, item.width, item.height}, {item.red, item.green, item.blue}, {background[0], background[1], background[2]}, pool, quads);
    }
}

std::size_t SceneState::CurrentScene() const { return currentScene; }
const GeneratedScene::SceneInfo& SceneState::CurrentSceneInfo() const { return GeneratedScene::Scenes[currentScene]; }

// Puts a scene back the way it starts, and runs its "When the game starts" blocks. Variables are not touched.
void SceneState::enterScene(std::size_t scene) {
    currentScene = scene;
    pendingScene = -1;
    const auto& info = GeneratedScene::Scenes[scene];
    rebuildText(); // so a controllable text is kept on the stage at its starting width
    for (std::size_t index = info.firstItem; index < info.firstItem + info.itemCount; ++index) {
        const auto& item = GeneratedScene::Items[index];
        positions[index] = {item.x, item.y};
        colors[index] = {item.red, item.green, item.blue};
        flipped[index] = item.flipped;
        visible[index] = true;
        if constexpr (GeneratedScene::SpriteCount != 0) {
            if (item.sprite >= 0) {
                const auto spriteIndex = static_cast<std::size_t>(item.sprite);
                const auto& sprite = GeneratedScene::Sprites[spriteIndex];
                animations[spriteIndex] = World2D::AnimationPlayer{};
                if (sprite.animationCount != 0) animations[spriteIndex].Play(ClipOf(sprite, sprite.startAnimation));
                looks[spriteIndex] = 0;
            }
        }
        if (item.controllable) clampToBounds(index, positions[index]);
        resetParticlesOf(index);
        alive[index] = CopyGroupOf[index] < 0;
        if (!alive[index]) visible[index] = false; // a copy waits, hidden, for a block to spawn it
        followerHidden[index] = false;
        home[index] = positions[index];
        wandering[index] = {};
        if constexpr (GeneratedScene::BodyCount != 0) {
            if (item.body >= 0) {
                const auto bodyIndex = static_cast<std::size_t>(item.body);
                const auto& settings = GeneratedScene::Bodies[bodyIndex];
                bodies[bodyIndex] = Physics2D::MakeBody({settings.gravityPercent, settings.platformer, settings.moveSpeed, settings.jumpHeight, settings.topDown, settings.player}, FixedOf(item.x), FixedOf(item.y), FixedOf(item.width), FixedOf(item.height));
                syncedPositions[bodyIndex] = positions[index];
            }
        }
    }
    camera = World2D::ClampCamera({info.cameraStartX, info.cameraStartY}, info.worldWidth, info.worldHeight);
    followedItem = -1;
    secondFollowed = -1;
    paused = false;
    logicMemory = {};
    physicsClock = {};
    jumpPressPending = {};
    self = NoItemIndex;
    currentPlayer = 0;
    GeneratedLogic::OnSceneStart(*this, scene);
    self = NoItemIndex;
    currentPlayer = 0;
    updateFollowers();
}

// Puts one item back as the scene starts it: its place, color, facing, animation and body.
void SceneState::resetItem(std::size_t index) {
    const auto& item = GeneratedScene::Items[index];
    positions[index] = {item.x, item.y};
    colors[index] = {item.red, item.green, item.blue};
    flipped[index] = item.flipped;
    if constexpr (GeneratedScene::SpriteCount != 0) {
        if (item.sprite >= 0) {
            const auto spriteIndex = static_cast<std::size_t>(item.sprite);
            const auto& sprite = GeneratedScene::Sprites[spriteIndex];
            animations[spriteIndex] = World2D::AnimationPlayer{};
            if (sprite.animationCount != 0) animations[spriteIndex].Play(ClipOf(sprite, sprite.startAnimation));
            looks[spriteIndex] = 0;
        }
    }
    if constexpr (GeneratedScene::BodyCount != 0) {
        if (item.body >= 0) {
            const auto bodyIndex = static_cast<std::size_t>(item.body);
            const auto& settings = GeneratedScene::Bodies[bodyIndex];
            bodies[bodyIndex] = Physics2D::MakeBody({settings.gravityPercent, settings.platformer, settings.moveSpeed, settings.jumpHeight, settings.topDown, settings.player}, FixedOf(item.x), FixedOf(item.y), FixedOf(item.width), FixedOf(item.height));
            syncedPositions[bodyIndex] = positions[index];
        }
    }
    wandering[index] = {};
}

// Followers stay on what they follow, and show only while it shows.
void SceneState::updateFollowers() {
    for (const auto& follow : GeneratedScene::Follows) {
        positions[follow.item] = {positions[follow.target][0] + follow.x, positions[follow.target][1] + follow.y};
        visible[follow.item] = !followerHidden[follow.item] && visible[follow.target] && alive[follow.target];
    }
}

void SceneState::updatePlayers(const std::array<PlayerInput, PlayerCount>& players) {
    for (std::size_t player = 0; player < PlayerCount; ++player) {
        const auto& input = players[player];
        const auto held = static_cast<std::uint32_t>(input.buttons | DirectionButtons(input.direction[0], input.direction[1]));
        playerPressed[player] = held & ~playerHeld[player];
        playerHeld[player] = held;
        playerJoinedNow[player] = player == 0 && !firstPlayerAnnounced;
        if (!playerPresent[player] && held != 0) {
            playerPresent[player] = true;
            playerJoinedNow[player] = true;
        }
    }
    firstPlayerAnnounced = true;
}

Particles::Emitter* SceneState::emitterFor(std::size_t item) {
    if constexpr (GeneratedScene::ParticleEffectCount != 0) {
        const auto& scene = GeneratedScene::Items[item];
        if (scene.kind == GeneratedScene::Kind::Particles) return &particleWorld->emitters[static_cast<std::size_t>(scene.particles)];
    }
    return nullptr;
}

void SceneState::Update(double seconds, Position direction, std::uint64_t buttons) {
    Update(seconds, {PlayerInput{direction, static_cast<std::uint32_t>(buttons)}, PlayerInput{}});
}

void SceneState::Update(double seconds, const std::array<PlayerInput, PlayerCount>& players) {
    applyLoadedVariables();
    frameSeconds = static_cast<float>(seconds);
    updatePlayers(players);
    const auto distance = static_cast<float>(UnitsPerSecond * seconds);
    const auto& info = GeneratedScene::Scenes[currentScene];
    const auto direction = players[0].direction;
    for (std::size_t index = info.firstItem; index < info.firstItem + info.itemCount && !paused; ++index) {
        if (GeneratedScene::Items[index].controllable) {
            positions[index][0] += direction[0] * distance;
            positions[index][1] += direction[1] * distance;
            clampToBounds(index, positions[index]);
        }
    }
    std::uint64_t allButtons = 0;
    for (std::size_t player = 0; player < 2; ++player) { // the older Buttons::P2 bits name only the first two players
        const auto& input = players[player];
        allButtons |= (input.buttons | DirectionButtons(input.direction[0], input.direction[1])) << (Buttons::Player2Shift * player);
    }
    pressedButtons = allButtons & ~heldButtons;
    heldButtons = allButtons;
    if (!paused) stepPhysics(seconds, players);
    self = NoItemIndex;
    currentPlayer = 0;
    GeneratedLogic::OnSceneFrame(*this, logicMemory, currentScene);
    self = NoItemIndex;
    currentPlayer = 0;
    clearBodyEvents();
    if (pendingScene >= 0) enterScene(static_cast<std::size_t>(pendingScene));
    updateFollowers();
    rebuildText();
    updateCamera();
    if (paused) return;
    const auto& shown = GeneratedScene::Scenes[currentScene];
    for (std::size_t index = shown.firstItem; index < shown.firstItem + shown.itemCount; ++index) {
        if constexpr (GeneratedScene::SpriteCount != 0) {
            if (GeneratedScene::Items[index].sprite >= 0) animations[static_cast<std::size_t>(GeneratedScene::Items[index].sprite)].Advance(seconds);
        }
        if (auto* emitter = emitterFor(index)) {
            emitter->SetPosition(positions[index][0], positions[index][1]);
            emitter->Update(seconds);
        }
    }
}

Physics2D::Body* SceneState::bodyOf([[maybe_unused]] std::size_t item) {
    if constexpr (GeneratedScene::BodyCount != 0) {
        const auto index = GeneratedScene::Items[item].body;
        return index >= 0 ? &bodies[static_cast<std::size_t>(index)] : nullptr;
    }
    return nullptr;
}

const Physics2D::Body* SceneState::bodyOf(std::size_t item) const { return const_cast<SceneState*>(this)->bodyOf(item); }

bool SceneState::Landed(std::size_t item) const { if (!Valid(item)) return false; const auto* body = bodyOf(item); return body && body->landed; }
bool SceneState::HitSolid(std::size_t item) const { if (!Valid(item)) return false; const auto* body = bodyOf(item); return body && body->hitSolid; }
bool SceneState::LeftWorld(std::size_t item) const { if (!Valid(item)) return false; const auto* body = bodyOf(item); return body && body->leftWorld; }
bool SceneState::OnGround(std::size_t item) const { if (!Valid(item)) return false; const auto* body = bodyOf(item); return body && body->grounded; }

void SceneState::SetVelocity(std::size_t item, float x, float y) {
    if (!Valid(item)) return;
    if (auto* body = bodyOf(item)) Physics2D::SetVelocity(*body, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y));
}

SceneState::Position SceneState::centerOf(std::size_t item) const {
    return {positions[item][0] + drawnWidth(item) / 2.0f, positions[item][1] + GeneratedScene::Items[item].height / 2.0f};
}

float SceneState::Distance(std::size_t first, std::size_t second) const {
    if (!Valid(first) || !Valid(second)) return LogicWorldLimit * 4.0f;
    const auto [ax, ay] = centerOf(first);
    const auto [bx, by] = centerOf(second);
    return std::hypot(bx - ax, by - ay);
}

void SceneState::Chase(std::size_t item, std::size_t target, float speed) {
    if (!Valid(item) || !Valid(target)) return;
    const auto [ax, ay] = centerOf(item);
    const auto [bx, by] = centerOf(target);
    const auto length = std::hypot(bx - ax, by - ay);
    if (length < 1.0f) {
        SetVelocity(item, 0.0f, 0.0f);
        return;
    }
    SetVelocity(item, (bx - ax) / length * speed, (by - ay) / length * speed);
}

void SceneState::PutAt(std::size_t item, std::size_t target, float x, float y) {
    if (!Valid(item) || !Valid(target)) return;
    SetPosition(item, positions[target][0] + x, positions[target][1] + y);
}

void SceneState::Jump(std::size_t item) {
    if (!Valid(item)) return;
    if (auto* body = bodyOf(item)) Physics2D::Jump(*body);
}

void SceneState::GoToScene(std::size_t scene) { pendingScene = static_cast<std::int32_t>(scene); }
void SceneState::RestartScene() { pendingScene = static_cast<std::int32_t>(currentScene); }

void SceneState::clearBodyEvents() {
    for (auto& body : bodies) {
        body.landed = body.hitSolid = body.leftWorld = false;
    }
}

// Runs as many fixed steps as this frame's time calls for, against the visible solids of the scene showing.
// The stick or D-pad and Cross drive every Platformer controls body; a body moved by a block starts from there.
void SceneState::stepPhysics([[maybe_unused]] double seconds, [[maybe_unused]] const std::array<PlayerInput, PlayerCount>& players) {
    if constexpr (GeneratedScene::BodyCount != 0) {
        const auto scene = currentScene;
        const auto firstBody = SceneFirstBody[scene];
        const auto bodyCount = SceneFirstBody[scene + 1] - firstBody;
        if (bodyCount == 0) return;

        const auto& info = GeneratedScene::Scenes[scene];
        for (std::size_t index = info.firstItem; index < info.firstItem + info.itemCount; ++index) {
            const auto body = GeneratedScene::Items[index].body;
            if (body < 0) continue;
            const auto bodyIndex = static_cast<std::size_t>(body);
            if (positions[index] != syncedPositions[bodyIndex]) {
                Physics2D::Teleport(bodies[bodyIndex], FixedOf(positions[index][0]), FixedOf(positions[index][1]));
                syncedPositions[bodyIndex] = positions[index];
            }
        }

        std::size_t solidCount = 0;
        std::size_t layerCount = 0;
        for (std::size_t index = info.firstItem; index < info.firstItem + info.itemCount; ++index) {
            const auto& item = GeneratedScene::Items[index];
            if (item.solid && visible[index]) {
                const auto left = FixedOf(positions[index][0]);
                const auto top = FixedOf(positions[index][1]);
                solidBoxes[solidCount++] = {left, top, left + FixedOf(item.width), top + FixedOf(item.height)};
            }
            if constexpr (GeneratedScene::TilemapCount != 0) {
                if (item.kind == GeneratedScene::Kind::Tilemap && visible[index]) {
                    const auto mapIndex = static_cast<std::size_t>(item.tilemap);
                    const auto& map = GeneratedScene::Tilemaps[mapIndex];
                    if (map.solidCount != 0) {
                        tileLayers[layerCount++] = {GeneratedScene::TilemapCells[mapIndex], map.columns, map.rows, static_cast<std::int32_t>(map.cellWidth), static_cast<std::int32_t>(map.cellHeight), FixedOf(positions[index][0]), FixedOf(positions[index][1]), GeneratedScene::SolidTiles.data() + map.firstSolid, map.solidCount};
                    }
                }
            }
        }

        std::array<Physics2D::Input, PlayerCount> inputs{};
        for (std::size_t player = 0; player < PlayerCount; ++player) {
            const auto& input = players[player];
            const bool jumpHeld = (input.buttons & Buttons::Cross) != 0;
            if (jumpHeld && !jumpWasHeld[player]) jumpPressPending[player] = true;
            jumpWasHeld[player] = jumpHeld;
            const auto move = input.direction[0] > 0.25f ? 1000 : (input.direction[0] < -0.25f ? -1000 : 0);
            inputs[player] = {move, jumpHeld, false, static_cast<std::int32_t>(input.direction[0] * 1000.0f), static_cast<std::int32_t>(input.direction[1] * 1000.0f)};
        }

        const Physics2D::WorldView view{std::span(bodies).subspan(firstBody, bodyCount), std::span(solidBoxes).first(solidCount), std::span(tileLayers).first(layerCount), FixedOf(info.worldWidth), FixedOf(info.worldHeight)};
        const auto steps = physicsClock.StepsFor(seconds);
        for (int step = 0; step < steps; ++step) {
            for (std::size_t player = 0; player < PlayerCount; ++player) inputs[player].jumpPressed = jumpPressPending[player];
            Physics2D::StepPlayers(view, std::span<const Physics2D::Input>(inputs));
            jumpPressPending = {};
        }

        for (std::size_t index = info.firstItem; index < info.firstItem + info.itemCount; ++index) {
            const auto body = GeneratedScene::Items[index].body;
            if (body < 0) continue;
            const auto bodyIndex = static_cast<std::size_t>(body);
            positions[index] = {static_cast<float>(Physics2D::WholeUnits(bodies[bodyIndex].x)), static_cast<float>(Physics2D::WholeUnits(bodies[bodyIndex].y))};
            syncedPositions[bodyIndex] = positions[index];
        }
    }
}

const SceneState::Positions& SceneState::Current() const { return positions; }
const SceneState::Colors& SceneState::CurrentColors() const { return colors; }
const SceneState::Visibility& SceneState::CurrentVisibility() const { return visible; }
World2D::Point SceneState::Camera() const { return camera; }
bool SceneState::Flipped(std::size_t item) const { return Valid(item) && flipped[item]; }

std::uint32_t SceneState::SpriteFrame(std::size_t item) const {
    if (!Valid(item)) return 0;
    if constexpr (GeneratedScene::SpriteCount != 0) {
        const auto sprite = GeneratedScene::Items[item].sprite;
        if (sprite >= 0) {
            const auto& entry = GeneratedScene::Sprites[static_cast<std::size_t>(sprite)];
            const auto look = looks[static_cast<std::size_t>(sprite)] * entry.lookFrames;
            if (entry.animationCount == 0) return look;
            return GeneratedScene::AnimationFrames[animations[static_cast<std::size_t>(sprite)].Frame()] + look;
        }
    }
    return 0;
}

void SceneState::PlayAnimation(std::size_t item, std::size_t animation) {
    if (!Valid(item)) return;
    if constexpr (GeneratedScene::SpriteCount != 0) {
        const auto sprite = GeneratedScene::Items[item].sprite;
        if (sprite >= 0) {
            const auto& entry = GeneratedScene::Sprites[static_cast<std::size_t>(sprite)];
            animations[static_cast<std::size_t>(sprite)].Play(ClipOf(entry, animation));
        }
    }
}

void SceneState::SetFlipped(std::size_t item, bool value) {
    if (Valid(item)) flipped[item] = value;
}

void SceneState::CameraFollow(std::size_t item) {
    if (!Valid(item)) return;
    followedItem = static_cast<std::int32_t>(item);
    secondFollowed = -1;
}

void SceneState::CameraFollowBoth(std::size_t item, std::size_t other) {
    if (!Valid(item) || !Valid(other)) return;
    followedItem = static_cast<std::int32_t>(item);
    secondFollowed = static_cast<std::int32_t>(other);
}

void SceneState::PauseGame() { paused = true; }
void SceneState::ResumeGame() { paused = false; }
bool SceneState::Paused() const { return paused; }

void SceneState::SetCameraPosition(float x, float y) {
    followedItem = -1;
    secondFollowed = -1;
    const auto& info = CurrentSceneInfo();
    camera = World2D::ClampCamera({x, y}, info.worldWidth, info.worldHeight);
}

void SceneState::updateCamera() {
    if (followedItem < 0) return;
    const auto boxOf = [&](std::size_t item) { return World2D::FollowRect{positions[item][0], positions[item][1], drawnWidth(item), GeneratedScene::Items[item].height}; };
    const auto first = static_cast<std::size_t>(followedItem);
    auto box = boxOf(first);
    if (secondFollowed >= 0) {
        const auto second = static_cast<std::size_t>(secondFollowed);
        const auto other = boxOf(second);
        if (!visible[first] && visible[second]) {
            box = other;
        } else if (visible[first] && visible[second]) {
            const float left = std::min(box.x, other.x);
            const float top = std::min(box.y, other.y);
            const float right = std::max(box.x + box.width, other.x + other.width);
            const float bottom = std::max(box.y + box.height, other.y + other.height);
            box = {left, top, right - left, bottom - top};
        }
    }
    const auto& info = CurrentSceneInfo();
    camera = World2D::FollowCamera(box, info.worldWidth, info.worldHeight);
}

void SceneState::MoveBy(std::size_t item, float x, float y) {
    if (!Valid(item)) return;
    auto position = positions[item];
    position[0] += x;
    position[1] += y;
    limitPosition(item, position);
    positions[item] = position;
}

void SceneState::SetPosition(std::size_t item, float x, float y) {
    if (!Valid(item)) return;
    Position position{x, y};
    limitPosition(item, position);
    positions[item] = position;
}

void SceneState::SetVisible(std::size_t item, bool value) {
    if (!Valid(item)) return;
    if (TargetOf[item] != NoItemIndex) {
        followerHidden[item] = !value; // the follower shows again with what it follows
        return;
    }
    if (!alive[item]) return; // a copy that is not spawned stays hidden
    visible[item] = value;
    if (auto* emitter = emitterFor(item)) emitter->SetVisible(value);
}

void SceneState::SetColor(std::size_t item, float red, float green, float blue) {
    if (!Valid(item)) return;
    colors[item] = {red, green, blue};
    if (auto* emitter = emitterFor(item)) emitter->SetBaseColor({red, green, blue});
}

void SceneState::BurstParticles(std::size_t item) {
    if (!Valid(item)) return;
    if (auto* emitter = emitterFor(item)) emitter->Burst();
}

void SceneState::AwaitBurst(std::size_t item) {
    if (!Valid(item)) return;
    if (auto* emitter = emitterFor(item)) emitter->AwaitBurst();
}

std::span<const Particles::Quad> SceneState::ParticleQuads(std::size_t item) const {
    if (!Valid(item)) return {};
    if constexpr (GeneratedScene::ParticleEffectCount != 0) {
        const auto& scene = GeneratedScene::Items[item];
        if (scene.kind == GeneratedScene::Kind::Particles) return particleWorld->emitters[static_cast<std::size_t>(scene.particles)].Quads();
    }
    return {};
}

void SceneState::SetVariable(std::size_t variable, std::int32_t value) { variables[variable] = value; }

void SceneState::PlaySound(std::size_t sound) { if (audioMixer) audioMixer->PlaySound(sound); }
void SceneState::PlayMusic(std::size_t sound) { if (audioMixer) audioMixer->PlayMusic(sound); }
void SceneState::StopMusic() { if (audioMixer) audioMixer->StopMusic(); }
void SceneState::SetVolume(std::int32_t percent) { if (audioMixer) audioMixer->SetVolume(percent); }
void SceneState::SaveVariables() {
    if (!saveMailbox) return;
    std::array<std::int32_t, GeneratedVariables::SavedCount> saved{};
    std::copy(variables.begin(), variables.end(), saved.begin());
    auto next = saved.begin() + static_cast<std::ptrdiff_t>(variables.size());
    for (const auto& values : playerVariables) next = std::copy(values.begin(), values.end(), next);
    saveMailbox->PublishSave(saved);
}
void SceneState::LoadVariables() { if (saveMailbox) saveMailbox->RequestLoad(); }

void SceneState::applyLoadedVariables() {
    if (!saveMailbox) return;
    std::array<std::int32_t, GeneratedVariables::SavedCount> loaded{};
    if (!saveMailbox->TakeLoaded(loaded)) return;
    std::copy_n(loaded.begin(), variables.size(), variables.begin());
    auto next = loaded.begin() + static_cast<std::ptrdiff_t>(variables.size());
    for (auto& values : playerVariables) {
        std::copy_n(next, values.size(), values.begin());
        next += static_cast<std::ptrdiff_t>(values.size());
    }
}

void SceneState::ChangeVariable(std::size_t variable, std::int32_t amount) {
    const auto total = static_cast<std::int64_t>(variables[variable]) + amount;
    const auto lowest = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min());
    const auto highest = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());
    variables[variable] = static_cast<std::int32_t>(std::clamp(total, lowest, highest));
}

std::int32_t SceneState::Variable(std::size_t variable) const { return variables[variable]; }

std::size_t SceneState::CurrentPlayer() const { return currentPlayer; }
void SceneState::SetCurrentPlayer(std::size_t player) { currentPlayer = player < PlayerCount ? player : 0; }
bool SceneState::PlayerHere(std::size_t player) const { return player < PlayerCount && playerPresent[player]; }
bool SceneState::PlayerJoined(std::size_t player) const { return player < PlayerCount && playerJoinedNow[player]; }
std::int64_t SceneState::PlayerNumber() const { return static_cast<std::int64_t>(currentPlayer) + 1; }
std::int64_t SceneState::PlayersHere() const { return std::count(playerPresent.begin(), playerPresent.end(), true); }
bool SceneState::PlayerHeld(std::uint64_t button) const { return (playerHeld[currentPlayer] & button) != 0; }
bool SceneState::PlayerPressed(std::uint64_t button) const { return (playerPressed[currentPlayer] & button) != 0; }
std::size_t SceneState::PlayerHero() const { return HeroOf[currentScene][currentPlayer]; }

std::size_t SceneState::NearestPlayerHero(std::size_t from) const {
    if (!Valid(from)) return NoItemIndex;
    std::size_t nearest = NoItemIndex;
    float best = 0.0f;
    for (std::size_t player = 0; player < PlayerCount; ++player) {
        const auto hero = HeroOf[currentScene][player];
        if (!playerPresent[player] || hero == NoItemIndex || !visible[hero]) continue;
        const auto distance = Distance(from, hero);
        if (nearest == NoItemIndex || distance < best) {
            nearest = hero;
            best = distance;
        }
    }
    return nearest;
}

std::int32_t SceneState::PlayerVariable([[maybe_unused]] std::size_t variable) const {
    if constexpr (GeneratedVariables::PlayerVariableCount != 0) return playerVariables[currentPlayer][variable];
    return 0;
}

void SceneState::SetPlayerVariable([[maybe_unused]] std::size_t variable, [[maybe_unused]] std::int32_t value) {
    if constexpr (GeneratedVariables::PlayerVariableCount != 0) playerVariables[currentPlayer][variable] = value;
}

void SceneState::ChangePlayerVariable(std::size_t variable, std::int32_t amount) {
    SetPlayerVariable(variable, Formula::ToVariable(static_cast<std::int64_t>(PlayerVariable(variable)) + amount));
}

std::size_t SceneState::Self() const { return self; }
void SceneState::SetSelf(std::size_t item) { self = Valid(item) ? item : NoItemIndex; }
bool SceneState::Alive(std::size_t item) const { return Valid(item) && alive[item]; }

std::size_t SceneState::SpawnCopy(std::size_t first, std::size_t last, std::size_t at, float x, float y) {
    last = std::min(last, ItemCount);
    for (std::size_t item = first; item < last; ++item) {
        if (alive[item]) continue;
        resetItem(item);
        alive[item] = true;
        visible[item] = true;
        if constexpr (GeneratedVariables::CopyVariableCount != 0) copyVariables[item] = GeneratedVariables::CopyInitialValues;
        if (Valid(at)) SetPosition(item, positions[at][0] + x, positions[at][1] + y);
        if constexpr (GeneratedScene::BodyCount != 0) {
            if (auto* body = bodyOf(item)) {
                Physics2D::Teleport(*body, FixedOf(positions[item][0]), FixedOf(positions[item][1]));
                syncedPositions[static_cast<std::size_t>(GeneratedScene::Items[item].body)] = positions[item];
            }
        }
        home[item] = positions[item];
        for (const auto& follow : GeneratedScene::Follows) {
            if (follow.target == item) followerHidden[follow.item] = false;
        }
        return item;
    }
    return NoItemIndex;
}

void SceneState::RemoveCopy(std::size_t item) {
    if (!Valid(item) || CopyGroupOf[item] < 0 || !alive[item]) return;
    alive[item] = false;
    visible[item] = false;
    SetVelocity(item, 0.0f, 0.0f);
}

std::int32_t SceneState::CopyVariable([[maybe_unused]] std::size_t variable) const {
    if constexpr (GeneratedVariables::CopyVariableCount != 0) {
        if (Valid(self)) return copyVariables[self][variable];
    }
    return 0;
}

void SceneState::SetCopyVariable([[maybe_unused]] std::size_t variable, [[maybe_unused]] std::int32_t value) {
    if constexpr (GeneratedVariables::CopyVariableCount != 0) {
        if (Valid(self)) copyVariables[self][variable] = value;
    }
}

void SceneState::ChangeCopyVariable(std::size_t variable, std::int32_t amount) {
    SetCopyVariable(variable, Formula::ToVariable(static_cast<std::int64_t>(CopyVariable(variable)) + amount));
}

std::int64_t SceneState::Random(std::int64_t low, std::int64_t high) {
    if (low > high) std::swap(low, high);
    randomState ^= randomState >> 12;
    randomState ^= randomState << 25;
    randomState ^= randomState >> 27;
    const auto value = randomState * 2685821657736338717ull;
    const auto span = static_cast<std::uint64_t>(Formula::Bound(high) - Formula::Bound(low)) + 1;
    return Formula::Bound(low) + static_cast<std::int64_t>(value % span);
}

// Heads for a point at `speed` units per second, and turns an image to face that way.
void SceneState::moveToward(std::size_t item, Position target, float speed) {
    const auto center = centerOf(item);
    const auto dx = target[0] - center[0];
    const auto dy = target[1] - center[1];
    const auto length = std::hypot(dx, dy);
    if (length < 1.0f || speed <= 0.0f) {
        SetVelocity(item, 0.0f, 0.0f);
        return;
    }
    if (GeneratedScene::Items[item].kind == GeneratedScene::Kind::Image && std::abs(dx) > 2.0f) flipped[item] = dx < 0.0f;
    if (bodyOf(item)) {
        SetVelocity(item, dx / length * speed, dy / length * speed);
    } else {
        const auto step = std::min(length, speed * frameSeconds);
        MoveBy(item, dx / length * step, dy / length * step);
    }
}

void SceneState::Wander(std::size_t item, float speed, float radius) {
    if (!Valid(item) || !alive[item] || paused) return;
    auto& state = wandering[item];
    state.timer -= frameSeconds;
    const auto center = centerOf(item);
    const auto arrived = std::hypot(state.target[0] - center[0], state.target[1] - center[1]) < 12.0f;
    if (state.moving && (arrived || state.timer <= 0.0f)) {
        state.moving = false; // rest a while before the next stroll
        state.timer = static_cast<float>(Random(400, 2200)) / 1000.0f;
    } else if (!state.moving && state.timer <= 0.0f) {
        const auto angle = static_cast<float>(Random(0, 6283)) / 1000.0f;
        const auto reach = radius * static_cast<float>(Random(25, 100)) / 100.0f;
        const auto& item0 = GeneratedScene::Items[item];
        state.target = {home[item][0] + item0.width / 2.0f + std::cos(angle) * reach, home[item][1] + item0.height / 2.0f + std::sin(angle) * reach};
        state.moving = true;
        state.timer = speed > 0.0f ? reach / speed + 1.5f : 0.0f;
    }
    if (state.moving) moveToward(item, state.target, speed);
    else SetVelocity(item, 0.0f, 0.0f);
}

void SceneState::Flee(std::size_t item, std::size_t from, float speed) {
    if (!Valid(item) || !Valid(from) || paused) return;
    const auto center = centerOf(item);
    const auto other = centerOf(from);
    auto dx = center[0] - other[0];
    auto dy = center[1] - other[1];
    if (std::hypot(dx, dy) < 1.0f) dx = 1.0f;
    moveToward(item, {center[0] + dx * 100.0f, center[1] + dy * 100.0f}, speed);
    wandering[item] = {};
}

void SceneState::AlertNearby(std::size_t first, std::size_t last, [[maybe_unused]] std::size_t variable, [[maybe_unused]] std::int32_t value, float radius) {
    if (!Valid(self)) return;
    last = std::min(last, ItemCount);
    for (std::size_t item = first; item < last; ++item) {
        if (!alive[item] || Distance(self, item) > radius) continue;
        if constexpr (GeneratedVariables::CopyVariableCount != 0) copyVariables[item][variable] = value;
    }
}

void SceneState::SetLook([[maybe_unused]] std::size_t item, [[maybe_unused]] std::int64_t look) {
    if constexpr (GeneratedScene::SpriteCount != 0) {
        if (!Valid(item) || GeneratedScene::Items[item].sprite < 0) return;
        const auto sprite = static_cast<std::size_t>(GeneratedScene::Items[item].sprite);
        const auto& entry = GeneratedScene::Sprites[sprite];
        if (entry.lookFrames == 0) return;
        const auto count = static_cast<std::int64_t>(entry.columns * entry.rows / entry.lookFrames);
        looks[sprite] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(look, 0, count - 1));
    }
}

std::int64_t SceneState::SelfX() const { return Valid(self) ? static_cast<std::int64_t>(centerOf(self)[0]) : 0; }
std::int64_t SceneState::SelfY() const { return Valid(self) ? static_cast<std::int64_t>(centerOf(self)[1]) : 0; }
std::int64_t SceneState::PlayerX() const { const auto hero = PlayerHero(); return Valid(hero) ? static_cast<std::int64_t>(centerOf(hero)[0]) : 0; }
std::int64_t SceneState::PlayerY() const { const auto hero = PlayerHero(); return Valid(hero) ? static_cast<std::int64_t>(centerOf(hero)[1]) : 0; }
std::int64_t SceneState::PlayerDistance() const { return static_cast<std::int64_t>(Distance(self, PlayerHero())); }
std::int64_t SceneState::NearestPlayerDistance() const { return static_cast<std::int64_t>(Distance(self, NearestPlayerHero(self))); }
std::int64_t SceneState::SelfVelocityX() const { const auto* body = Valid(self) ? bodyOf(self) : nullptr; return body ? body->velocityX : 0; }
std::int64_t SceneState::SelfVelocityY() const { const auto* body = Valid(self) ? bodyOf(self) : nullptr; return body ? body->velocityY : 0; }

void SceneState::rebuildText() {
    std::size_t binding = 0;
    for (const auto& text : GeneratedVariables::BoundTexts) {
        char* const first = boundCharacters.data() + BoundOffsets[binding];
        char* cursor = first;
        for (const auto& part : std::span(GeneratedVariables::Parts).subspan(text.firstPart, text.partCount)) {
            if (part.variable < 0) {
                cursor = std::copy(part.text.begin(), part.text.end(), cursor);
                continue;
            }
            const auto variable = static_cast<std::size_t>(part.variable);
            std::int32_t value = 0;
            if (part.scope == 0) {
                value = variables[variable];
            } else if (part.scope == 1) {
                if constexpr (GeneratedVariables::PlayerVariableCount != 0) value = playerVariables[part.player > 0 ? static_cast<std::size_t>(part.player) : 0][variable];
            } else {
                if constexpr (GeneratedVariables::CopyVariableCount != 0) {
                    const auto context = CopyContextOf[text.item];
                    if (context != NoItemIndex) value = copyVariables[context][variable];
                }
            }
            if (part.letter) *cursor++ = value >= 1 && value <= 26 ? static_cast<char>('A' + value - 1) : value >= 27 && value <= 36 ? static_cast<char>('0' + value - 27) : ' ';
            else cursor = writeNumber(cursor, value);
        }
        boundLengths[binding] = static_cast<std::size_t>(cursor - first);
        ++binding;
    }
}

std::string_view SceneState::CurrentText(std::size_t item) const {
    if (!Valid(item)) return {};
    const auto binding = BindingOfItem[item];
    if (binding < 0) return GeneratedScene::Items[item].text;
    const auto index = static_cast<std::size_t>(binding);
    return {boundCharacters.data() + BoundOffsets[index], boundLengths[index]};
}

float SceneState::drawnWidth(std::size_t item) const {
    const auto& scene = GeneratedScene::Items[item];
    if (BindingOfItem[item] < 0) return scene.width;
    return static_cast<float>(CurrentText(item).size() * 6 - 1) * scene.textScale;
}

bool SceneState::Touching(std::size_t first, std::size_t second) const {
    if (!Valid(first) || !Valid(second) || first == second) return false;
    if (!visible[first] || !visible[second]) return false;
    const auto& a = GeneratedScene::Items[first];
    const auto& b = GeneratedScene::Items[second];
    const auto [ax, ay] = positions[first];
    const auto [bx, by] = positions[second];
    return ax < bx + drawnWidth(second) && bx < ax + drawnWidth(first) && ay < by + b.height && by < ay + a.height;
}

bool SceneState::Held(std::uint64_t button) const { return (heldButtons & button) != 0; }
bool SceneState::Pressed(std::uint64_t button) const { return (pressedButtons & button) != 0; }

void SceneState::clampToBounds(std::size_t item, Position& position) const {
    const auto& scene = GeneratedScene::Items[item];
    const auto& info = CurrentSceneInfo();
    const float boundsWidth = scene.screenFixed ? stageWidth : info.worldWidth;
    const float boundsHeight = scene.screenFixed ? stageHeight : info.worldHeight;
    position[0] = clampAxis(position[0], boundsWidth, drawnWidth(item));
    position[1] = clampAxis(position[1], boundsHeight, scene.height);
}

void SceneState::limitPosition(std::size_t item, Position& position) const {
    if (GeneratedScene::Items[item].controllable) {
        clampToBounds(item, position);
        return;
    }
    position[0] = std::clamp(position[0], -LogicWorldLimit, LogicWorldLimit);
    position[1] = std::clamp(position[1], -LogicWorldLimit, LogicWorldLimit);
}
