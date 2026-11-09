Packages for macOS, Windows and Linux. Qt is included, so there is nothing else to install to open
Studio. To build PS5 packages you still need the
[AnyDevKit SDK](https://github.com/boykopovar/AnyDevKit/releases/latest) and its tools.

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
