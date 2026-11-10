> **Alpha.** Expect rough edges. Scenes with images, text, shapes, tilemaps, sprite sheets, controller movement
> and logic blocks have run in AnyPS5 on a Mac. The new blocks of this version have been played through by
> a scripted test on the real game runtime, not yet on a PS5 or in AnyPS5. Windows and Linux builds of games
> are new: they build, but have not been run on Windows or Linux by us. Nothing has been run on a real PS5, and sound and saving build
> for PS5 but have not yet been confirmed working there.

New in this version:

- **Build for Windows and Linux.** Next to the PS5 package, the build menu makes a Windows `.zip` and a Linux
  `.tar.gz` of your game. Studio installs the Windows or Linux libraries the first time. On a Mac this needs
  LLVM from Homebrew (`brew install llvm lld`). These builds run on AnyPS5's desktop libraries; Studio cannot make
  a macOS build of a game yet.
- **Formulas.** Set variable to a formula and If take expressions such as `hp - max(1, atk * 2 - def)`,
  with `rand`, `min`, `max`, `abs` and `clamp`, and Otherwise runs when an If did not.
- **Up to four players.** Player events, For each player, player variables and `@player`. A body can belong to
  players 1 to 4.
- **Templates and copies.** Give an item a number of copies, then spawn, remove and go through them. Each copy
  has its own copy variables, and an item can follow a template so every copy carries its own label.
- **Behaviors.** Wander, Run away from and Tell nearby copies, for calm, skittish and pack creatures.
- **Looks, pause and more.** Set look picks one of several looks on a sprite sheet. Pause and Resume freeze the
  world while blocks keep running. Camera follows two items. Texts can spell names from variables.
- **Editor.** Zoom the Logic canvas with Ctrl or Cmd and the wheel, and keep panels hidden while editing.
- **Bigger games.** A scene can hold 1024 items and 16384 blocks, and a program 512 variables.

[Greenhollow](https://github.com/AnyDevStudio/Greenhollow) uses all of it: a small RPG with three classes, four
places, five quests, creature packs, pets and two players.

Packages for macOS, Windows and Linux. Qt is included, so there is nothing else to install to open
Studio. To build PS5 packages you also need the AnyDevKit SDK and its tools: the Set up PS5 tools
screen downloads AnyDevKit and the PS5 libraries for you and tells you which build tools to install.

| System | Download |
|---|---|
| macOS 26 or newer on Apple Silicon | `AnyDevKitStudio-*-macos-arm64.zip` |
| Windows x64 | `AnyDevKitStudio-*-windows-x64.zip` |
| Linux x86_64 | `AnyDevKitStudio-*-linux-x86_64.AppImage` |

These builds are not signed or notarized, because the project has no signing certificates yet.
Your system will warn you the first time you open one.

**macOS.** Unzip the file and move `AnyDevKitStudio.app` to Applications. Open it once and macOS
blocks it. Go to System Settings > Privacy & Security, scroll to Security and choose Open Anyway.
Or, from Terminal:

```sh
xattr -dr com.apple.quarantine /Applications/AnyDevKitStudio.app
```

**Windows.** Unzip the whole folder and run `bin\AnyDevKitStudio.exe`. If SmartScreen appears,
choose More info, then Run anyway.

**Linux.** Make the file executable with `chmod +x`, then run it. If it says FUSE is missing, run it
with `APPIMAGE_EXTRACT_AND_RUN=1` set or install `libfuse2`.
