# Blocks format

Game logic built from jigsaw style blocks is saved in `logic.json`, at the project root next to
`scene.json`. Before each build Studio turns it into `src/GeneratedLogic.hpp`, which the game runtime
compiles in. There is no interpreter and no script: each block becomes ordinary C++.

A project without `logic.json` builds with an empty logic header and behaves as it did before blocks
existed. The API for editors is in [src/core/Blocks.h](../src/core/Blocks.h).

## logic.json

A program is a tree. Events are the top level, each holding actions, and an `ifScore` action can hold
more actions.

```json
{
  "schemaVersion": 1,
  "events": [
    {
      "id": "event-1",
      "type": "whenGameStarts",
      "actions": [
        { "id": "block-1", "type": "setScore", "value": 0 }
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
        { "id": "block-3", "type": "addScore", "value": 1 },
        { "id": "block-4", "type": "hide", "item": "item-2" }
      ]
    },
    {
      "id": "event-4",
      "type": "everyFrame",
      "actions": [
        {
          "id": "block-5",
          "type": "ifScore",
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
| `addScore`    | Add to score      | `value`                 | adds `value`, which can be negative    |
| `setScore`    | Set score         | `value`                 | replaces the score                     |
| `ifScore`     | If score          | `comparison`, `value`, `actions` | runs `actions` when the score compares true |
| `burstParticles` | Burst particles from item | `item` | plays that Particles item once, at its current position, on top of anything already running. The item must be a Particles item |

`comparison` is one of `equal`, `notEqual`, `less`, `lessOrEqual`, `greater`, `greaterOrEqual`.

### Rules

- `schemaVersion` is `1`.
- `id` is 1 to 64 letters, digits, `-` or `_`, unique across the whole program. It stays the same
  across saves, so an editor can refer to a block.
- Events go at the top level only. Actions go inside an event or inside `ifScore`.
- `item` and `otherItem` are `id` values from `scene.json`. A build fails when one is missing or not
  in the scene, when an event's two items are the same, or when `burstParticles` names an item that is
  not a Particles item. A file in that state can still be opened and saved, so it can be repaired.
- `x` and `y` are numbers from -1000000 to 1000000. `color` is `#RRGGBB`.
- `value` is a whole number from -1000000000 to 1000000000.
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

- A `controllable` item always stays inside the stage, even when a block moves it.
- Any other item stays within -1000000 to 1000000.
- The score stops at the largest or smallest 32-bit whole number instead of overflowing.

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
    world.SetScore(0); // block-1
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
            world.AddScore(1); // block-3
            world.SetVisible(1, false); // block-4
        }
    }
    // event-4: Every frame
    if (world.Score() >= 10) { // block-5
        world.SetColor(0, 1.0f, 0.301961f, 0.301961f); // block-6
    }
}

}
```

Items become indexes into `GeneratedScene::Items`, so item ids and names never reach the code. The
`World` is the runtime's `SceneState`. A program with no events generates the same header as a project
with no `logic.json`. The header allocates nothing, and every block is a fixed amount of work, so a
frame's cost is bounded by the block limit.
