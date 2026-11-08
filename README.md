# AnyDevKit Studio

An open source desktop studio for making PS5 homebrew games and apps with
[AnyDevKit](https://github.com/boykopovar/AnyDevKit), on macOS, Windows and Linux.

AnyDevKit is a command line SDK. It compiles C++ and GLSL and packages the result for PS5.
Studio puts a visual workspace on top of it, so people can open a project, build it and
eventually create a whole game without living in a terminal.

> **Status: early development.** Nothing is ready to download yet. This repository is public from
> day one so the work can happen in the open.

## What it will do

| Stage | Goal | State |
|---|---|---|
| Project workspace | Open an `adk init` project and see its files and PS5 metadata | In progress |
| One button build | Configure, compile and create a PS5 package with a live log | In progress |
| Preview | Run the project on the developer's computer without a console | Planned |
| Scene editor | Place sprites, text and sounds on a 16:9 stage | Planned |
| Game runtime | A small C++ engine on top of AGC, the PS5 graphics API, starting with 2D | Planned |
| Scripting | Game logic in Lua | Planned |

## How it is built

- **C++20** shares code with the engine runtime and fits the AnyDevKit toolchain.
- **Qt 6 and QML** give one GPU drawn interface that uses Metal on macOS, Direct3D on Windows and
  Vulkan on Linux.
- **A UI-free core library** holds projects and builds, and has its own tests.
- **`adk` stays a separate program.** Studio launches it rather than linking it, which keeps the
  two projects and their licenses independent. A crash in a build or a game cannot take the editor
  down with it.

## Contributing

Fork it, build on it and send pull requests. Ideas and bug reports are welcome in the issues.
Build instructions will land here together with the first working version.

## Legal

Licensed under the [MIT License](LICENSE). AnyDevKit is a separate project under GPL-2.0, and
Studio only runs it as an external tool.

This is an independent project, not affiliated with or endorsed by Sony Interactive Entertainment.
PlayStation and PS5 are trademarks of their respective owner. Packages made with AnyDevKit are
homebrew and cannot be published on PlayStation Store.
