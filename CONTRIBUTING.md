# Contributing

Fork the repository, make your change on a branch and open a pull request. Small, focused pull
requests are the easiest to review. If you plan something large, open an issue first so we can
agree on the direction.

## Build and test

You need CMake 3.22 or newer, a C++20 compiler and Qt 6.8 or newer with Qt Declarative.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Where things go

- `src/core`: projects and builds. It uses only Qt Core and has no interface code, so it can be
  tested without a window. Behaviour changes here need a test in `tests/tst_core.cpp`.
- `src/gamepage`: the Game page's `param.json` edits and image work. It uses Qt Gui, so it is separate
  from `src/core`. Tests are in `tests/tst_gamepage.cpp`.
- `src/setup`: the guided first-run setup: tool checks, the AnyDevKit download, safe unpacking and
  `adk setup`. It uses Qt Network, so it is separate from `src/core`. Tests are in `tests/tst_setup.cpp`
  and use a fake HTTP layer and fake tools, so they need no network.
- `src/export`: turns a scene into the project's generated files. It decodes images, so unlike
  `src/core` it uses Qt Gui. Behaviour changes need a test in `tests/tst_scene_export.cpp`.
- `src/app`: the application and the bridge between C++ and QML.
- `src/app/qml`: the interface. Colors, fonts and motion timings live in `Theme.qml`.
- `runtime/SceneGame`: the PS5 game code Studio adds to a project. It compiles the headers exported
  from the scene and from the block logic, as described in [docs/scene-format.md](docs/scene-format.md)
  and [docs/blocks-format.md](docs/blocks-format.md). `include/Scene/Particles.hpp.in` is also compiled
  by the stage preview and `tests/tst_particles.cpp`, so it must stay free of `@NAME@` and Qt.

## Rules worth knowing

- Studio runs `adk` as a separate program and must not link AnyDevKit code. AnyDevKit is GPL-2.0
  and Studio is MIT.
- Anything a build deletes must be inside the project folder. `BuildRunner` enforces this.
- Interface motion must respect `Theme.reducedMotion`, and controls need a visible keyboard focus.
- Write messages for the person using Studio: say what happened and what to do next.
- Use commit messages like `fix(core): keep build output inside the project`.
