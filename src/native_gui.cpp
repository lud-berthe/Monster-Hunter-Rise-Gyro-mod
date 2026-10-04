#include "native_gui.hpp"
#include <gyrolib/overlay.h>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using Microsoft::WRL::ComPtr;

struct MhrGui {
    std::mutex mutex;
    gl_overlay* overlay=nullptr;
    bool attached=true,initialized=false,faulted=false;
    uint32_t owner=0;
    void* swapchain=nullptr;
    void* queue=nullptr;
    ComPtr<ID3D12Device> device;
    int result=GL_OK;
    std::string error;
};
namespace {
std::mutex registry_mutex;
std::vector<std::shared_ptr<MhrGui>> registry;
std::atomic<bool> enabled=false;
MhrGuiLog logger=nullptr;

// Public REFramework resize callbacks may arrive on a different game thread
// from Present. All overlay render operations use this single private thread;
// callbacks wait for submission/cleanup before Present or ResizeBuffers proceeds.
class RenderWorker {
public:
    // Initialized before starting the thread that publishes into it.
    std::atomic<uint32_t> id=0;
private:
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::function<void()>> tasks;
    std::thread thread;
public:
    RenderWorker():thread([this]{
        id.store(GetCurrentThreadId());
        for(;;) {
            std::function<void()> task;
            {std::unique_lock lock(mutex);changed.wait(lock,[&]{return !tasks.empty();});
                task=std::move(tasks.front());tasks.pop_front();}
            task();
        }
    }){}
    void run(std::function<void()> operation) {
        if(id.load()==GetCurrentThreadId()){operation();return;}
        auto task=std::make_shared<std::packaged_task<void()>>(std::move(operation));
        auto done=task->get_future();
        {std::scoped_lock lock(mutex);tasks.emplace_back([task]{(*task)();});}
        changed.notify_one();done.get();
    }
};
// REFramework keeps plugins loaded until process exit. Deliberately process
// lifetime: no thread creation/join or DX12 cleanup in static destructors/DllMain.
RenderWorker* worker=nullptr;
std::vector<std::shared_ptr<MhrGui>> handles() {
    std::scoped_lock lock(registry_mutex);return registry;
}
void diagnostic(MhrGui& gui,int result,const char* context) {
    const std::string detail=gl_overlay_error();
    const std::string message=std::string(context)+": "+detail;
    if(message!=gui.error && logger)logger("MHRGyro GUI: %s",message.c_str());
    gui.result=result;gui.error=message;
}
bool shutdown(MhrGui& gui) {
    if(!gui.initialized)return true;
    const auto result=gl_overlay_dx12_shutdown(gui.overlay);
    const bool removed=gui.device && FAILED(gui.device->GetDeviceRemovedReason());
    if(result!=GL_OK)diagnostic(gui,result,"renderer shutdown");
    if(result!=GL_OK && !removed)return false; // Keep live resources after a GPU timeout.
    gui.initialized=false;gui.swapchain=nullptr;gui.queue=nullptr;gui.device.Reset();
    return true;
}
void collect() {
    // Called only by the render worker. Resource locks exclude window/owner
    // users while detach + renderer shutdown + destroy are completed.
    for(const auto& gui:handles()) {
        std::scoped_lock lock(gui->mutex);
        if(gui->attached || !gui->overlay || !shutdown(*gui))continue;
        const auto result=gl_overlay_destroy(gui->overlay);
        if(result==GL_OK)gui->overlay=nullptr;
        else diagnostic(*gui,result,"destroy");
    }
    std::scoped_lock lock(registry_mutex);
    registry.erase(std::remove_if(registry.begin(),registry.end(),[](const auto& gui){
        std::scoped_lock guard(gui->mutex);return !gui->overlay;
    }),registry.end());
}
int request_open(bool open) {
    int result=GL_UNAVAILABLE;
    for(const auto& gui:handles()) {
        std::scoped_lock lock(gui->mutex);
        if(gui->attached && gui->overlay && (!open || (gui->initialized && !gui->faulted)))
            result=gl_overlay_set_open(gui->overlay,open?1:0);
    }
    return result;
}
}
void mhr_gui_enable(MhrGuiLog log) {
    logger=log;
    if(!worker)worker=new RenderWorker();
    enabled.store(true);
}
bool mhr_gui_available(){return enabled.load();}
std::shared_ptr<MhrGui> mhr_gui_attach(gl_context* context) {
    if(!mhr_gui_available())return {};
    auto gui=std::make_shared<MhrGui>();gui->owner=GetCurrentThreadId();
    gui->overlay=gl_overlay_create(context,GL_OVERLAY_ABI_VERSION);
    if(!gui->overlay){if(logger)logger("MHRGyro GUI: create: %s",gl_overlay_error());return {};}
    {std::scoped_lock lock(registry_mutex);registry.push_back(gui);}
    return gui;
}
int mhr_gui_process(const std::shared_ptr<MhrGui>& gui) {
    if(!gui)return GL_UNAVAILABLE;
    std::scoped_lock lock(gui->mutex);
    if(!gui->overlay || !gui->attached)return GL_UNAVAILABLE;
    const auto result=gl_overlay_process(gui->overlay);gui->result=result;
    return result;
}
bool mhr_gui_detach(const std::shared_ptr<MhrGui>& gui) {
    if(!gui)return true;
    std::scoped_lock lock(gui->mutex);
    if(!gui->attached)return true;
    const auto result=gl_overlay_detach(gui->overlay);
    if(result!=GL_OK){diagnostic(*gui,result,"owner detach");return false;}
    gui->attached=false;return true;
}
int mhr_gui_update(const std::shared_ptr<MhrGui>& gui,gl_context* context,uint64_t now,
                   const gl_host_state* host,gl_output* output) {
    // Commit UI commands and capture before processing motion.
    if(gui) {
        const auto result=mhr_gui_process(gui);
        if(result!=GL_OK){if(output)*output={};return result;}
    }
    const auto result=gl_update(context,now,host,output);
    // GyroLib publishes the completed frame itself; no second process call.
    return result;
}
void mhr_gui_present(const MhrGuiRenderer& renderer) {
    if(!worker || !enabled.load())return;
    worker->run([renderer]{
        collect();
        static auto previous=std::chrono::steady_clock::now();
        const auto now=std::chrono::steady_clock::now();
        const double delta=std::clamp(std::chrono::duration<double>(now-previous).count(),0.001,1.0);
        previous=now;
        for(const auto& gui:handles()) {
            std::scoped_lock lock(gui->mutex);
            if(!gui->attached || !gui->overlay)continue;
            if(renderer.type!=1 || !renderer.swapchain || !renderer.queue || !renderer.device) {
                gl_overlay_set_open(gui->overlay,0); // No capture without a usable renderer.
                continue;
            }
            if(gui->initialized && (gui->swapchain!=renderer.swapchain || gui->queue!=renderer.queue ||
                gui->device.Get()!=renderer.device)) {
                if(!shutdown(*gui))continue;
                gui->faulted=false;
            }
            if(gui->faulted)continue;
            if(!gui->initialized) {
                ComPtr<IDXGISwapChain3> swapchain;
                auto* original=static_cast<IDXGISwapChain*>(renderer.swapchain);
                if(FAILED(original->QueryInterface(IID_PPV_ARGS(&swapchain)))) {
                    gui->error="IDXGISwapChain3 unavailable";gui->faulted=true;continue;
                }
                DXGI_SWAP_CHAIN_DESC description{};
                if(FAILED(swapchain->GetDesc(&description))){gui->error="Swapchain description unavailable";continue;}
                const gl_overlay_dx12_desc desc{sizeof(desc),GL_OVERLAY_ABI_VERSION,0,0,
                    description.OutputWindow,swapchain.Get(),renderer.queue};
                const auto result=gl_overlay_dx12_init(gui->overlay,&desc);
                if(result!=GL_OK){diagnostic(*gui,result,"DX12/SDR initialization");gui->faulted=true;continue;}
                gui->initialized=true;gui->swapchain=renderer.swapchain;gui->queue=renderer.queue;
                gui->device=static_cast<ID3D12Device*>(renderer.device);
                gui->error.clear();
                if(logger)logger("MHRGyro GUI: independent DX12 renderer ready on thread %lu",GetCurrentThreadId());
            }
            DXGI_SWAP_CHAIN_DESC description{};
            if(FAILED(static_cast<IDXGISwapChain*>(renderer.swapchain)->GetDesc(&description)))continue;
            const UINT dpi=GetDpiForWindow(description.OutputWindow);
            const auto result=gl_overlay_dx12_render(gui->overlay,delta,dpi?float(dpi)/96.0f:1.0f);
            if(result!=GL_OK){diagnostic(*gui,result,"render");gl_overlay_set_open(gui->overlay,0);gui->faulted=true;}
        }
    });
}
bool mhr_gui_reset() {
    if(!worker)return true;
    bool success=true;
    worker->run([&]{
        for(const auto& gui:handles()) {
            std::scoped_lock lock(gui->mutex);
            if(!shutdown(*gui))success=false;
            else gui->faulted=false;
        }
        collect();
    });
    return success;
}
uint32_t mhr_gui_capture() {
    uint32_t capture=0;
    for(const auto& gui:handles()) {
        std::scoped_lock lock(gui->mutex);
        if(gui->overlay)capture|=gl_overlay_capture(gui->overlay);
    }
    return capture;
}
void mhr_gui_close(){request_open(false);}
bool mhr_gui_message(void* window,unsigned int message,uint64_t wparam,int64_t lparam) {
    // Always preserve Alt+F4 and all window/focus/device lifecycle processing.
    if(message==WM_SYSKEYDOWN && wparam==VK_F4 && (lparam&(1ll<<29)))return true;
    uint32_t capture=0;
    for(const auto& gui:handles()) {
        std::scoped_lock lock(gui->mutex);
        if(gui->overlay)capture|=gl_overlay_win32_message(gui->overlay,window,message,wparam,lparam);
    }
    if(message==WM_INPUT) {
        RAWINPUTHEADER header{};UINT size=sizeof(header);
        if(GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam),RID_HEADER,&header,&size,sizeof(header))!=UINT(-1)) {
            const auto all=mhr_gui_capture();
            const bool blocked=(header.dwType==RIM_TYPEMOUSE && (all&GL_OVERLAY_CAPTURE_MOUSE)) ||
                (header.dwType==RIM_TYPEKEYBOARD && (all&GL_OVERLAY_CAPTURE_KEYBOARD));
            if(blocked){DefWindowProcW(static_cast<HWND>(window),message,WPARAM(wparam),LPARAM(lparam));return false;}
        }
    }
    return capture==0;
}
int mhr_gui_state(lua_State* l) {
    lua_newtable(l);
    lua_pushboolean(l,mhr_gui_available());lua_setfield(l,-2,"available");
    lua_pushinteger(l,mhr_gui_capture());lua_setfield(l,-2,"capture");
    lua_pushinteger(l,worker?worker->id.load():0);lua_setfield(l,-2,"render_thread");
    for(const auto& gui:handles()) {
        std::scoped_lock lock(gui->mutex);
        if(!gui->attached)continue;
        lua_pushboolean(l,gui->initialized);lua_setfield(l,-2,"ready");
        lua_pushinteger(l,gui->owner);lua_setfield(l,-2,"owner_thread");
        lua_pushinteger(l,gui->result);lua_setfield(l,-2,"result");
        lua_pushlstring(l,gui->error.data(),gui->error.size());lua_setfield(l,-2,"error");
    }
    return 1;
}
int mhr_gui_set_open(lua_State* l) {
    luaL_checktype(l,1,LUA_TBOOLEAN);
    lua_pushinteger(l,request_open(lua_toboolean(l,1)!=0));return 1;
}
