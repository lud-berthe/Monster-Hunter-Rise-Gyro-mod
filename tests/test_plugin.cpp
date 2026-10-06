#include <windows.h>
#include <reframework/API.h>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <cstdio>
#include <cstring>
#include <cstdlib>

static void check(bool value,const char* message) {
    if(!value) {std::fprintf(stderr,"%s\n",message);std::exit(1);}
}
static REFLuaStateCreatedCb lua_created=nullptr;
static REFOnPresentCb present=nullptr;
static REFOnDeviceResetCb reset=nullptr;
static REFOnMessageCb message=nullptr;
static int locks=0,unlocks=0,errors=0;
static void log_info(const char*,...) {}
static void log_error(const char*,...) {++errors;}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    check(argc==2,"Expected plugin DLL path");
    auto dll=LoadLibraryExA(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!dll)std::fprintf(stderr,"LoadLibrary error %lu\n",GetLastError());
    check(dll!=nullptr,"Plugin DLL could not load");
    auto required=reinterpret_cast<REFPluginRequiredVersionFn>(GetProcAddress(dll,"reframework_plugin_required_version"));
    auto initialize=reinterpret_cast<REFPluginInitializeFn>(GetProcAddress(dll,"reframework_plugin_initialize"));
    check(required && initialize,"Plugin exports missing");
    REFrameworkPluginVersion request{}; required(&request);
    check(request.major==1 && request.minor==10 && request.patch==0,"Minimum host API must stay 1.10.0");
    check(request.game_name && std::strcmp(request.game_name,"MHRISE")==0,"Wrong target game");
    REFrameworkPluginVersion version{REFRAMEWORK_PLUGIN_VERSION_MAJOR,REFRAMEWORK_PLUGIN_VERSION_MINOR,REFRAMEWORK_PLUGIN_VERSION_PATCH,"MHRISE"};
    check(request.major==version.major && request.minor<=version.minor,"Host would reject plugin version");
    REFrameworkPluginFunctions functions{};
    functions.on_lua_state_created=+[](REFLuaStateCreatedCb cb){lua_created=cb;return true;};
    functions.on_present=+[](REFOnPresentCb cb){present=cb;return true;};
    functions.on_device_reset=+[](REFOnDeviceResetCb cb){reset=cb;return true;};
    functions.on_message=+[](REFOnMessageCb cb){message=cb;return true;};
    functions.lock_lua=+[](){++locks;};
    functions.unlock_lua=+[](){++unlocks;};
    functions.log_info=log_info;functions.log_error=log_error;
    REFrameworkRendererData renderer{REFRAMEWORK_RENDERER_D3D12,nullptr,nullptr,nullptr};
    REFrameworkPluginInitializeParam params{};
    params.version=&version;params.functions=&functions;params.renderer_data=&renderer;
    check(!initialize(nullptr),"Null host accepted");
    version.minor=9;check(!initialize(&params),"Unsupported older API accepted");
    version.minor=REFRAMEWORK_PLUGIN_VERSION_MINOR;
    functions.lock_lua=nullptr;check(!initialize(&params),"Missing required function accepted");
    functions.lock_lua=+[](){++locks;};
    check(initialize(&params),"Plugin initialization failed");
    check(lua_created && present && reset && message,"Required callback not registered");
    // REFramework owns the Lua state; the DLL contains a separate Lua runtime.
    // Exercise that boundary twice, as on ScriptRunner Reset scripts.
    for(int i=0;i<2;++i) {
        auto* state=luaL_newstate();check(state!=nullptr,"Lua state creation failed");
        luaL_openlibs(state);lua_created(state);
        const int result=luaL_dostring(state,R"(
            assert(type(mhr_gyro_native)=='table')
            local session=mhr_gyro_native.create(false)
            local result=session:tick({}, {})
            assert(result.yaw_degrees==0 and result.pitch_degrees==0)
            assert(result.reader_available==false)
            session=nil;collectgarbage('collect')
        )");
        if(result)std::fprintf(stderr,"%s\n",lua_tostring(state,-1));
        check(result==0,"Cross-DLL Lua bridge failed");
        lua_close(state);
    }
    check(locks==2 && unlocks==2,"Lua locking mismatch");
    present();reset();
    check(errors==0,"Plugin reported an error");
    // Callbacks remain host-registered for process lifetime, as in REFramework.
    std::printf("Plugin load and Lua bridge passed with official API %d.%d.%d\n",version.major,version.minor,version.patch);
    return 0;
}
