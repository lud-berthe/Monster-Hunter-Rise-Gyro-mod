# Monster Hunter Rise Gyro mod 1.1.2

Keeps the Menu view's Steam Input mouse policy active in Options, dialogue and paused menus, including when the background camera is unavailable. Camera movement remains blocked where the game does not allow it.

Use **GyroLib 1.3.0 or later** with this update. Its general fixes prevent identified Steam mouse movement from also moving the cursor in Block/Convert modes, improve manual calibration for stationary noisy sensors, remember successful calibration per identifiable sensor, and fix the Recalibrate gyro button returning immediately to idle. GyroLib is downloaded separately.

## Update

1. Close the game.
2. Replace `gyrolib.dll` beside `MonsterHunterRise.exe` with the DLL from [GyroLib 1.3.0](https://github.com/lud-berthe/GyroLib/releases/tag/v1.3.0).
3. Extract `Monster-Hunter-Rise-Gyro-mod-1.1.2.zip` beside `MonsterHunterRise.exe`, replacing the mod files.
4. Keep `reframework/data/gyrolib.ini` to preserve your settings.

The archive contains exactly three files: the mod DLL, bundled Lua script and license notices. It contains no GyroLib DLL, INI or REFramework runtime. Nexus REFramework Nightly939 or a newer compatible build remains supported. See [installation instructions](https://github.com/lud-berthe/Monster-Hunter-Rise-Gyro-mod/blob/v1.1.2/README.md).

## Validation

All nine automated tests pass with the official Nightly939 sources and GyroLib 1.3.0. Tests cover Menu-view detection independently of camera permission, unknown UI state, packaged Lua, plugin loading, input transport and the hidden DX12 panel. Confirmation in Rise with the reporting user's original Switch Pro controllers remains pending; automated calibration checks use synthetic sensor data. See [validation details](https://github.com/lud-berthe/Monster-Hunter-Rise-Gyro-mod/blob/v1.1.2/docs/VALIDATION.md).
