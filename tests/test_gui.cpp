// Hidden WARP graphics fixture adapted from GyroLib's MIT-licensed tests.
// This tests the Rise adapter, using the installed DLL; no GyroLib build/write.
#include "native_gui.hpp"
#include <gyrolib/overlay.h>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"Rise GUI:%d %s\n",__LINE__,#x);throw std::runtime_error(#x);}}while(0)
LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
struct Graphics {
    HWND window{};ComPtr<IDXGIFactory4> factory;ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swap;ComPtr<ID3D12DescriptorHeap> heap;ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;HANDLE event{};uint64_t value{};
    void drain(){CHECK(SUCCEEDED(queue->Signal(fence.Get(),++value)));CHECK(SUCCEEDED(fence->SetEventOnCompletion(value,event)));CHECK(WaitForSingleObject(event,5000)==WAIT_OBJECT_0);}
    Graphics(){WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"GyroLibOverlayHiddenTest";RegisterClassW(&cls);
        window=CreateWindowW(cls.lpszClassName,L"hidden overlay fixture",WS_POPUP,0,0,1024,720,nullptr,nullptr,cls.hInstance,nullptr);CHECK(window);
        CHECK(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));ComPtr<IDXGIAdapter> warp;CHECK(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
        CHECK(SUCCEEDED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;CHECK(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))));
        DXGI_SWAP_CHAIN_DESC1 sc{};sc.Width=1024;sc.Height=720;sc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sc.SampleDesc.Count=1;sc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc.BufferCount=2;sc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;ComPtr<IDXGISwapChain1> first;
        CHECK(SUCCEEDED(factory->CreateSwapChainForHwnd(queue.Get(),window,&sc,nullptr,nullptr,&first)));CHECK(SUCCEEDED(first.As(&swap)));
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;CHECK(SUCCEEDED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap))));
        CHECK(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));
        CHECK(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));CHECK(SUCCEEDED(list->Close()));
        CHECK(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CHECK(event);
    }
    void begin(){drain();CHECK(SUCCEEDED(allocator->Reset()));CHECK(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));}
    void submit(){CHECK(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);drain();}
    static D3D12_RESOURCE_BARRIER barrier(ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};return b;}
    void clear(){begin();ComPtr<ID3D12Resource> buffer;CHECK(SUCCEEDED(swap->GetBuffer(swap->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&buffer))));
        auto b=barrier(buffer.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ResourceBarrier(1,&b);
        auto rtv=heap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(buffer.Get(),nullptr,rtv);const float color[]={.02f,.04f,.06f,1};list->ClearRenderTargetView(rtv,color,0,nullptr);
        std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);submit();}
    std::vector<uint32_t> pixels(){begin();ComPtr<ID3D12Resource> buffer;CHECK(SUCCEEDED(swap->GetBuffer(swap->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&buffer))));
        auto desc=buffer->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total{};device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&total);
        D3D12_HEAP_PROPERTIES props{};props.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=total;
        rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> readback;
        CHECK(SUCCEEDED(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))));
        auto b=barrier(buffer.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&b);
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=buffer.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);submit();
        void* mapped{};D3D12_RANGE range{0,size_t(total)};CHECK(SUCCEEDED(readback->Map(0,&range,&mapped)));std::vector<uint32_t> out(size_t(desc.Width)*desc.Height);
        for(unsigned y=0;y<desc.Height;++y)std::memcpy(out.data()+y*desc.Width,static_cast<char*>(mapped)+y*footprint.Footprint.RowPitch,size_t(desc.Width)*4);
        D3D12_RANGE empty{};readback->Unmap(0,&empty);return out;
    }
    ~Graphics(){if(event)CloseHandle(event);swap.Reset();if(window)DestroyWindow(window);}
};

int main()try{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    Graphics g; mhr_gui_enable();
    auto* c=gl_create(GL_ABI_VERSION);CHECK(c);
    const gl_gameplay_context view{1,"Caméra normale","Normal camera integration",0};
    CHECK(gl_register_gameplay_context(c,&view)==GL_OK);
    CHECK(gl_set_gameplay_context_output_target(c,1,GL_OUTPUT_CAMERA)==GL_OK);
    // Current SDK requires a connected controller to edit device settings.
    gl_endpoint fixture{};fixture.id=fixture.physical_id=1;fixture.source=GL_SOURCE_SDL;
    fixture.connected=1;fixture.caps.gyro=1;std::strcpy(fixture.name,"Memory controller");
    CHECK(gl_register_endpoint(c,&fixture)==GL_OK);CHECK(gl_select_device(c,1)==GL_OK);
    const auto settings=std::filesystem::current_path()/("mhr-gui-test-"+std::to_string(GetCurrentProcessId())+".ini");
    CHECK(gl_set_settings_path(c,settings.string().c_str())==GL_OK);
    auto gui=mhr_gui_attach(c);CHECK(gui);
    const MhrGuiRenderer renderer{1,g.device.Get(),g.swap.Get(),g.queue.Get()};
    auto* l=luaL_newstate();
    bool controls_enabled=false;
    auto frame=[&]{
        gl_host_state host{};host.focused=host.camera_allowed=1;
        CHECK(gl_set_gameplay_context_state(c,1,1,1)==GL_OK);
        gl_output out{};static uint64_t now=1000000000;
        now+=16000000;
        if(controls_enabled){gl_controls controls{};controls.timestamp_ns=now;
            CHECK(gl_submit_controls(c,1,&controls)==GL_OK);}
        CHECK(mhr_gui_update(gui,c,now,&host,&out)==GL_OK);
        g.clear();mhr_gui_present(renderer);
    };
    frame();const auto closed=g.pixels();CHECK(mhr_gui_capture()==0);
    mhr_gui_state(l);lua_getfield(l,-1,"ready");CHECK(lua_toboolean(l,-1));lua_pop(l,1);
    lua_getfield(l,-1,"render_thread");CHECK(lua_tointeger(l,-1)!=GetCurrentThreadId());lua_settop(l,0);
    CHECK(!mhr_gui_message(g.window,WM_KEYDOWN,VK_F10,0));frame();
    CHECK(gl_panel_open(c) && mhr_gui_capture()==7);
    for(int i=0;i<3;++i)frame();const auto opened=g.pixels();
    CHECK(opened[100*1024+100]!=closed[100*1024+100]);
    CHECK(mhr_gui_message(g.window,WM_SIZE,0,0));
    CHECK(mhr_gui_message(g.window,WM_KILLFOCUS,0,0));
    CHECK(mhr_gui_message(g.window,WM_SYSKEYDOWN,VK_F4,1ll<<29));
    CHECK(!mhr_gui_message(g.window,WM_KEYDOWN,VK_TAB,0));
    CHECK(!mhr_gui_message(g.window,WM_KEYUP,VK_TAB,0));
    double before{},after{};CHECK(gl_setting_get(c,"context.1.sensitivity_x",&before)==GL_OK);
    const auto pos=MAKELPARAM(700,282);
    CHECK(!mhr_gui_message(g.window,WM_MOUSEMOVE,0,pos));
    CHECK(!mhr_gui_message(g.window,WM_LBUTTONDOWN,0,pos));frame();
    CHECK(!mhr_gui_message(g.window,WM_LBUTTONUP,0,pos));frame();frame();
    CHECK(gl_setting_get(c,"context.1.sensitivity_x",&after)==GL_OK && after!=before);
    CHECK(gl_get_settings_save_result(c)==GL_OK && std::filesystem::exists(settings));
    auto* loaded=gl_create(GL_ABI_VERSION);CHECK(loaded);
    CHECK(gl_register_gameplay_context(loaded,&view)==GL_OK);
    CHECK(gl_load_settings(loaded,settings.string().c_str())==GL_OK);
    double persisted{};CHECK(gl_setting_get(loaded,"context.1.sensitivity_x",&persisted)==GL_OK && persisted==after);
    gl_destroy(loaded);
    // SDL polls controls at this frame's time before the core advances its clock.
    // The pre-update UI snapshot hides all five families as future-dated input.
    // A post-update publication must expose them in the actual rendered GUI.
    fixture.caps={1,3,3,3,3,1,0};
    CHECK(gl_register_endpoint(c,&fixture)==GL_OK);CHECK(gl_select_device(c,1)==GL_OK);
    CHECK(gl_setting_set(c,"context.1.gyro.activation",GL_HOLD)==GL_OK);
    frame();frame();const auto no_controls=g.pixels();
    controls_enabled=true;frame();frame();const auto with_controls=g.pixels();
    bool activator_pixels_changed=false;
    for(unsigned y=400;y<650;++y)for(unsigned x=100;x<900;++x)
        activator_pixels_changed|=no_controls[y*1024+x]!=with_controls[y*1024+x];
    CHECK(activator_pixels_changed);
    // Resize invoked from the owner/window thread; the adapter drains and
    // shuts down on its own renderer thread before ResizeBuffers proceeds.
    CHECK(mhr_gui_reset());g.drain();
    CHECK(SUCCEEDED(g.swap->ResizeBuffers(2,800,600,DXGI_FORMAT_R8G8B8A8_UNORM,0)));
    SetWindowPos(g.window,nullptr,0,0,800,600,SWP_NOZORDER|SWP_NOACTIVATE);
    frame();mhr_gui_state(l);lua_getfield(l,-1,"ready");CHECK(lua_toboolean(l,-1));lua_settop(l,0);
    CHECK(gl_set_menu_key(c,8)==GL_OK);frame();
    mhr_gui_message(g.window,WM_KEYDOWN,VK_F8,0);frame();CHECK(!gl_panel_open(c));
    mhr_gui_message(g.window,WM_KEYDOWN,VK_F10,0);frame();CHECK(!gl_panel_open(c));
    lua_pushboolean(l,1);mhr_gui_set_open(l);CHECK(lua_tointeger(l,-1)==GL_OK);lua_settop(l,0);
    frame();CHECK(gl_panel_open(c));mhr_gui_close();frame();CHECK(!gl_panel_open(c));
    CHECK(mhr_gui_detach(gui));gl_destroy(c); // Context may die before renderer cleanup.
    CHECK(mhr_gui_capture()==0);CHECK(mhr_gui_reset());gui.reset();lua_close(l);
    std::filesystem::remove(settings);
    std::puts("Rise native GUI: WARP pixels, mouse edits, persisted owner settings, independent render thread, shortcut, resize and detach passed");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
