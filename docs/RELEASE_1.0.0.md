# Monster Hunter Rise Gyro mod 1.0.0

Native gyro camera control through GyroLib, with an F10 settings overlay and six independent views: Free camera, Menu camera, Wirebug aim, Weapon aim, Ballista and Cannon. Includes stick/touchpad flick, recenter support and the author's recommended preset through GyroLib's native settings action.

## Installation

1. Install [REFramework for Monster Hunter Rise](https://github.com/praydog/REFramework-nightly/releases/tag/nightly-01424-d1461375aee4ec3f313170f8eaad12064eb542d9).
2. Download `gyrolib.dll` from [GyroLib 1.0.0](https://github.com/lud-berthe/GyroLib/releases/tag/v1.0.0) and put it beside `MonsterHunterRise.exe`.
3. Extract `Monster-Hunter-Rise-Gyro-mod-1.0.0.zip` into that same game directory, then launch Rise and press **F10**.

The minimal archive contains exactly **three files**:

- `reframework/plugins/MHRGyro.dll`
- `reframework/autorun/mhr_gyro.lua` (all modules bundled)
- `reframework/MHRGyro/LICENSES.txt`

It contains **no `gyrolib.dll`, no INI file and no REFramework runtime**. This is a packaging-only update: the mod DLL and Lua module behavior are unchanged; the new bundle is tested without any external Lua module files. The recommended preset is embedded in the mod's DLL. GyroLib creates `reframework/data/gyrolib.ini` at startup; existing player preferences are preserved.

Requires Windows x64, the Microsoft Visual C++ x64 runtime and DirectX 12 with HDR disabled. For touchpad flick, set Steam Input's right touchpad to None; disable Steam Input's gyro output to prevent double movement.

When updating the original package, the old `reframework/autorun/mhr_gyro/`, `reframework/MHRGyro/licenses/` and `README_MHRGyro.txt` are no longer required. Download the named mod ZIP; GitHub's automatic source archives are for developers.

## Validation and limits

Tested with Steam Controller 2026 through its puck. Automated tests pass in both SDL and no-SDL builds (8/8 each), including persistence, input/camera contracts, flick angles, thread transport, GUI and distribution. The embedded preset matches all 277 numeric values of the author's original snapshot.

This remains an experimental mod. Final live checks of F10 input capture, long-press blocking and recenter are pending. Arbitrary Steam Input remaps and extra buttons are not universally covered by long-press blocking. Other hardware, Proton and other game versions are unverified; HDR is unsupported. See [validation details](https://github.com/lud-berthe/Monster-Hunter-Rise-Gyro-mod/blob/v1.0.0/docs/VALIDATION.md).
