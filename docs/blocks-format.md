# Blocks format

Game logic built from jigsaw style blocks is saved in `logic.json`, at the project root next to
`scene.json`. Before each build Studio turns it into `src/GeneratedLogic.hpp` and
`src/GeneratedVariables.hpp`, which the game runtime compiles in. There is no interpreter and no script:
each block becomes ordinary C++.

A project without `logic.json` builds with an empty logic header and behaves as it did before blocks
existed. The API for editors is in [src/core/Blocks.h](../src/core/Blocks.h).

## logic.json

A program is a list of variables and a tree of blocks. Events are the top level, each holding actions,
and an `ifVariable` action can hold more actions.

```json
{
  "schemaVersion": 3,
  "variables": [
    { "name": "score", "initial": 0 }
  ],
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
```

### Events

| `type`            | Title                  | Fields                | Runs                                                  |
| ----------------- | ---------------------- | --------------------- | ----------------------------------------------------- |
| `whenGameStarts`  | When the game starts   |                       | once, before the first frame                          |
| `whileButtonHeld` | While a button is held | `button`              | every frame the button is down                        |
| `everyFrame`      | Every frame            |                       | every frame                                           |
| `whenItemsTouch`  | When two items touch   | `item`, `otherItem`   | once when the two items start touching                |

`button` is one of `cross`, `circle`, `square`, `triangle`, `dpadUp`, `dpadDown`, `dpadLeft`,
`dpadRight`. Every event needs an `actions` list, which may be empty.

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
| `playAnimation` | Play animation on item | `item`, `animation` | starts that animation of a sprite sheet, or keeps playing it if it is already on. The item must be an image with frames, and `animation` one of its names |
| `flipItem` | Flip item | `item`, `flipped` | mirrors an image left to right (`true`) or back (`false`). The item must be an image |
| `cameraFollow` | Camera follows item | `item` | keeps the camera centered on the item, every frame, as far as the world allows. The item must not have Stays on screen turned on |
| `setCameraPosition` | Set camera position | `x`, `y` | puts the camera's top left corner at `x`, `y`, clamped to the world, and stops following any item |

`comparison` is one of `equal`, `notEqual`, `less`, `lessOrEqual`, `greater`, `greaterOrEqual`.

### Variables

`variables` lists the program's named whole numbers, in the order the Variables area shows them.

| Field     | Values                                                      | Does                         |
| --------- | ----------------------------------------------------------- | ---------------------------- |
| `name`    | letters, digits and `_`, 1 to 24 characters, no digit first | what blocks and text call it |
| `initial` | whole number from -1000000000 to 1000000000                 | its value when the game starts |

A name is unique regardless of case. At most 64 variables. A text item shows one by writing `{name}` in
its `text`, as described in [scene-format.md](scene-format.md). A program with no variables and text with
no `{name}` behave exactly as before.

### Rules

- `schemaVersion` is `3`, the version Studio saves. A file saved with `1` still loads: it has no `variables`
  list, and its `addScore`, `setScore` and `ifScore` blocks become `changeVariable`, `setVariable` and
  `ifVariable` on a variable named `score` that starts at 0. A file with no score blocks gets no variables.
  The block types `addScore`, `setScore` and `ifScore` only exist in version 1 files. A file saved with `2`
  still loads but cannot use `playAnimation`, `flipItem`, `cameraFollow` or `setCameraPosition`.
- `id` is 1 to 64 letters, digits, `-` or `_`, unique across the whole program. It stays the same
  across saves, so an editor can refer to a block.
- Events go at the top level only. Actions go inside an event or inside `ifVariable`.
- `item` and `otherItem` are `id` values from `scene.json`. A build fails when one is missing or not
  in the scene, when an event's two items are the same, when `burstParticles` names an item that is
  not a Particles item, when `playAnimation` names an item without that animation, when `flipItem` names an
  item that is not an image, or when `cameraFollow` names an item with Stays on screen on. A file in that state
  can still be opened and saved, so it can be repaired.
- `variable` is the `name` of a variable in `variables`. A build fails when it is empty or names a variable
  that does not exist, and when a text item shows a variable that does not exist. Deleting a variable in
  Studio asks first and leaves the blocks and text that used it to be repaired, for the same reason.
- `x` and `y` are numbers from -1000000 to 1000000. `color` is `#RRGGBB`.
- `value` and `initial` are whole numbers from -1000000000 to 1000000000.
- Limits: 128 events, 2048 blocks in total, blocks nested at most 8 levels deep counting the event as
  level 1, and a file of at most 4 MB.

Problems are reported with the block's title and id, for example:
`The "Move item" block (block-2) uses the item "item-9", which is not in the scene. Choose an item
that exists, or delete this block.`

## How a program runs

Each frame the runtime first applies `controllable` movement, then runs every event except
`whenGameStarts`, from the first event in the file to the last. Everything an earlier block changed
is seen by the later ones.

Distances are applied each time a block runs, so a move inside `everyFrame` or `whileButtonHeld`
happens once per displayed frame and its speed follows the frame rate.

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

}
```

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

// One piece of a text item that shows variables: plain text, or a variable's value when variable is not -1.
struct Part {
    std::string_view text;
    std::int32_t variable;
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
