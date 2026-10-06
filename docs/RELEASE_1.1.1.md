# Monster Hunter Rise Gyro mod 1.1.1

Fixes the mod failing to load with **REFramework Nightly939**, the version available on Nexus Mods. This caused F10 to do nothing even when the mod files were installed correctly.

The mod now requires plugin API **1.10.0**, instead of unnecessarily requiring 1.15.0. Nightly939 and newer compatible REFramework builds can load it; updating REFramework from Nexus to a GitHub nightly is no longer necessary for this mod.

## Update

1. Close the game.
2. Extract `Monster-Hunter-Rise-Gyro-mod-1.1.1.zip` beside `MonsterHunterRise.exe`, replacing the mod files.
3. Keep `reframework/data/gyrolib.ini` and your existing compatible GyroLib installation. GyroLib 1.2.0 or later is required; this fix is in the mod DLL.

The archive contains the mod DLL, bundled Lua script and license notices. REFramework and GyroLib are installed separately. See [installation instructions](https://github.com/lud-berthe/Monster-Hunter-Rise-Gyro-mod/blob/v1.1.1/README.md).

## Validation

Nightly939 was confirmed working in game on Windows. All nine automated tests pass with both its API/Lua sources and the newer nightly 01424 sources. Cross-version tests also load the compiled DLL and exercise a host-owned Lua state. See [validation details](https://github.com/lud-berthe/Monster-Hunter-Rise-Gyro-mod/blob/v1.1.1/docs/VALIDATION.md).
