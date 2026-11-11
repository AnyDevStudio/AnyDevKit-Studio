> **Alpha.** Expect rough edges. Scenes with images, text, shapes, tilemaps, sprite sheets, controller movement
> and logic blocks have run in AnyPS5 on a Mac. Playing online, new in this version, has been played by a scripted
> test over one computer's network and by hand between two windows of a game on one Mac; it has not been tried
> between two machines or consoles. Windows and Linux builds of games build, but have not been run on Windows or
> Linux by us. Nothing has been run on a real PS5, and sound and saving build for PS5 but have not yet been
> confirmed working there.

New in this version:

- **Play online.** Friends on the same network play one game, each on their own console. Three blocks: Let friends
  join online, Join a friend's game online, and Stop playing online. The console that hosts runs the game; a
  friend's console shows the screen the host makes for it.
- **A screen for each friend.** Every friend has their own scene, camera, screen-fixed items, music and screen
  variables, so they can open a menu or walk to another scene without touching anyone else's screen. The world of
  a scene is the same for everyone in it.
- **Screen variables.** A fourth kind next to game, player and copy: one value for each screen. Formulas can
  read `seat`, `online` and `guest`.
- A project saved with this version uses `schemaVersion` 6 in `logic.json`; older Studio versions cannot open it.

From 0.1.0-alpha.4: builds for Windows and Linux, formulas, up to four players, templates and copies, behaviors,
looks and pause.

[Greenhollow](https://github.com/AnyDevStudio/Greenhollow) uses all of it: a small RPG with three classes, six
places, five quests, creature packs, pets, two players on one screen and friends online.

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
