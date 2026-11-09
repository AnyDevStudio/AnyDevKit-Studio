# Scene format

Studio's drag and drop editor saves one scene per project in `scene.json`, at the project root.
Before each build Studio turns it into `src/GeneratedScene.hpp` and `src/GeneratedImages.cpp`, which
the game runtime compiles in.

The stage is always 1920x1080. The origin is the top left corner and `y` grows downwards.

## scene.json

```json
{
  "schemaVersion": 3,
  "background": "#151C3A",
  "items": [
    {
      "id": "item-1",
      "type": "rectangle",
      "name": "Player",
      "x": 120,
      "y": 300,
      "width": 160,
      "height": 160,
      "color": "#FFD23F",
      "controllable": true
    },
    {
      "id": "item-2",
      "type": "text",
      "name": "Title",
      "x": 400,
      "y": 96,
      "scale": 12,
      "text": "Hello PS5",
      "color": "#F3F1E2",
      "controllable": false
    },
    {
      "id": "item-3",
      "type": "image",
      "name": "Logo",
      "x": 900,
      "y": 500,
      "width": 256,
      "height": 128,
      "source": "assets/logo.png",
      "controllable": false
    },
    {
      "id": "item-4",
      "type": "particles",
      "name": "Finale",
      "x": 300,
      "y": 100,
      "width": 1320,
      "height": 900,
      "color": "#FFD23F",
      "preset": "fireworks",
      "palette": "festival",
      "amount": 600,
      "spread": 50,
      "speed": 100,
      "gravity": 100,
      "pixelSize": 12,
      "looping": true,
      "controllable": false
    }
  ]
}
```

Rules:

- `schemaVersion` is `3`, the version Studio saves. Scenes saved with `1` or `2` still load: `1` cannot
  hold image items, and neither can hold particles items. A scene holds at most 256 items, drawn in
  list order, later items on top.
- `id` values are unique, non-empty strings. `name` is only for the editor.
- Hiding an item with the eye in the Layers panel only hides it on the stage while editing. It is not
  saved here and the game does not see it. The Logic tab's show and hide blocks control the game.
- Colors are `#RRGGBB`. `image` items have no color.
- Positions and sizes are numbers. `rectangle`, `image` and `particles` need `width` and `height` above 0.
- `text` holds 1 to 64 printable ASCII characters (U+0020 to U+007E). Its `scale` is a whole number
  from 1 to 40. Each character is a 5x7 pixel cell advancing 6 cells, so the drawn width is
  `(length * 6 - 1) * scale` and the height is `7 * scale`.
- `image` draws the PNG file `source` stretched to `width` by `height`, respecting its transparency.
  `source` is a project-relative path that starts with `assets/` and ends in `.png`, with `/`
  separators and no `.` or `..` parts. The file must exist inside the project when the scene is built,
  and neither it nor a folder on the way to it may be a link. Items that name the same file share one
  texture. An image can be at most 4096 pixels on each side, and all of a scene's different images
  together at most 256 MB once decoded (4 bytes per pixel).
- A `controllable` item moves with the controller's left stick or D-pad at 600 units per second and
  stays inside the stage.
- `particles` draws an effect with blocky particles snapped to a pixel grid. `x`, `y`, `width` and
  `height` are the area the effect happens in. See [Particles](#particles).

### Particles

| Field       | Values                                                   | Beginners read it as                                   |
| ----------- | -------------------------------------------------------- | ------------------------------------------------------ |
| `preset`    | `fireworks`, `fountain`, `sparks`, `snow`, `rain`        | what the effect is                                     |
| `palette`   | `color`, `festival`, `fire`, `ice`                       | which colors it uses. `color` means the `color` field  |
| `color`     | `#RRGGBB`                                                | the color when `palette` is `color`                    |
| `amount`    | whole number from 1 to 8000                              | the most particles alive at once                       |
| `spread`    | whole number from 0 to 100                               | how widely it fans out                                 |
| `speed`     | whole number from 10 to 300 (100 is natural)             | how fast it moves                                      |
| `gravity`   | whole number from -300 to 300 (100 is natural)           | how hard it falls. Negative rises instead              |
| `pixelSize` | whole number from 2 to 32                                | how big its blocks are, in stage units                 |
| `looping`   | `true` or `false`                                        | keeps going, or plays once when the game starts        |

Picking a preset in the editor sets all of these to the preset's look. Snow and Rain cover the whole
stage; the others take a smaller area. A one-shot item a block bursts waits for that block instead of
also playing when the game starts (see [docs/blocks-format.md](blocks-format.md)).

A scene that has particles draws at most 16000 quads in total: one for the background, one per
rectangle and image, one per text character and `amount` for each particles item. A scene over this
is refused with a message saying which limit it hit. Scenes with no particles are not limited, so
older files keep working.

Each particle lives in its own slot, one slot per `amount`, and the whole budget is reserved before
the first frame. When a burst would need more slots than are free it is clipped instead of overrunning.

## src/GeneratedScene.hpp

```cpp
// Generated by AnyDevKit Studio from scene.json. Do not edit.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace GeneratedScene {

enum class Kind : std::uint8_t { Rectangle, Text, Image, Particles };

struct Item {
    Kind kind;
    float x, y, width, height;
    float red, green, blue;
    float textScale;
    std::string_view text;
    bool controllable;
    std::int32_t image = -1; // index into Images for an image item
    std::int32_t particles = -1; // index into ParticleEffects for a particles item
};

// Row by row, top row first, 4 bytes per pixel: red, green, blue, alpha, with alpha already applied.
struct Image {
    const std::uint8_t* pixels;
    std::uint32_t width, height;
};

// One particles item. preset and palette are the numbers of Particles::Preset and Particles::Palette
// in the runtime. spread, speed and gravity are the percentages of scene.json divided by 100.
struct ParticleEffect {
    std::uint8_t preset, palette;
    std::uint32_t amount, seed;
    float spread, speed, gravity, pixelSize;
    bool looping;
};

inline constexpr std::array<float, 3> Background{0.082353f, 0.109804f, 0.227451f};
inline constexpr std::size_t ImageCount = 1;
extern const std::array<Image, ImageCount> Images; // defined in src/GeneratedImages.cpp
inline constexpr std::size_t ParticleEffectCount = 1;
inline constexpr std::size_t ParticleCapacity = 600; // the sum of every amount, sized before the first frame
inline constexpr std::array<ParticleEffect, 1> ParticleEffects{{
    {0, 1, 600, 3025279935u, 0.5f, 1.0f, 1.0f, 12.0f, true},
}};
inline constexpr std::array<Item, 4> Items{{
    {Kind::Rectangle, 120.0f, 300.0f, 160.0f, 160.0f, 1.0f, 0.823529f, 0.247059f, 0.0f, "", true},
    {Kind::Text, 400.0f, 96.0f, 636.0f, 84.0f, 0.952941f, 0.945098f, 0.886275f, 12.0f, "Hello PS5", false},
    {Kind::Image, 900.0f, 500.0f, 256.0f, 128.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", false, 0},
    {Kind::Particles, 300.0f, 100.0f, 1320.0f, 900.0f, 1.0f, 0.823529f, 0.247059f, 0.0f, "", false, -1, 0},
}};

}
```

`seed` is the FNV-1a hash of the item's `id`, so a particles item plays the same way on every run. Color
channels are the `#RRGGBB` bytes divided by 255. For text items `width` and `height` hold the
drawn size from the rule above. In a text literal `"` and `\\` are escaped, and so is every `?` that
follows another `?`, because `??=` and similar sequences are trigraphs. An empty scene is `inline constexpr std::array<Item, 0> Items{};`, and a scene without
particles has an empty `ParticleEffects` table.

## src/GeneratedImages

Each distinct image becomes one raw file, `src/GeneratedImages/image-N.rgba`, numbered in the order
the scene first uses it. It holds `width * height * 4` bytes of RGBA8, top row first and with no
padding. Color channels are already multiplied by alpha, because filtering straight-alpha pixels
leaks the color of transparent pixels into soft edges.

`src/GeneratedImages.cpp` pulls those files into the program with the assembler's `.incbin` directive
and defines the `Images` table. Pixels are embedded this way, rather than as a C array in generated
code, to keep compile time flat: a 1024x1024 image is 4 MB of pixels but would be about 16 MB of
source for the compiler to parse into a giant initializer, while `.incbin` costs only the link.
Studio leaves a generated file untouched when its contents have not changed, so an unchanged scene
does not recompile. A scene with no images still gets the file, with an empty table.
