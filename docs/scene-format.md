# Scene format

Studio's drag and drop editor saves one scene per project in `scene.json`, at the project root.
Before each build Studio turns it into `src/GeneratedScene.hpp` and `src/GeneratedImages.cpp`, which
the game runtime compiles in.

The screen is always 1920x1080. The origin is the top left corner and `y` grows downwards. A scene can
also have a [world larger than the screen](#world-and-camera) that a camera moves over.

## scene.json

```json
{
  "schemaVersion": 5,
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

- `schemaVersion` is `5`, the version Studio saves. Older scenes still load: `1` cannot hold image items,
  `1` and `2` cannot hold particles items, before `4` a brace in `text` was only a brace, so `{` and
  `}` in an older file are read as `{{` and `}}`, and before `5` a scene cannot hold sprite sheets,
  tilemaps, a world or a camera. A scene holds at most 256 items, drawn in list order, later items on top.
- `id` values are unique, non-empty strings. `name` is only for the editor.
- Hiding an item with the eye in the Layers panel only hides it on the stage while editing. It is not
  saved here and the game does not see it. The Logic tab's show and hide blocks control the game.
- Colors are `#RRGGBB`. `image` items have no color.
- Positions and sizes are numbers. `rectangle`, `image` and `particles` need `width` and `height` above 0.
- `text` holds 1 to 64 printable ASCII characters (U+0020 to U+007E), which can show variables; see
  [Text that shows variables](#text-that-shows-variables). Its `scale` is a whole number from 1 to 40.
  Each character is a 5x7 pixel cell advancing 6 cells, so the drawn width is `(length * 6 - 1) * scale`
  and the height is `7 * scale`, where `length` is the characters it shows right now.
- `image` draws the PNG file `source` stretched to `width` by `height`, respecting its transparency, or
  one frame of it when it is a [sprite sheet](#sprite-sheets).
  `source` is a project-relative path that starts with `assets/` and ends in `.png`, with `/`
  separators and no `.` or `..` parts. The file must exist inside the project when the scene is built,
  and neither it nor a folder on the way to it may be a link. Items that name the same file share one
  texture. An image can be at most 4096 pixels on each side, and all of a scene's different images
  together at most 256 MB once decoded (4 bytes per pixel).
- A `controllable` item moves with the controller's left stick or D-pad at 600 units per second and
  stays inside the world, or inside the screen when it is `screenFixed`. Without a world the world is the
  screen, so nothing changes for older scenes.
- `screenFixed` is optional. An item with `"screenFixed": true` is placed on the screen, for example a
  score, and stays there as the camera moves.
- `particles` draws an effect with blocky particles snapped to a pixel grid. `x`, `y`, `width` and
  `height` are the area the effect happens in. See [Particles](#particles).

### Text that shows variables

`text` can show the variables of [logic.json](blocks-format.md) as they change while the game runs.

| In `text`  | Shows                                                       |
| ---------- | ----------------------------------------------------------- |
| `{score}`  | the value of the variable named `score`, as a whole number  |
| `{{`       | a literal `{`                                               |
| `}}`       | a literal `}`                                               |

A brace on its own, or `{}`, or `{` around something that is not a variable name, is an error in the
scene and is refused with a message that says how to show a brace. A variable counts as the characters
typed for it towards the 64, and `{{` and `}}` count as one each. A build fails when a text item
shows a variable that does not exist, naming the item and the variable, with an empty `logic.json` or
none at all counted as having no variables.

A text item reserves room for the widest number each variable could show, 11 characters for the sign and
all ten digits of -2147483648. That is its length for the quad budget below. On the stage a variable
appears with its `initial` value. The game rebuilds the text from the current values each frame with no
allocation. Text with no variables behaves exactly as it did.

### World and camera

```json
{
  "schemaVersion": 5,
  "background": "#151C3A",
  "world": { "width": 7680, "height": 2160 },
  "camera": { "x": 0, "y": 0 },
  "items": []
}
```

`world` is the area the camera can show, in whole units, from 1920x1080 up to 32768 on each side. `camera`
is where the camera starts (its top left corner) and must keep the 1920x1080 screen inside the world.
Both are optional, and a scene saves them only when they differ from the defaults, the stage itself.
Items, apart from `screenFixed` ones, are placed in the world and may lie past the edge of the first
screen. The Logic tab's camera blocks move the camera while the game runs.

A larger world only changes where the game looks: tiles and sprites off the screen are not drawn at all,
and pixel art lands on whole screen pixels so it does not shimmer as the camera moves.

### Sprite sheets

```json
{
  "id": "item-5",
  "type": "image",
  "name": "Hero",
  "x": 96,
  "y": 40,
  "width": 64,
  "height": 64,
  "controllable": true,
  "source": "assets/hero.png",
  "pixelated": true,
  "flipX": false,
  "sheet": {
    "frameWidth": 16,
    "frameHeight": 16,
    "animation": "walk",
    "animations": [
      { "name": "idle", "frames": [0], "fps": 1 },
      { "name": "walk", "frames": [0, 1, 2, 3], "fps": 8 }
    ]
  }
}
```

An `image` item with `sheet` is cut into frames: `frameWidth` by `frameHeight` pixels, numbered from 0, left
to right and then top to bottom. Without `sheet` it is one plain picture, exactly as before.

| Field           | Values                                  | Beginners read it as                                 |
| --------------- | --------------------------------------- | ---------------------------------------------------- |
| `frameWidth`, `frameHeight` | whole numbers from 1 to 4096 | how big one frame is, in pixels of the file          |
| `pixelated`     | `true` or `false`                       | draws with nearest-neighbour scaling, sharp blocks instead of blur. Also for a plain picture |
| `flipX`         | `true` or `false`                       | starts mirrored left to right                        |
| `animations`    | at most 16 named animations            | each `name` is letters, digits and `_`, 1 to 24 long, unique; each has `frames`, 1 to 256 frame numbers, and `fps`, whole number from 1 to 60 |
| `animation`     | one of the `animations` names          | which one plays when the game starts. Empty means the first. An animation repeats |

Frame numbers are checked against the picture when the scene is built: a frame the sheet does not have
is an error naming it. Frames play one per `1/fps` seconds of game time. The game draws a sheet's frame
as one textured quad, so a sprite costs the same as any image.

### Tilemaps

```json
{
  "id": "item-6",
  "type": "tilemap",
  "name": "Ground",
  "x": 0,
  "y": 0,
  "controllable": false,
  "source": "assets/tiles.png",
  "tileWidth": 16,
  "tileHeight": 16,
  "tileScale": 4,
  "columns": 120,
  "rows": 34,
  "tiles": ["-1*120", "...one string per row..."],
  "solidTiles": [0, 3]
}
```

A tilemap is a layer of tiles from one picture, the tileset, cut into `tileWidth` by `tileHeight` pieces
numbered like a sprite sheet's frames. Several tilemaps are layers: use the Layers panel to order them, and
drawing follows the order of `items` as for any other item.

| Field        | Values                      | Beginners read it as                                       |
| ------------ | --------------------------- | ---------------------------------------------------------- |
| `source`     | same rules as an image      | the tileset picture                                        |
| `tileWidth`, `tileHeight` | whole numbers from 4 to 256 | size of one tile, in pixels of the file   |
| `tileScale`  | whole number from 1 to 16   | how much bigger each tile is drawn: a tile takes `tileWidth * tileScale` world units |
| `columns`, `rows` | whole numbers from 1 to 512 | the size of the map in tiles. The drawn size is `columns * tileWidth * tileScale` by `rows * tileHeight * tileScale` |
| `tiles`      | one text per row, top row first | each is comma separated `tile` or `tile*count` runs, adding up to `columns`. A tile is -1 for empty or a number from 0 to 65535, for example `"-1*3,5*2,2"` |
| `solidTiles` | sorted tile numbers, no repeats | which tiles are solid. Kept for collisions; the game does not use it yet |

The picture is checked against the map when the scene is built: a tile number the tileset does not have is an
error naming it. Tilemaps always draw with nearest-neighbour sampling. Each tile is `tileScale` units
wide, so at a `tileScale` of 4 with 16 pixel tiles the map is drawn in 64 unit cells.

A scene holds at most 1048576 tiles across all its tilemaps. On screen a tilemap draws only what is visible:
at most one quad per cell in view, one extra row and column for a camera between grid lines, counted
against the same 16000 quad budget as particles.

### Budgets

A scene with tilemaps or particles can draw at most 16000 quads in total, and a scene with tilemaps or sprite
sheets can use at most 64 draw calls. Consecutive items that use the same picture with the same sampling are
one draw call, so tile layers on one tileset cost one. A scene over either limit is refused with a message
saying which limit it hit. The limits are worked out from the screen, not from the size of the map or the
world, so a huge world costs nothing per frame beyond its tile data.

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

enum class Kind : std::uint8_t { Rectangle, Text, Image, Particles, Tilemap };

struct Item {
    Kind kind;
    float x, y, width, height;
    float red, green, blue;
    float textScale;
    std::string_view text;
    bool controllable;
    std::int32_t image = -1; // index into Images for an image item
    std::int32_t particles = -1; // index into ParticleEffects for a particles item
    std::int32_t tilemap = -1; // index into Tilemaps for a tilemap item
    std::int32_t sprite = -1; // index into Sprites for an image that is a sprite sheet
    bool screenFixed = false; // stays at the same place on the screen as the camera moves
    bool pixelated = false; // drawn with nearest-neighbour sampling, on whole pixels
    bool flipped = false; // drawn mirrored left to right when the game starts
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

// One animation of a sprite. Its frames are AnimationFrames[firstFrame] onward, frameCount of them.
struct Animation {
    std::uint32_t firstFrame, frameCount, framesPerSecond;
};

// A sprite sheet's layout, and which of Animations are its own.
struct Sprite {
    std::uint32_t frameWidth, frameHeight, columns, rows, firstAnimation, animationCount, startAnimation;
};

// A tilemap. Its cells live in TilemapCells; solid tiles are SolidTiles[firstSolid] onward, solidCount of them.
struct Tilemap {
    std::uint32_t columns, rows, tileWidth, tileHeight, cellWidth, cellHeight, tilesetColumns;
    std::int32_t image;
    std::uint32_t reservedQuads, firstSolid, solidCount;
};

inline constexpr float WorldWidth = 1920.0f;
inline constexpr float WorldHeight = 1080.0f;
inline constexpr float CameraStartX = 0.0f;
inline constexpr float CameraStartY = 0.0f;
inline constexpr std::array<float, 3> Background{0.082353f, 0.109804f, 0.227451f};
inline constexpr std::size_t ImageCount = 1;
extern const std::array<Image, ImageCount> Images; // defined in src/GeneratedImages.cpp
inline constexpr std::size_t ParticleEffectCount = 1;
inline constexpr std::size_t ParticleCapacity = 600; // the sum of every amount, sized before the first frame
inline constexpr std::array<ParticleEffect, 1> ParticleEffects{{
    {0, 1, 600, 3025279935u, 0.5f, 1.0f, 1.0f, 12.0f, true},
}};
inline constexpr std::size_t SpriteCount = 0;
inline constexpr std::array<Sprite, 0> Sprites{};
inline constexpr std::array<Animation, 0> Animations{};
inline constexpr std::array<std::uint16_t, 0> AnimationFrames{};
inline constexpr std::size_t TilemapCount = 0;
inline constexpr std::array<Tilemap, 0> Tilemaps{};
inline constexpr std::array<std::uint16_t, 0> SolidTiles{};
extern const std::array<const std::int32_t*, TilemapCount> TilemapCells; // defined in src/GeneratedImages.cpp, columns * rows cells each
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
follows another `?`, because `??=` and similar sequences are trigraphs. A text item's `text` here is what
it shows when nothing changes: `{{` and `}}` are written as one brace, and each `{name}` is written as
`00000000000`, so the length of `text` is always the quads the item reserves. An empty scene is `inline constexpr std::array<Item, 0> Items{};`, and a scene without
particles has an empty `ParticleEffects` table.

`WorldWidth`, `WorldHeight`, `CameraStartX` and `CameraStartY` come from `world` and `camera`. A sprite sheet
item points into `Sprites` and through it into `Animations` and `AnimationFrames`. A tilemap item points into
`Tilemaps`, where `reservedQuads` is the most tiles of that map the screen can show, and `firstSolid` and
`solidCount` give its slice of `SolidTiles`. Items that do not use these features keep the shorter lines
shown above. A scene without tilemaps or sprites has empty tables.

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

Each tilemap's cells become one raw file, `src/GeneratedImages/tilemap-N.tiles`, numbered in the order the
tilemaps appear in the scene. It holds `columns * rows` little-endian 32-bit tile numbers, row by row. They
reach the program with the same `.incbin` method and are read through `TilemapCells`, one pointer per map.
