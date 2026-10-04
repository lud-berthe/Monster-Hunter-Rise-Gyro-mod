# Sources and dependency notices

- [REFramework source](https://github.com/praydog/REFramework/tree/d1461375aee4ec3f313170f8eaad12064eb542d9), pinned to `d1461375aee4ec3f313170f8eaad12064eb542d9`. Used files are `include/reframework/API.h` and bundled Lua sources under `dependencies/lua/src/`.
- [Matching REFramework nightly 01424](https://github.com/praydog/REFramework-nightly/releases/tag/nightly-01424-d1461375aee4ec3f313170f8eaad12064eb542d9), plugin API 1.15.0. Its runtime is installed separately and never bundled in the mod release.
- [Official native plugin documentation](https://cursey.github.io/reframework-book/) and [example plugin](https://github.com/praydog/REFramework/blob/d1461375aee4ec3f313170f8eaad12064eb542d9/examples/example_plugin/Plugin.cpp): lifecycle and Lua registration.
- [Official ImGui bindings](https://github.com/praydog/REFramework/blob/d1461375aee4ec3f313170f8eaad12064eb542d9/src/mods/bindings/ImGui.cpp): REFramework diagnostic panel.
- [Public Rise FOV script](https://gist.github.com/carupota/5f1f8cdc5abb63ba9e4192ecb1ae9ab7) and [EMV Engine](https://github.com/alphazolam/EMV-Engine): historical camera/player API research; no source from either is shipped.

GyroLib is consumed as an installed external SDK. Its exported headers and license bundle describe the exact runtime used by each build. The game hooks are based on local metadata/observations; online examples alone do not establish compatibility.

REFramework is MIT (Copyright 2019 praydog); Lua 5.4.3 is MIT (Copyright 1994–2021 Lua.org, PUC-Rio). Staging/installation preserve these notices from the configured dependency checkout. The minimal package consolidates the MHRGyro, REFramework, Lua and GyroLib license texts in `reframework/MHRGyro/LICENSES.txt`. GyroLib's implementation and its SDL/ImGui/font dependencies remain in the separately distributed GyroLib DLL; their patch files and distribution materials are not duplicated in this mod-only archive. The local developer installer still carries the SDK notice bundle when installing that DLL. No Valve SDK, Steam DLL or game binary is redistributed by this project.

The mod's MIT license retains the existing ReturnalGyro contributor attribution and adds the MHRGyro contributor attribution. The hidden graphics fixture is adapted from GyroLib's MIT-licensed tests. Source repositories, release links and historical examples are references, not automatic downloads during normal gameplay.
