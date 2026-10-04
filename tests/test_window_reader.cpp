#include "bridge.hpp"
#include "window_reader.hpp"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <chrono>
#include <atomic>
#include <cstdio>
#include <future>
#include <memory>
#include <thread>
static std::shared_ptr<MhrWindowReader> reader;
static HANDLE lock_reached,allow_lock,process_finished,allow_next;
static std::atomic<bool> before_hook_executed=false;
static void before_reader_lock() {before_hook_executed=true;SetEvent(lock_reached);WaitForSingleObject(allow_lock,5000);}
static void after_reader_process() {if(before_hook_executed){SetEvent(process_finished);WaitForSingleObject(allow_next,5000);}}
static int tick(lua_State* l){return mhr_window_reader_tick(l,reader);}
static LRESULT CALLBACK window_proc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam) {
    if(!mhr_on_window_message(hwnd,message,wparam,lparam))return 0;
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(hwnd,message,wparam,lparam);
}
int main() {
    // Hidden test window, a separate owner thread, real GyroLib core, no SDL,
    // no sensors and no settings directory. Exercise the actual dispatcher.
    mhr_enable_window_reader();
    std::promise<HWND> ready;
    auto result=ready.get_future();
    std::thread owner([&] {
        WNDCLASSW klass{};klass.lpfnWndProc=window_proc;
        klass.hInstance=GetModuleHandleW(nullptr);klass.lpszClassName=L"MHRGyroTransportTest";
        RegisterClassW(&klass);
        const auto hwnd=CreateWindowW(klass.lpszClassName,L"MHRGyro hidden test",WS_OVERLAPPED,
            0,0,100,100,nullptr,nullptr,klass.hInstance,nullptr);
        mhr_set_game_window(hwnd);ready.set_value(hwnd);
        if(!hwnd)return;
        MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
        UnregisterClassW(klass.lpszClassName,klass.hInstance);
    });
    const auto hwnd=result.get();
    if(!hwnd){owner.join();return 1;}
    reader=mhr_request_window_reader("",false);
    auto* l=luaL_newstate();luaL_openlibs(l);mhr_register(l);
    lua_pushcfunction(l,tick);lua_setglobal(l,"transport_tick");
    const char* initialize=R"(
      host={bindings_verified=true,menu_open=false,paused=false,focused=true,camera_allowed=true,
        panel_open=false,contexts={
          {id=401,label='Camera',description='Memory-only transport test',priority=0,active=true,available=true},
          {id=4294967295,label='Boundary',description='Exact uint64 tab',priority=10,active=true,available=true}}}
      snapshot=transport_tick(nil,host,{
        {op='initialize'},
        {op='set',id='context.401.sensitivity_x',value=4},
        {op='set',id='context.401.sensitivity_x',value=7}})
      function refresh()
        snapshot=transport_tick(nil,host,{})
        local ready=false
        for _,s in ipairs(snapshot.settings or {}) do
          if s.id=='context.401.sensitivity_x' then ready=s.value==7 end
        end
        return ready and snapshot.active_context==4294967295
      end
      function verify()
        assert(snapshot.update_result==0 and snapshot.settings_save_result==-2)
        assert(snapshot.last_command_result==0 and snapshot.window_transport)
        assert(not snapshot.reader_available and snapshot.yaw_degrees==0 and snapshot.pitch_degrees==0)
        assert(snapshot.reader_owner_thread==mhr_gyro_native.window_state().window_thread)
        assert(snapshot.reader_owner_thread~=mhr_gyro_native.window_state().caller_thread)
        local boundary=false
        for _,tab in ipairs(snapshot.tabs) do
          if tab.context_id==4294967295 then boundary=tab.id==4294967296 and math.type(tab.id)=='integer' end
        end
        assert(boundary and snapshot.active_context==4294967295)
      end
    )";
    bool passed=luaL_dostring(l,initialize)==LUA_OK;
    bool observed=false;
    for(int n=0;passed && n<200 && !observed;++n) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        lua_getglobal(l,"refresh");passed=lua_pcall(l,0,1,0)==LUA_OK;
        if(passed){observed=lua_toboolean(l,-1)!=0;lua_pop(l,1);}
    }
    if(passed && observed)passed=luaL_dostring(l,"verify()")==LUA_OK;
    else passed=false;
    if(passed) {
        // The owner can capture its dispatch clock, then wait while the producer
        // publishes a newer observation. It must not underflow the age and
        // disable that fresh view. Synchronize the actual two-thread path.
        lock_reached=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        allow_lock=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        process_finished=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        allow_next=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        mhr_window_reader_test_hooks(before_reader_lock,after_reader_process);
        passed=luaL_dostring(l,"snapshot=transport_tick(nil,host,{})")==LUA_OK;
        passed=passed && WaitForSingleObject(lock_reached,2000)==WAIT_OBJECT_0;
        if(passed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            passed=luaL_dostring(l,"snapshot=transport_tick(nil,host,{})")==LUA_OK;
        }
        if(passed) {
            // The window owner is blocked before taking the reader lock, so
            // its timer cannot invalidate a previously successful snapshot.
            std::this_thread::sleep_for(std::chrono::milliseconds(125));
            passed=luaL_dostring(l,R"(
              snapshot=transport_tick(nil,host,{})
              assert(snapshot.update_result==-2 and #snapshot.tabs==2)
              assert(not snapshot.suppress_native_right_stick and not snapshot.suppress_native_right_touchpad)
              assert(snapshot.yaw_degrees==0 and not snapshot.recenter_requested)
              assert(not snapshot.long_press_filter.enabled and #snapshot.filter_results==0)
            )")==LUA_OK;
        }
        SetEvent(allow_lock);
        passed=passed && WaitForSingleObject(process_finished,2000)==WAIT_OBJECT_0;
        if(passed)passed=luaL_dostring(l,R"(
          snapshot=transport_tick(nil,host,{})
          assert(snapshot.active_context==4294967295,
            'fresh observation invalidated by dispatcher clock sampled before producer publication')
        )")==LUA_OK;
        mhr_window_reader_test_hooks(nullptr,nullptr);SetEvent(allow_next);
        // Drain the callback before closing the events it may still use.
        SendMessageW(hwnd,WM_NULL,0,0);
        for(HANDLE event:{lock_reached,allow_lock,process_finished,allow_next})CloseHandle(event);
    }
    if(passed) {
        // A delayed producer must suspend motion without removing registered
        // settings tabs and recreating them on the next game observation.
        std::this_thread::sleep_for(std::chrono::milliseconds(220));
        passed=luaL_dostring(l,R"(
          snapshot=transport_tick(nil,host,{})
          assert(#snapshot.tabs==2, 'stale observation removed configuration tabs')
          assert(snapshot.active_context==0 and snapshot.yaw_degrees==0 and snapshot.pitch_degrees==0)
          assert(snapshot.bindings_verified and snapshot.host_capabilities==4)
          assert(not snapshot.suppress_native_right_stick and not snapshot.recenter_requested)
          for _,tab in ipairs(snapshot.tabs) do assert(not tab.active and not tab.available) end
          local editable=false
          for _,s in ipairs(snapshot.settings) do
            if s.id=='context.401.sensitivity_x' then editable=s.visible and s.value==7 end
          end
          assert(editable, 'stale observation removed the saved configuration')
        )")==LUA_OK;
        observed=false;
        for(int n=0;passed && n<200 && !observed;++n) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            lua_getglobal(l,"refresh");passed=lua_pcall(l,0,1,0)==LUA_OK;
            if(passed){observed=lua_toboolean(l,-1)!=0;lua_pop(l,1);}
        }
        if(passed && observed)passed=luaL_dostring(l,"verify()")==LUA_OK;
        else passed=false;
    }
    if(passed) {
        // Transfer a detached backend as a Lua reset would, preserving SDK/SDL
        // ownership while discarding the old producer's commands and output.
        const auto previous=reader;
        mhr_detach_window_reader(reader);
        reader=mhr_request_window_reader("",false);
        passed=reader==previous && luaL_dostring(l,R"(
          snapshot=transport_tick(nil,host,{})
          assert(snapshot.update_result==-2 and snapshot.active_context==0)
          assert(snapshot.reader_resume_count==1 and snapshot.yaw_degrees==0 and snapshot.pitch_degrees==0)
        )")==LUA_OK;
        observed=false;
        for(int n=0;passed && n<200 && !observed;++n) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            lua_getglobal(l,"refresh");passed=lua_pcall(l,0,1,0)==LUA_OK;
            if(passed){observed=lua_toboolean(l,-1)!=0;lua_pop(l,1);}
        }
        if(passed && observed)passed=luaL_dostring(l,"verify(); assert(snapshot.reader_resume_count==1)")==LUA_OK;
        else passed=false;
    }
    if(passed) {
        // A queued filter verdict must survive the owner tick and be consumed
        // once, with its full integer token, even though this fixture has no pad.
        passed=luaL_dostring(l,R"(
          local event={op='filter',token=9007199254740993,key=2.0,event=0.0,
            timestamp_ns=mhr_gyro_native.now_ns(),view=401.0,device=123,
            eligible=true,native_hold=false}
          transport_tick(nil,host,{event})
        )")==LUA_OK;
        SendMessageW(hwnd,WM_NULL,0,0);
        for(int n=0;passed && n<40;++n) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            passed=luaL_dostring(l,R"(
              snapshot=transport_tick(nil,host,{})
              for _,result in ipairs(snapshot.filter_results or {}) do
                assert(not filter_seen and result.token==9007199254740993 and math.type(result.token)=='integer')
                assert(result.decision==0 and not result.current and result.event==0)
                filter_seen=true
              end
            )")==LUA_OK;
        }
        if(passed)passed=luaL_dostring(l,"assert(filter_seen, 'filter verdict lost across the window transport')")==LUA_OK;
    }
    if(!passed)std::fprintf(stderr,"Window transport: %s\n",lua_tostring(l,-1)?lua_tostring(l,-1):"No completed snapshot");
    mhr_close_window_reader(reader);
    bool closed=false;
    for(int n=0;n<200 && !closed;++n) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        mhr_window_state(l);lua_getfield(l,-1,"reader_count");closed=lua_tointeger(l,-1)==0;lua_pop(l,2);
    }
    if(passed && closed) {
        // A removed script must not leave its backend/helper alive indefinitely.
        reader=mhr_request_window_reader("",false);
        passed=luaL_dostring(l,initialize)==LUA_OK;observed=false;
        for(int n=0;passed && n<200 && !observed;++n) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            lua_getglobal(l,"refresh");passed=lua_pcall(l,0,1,0)==LUA_OK;
            if(passed){observed=lua_toboolean(l,-1)!=0;lua_pop(l,1);}
        }
        passed=passed && observed;
        mhr_detach_window_reader(reader);reader.reset();closed=false;
        for(int n=0;n<600 && !closed;++n) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            mhr_window_state(l);lua_getfield(l,-1,"reader_count");closed=lua_tointeger(l,-1)==0;lua_pop(l,2);
        }
    }
    reader.reset();lua_close(l);PostMessageW(hwnd,WM_CLOSE,0,0);owner.join();
    if(!closed){std::fprintf(stderr,"Window reader was not disposed on its owner thread\n");passed=false;}
    if(passed)std::puts("Window transport: separate threads, reset transfer, bounded expiry and owner-thread cleanup passed");
    return passed?0:1;
}
