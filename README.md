# Monster Hunter Rise Gyro

A Windows x64 REFramework mod that adds native gyro camera control to Monster Hunter Rise using GyroLib. Open the settings with **F10**. The interface, processing, activation modes, calibration, flick stick and recommended-settings action come from GyroLib; this mod connects them to Rise.

## Features

- Separate profiles for Free camera, Menu camera, Wirebug aim, Weapon aim, Ballista and Cannon.
- Flick stick using the right stick, physical right touchpad, or both when supported by the controller.
- Configurable recenter button and automatic settings persistence.
- GyroLib's native DirectX 12 settings overlay, with game-input capture while open.
- SDL motion acquisition with Steam Input fallback, on the game window thread.

This is an experimental mod. The tested setup is Steam Controller 2026 through its puck, Windows, DirectX 12 and HDR disabled. See [validation and limits](docs/VALIDATION.md) before assuming support for other setups.

## Install

1. Close Rise and install [REFramework for Rise](https://github.com/praydog/REFramework-nightly/releases/tag/nightly-01424-d1461375aee4ec3f313170f8eaad12064eb542d9). The tested runtime is nightly 01424, plugin API 1.15.0. The Microsoft Visual C++ x64 runtime is required.
2. Download **`gyrolib.dll`** from [GyroLib 1.0.0](https://github.com/lud-berthe/GyroLib/releases/tag/v1.0.0) and put it beside `MonsterHunterRise.exe`.
3. Extract **`Monster-Hunter-Rise-Gyro-mod-1.0.0.zip`** from [Releases](https://github.com/lud-berthe/Monster-Hunter-Rise-Gyro-mod/releases) beside `MonsterHunterRise.exe`. Back up any existing mod files first.
4. Use DirectX 12 with HDR off. Disable Steam Input gyro-to-mouse/joystick output to avoid duplicate rotation.
5. For **touchpad flick**, set Steam Input's right touchpad to **None**. The mod reads the physical touchpad through GyroLib; it does not inspect or change your Steam layout.
6. Launch Rise, load a save and press F10. Close REFramework's own panel with Insert if necessary.

The release contains exactly **three files**: `reframework/plugins/MHRGyro.dll`, the bundled `reframework/autorun/mhr_gyro.lua` script and `reframework/MHRGyro/LICENSES.txt`. All Lua modules are included in that one script; installation instructions stay on this page. It contains **no GyroLib DLL, INI file or REFramework runtime**. The `reframework/` directory is the required installation location for the mod's own files.

## Settings

Selecting a settings tab edits that view without changing the active game view. Settings save automatically to **`reframework/data/gyrolib.ini`**. Your file is never included in packages or replaced by the installer. The configured GyroLib menu shortcut takes precedence over F10. Earlier builds used the misspelled `girolib.ini`: with Rise closed, rename that file to `gyrolib.ini` if the new file does not already exist. Keep a backup and do not overwrite an existing newer configuration.

**Use recommended settings** restores the author's six-view preset: gyro Off in Free camera and Menu camera, Always on in the four aiming views; sensitivity X/Y 2.5, Player space, no inversion, smoothing, acceleration or flick. This also supplies first-install defaults. Applying it preserves your language and menu shortcut. **Reset all settings** remains GyroLib's standard reset. The author preset is compiled into `src/recommended_settings.hpp`, applied with `gl_setting_set` and captured through `gl_capture_recommended_settings` before loading your preferences. No external preset file is required.

Menus that pause the game, dialogs, cutscenes, camera locks, focus loss and open settings interfaces block camera output. Recenter levels pitch without changing yaw. Release held buttons after closing the settings overlay. The experimental **Block long press** integration covers eligible standard gamepad buttons; custom Steam Input remaps and extra physical buttons are not universally resolved.

F9 records a 30-second diagnostic; press again to stop early. REFramework's Script Generated UI also offers **Open GyroLib settings** and read-only inspection. Do not publish generated reports without reviewing them.

## Build

Requirements: Windows x64, CMake 3.24+, a C++17 MSVC toolchain, Git, and a current installed **GyroLib 1.0 SDK with Core, Steam and Overlay components**. The SDK must expose the recommended-settings and long-press-blocking APIs used by this source. The standard build requires its shared bundled SDL runtime. GyroLib is an external dependency and is never built or modified by this project.

```powershell
./tools/bootstrap.ps1
cmake -S . -B build -A x64 -DGYROLIB_SDK_DIR="C:/path/to/GyroLib/sdk"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
./tools/stage.ps1 -BuildDirectory ./build
python ./tools/package.py Monster-Hunter-Rise-Gyro-mod-1.0.0.zip
```

`bootstrap.ps1` downloads the pinned REFramework source revision; its plugin header and bundled Lua 5.4.3 are compiled, not REFramework itself. You can set `REFRAMEWORK_SOURCE_DIR` to an existing pinned checkout. The staging manifest remembers that path so notices come from the dependency actually built. The local sibling SDK path is only a development convenience; use `GYROLIB_SDK_DIR` on another machine.

`-DMHR_WITH_SDL=OFF` tests the bridge without SDL acquisition. CTests use synthetic input and a hidden DirectX 12 WARP window, never the game or a real controller. Python 3.10+ enables the additional packaging/installer tests. DLL-only SDK changes are copied on the next build even without relinking.

Staging creates `dist/MHRGyro`; packaging creates a new mod-only ZIP in `dist` and refuses to overwrite an existing archive. Packaging combines the source modules through Lua's standard `package.preload` mechanism and consolidates the applicable licenses. Neither step bundles external runtimes or INI files. Source checkouts contain no game binaries or dependency downloads.

For a local installation from a build:

```powershell
./tools/install-update.ps1 -GameDirectory "C:/path/to/MonsterHunterRise" -BuildDirectory ./build -CheckOnly
./tools/install-update.ps1 -GameDirectory "C:/path/to/MonsterHunterRise" -BuildDirectory ./build
```

The installer records hashes under `reframework/MHRGyro/install-manifest.json`. It supports a clean first install and tracked updates, and refuses unrelated modified files. `-ScriptsOnly` requires both installed DLLs to match the build; after it completes, use ScriptRunner â†’ Reset scripts. Full DLL updates require closing Rise. This developer installer also copies the configured SDK's `gyrolib.dll`; it does not install REFramework. Public release archives omit both dependencies.

## Development and reports

- [Binding and lifecycle design](docs/BINDINGS.md)
- [Validation, audit and remaining live checks](docs/VALIDATION.md)
- [Sources and dependency licenses](docs/SOURCES.md)

Include the game/REFramework version, controller and connection, active view, relevant settings, and steps to reproduce a bug. Useful local reports are `mhr_gyro_state_probe.json`, `mhr_gyro_reader_probe.json` and `re2_framework_log.txt`.

To uninstall, close Rise and remove the three mod files listed above. Older packages also installed `reframework/autorun/mhr_gyro/`, `reframework/MHRGyro/licenses/` and `README_MHRGyro.txt`; these can be removed when updating to the bundled release. Keep `gyrolib.ini` if you want your preferences for a later reinstall. Remove REFramework's `dinput8.dll` only if no other mod uses it. This project is not affiliated with Capcom. See [LICENSE](LICENSE) and the packaged dependency notices.
