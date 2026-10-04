#pragma once
#include <cstdint>
#include <array>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Explicit MHR validation opt-in (approved 2026-10-04), separate from the
// borrowed GyroLib reader. Init(false) uses the game's SteamAPI_RunCallbacks.
// No SteamAPI_Init, action-set/configuration writes, RunFrame or Shutdown.
// One attempt per process, including across Lua resets; never reinitialize an
// interface already reporting controllers. Restrict to MHR's observed v005.
struct MhrSteamSession {
    using Accessor=void*(*)();
    using User=int(*)();
    using Handles=int(*)(void*,uint64_t*);
    using Init=bool(*)(void*,bool);
    bool finished=false;
    uint64_t retry_at=0;
    const char* error="Waiting for Steam Input";
    template<class Resolve> void prepare(uint64_t now,Resolve resolve) {
        if(finished || now<retry_at)return;
        retry_at=now+1000000000ull;
        const auto user=reinterpret_cast<User>(resolve("SteamAPI_GetHSteamUser"));
        const auto get=reinterpret_cast<Accessor>(resolve("SteamAPI_SteamInput_v005"));
        const auto handles=reinterpret_cast<Handles>(resolve("SteamAPI_ISteamInput_GetConnectedControllers"));
        const auto init=reinterpret_cast<Init>(resolve("SteamAPI_ISteamInput_Init"));
        if(!user || !get || !handles || !init) {error="Rise Steam Input v005 exports unavailable";return;}
        if(!user()) {error="Waiting for Rise's Steam session";return;}
        auto* input=get();if(!input) {error="Rise Steam Input v005 unavailable";return;}
        std::array<uint64_t,16> connected{};
        const int count=handles(input,connected.data());
        if(count<0 || count>16) {error="Invalid Steam controller count";return;}
        if(count>0) {finished=true;error="";return;}
        finished=true;
        error=init(input,false)?"":"Steam Input initialization failed (not retried)";
    }
};
inline const char* mhr_prepare_steam_input(uint64_t now) {
#ifdef _WIN32
    static MhrSteamSession session;
    const auto module=GetModuleHandleW(L"steam_api64.dll");
    if(!module)return "Waiting for Rise's Steam DLL";
    session.prepare(now,[module](const char* name){return GetProcAddress(module,name);});
    return session.error;
#else
    return "Rise Steam Input initialization requires Windows";
#endif
}
