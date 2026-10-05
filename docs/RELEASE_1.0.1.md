# Monster Hunter Rise Gyro mod 1.0.1

Fixes the GyroLib settings panel failing to appear with HDR enabled, and washed-out panel colors when a 10-bit SDR buffer is presented on an HDR desktop. The mod now reports the game's color encoding to GyroLib and uses automatic detection when that information is unavailable.

## Update

1. Close the game.
2. Download `gyrolib.dll` from [GyroLib 1.1.0](https://github.com/lud-berthe/GyroLib/releases/tag/v1.1.0) and replace the copy beside `MonsterHunterRise.exe`.
3. Extract `Monster-Hunter-Rise-Gyro-mod-1.0.1.zip` beside the game executable, replacing the mod's three files.

Keep `reframework/data/gyrolib.ini`. The archive contains the mod DLL, bundled Lua script and license notices; GyroLib and REFramework are installed separately. Windows x64, DirectX 12 and the Microsoft Visual C++ x64 runtime are required. See [installation instructions](https://github.com/lud-berthe/Monster-Hunter-Rise-Gyro-mod/blob/v1.0.1/README.md).

## Validation

Fresh Windows x64 Release builds against the installed GyroLib 1.1.0 SDK pass **8/8 CTests with SDL enabled and 8/8 with SDL disabled**. This includes packaging, the bundled Lua script and hidden DX12 GUI checks.

The user confirmed panel opening and normal colors with native HDR enabled. The observed backbuffer was 10-bit SDR on a Windows HDR desktop; native PQ/scRGB presentation on physical displays remains unvalidated. Automated GUI tests cover both explicit encoding and automatic detection, including transitions on the same 10-bit format. Other platform, controller and input-filter limits remain listed in [Validation](https://github.com/lud-berthe/Monster-Hunter-Rise-Gyro-mod/blob/v1.0.1/docs/VALIDATION.md).
