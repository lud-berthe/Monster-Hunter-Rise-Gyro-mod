# Rise integration contract

## Views and camera units

Stable context IDs are 1 Free camera, 2 Menu camera, 3 Wirebug aim, 4 Weapon aim, 5 Ballista and 6 Cannon. Labels are English and do not identify saved profiles. The selected settings tab never selects the gameplay view.

`bindings.lua` observes CameraManager, GuiManager and the local player. Missing objects or unknown blocking state suppress output. The resolved wire reticle distinguishes wire/weapon aim. MachineAim plus the operation-speed kind distinguishes Cannon=1, BallistaNormal=2 and BallistaScope=3. Unsupported machine/camera states remain inert. Menu view permits non-pausing background camera control; options, conversations and dialogs still block it.

The highest-priority available active context wins; equal priorities use the smallest ID. The controller validates mandatory booleans, exact active context, update result and finite deltas before any camera action. A normal pause/focus/menu transition after that check discards the current delta without permanently stopping the adapter. Missing mandatory bindings or failed setters latch an error until script reset.

GyroLib returns integrated degrees; the mod applies them once, without another frame-time multiplier. Positive yaw means right: Rise player-camera yaw is reduced by that angle in radians and wrapped to [-pi, pi). Pitch is added and clamped to the camera's queried radian limits.

Siege weapons derive their camera from the mounted weapon. `machine.lua` adds radians to `_CameraOperation` after `CameraInput.ReflectInput`, preserving native input and game limits. Requests are consumed once, require matching view/input and expire after one frame of delay. Their age starts at the oldest queued delta, so repeated updates cannot retain an indefinitely unconsumed turn.

## Native inputs

The flick hook temporarily clears camera-owned `_AnalogR` and `_AnalogRraw` fields on `_PadOriginal` and `_PadArrange`, then restores them after ReflectInput. Original values and nested-hook state are retained. A resolved mouse-lever observation prevents masking real mouse input; missing observation withdraws stick-suppression capability.

Touchpad flick relies on an explicit user configuration: right touchpad set to None in Steam Input. With no native camera output assigned to that channel, the mod advertises touchpad suppression without masking mouse or stick fields. A mouse/joystick touchpad layout can otherwise double rotation.

`input_capture.lua` clears the game's resolved pad, keyboard, mouse, player and UI caches while the native settings overlay captures input. Held buttons are latched until release. GyroLib's independent controller path remains available to its GUI. No persistent game input mode is set.

`block_long_press.lua` routes eligible standard-gamepad press/release edges to GyroLib's `gl_filter_event`, which owns the threshold and verdict. Native holds, chords and non-gameplay actions are excluded. Short presses are replayed in the local player's command buffer; stale view/device decisions are discarded, including lost responses older than 100 ms. Custom Steam Input remaps and extra physical buttons still require further integration/validation.

## Ownership and GUI

REFramework creates the Lua-facing session. A separate Lua VM and GyroLib context live on the game window thread; SDL/Steam polling and context destruction occur there. Plain values cross a mutex-protected queue; no Lua pointers or borrowed SDK strings cross threads. Command order and 64-bit integer identities are preserved.

Each camera callback sends fresh host state and consumes accumulated output once. Both sides withdraw permissions after 100 ms of stale state: the owner timer handles a delayed producer, and the consumer rejects old snapshots if the owner is blocked. View changes, focus loss and overlay capture discard queued motion.

Script GC detaches the backend with permissions, commands and deltas cleared. A matching script can resume it within two seconds; otherwise it expires on the owner thread. Explicit close and window destruction dispose it there. Recursive window messages cannot re-enter a context during acquisition.

The mod polls SDL, polls GyroLib's borrowed Steam reader, commits overlay commands once and then calls `gl_update`. GyroLib publishes its own completed-frame snapshot. The MHR-specific Steam session helper attempts public `SteamInput::Init(false)` once if the game's observed v005 interface reports no controllers. It never calls SteamAPI_Init, RunFrame, Shutdown or changes layouts/action sets.

The native overlay requires DX12 SDR. A dedicated render worker owns its init/render/shutdown operations; Present and resize callbacks wait for completion. Owner-thread overlay detach precedes context destruction; GPU resources are retained after a live-device timeout for safe retry. The SDK owns its shortcut, controls and controller navigation. Lifecycle window messages and Alt+F4 remain available.

## Persistence and SDK boundary

The six-view author preset is embedded in the plugin, seeded with `gl_setting_set` and captured once via `gl_capture_recommended_settings` before loading the player's `gyrolib.ini`. There is no external preset or custom settings parser. Temporary context registration at startup captures all views even before game bindings are available; it grants no active gameplay permission. Player preferences win on startup. The native `settings.recommended` action applies the snapshot and saves through GyroLib. Reset/script reload cannot redefine it.

The bridge creates the settings directory on initialization; GyroLib creates or loads `gyrolib.ini` there. Existing malformed/newer-schema files are never replaced. The optional legacy `mhr_gyro_settings.ini` import must already use schema 0.2.0. Update, initialization, recommended-preset and save statuses remain distinct in bridge diagnostics.

GyroLib owns motion processing, device capability choices, activation, long-press timing, calibration, settings/inheritance and the GUI. This repository owns Rise observations, coordinate conversion and game-input hooks. No Rise-specific changes or documentation belong in GyroLib.
