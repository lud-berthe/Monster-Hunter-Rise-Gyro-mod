# Validation and audit

## Release 1.0.0 - 4 October 2026

GyroLib 1.0.0, Windows x64 Release: **8/8 CTests pass with SDL enabled and 8/8 with SDL disabled**. The embedded preset was independently compared with the original author INI: all 277 numeric values match. Tests cover a fresh installation without any INI or data directory, preservation of existing preferences, native recommended-settings restoration and script resets.

The minimal release ZIP contains **three files**: this mod's unchanged DLL, one Lua script bundling the original modules, and consolidated license notices. The earlier 31-file package has been replaced. A new regression executes the bundled script and loads all its modules with disk module search disabled, including native-GUI callbacks and a second load. Its only DLL is `reframework/plugins/MHRGyro.dll`; there is no INI, GyroLib runtime, REFramework runtime, personal report or debug symbol. GyroLib and REFramework must be installed separately. The original author preset and all local build/research artifacts stay outside the published source tree.

The live-validation limits below remain applicable to this release.

## SDK update — GyroLib 1.0.0, 4 October 2026

The mod now requires the GyroLib 1.0 SDK; the C/overlay ABI remains 1 and the settings schema remains 0.2.0. The Release build passes 8/8 CTests with the new runtime. The corrected default filename is `gyrolib.ini`. During the local update, all 277 saved numeric preferences were read back through the new SDK and compared before renaming the existing `girolib.ini`; its bytes were preserved and the original was backed up outside the source tree. This is an installation rename, not a custom settings loader or a modification to GyroLib. Earlier deployments should rename their old file with the game closed, without overwriting an existing `gyrolib.ini`.

## Automated audit — 4 October 2026

Windows x64, Release, current installed GyroLib 0.2 SDK and pinned REFramework Lua 5.4.3. The audit examines the C++ bridge, owner/render thread transport, camera/input Lua modules, preset persistence, build and distribution tools. It does not claim the absence of every possible bug or replace live hardware tests.

Corrections covered by regression tests:

- A menu/focus transition between observation and camera write no longer permanently stops gyro; it drops that delta and resumes next frame. The diagnostic F8 test distinguishes a skipped write from success.
- Repeated siege-camera queuing cannot indefinitely extend the lifetime of an unconsumed turn.
- If the window owner itself stalls, the consumer rejects old snapshots, suppression rights, filter verdicts and recenter requests while retaining configuration metadata.
- Lost long-press-filter replies expire, preventing retained pending taps and delayed replay.
- Invalid setting/action commands report failure instead of retaining a previous success status.
- A DLL-only SDK update refreshes the build runtime even when no relink is needed.
- Distribution uses the configured REFramework dependency path for notices. Packaging works without a private cached archive. Release 1.0.0 removes the former optional runtime-bundling mode.
- Installation supports clean first installs and portable tracked updates, retains user settings, checks both DLLs for Lua-only updates, and carries dependency notices.

Native suites: recommended settings; Steam session initialization; bridge/Lua contract; stick/touchpad flick angles; long-press filter; two-thread window transport; hidden DX12 WARP GUI. The Lua suite covers camera signs/limits, view transitions, mouse preservation, input capture, siege input, diagnostic callbacks and error handling. Python tooling tests cover packaging and isolated installer scenarios without touching the real game.

Historical audit result (before the mod-only release packaging change): clean Windows x64 Release builds with SDL enabled and disabled both pass **8/8 CTests**, including Python distribution tests. Staging succeeds with REFramework outside the source tree; the resulting 33-entry mod ZIP verifies successfully without bundling REFramework. A final source scan finds no generated binary, private report, user-specific path or detected credential pattern. No further reproducible defect was identified in the final pass of this scope.

The cleanup preserves only project sources, tests, author preset, build scripts, documentation and license files. Local builds, dependency checkouts, diagnostics, previous packages and the abandoned native-menu prototype are archived outside the source directory.

## Previously observed in Rise

Test setup: Steam Controller 2026 through its puck, Windows, DX12 SDR, Steam build 17920865, REFramework nightly 01424 / API 1.15.0.

- Plugin/GUI loaded in game; independent free/menu/wire/weapon views were observed, with gyro output in each eligible view.
- Mouse-look with flick enabled was confirmed after the game-specific mouse-lever correction.
- Ballista and cannon captures recorded angular movement on both axes and successful siege input application; captures alone do not isolate every gyro contribution from simultaneous native input.
- A 30-second weapon-aim capture on 4 October recorded SDL → Steam → SDL fallback/recovery. It contained 348 observations, including 92 on Steam (91 with camera output), at about 60 Hz Steam and 243 Hz SDL. The user reported no interruption or direction/speed change.

## Remaining live validation

- F10 capture of all gamepad/keyboard/mouse actions and held-button behavior after closing.
- Block long press for real commands; arbitrary Steam remaps and extra buttons are not universally supported by the current adapter.
- Controller-selected recenter, especially siege weapons; final subjective flick/touchpad angle and direction checks.
- Reconnect, sleep, calibration and haptic coexistence across other hardware/transports.
- Other game versions, HDR, Linux/Proton and coexistence with other camera mods. HDR is unsupported by this overlay backend.

The audit uses synthetic tests and a hidden graphics window. It does not silently mark these live checks as completed. Detailed historical experiments and private runtime captures are retained in the local archive, outside the publishable source tree.
