#include "bridge.hpp"
#include "window_reader.hpp"
#include "native_gui.hpp"
#include <cstdint>
#include <reframework/API.h>
#include <dxgi.h>
static const REFrameworkPluginFunctions* functions=nullptr;
static const REFrameworkRendererData* renderer=nullptr;
static void on_present() {
    if(renderer)mhr_gui_present({renderer->renderer_type,renderer->device,renderer->swapchain,renderer->command_queue});
}
static void on_reset() {
    if(!mhr_gui_reset())functions->log_error("MHRGyro GUI: renderer cleanup failed before device reset");
}
static bool on_message(void* window,unsigned int message,unsigned long long wp,long long lp) {
    if(!mhr_on_window_message(window,message,wp,lp))return false;
    const bool allow=mhr_gui_message(window,message,wp,lp);
    if(message==0x0002 || message==0x0082)mhr_gui_reset(); // WM_DESTROY / WM_NCDESTROY
    return allow;
}
static void on_lua_created(lua_State* l) {
    functions->lock_lua();
    mhr_register(l);
    functions->unlock_lua();
}
extern "C" __declspec(dllexport) void reframework_plugin_required_version(REFrameworkPluginVersion* v) {
    // Minimum tested host API: Nexus Nightly939. The build header can be newer;
    // its version is not the plugin's runtime requirement.
    v->major=1;
    v->minor=10;
    v->patch=0;
    v->game_name="MHRISE";
}
extern "C" __declspec(dllexport) bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam* p) {
    if(!p || !p->version || p->version->major!=1 || p->version->minor<10 || !p->functions) return false;
    const auto* f=p->functions;
    if(!f->on_message || !f->on_present || !f->on_device_reset ||
       !f->on_lua_state_created || !f->lock_lua || !f->unlock_lua ||
       !f->log_info || !f->log_error) return false;
    functions=p->functions;
    renderer=p->renderer_data;
    if(p->renderer_data && p->renderer_data->swapchain) {
        DXGI_SWAP_CHAIN_DESC description{};
        if(SUCCEEDED(static_cast<IDXGISwapChain*>(p->renderer_data->swapchain)->GetDesc(&description)))
            mhr_set_game_window(description.OutputWindow);
    }
    mhr_gui_enable(functions->log_info);
    if(!functions->on_message(on_message) || !functions->on_present(on_present) ||
        !functions->on_device_reset(on_reset))return false;
    mhr_enable_window_reader();
    functions->log_info("MHRGyro: window-thread reader service ready; camera output follows the Lua game profile");
    return functions->on_lua_state_created(on_lua_created);
}
