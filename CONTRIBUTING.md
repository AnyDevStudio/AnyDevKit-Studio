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
- `src/app`: the application and the bridge between C++ and QML.
- `src/app/qml`: the interface. Colors, fonts and motion timings live in `Theme.qml`.
- `runtime/SceneGame`: the PS5 game code Studio adds to a project. It compiles the header exported
  from the scene, as described in [docs/scene-format.md](docs/scene-format.md).

## Rules worth knowing

- Studio runs `adk` as a separate program and must not link AnyDevKit code. AnyDevKit is GPL-2.0
  and Studio is MIT.
- Anything a build deletes must be inside the project folder. `BuildRunner` enforces this.
- Interface motion must respect `Theme.reducedMotion`, and controls need a visible keyboard focus.
- Write messages for the person using Studio: say what happened and what to do next.
- Use commit messages like `fix(core): keep build output inside the project`.
