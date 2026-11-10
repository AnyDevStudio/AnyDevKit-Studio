# Blocks format

Game logic built from jigsaw style blocks is saved in `logic.json`, at the project root next to
`scene.json`. Before each build Studio turns it into `src/GeneratedLogic.hpp` and
`src/GeneratedVariables.hpp`, which the game runtime compiles in. There is no interpreter and no script:
each block becomes ordinary C++.

A project without `logic.json` builds with an empty logic header and behaves as it did before blocks
existed. The API for editors is in [src/core/Blocks.h](../src/core/Blocks.h).

## logic.json

A program is a list of variables, a list of sounds and, for each scene, a tree of blocks. Logic belongs per
scene; variables and sounds are shared by the whole project. Events are the top level, each holding actions,
and an `ifVariable` or `ifOnGround` action can hold more actions.

```json
{
  "schemaVersion": 4,
  "variables": [
    { "name": "score", "initial": 0 }
  ],
  "sounds": [],
  "scenes": [
    {
      "scene": "scene-1",
      "events": [
    {
      "id": "event-1",
      "type": "whenGameStarts",
      "actions": [
        { "id": "block-1", "type": "setVariable", "variable": "score", "value": 0 }
      ]
    },
    {
      "id": "event-2",
      "type": "whileButtonHeld",
      "button": "cross",
      "actions": [
        { "id": "block-2", "type": "moveBy", "item": "item-1", "x": 4, "y": 0 }
      ]
    },
    {
      "id": "event-3",
      "type": "whenItemsTouch",
      "item": "item-1",
      "otherItem": "item-2",
      "actions": [
        { "id": "block-3", "type": "changeVariable", "variable": "score", "value": 1 },
        { "id": "block-4", "type": "hide", "item": "item-2" }
      ]
    },
    {
      "id": "event-4",
      "type": "everyFrame",
      "actions": [
        {
          "id": "block-5",
          "type": "ifVariable",
          "variable": "score",
          "comparison": "greaterOrEqual",
          "value": 10,
          "actions": [
            { "id": "block-6", "type": "setColor", "item": "item-1", "color": "#FF4D4D" }
          ]
        }
      ]
    }
      ]
    }
  ]
}
```

`scenes` lists each scene's blocks by its `id` from [scene.json](scene-format.md). A scene with no blocks needs
no entry, and an entry whose `scene` is empty means the project's first scene, as an older file's blocks do.

### Events

| `type`            | Title                  | Fields                | Runs                                                  |
| ----------------- | ---------------------- | --------------------- | ----------------------------------------------------- |
| `whenGameStarts`  | When the game starts   |                       | once, before the first frame                          |
| `whileButtonHeld` | While a button is held | `button`              | every frame the button is down                        |
| `everyFrame`      | Every frame            |                       | every frame                                           |
| `whenItemsTouch`  | When two items touch   | `item`, `otherItem`   | once when the two items start touching                |
| `whenItemLands`   | When item lands        | `item`                | when a moving body comes to rest on something, after being in the air |
| `whenItemHitsSolid` | When item hits a solid | `item`              | when a moving body bumps into a solid or tile it was not already against |
| `whenItemLeavesWorld` | When item leaves the world | `item`         | once when a moving body falls out of the world, for pits |
| `whenButtonPressed` | When a button is pressed | `button`          | once each time the button goes down                   |
| `whenPlayerPresses` | When a player presses | `button`             | once for each player in the scene who pressed the button this frame, as the current player |
| `whilePlayerHolds` | While a player holds  | `button`              | every frame, for each player in the scene holding the button, as the current player |
| `whenPlayerJoins` | When a player joins    |                       | for a player who just arrived, as the current player: player 1 on the first frame, another player when their pad presses its first button |
| `everyFrameForEachCopy` | Every frame, for each copy of | `item`     | every frame, once for each live copy of the template `item`, with `@self` as that copy |
| `whenCopyTouches` | When a copy touches    | `item`, `otherItem`   | once when a live copy of the template `item` starts touching `otherItem`, with `@self` as that copy. `otherItem` may be `@player`: then once for each player, as the current player |

In the player events `button` is one without `p2`: each player uses their own pad.

`button` is one of `cross`, `circle`, `square`, `triangle`, `dpadUp`, `dpadDown`, `dpadLeft`,
`dpadRight`, or `moveUp`, `moveDown`, `moveLeft`, `moveRight`, which are the left stick or the D-pad pushed that way, or
`options`. The second player's pad has the same buttons with a `p2` prefix: `p2Cross`, `p2Circle`, `p2Square`,
`p2Triangle`, `p2Options`, `p2DpadUp`, `p2DpadDown`, `p2DpadLeft`, `p2DpadRight`, `p2MoveUp`, `p2MoveDown`, `p2MoveLeft`
and `p2MoveRight`. The second player is any other user logged in on the console with their own pad; until one is,
their buttons are never pressed. Every event needs an `actions` list, which may be empty.

### Actions

| `type`        | Title             | Fields                  | Does                                   |
| ------------- | ----------------- | ----------------------- | -------------------------------------- |
| `moveBy`      | Move item         | `item`, `x`, `y`        | adds `x` and `y` to the item's position |
| `setPosition` | Set item position | `item`, `x`, `y`        | puts the item at `x`, `y`              |
| `show`        | Show item         | `item`                  | draws the item again                   |
| `hide`        | Hide item         | `item`                  | stops drawing the item                 |
| `setColor`    | Set item color    | `item`, `color`         | changes the item's color               |
| `setVariable` | Set variable      | `variable`, `value`     | replaces the variable's value          |
| `changeVariable` | Change variable | `variable`, `value`     | adds `value`, which can be negative    |
| `ifVariable`  | If variable       | `variable`, `comparison`, `value`, `actions` | runs `actions` when the variable compares true to `value` |
| `burstParticles` | Burst particles from item | `item` | plays that Particles item once, at its current position, on top of anything already running. The item must be a Particles item |
| `playSound`   | Play sound        | `sound`                 | plays that sound once                  |
| `playMusic`   | Play music on loop | `sound`                | loops that sound as music, replacing any other music. If it is already playing, it keeps its place |
| `stopMusic`   | Stop music        |                         | stops the music; sounds keep playing   |
| `setVolume`   | Set volume        | `value`                 | sets the whole game's volume to `value` percent, 0 to 100 |
| `saveVariables` | Save variables  |                         | keeps every variable's value on the console |
| `loadVariables` | Load variables  |                         | asks for the saved values back         |
| `playAnimation` | Play animation on item | `item`, `animation` | starts that animation of a sprite sheet, or keeps playing it if it is already on. The item must be an image with frames, and `animation` one of its names |
| `flipItem` | Flip item | `item`, `flipped` | mirrors an image left to right (`true`) or back (`false`). The item must be an image |
| `cameraFollow` | Camera follows item | `item` | keeps the camera centered on the item, every frame, as far as the world allows. The item must not have Stays on screen turned on |
| `setCameraPosition` | Set camera position | `x`, `y` | puts the camera's top left corner at `x`, `y`, clamped to the world, and stops following any item |
| `setVelocity` | Set velocity | `item`, `x`, `y` | sets a moving body's velocity to `x`, `y` units per second. The item must be a moving body |
| `jumpItem` | Jump item | `item` | jumps with the item's jump height if it is on the ground or just left it, as the jump button does. The item must be a moving body |
| `ifOnGround` | If item is on the ground | `item`, `actions` | runs `actions` when a moving body stands on something. The item must be a moving body |
| `goToScene` | Go to scene | `scene` | after this frame's blocks, shows that scene from its start. Variables keep their values |
| `restartScene` | Restart scene | | after this frame's blocks, shows this scene again from its start. Variables keep their values |
| `ifItemDistance` | If distance between | `item`, `otherItem`, `comparison`, `value`, `actions` | runs `actions` when the distance between the two items' centers compares true to `value`, in world units. Hidden items are measured too |
| `chaseItem` | Chase | `item`, `otherItem`, `value` | sets a moving body's velocity toward the other item at `value` units per second, 0 to 3000. It takes effect on the next physics step and lasts until something changes it. The item must be a moving body |
| `putAtItem` | Put item | `item`, `otherItem`, `x`, `y` | puts the item at the other item's position plus `x` and `y` |
| `pauseGame` | Pause the game | | stops moving bodies, controllable items, animations and particles. Blocks keep running and buttons keep working, so a pause menu can be made of blocks. Going to a scene resumes |
| `resumeGame` | Resume the game | | lets the world move again after `pauseGame` |
| `cameraFollowBoth` | Camera follows two items | `item`, `otherItem` | keeps the camera centered on the middle of both items, every frame, as far as the world allows; while one of them is hidden, it follows the other. Neither may have Stays on screen turned on |
| `setFormula` | Set variable to a formula | `variable`, `formula` | replaces the variable's value with the formula's |
| `ifFormula` | If | `formula`, `actions` | runs `actions` when the formula is not 0 |
| `otherwise` | Otherwise | `actions` | right after an `ifVariable`, `ifFormula`, `ifItemDistance` or `ifOnGround`, runs `actions` when that block did not run its own |
| `forEachPlayer` | For each player | `actions` | runs `actions` once for each player in the scene, as the current player |
| `spawnCopy` | Spawn a copy | `item`, `otherItem`, `x`, `y`, `actions` | brings out a hidden copy of the template `item` at `otherItem`'s position plus `x` and `y`, with its copy variables at their starting values, and runs `actions` for it as `@self`. When every copy is out, nothing happens |
| `removeCopy` | Remove this copy | `item` | hides the copy and frees it for a later spawn. `item` is `@self` |
| `forEachCopy` | For each copy of | `item`, `actions` | runs `actions` once for each live copy of the template, with `@self` as that copy |
| `wander` | Wander | `item`, `value`, `radius` | walks about within `radius` of where the item started or was spawned, at `value` units per second, resting now and then |
| `flee` | Run away from | `item`, `otherItem`, `value` | moves away from the other item at `value` units per second |
| `alertNearby` | Tell nearby copies | `item`, `variable`, `value`, `radius` | sets the copy variable `variable` to `value` for every live copy of the template `item` within `radius` of `@self`, `@self` included. This is how a pack hears that one of them was hit |
| `setLook` | Set look | `item`, `formula` | shows look number `formula`, counted from 0, of a sprite sheet that holds several looks |

`wander` and `flee` set a moving body's velocity and move any other item directly, and turn an image to face the way
it goes.

### Formulas

`formula` is a whole-number expression of up to 400 characters: numbers, variable names, `+ - * / %`, brackets,
the comparisons `= == != < <= > >=`, `and`, `or`, `not` (or `&& || !`), and the functions `rand(low, high)`,
`min(a, b)`, `max(a, b)`, `abs(a)` and `clamp(value, low, high)`. `/` and `%` round toward zero and give 0 when
dividing by 0, a comparison gives 1 or 0, and no step can overflow. Besides the variables a formula can read
`player` (the current player, 1 to 4), `players` (how many are in the scene), and these, in world units:
`selfX` and `selfY` (the middle of `@self`), `playerX` and `playerY` (the middle of the current player's
character), `dist` (between those two), `nearestDist` (from `@self` to the nearest player's character), and
`selfVX` and `selfVY` (how fast `@self`, a moving body, is going). For example `hp - max(1, atk * 2 - def)` or
`mstate = 0 and nearestDist < 400`.

### Players, templates and copies

A game has up to 4 players. Blocks run for a current player: player 1 unless a player event or `forEachPlayer`
says otherwise. A player's character in a scene is the moving body with controls whose `player` is theirs; blocks
name it `@player`.

A template is a scene item with `copies` above 0 (see [scene-format.md](scene-format.md)). Blocks never name a
template directly: they spawn its copies, go through them, and inside those blocks name the copy as `@self`.
`@nearestPlayer` is the shown player character nearest to `@self`. Any block given a player or a copy that is not
there does nothing.

`comparison` is one of `equal`, `notEqual`, `less`, `lessOrEqual`, `greater`, `greaterOrEqual`.

### Variables

`variables` lists the program's named whole numbers, in the order the Variables area shows them.

| Field     | Values                                                      | Does                         |
| --------- | ----------------------------------------------------------- | ---------------------------- |
| `name`    | letters, digits and `_`, 1 to 24 characters, no digit first | what blocks and text call it |
| `initial` | whole number from -1000000000 to 1000000000                 | its value when the game starts |

| `scope`   | `game` (left out), `player` or `copy`                       | how many values it has       |

A game variable has one value. A player variable has one for each player, and blocks use the current player's.
A copy variable has one for each copy of a template, and is only used inside blocks for a copy. A name is unique
regardless of case, and cannot be `player`, `players` or one of a formula's place names. At most 512 variables. A text item shows one by writing `{name}` in
its `text`, as described in [scene-format.md](scene-format.md). A program with no variables and text with
no `{name}` behave exactly as before.

### Sounds

`sounds` lists the sounds of the project, in the order the Sounds area shows them. Each is a 16-bit
source file Studio keeps in `assets/sounds`.

| Field    | Values                                                      | Does                       |
| -------- | ----------------------------------------------------------- | -------------------------- |
| `name`   | 1 to 40 letters, digits, `-` or `_`, unique regardless of case | what the blocks call it |
| `source` | `assets/sounds/<file>.wav`, a plain file in that folder      | the file the sound comes from |

Importing copies the file there. A WAV is kept as it is. MP3 and OGG are decoded once, when imported, and
kept as a 16-bit WAV, so a project builds on a copy of Studio that has no decoder. This decoding needs Qt
Multimedia; Studio built without it imports WAV only and says so. At most 64 sounds. The WAV can be PCM 8,
16, 24 or 32-bit, or 32-bit float, mono or stereo. A compressed WAV (such as MP3 inside WAV) is refused.
Deleting a sound removes it from the list and leaves its file, so undo can bring it back; a build only embeds
sounds on the list. Deleting a sound blocks still use asks first, and those blocks become problems to fix,
the same as a deleted variable.

### Rules

- `schemaVersion` is `5`, the version Studio saves. Version `5` adds formulas, players, templates and copies,
  behaviors, `setLook`, and player and copy variables; a file saved with `4` loads as it was. A file saved with `2` still loads and simply has no
  sounds. A file saved with `1` still loads too: it has no `variables` list, and its `addScore`,
  `setScore` and `ifScore` blocks become `changeVariable`, `setVariable` and `ifVariable` on a variable named
  `score` that starts at 0. A file with no score blocks gets no variables. The block types `addScore`,
  `setScore` and `ifScore` only exist in version 1 files. Sound, save, `playAnimation`, `flipItem`,
  `cameraFollow` and `setCameraPosition` blocks need version `3`, which also needs the `sounds` list, even if
  it is empty. Version `4` adds the `scenes` list in place of the top level `events`, and physics and scene
  blocks. A file saved with `3` or earlier loads as the logic of the project's first scene.
- `id` is 1 to 64 letters, digits, `-` or `_`, unique across the whole program, every scene included. It stays the same
  across saves, so an editor can refer to a block.
- Events go at the top level only. Actions go inside an event or inside a block that holds actions.
- `item` and `otherItem` are `id` values from `scene.json`. A build fails when one is missing or not
  in the scene, when an event's two items are the same, when `burstParticles` names an item that is
  not a Particles item, when `playAnimation` names an item without that animation, when `flipItem` names an
  item that is not an image, when `cameraFollow` names an item with Stays on screen on, or when a physics block
  names an item that is not a moving body. A file in that state can still be opened and saved, so it can be
  repaired.
- `scene` is the `id` of a scene of the project. A build fails when it is empty or names a scene that does not exist.
- `variable` is the `name` of a variable in `variables`. A build fails when it is empty or names a variable
  that does not exist, and when a text item shows a variable that does not exist. Deleting a variable in
  Studio asks first and leaves the blocks and text that used it to be repaired, for the same reason.
- `sound` is the `name` of a sound in `sounds`. A build fails when it is empty or names a sound not in the list.
- `x` and `y` are numbers from -1000000 to 1000000. `color` is `#RRGGBB`.
- `value` and `initial` are whole numbers from -1000000000 to 1000000000, except that `setVolume`'s `value` is
  a percentage from 0 to 100.
- Limits, per scene: 1024 events, 16384 blocks in total, blocks nested at most 12 levels deep counting the event as
  level 1. A file is at most 64 MB.

Problems are reported with the block's title and id, for example:
`The "Move item" block (block-2) uses the item "item-9", which is not in the scene. Choose an item
that exists, or delete this block.`

## How a program runs

Only the scene showing runs. Its `whenGameStarts` blocks run when it starts: at the beginning of the game for
the start scene, and whenever Go to scene or Restart scene enters it. Each frame the runtime first applies
`controllable` movement, then steps the physics (see [Physics](scene-format.md#physics)), then runs every event
except `whenGameStarts`, from the first event in the file to the last. Everything an earlier block changed is
seen by the later ones. `Go to scene` and `Restart scene` take effect after every event of the frame has run.
Entering a scene puts all its items back at their starting places, colors and animations and resets its
touching memory and camera; variables keep their values across scenes, and only the `whenGameStarts` blocks of a
scene change them.

Distances are applied each time a block runs, so a move inside `everyFrame` or `whileButtonHeld`
happens once per displayed frame and its speed follows the frame rate.

Physics events (`whenItemLands`, `whenItemHitsSolid`, `whenItemLeavesWorld`) run on the frame it happened,
including when it happened during a few fixed steps run in one frame. Standing still does not repeat a landing;
pushing against a wall reports one hit until the push stops. A moving body moved by a block continues from where
the block put it. A Hide block on a solid turns it off for collisions, and one on a body keeps it simulating.

Touching:

- Each item is an axis-aligned rectangle at its current position with its drawn size. For text that
  is the size given in [scene-format.md](scene-format.md).
- Rectangles whose edges only meet are not touching.
- A hidden item touches nothing, so hiding a coin when it is collected ends the touch. If it is shown
  again while still overlapping, the event runs again.
- An event runs once when a touch begins and again only after the items have separated. Items that
  overlap when the game starts run the event on the first frame.

Limits at run time:

- A `controllable` item always stays inside the world, or the screen if it has Stays on screen, even when a block moves it.
- Any other item stays within -1000000 to 1000000.
- A variable stops at the largest or smallest 32-bit whole number instead of overflowing.

Sound and saving never hold up a frame:

- `playSound`, `playMusic`, `stopMusic` and `setVolume` hand a short command to the mixer and return at once.
  If the queue of commands is full, which needs hundreds of sounds in one frame, the newest is dropped.
- The mixer has 16 voices, one for music and 15 for sounds. A sound that starts when all 15 are busy takes the
  voice that has played the longest. A `playSound` in `everyFrame` restarts a voice each frame.
- `saveVariables` copies the variables and `loadVariables` asks for a load, and both return at once. The work
  happens on a background thread: at most one save runs at a time and only the newest values are kept, so
  saving every frame is safe but wasteful. Loaded values replace the variables at the start of a later frame,
  so a block after `loadVariables` in the same event still sees the old values. A variable that was not in the
  saved file keeps its current value.
- A game with no sounds opens no audio port, and one with no save or load blocks never starts the save service.
- If audio cannot start or a save or load fails, for example because the user has no save data space, the block
  does nothing and the game keeps running. The game never stops because of a failed save.

The camera follows its item after the blocks of a frame have run. It never leaves the world, however far
its item goes, and its position does not change `x` or `y` of anything. Animations play for as long as the game
runs; an animation repeats, and `playAnimation` on the one already playing changes nothing.

A hidden item is not drawn but keeps its position, can still be moved and still follows the
controller if it is `controllable`. A hidden Particles item also stops making new particles; the ones
already alive keep aging out.

`burstParticles` on a one-shot Particles item (`looping` is `false` in [scene-format.md](scene-format.md))
means the item waits for the block instead of also playing when the game starts: the generated `OnStart`
calls `world.AwaitBurst(i)` for it.

## src/GeneratedLogic.hpp

The program above, with the example scene from [scene-format.md](scene-format.md), generates:

```cpp
// Generated by AnyDevKit Studio from logic.json. Do not edit.
#pragma once

#include "GeneratedScene.hpp"
#include "Input/Buttons.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace GeneratedLogic {

static_assert(GeneratedScene::Items.size() == 2, "GeneratedScene.hpp and GeneratedLogic.hpp do not match. Build the project again from Studio.");

inline constexpr std::size_t TouchEventCount = 1;

struct Memory {
    std::array<bool, TouchEventCount> touching{};
};

template <typename World>
void OnStart([[maybe_unused]] World& world) {
    // event-1: When the game starts
    world.SetVariable(0, 0); // block-1
}

template <typename World>
void OnFrame([[maybe_unused]] World& world, [[maybe_unused]] Memory& memory) {
    // event-2: While a button is held
    if (world.Held(Buttons::Cross)) {
        world.MoveBy(0, 4.0f, 0.0f); // block-2
    }
    // event-3: When two items touch
    {
        const bool touching = world.Touching(0, 1);
        const bool started = touching && !memory.touching[0];
        memory.touching[0] = touching;
        if (started) {
            world.ChangeVariable(0, 1); // block-3
            world.SetVisible(1, false); // block-4
        }
    }
    // event-4: Every frame
    if (world.Variable(0) >= 10) { // block-5
        world.SetColor(0, 1.0f, 0.301961f, 0.301961f); // block-6
    }
}

// Runs the blocks of one scene: when it starts, and each frame while it is showing.
template <typename World>
void OnSceneStart([[maybe_unused]] World& world, [[maybe_unused]] std::size_t scene) {
    OnStart(world);
}

template <typename World>
void OnSceneFrame([[maybe_unused]] World& world, [[maybe_unused]] Memory& memory, [[maybe_unused]] std::size_t scene) {
    OnFrame(world, memory);
}

}
```

With more than one scene the functions are numbered, `OnStart0`, `OnFrame0` and so on, and `OnSceneStart` and
`OnSceneFrame` pick one with a `switch`. Item indexes then count across every scene's items (see
[Budgets per scene](scene-format.md#budgets)), and touch memory is counted across every scene's touch events.

Items become indexes into `GeneratedScene::Items` and variables indexes into the runtime's table, so
item ids, item names and variable names never reach the code. The
`World` is the runtime's `SceneState`. A program with no events generates the same header as a project
with no `logic.json`. The header allocates nothing, and every block is a fixed amount of work, so a
frame's cost is bounded by the block limit.

## src/GeneratedVariables.hpp

This file holds what `scene.json` and `logic.json` say together: the variables' starting values, and how
to build each text item that shows a variable. It always exists, so the runtime can include it, and a
project with no variables gets a table with nothing in it and otherwise the same two headers as before.

With two variables, `score` and `lives` (which starts at 3), and the example scene's text set to
`Score: {score}`:

```cpp
// Generated by AnyDevKit Studio from logic.json and scene.json. Do not edit.
#pragma once

#include "GeneratedScene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace GeneratedVariables {

inline constexpr std::size_t VariableCount = 2;
inline constexpr std::array<std::int32_t, 2> InitialValues{{0, 3}}; // score, lives
inline constexpr std::array<std::uint32_t, 2> NameHashes{{3526332565u, 1526042452u}}; // how a saved value finds its variable
// Each player has their own value of these, and each copy of a template its own value of the copy variables.
inline constexpr std::size_t PlayerSlots = 4;
inline constexpr std::size_t PlayerVariableCount = 0;
inline constexpr std::array<std::int32_t, 0> PlayerInitialValues{};
inline constexpr std::size_t CopyVariableCount = 0;
inline constexpr std::array<std::int32_t, 0> CopyInitialValues{};
// What a save holds: the game variables, then each player's variables, player 1 first.
inline constexpr std::size_t SavedCount = 2;
inline constexpr std::array<std::int32_t, 2> SavedInitialValues{{0, 3}};
inline constexpr std::array<std::uint32_t, 2> SavedHashes{{3526332565u, 1526042452u}};
inline constexpr bool UsesSaveData = false;

// One piece of a text item that shows variables: plain text, or a variable's value when variable is not -1.
// scope is 0 for a game variable, 1 for a player variable and 2 for a copy variable; player is the player whose
// value it shows, or -1 for the first player.
struct Part {
    std::string_view text;
    std::int32_t variable;
    std::int32_t scope = 0;
    std::int32_t player = -1;
    bool letter = false; // shows the value as one letter: 1 to 26 as A to Z, 27 to 36 as 0 to 9, else a space
};

// A text item that shows variables. quads is how many characters it can ever need, sign and all digits included.
struct BoundText {
    std::size_t item, firstPart, partCount, quads;
};

inline constexpr std::array<Part, 2> Parts{{
    {"Score: ", -1},
    {"", 0},
}};
inline constexpr std::array<BoundText, 1> BoundTexts{{
    {1, 0, 2, 18},
}};

static_assert(GeneratedScene::Items.size() == 2, "GeneratedScene.hpp and GeneratedVariables.hpp do not match. Build the project again from Studio.");
static_assert(GeneratedScene::Items[1].text.size() == 18, "GeneratedScene.hpp and GeneratedVariables.hpp do not match. Build the project again from Studio.");

}
```

Each frame, after the blocks run, the runtime writes every bound text into a fixed buffer from the
variables' current values. Each one is sized for the widest number it could ever show (11 characters for
each variable, enough for the sign and ten digits of -2147483648), so that happens with no allocation.

A project that started with an older Studio gets this game code at its next build that uses a variable.
Studio updates the project's runtime files then and keeps the previous ones in `.studio-backup`, the same
way as Add the scene editor. A project that uses no variables is left as it is.

## Sound files

Each sound in `sounds` becomes one raw file, `src/GeneratedSounds/sound-N.pcm`, numbered in list order. It is
the game's only audio format, with no header:

- 48000 Hz, 2 channels, signed 16-bit little-endian samples, left then right, so one frame is 4 bytes.
- Mono sources are copied to both channels. Other sample rates are resampled with linear interpolation.

`src/GeneratedSounds.cpp` embeds those files with `.incbin` and defines the `Sounds` table declared in
`src/GeneratedSounds.hpp`. As with images, unchanged files are left alone so an unchanged project does not
recompile, and a project with no sounds still gets an empty table. The project's sounds together can use at
most 256 MB of PCM, about 23 minutes.

## Save files

A game with save or load blocks stores its variables through libSceSaveData, in a save named
`StudioVariables`, in the file `variables.bin` (written through a temporary file and renamed, then committed).
The file is, little endian:

| Bytes        | Holds                                                         |
| ------------ | ------------------------------------------------------------- |
| 4            | `ADKS`                                                        |
| 4            | format version, 1                                             |
| 4            | count                                                         |
| count x 8    | each variable's name hash (FNV-1a of its name) and its value  |
| 4            | FNV-1a of all of the above                                    |

`GeneratedVariables::NameHashes` holds the hashes. A value finds its variable by name hash, so a later
version of a game can reorder, add or remove variables without losing saved values. A damaged file, or one with
an unknown version, is ignored and changes nothing.
