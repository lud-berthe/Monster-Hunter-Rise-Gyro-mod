# Monster Hunter Rise Gyro mod 1.1.0

Adds adjustable camera recentering duration and fixes physical-mouse camera stutter observed at 144 fps.

- **Progressive recentering:** assign a button to Recenter camera, then open its + to adjust the duration from 0 to 1000 ms. The default is 0 ms. Each view keeps its own setting, including inheritance.
- **Lower CPU overhead:** the mod no longer rebuilds and transfers every settings menu during gameplay. The supplied GyroLib panel and the Lua fallback remain available.

## Update

1. Close the game.
2. Install `gyrolib.dll` from [GyroLib 1.2.0](https://github.com/lud-berthe/GyroLib/releases/tag/v1.2.0) beside `MonsterHunterRise.exe`. This update requires GyroLib 1.2.0 or later.
3. Extract `Monster-Hunter-Rise-Gyro-mod-1.1.0.zip` beside the game executable, replacing the mod files.

Keep `reframework/data/gyrolib.ini`. The archive contains only the mod DLL, bundled Lua script and license notices. GyroLib and REFramework are installed separately. Windows x64, DirectX 12 and the Microsoft Visual C++ x64 runtime are required. See [installation instructions](https://github.com/lud-berthe/Monster-Hunter-Rise-Gyro-mod/blob/v1.1.0/README.md).

## Validation

The user confirmed that the mouse stutter at 144 fps is fixed and that progressive recentering works in game. Automated checks cover the default duration, partial steps, exact completion, cancellation and camera safety, along with packaging and the settings overlay. See [validation details](https://github.com/lud-berthe/Monster-Hunter-Rise-Gyro-mod/blob/v1.1.0/docs/VALIDATION.md).
