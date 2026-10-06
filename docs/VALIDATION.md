# Validation and audit

## Release 1.1.1 — 6 October 2026

A user log showed that Nexus Nightly939 (`654bac566ad2cb645a5ddbf72d0008e8698187b8`, plugin API 1.10.0) rejected the plugin before initialization. The plugin had advertised the compilation header's API 1.15.0 even though it uses only callbacks and renderer fields present in 1.10.0. The runtime requirement is now explicitly 1.10.0, independently of the build SDK version. Required function pointers are checked before registration.

Automated checks, Windows x64 Release, GyroLib 1.2.1:

- All 9 CTests pass using Nightly939's official API header and Lua sources.
- All 9 pass using the pinned nightly 01424 header and Lua sources.
- The new `mhr_plugin_host_api` fixture loads the actual plugin DLL, checks its exports and minimum version, registers callbacks and exercises a host-owned Lua state across the DLL boundary. It recreates the Lua state to cover script reset and rejects an unsupported host or missing required callback.
- Cross-version runs also pass: the Nightly939 host fixture loads the modern-SDK plugin, and the modern fixture loads the Nightly939-SDK plugin. The Lua state layouts are identical between these source revisions; the two changed Lua implementation files contain error-path fixes.
- The Lua hooks used by the mod, including `thread.get_hook_storage`, exist in the Nightly939 source. Existing script tests run under both versions of its bundled Lua.

To repeat the baseline build, configure a separate build directory with `REFRAMEWORK_SOURCE_DIR` pointing to the official Nightly939 source revision, then build Release and run CTest. Run that build's `mhr_plugin_tests.exe` with the full path to the modern build's `MHRGyro.dll` for the cross-version check. Keep the normal pinned dependency checkout unchanged.

The user then confirmed that the mod works in game with Nightly939. The local log confirms revision `654bac566ad2cb645a5ddbf72d0008e8698187b8`, acceptance of the API 1.10.0 requirement, plugin and Lua initialization, and a ready independent DX12 panel. This is a Windows test, not a Proton/Wine validation; it does not establish exhaustive coverage of every feature on Nightly939. Newer builds that preserve the plugin API are expected to remain compatible. Nightly 01424 was already validated in game.

## Release 1.1.0 — 6 October 2026

The user reproduced physical-mouse stutter with the mod at 144 fps, but not at 60 or 120 fps, including with Flick Stick disabled and the controller disconnected. After removing unnecessary menu serialization from gameplay updates, the user confirmed that the stutter was fixed at 144 fps.

The six-view synthetic bridge benchmark measured about 4.55 ms per update with full menu tables and 0.006 ms without them in the same executable. These are bridge timings, not whole-game frame times. The DLL panel owns its model; Lua menu tables are now built only for an open fallback panel. `mhr_bridge_benchmark` reproduces this comparison without hardware or persistent settings.

The progressive recenter adapter builds against the published GyroLib 1.2.0 SDK. All 8 CTests pass with SDL enabled and all 8 with SDL disabled, including duration/default checks, exact completion, cancellation when the panel opens, fractional camera pitch, invalid fractions and active-view safety. The user confirmed timed recentering and continued mouse-camera smoothness at 144 fps in the updated installation.

## Release 1.0.1 — 5 October 2026

Fresh Windows x64 Release builds against the installed GyroLib 1.1.0 SDK pass **8/8 CTests with SDL enabled and 8/8 with SDL disabled**. This includes packaging, the bundled Lua script and hidden DX12 GUI checks.

Requires GyroLib 1.1.0 or later. The settings overlay accepts SDR 10-bit, scRGB and HDR10 buffers. The adapter reports the game's observed color space and falls back to automatic detection when unavailable.

With native HDR enabled in the game, the user confirmed that F10 opens the panel and that its colors are normal. The observed buffer was R10G10B10A2 with reported DXGI color space 0 (SDR), on a Windows HDR desktop. Treating that buffer as PQ had desaturated the panel. This session does not validate native PQ/scRGB presentation on a physical display.

The GUI regression covers automatic detection and explicit SDR/HDR10 changes on the same 10-bit format, FP16 buffers, panel lifecycle and return to SDR. GyroLib separately tests HDR composition through WARP pixel readback.

## Release 1.0.0 - 4 October 2026

GyroLib 1.0.0, Windows x64 Release: **8/8 CTests pass with SDL enabled and 8/8 with SDL disabled**. The embedded preset was independently compared with the original author INI: all 277 numeric values match. Tests cover a fresh installation without any INI or data directory, preservation of existing preferences, native recommended-settings restoration and script resets.

The minimal release ZIP contains **three files**: this mod's unchanged DLL, one Lua script bundling the original modules, and consolidated license notices. The earlier 31-file package has been replaced. A new regression executes the bundled script and loads all its modules with disk module search disabled, including native-GUI callbacks and a second load. Its only DLL is `reframework/plugins/MHRGyro.dll`; there is no INI, GyroLib runtime, REFramework runtime, personal report or debug symbol. GyroLib and REFramework must be installed separately. The original author preset and all local build/research artifacts stay outside the published source tree.

The live-validation limits below remain applicable to this release.

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
- Other game versions, Linux/Proton and coexistence with other camera mods. Native PQ/scRGB output on physical displays and other HDR display configurations remain unvalidated.

The audit uses synthetic tests and a hidden graphics window. It does not silently mark these live checks as completed. Detailed historical experiments and private runtime captures are retained in the local archive, outside the publishable source tree.
