#include "window_reader.hpp"
#include "bridge.hpp"
#include "native_gui.hpp"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
// Copy plain data between two independent Lua VMs. No borrowed Lua pointers,
// functions, userdata or GyroLib contexts cross their owning thread.
struct Value {
    enum Kind { Nil,Bool,Integer,Number,String,Table } kind=Nil;
    bool boolean=false;
    lua_Integer integer=0;
    double number=0;
    std::string string;
    std::vector<std::pair<Value,Value>> fields;
};
uint64_t clock_ns() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
Value text(const char* s) { Value v;v.kind=Value::String;v.string=s?s:"";return v; }
Value number(double n) { Value v;v.kind=Value::Number;v.number=n;return v; }
Value boolean(bool b) { Value v;v.kind=Value::Bool;v.boolean=b;return v; }
Value table() { Value v;v.kind=Value::Table;return v; }
Value* field(Value& v,const char* name) {
    for(auto& p:v.fields) if(p.first.kind==Value::String && p.first.string==name) return &p.second;
    return nullptr;
}
const Value* field(const Value& v,const char* name) {
    for(const auto& p:v.fields) if(p.first.kind==Value::String && p.first.string==name) return &p.second;
    return nullptr;
}
void set(Value& v,const char* name,Value value) {
    if(auto* p=field(v,name)) *p=std::move(value);
    else v.fields.emplace_back(text(name),std::move(value));
}
double numeric(const Value* v) {
    if(!v) return 0;
    return v->kind==Value::Integer?static_cast<double>(v->integer):v->number;
}
bool truth(const Value& v,const char* name) {
    const auto* p=field(v,name);return p && p->kind==Value::Bool && p->boolean;
}
bool false_value(const Value& v,const char* name) {
    const auto* p=field(v,name);return p && p->kind==Value::Bool && !p->boolean;
}
Value copy(lua_State* l,int index,size_t& budget,int depth=0) {
    if(!budget-- || depth>16) throw std::runtime_error("Reader data limit exceeded");
    index=lua_absindex(l,index);Value v;
    switch(lua_type(l,index)) {
    case LUA_TBOOLEAN:v.kind=Value::Bool;v.boolean=lua_toboolean(l,index)!=0;break;
    case LUA_TNUMBER:
        if(lua_isinteger(l,index)){v.kind=Value::Integer;v.integer=lua_tointeger(l,index);}
        else {v.kind=Value::Number;v.number=lua_tonumber(l,index);}
        break;
    case LUA_TSTRING:{
        size_t size=0;const auto* s=lua_tolstring(l,index,&size);
        if(size>8192) throw std::runtime_error("Reader string limit exceeded");
        v.kind=Value::String;v.string.assign(s,size);break;
    }
    case LUA_TTABLE:
        v.kind=Value::Table;lua_pushnil(l);
        while(lua_next(l,index)) {
            auto key=copy(l,-2,budget,depth+1);
            auto value=copy(l,-1,budget,depth+1);
            if(key.kind!=Value::Nil) v.fields.emplace_back(std::move(key),std::move(value));
            lua_pop(l,1);
        }
        break;
    default:break;
    }
    return v;
}
void push(lua_State* l,const Value& v) {
    switch(v.kind) {
    case Value::Nil:lua_pushnil(l);break;
    case Value::Bool:lua_pushboolean(l,v.boolean);break;
    case Value::Integer:lua_pushinteger(l,v.integer);break;
    case Value::Number:lua_pushnumber(l,v.number);break;
    case Value::String:lua_pushlstring(l,v.string.data(),v.string.size());break;
    case Value::Table:
        lua_createtable(l,0,static_cast<int>(v.fields.size()));
        for(const auto& p:v.fields){push(l,p.first);push(l,p.second);lua_settable(l,-3);}break;
    }
}
uint32_t active_view(const Value& host) {
    const auto* views=field(host,"contexts");if(!views)return 0;
    uint32_t selected=0;double best=0;
    for(const auto& p:views->fields) {
        const auto& view=p.second;
        const double id=numeric(field(view,"id"));
        const double priority=numeric(field(view,"priority"));
        if(!truth(view,"active") || !truth(view,"available") || !std::isfinite(id) ||
            id<1 || id>4294967295.0 || id!=std::floor(id) || !std::isfinite(priority))continue;
        if(!selected || priority>best || (priority==best && id<selected)) {
            selected=static_cast<uint32_t>(id);best=priority;
        }
    }
    return selected;
}
bool safe(const Value& host) {
    bool menu_camera=false;
    const auto selected=active_view(host);
    if(const auto* views=field(host,"contexts");views && views->kind==Value::Table)
        for(const auto& entry:views->fields)
            if(numeric(field(entry.second,"id"))==selected && truth(entry.second,"camera_in_menu"))menu_camera=true;
    return truth(host,"bindings_verified") && truth(host,"camera_allowed") && truth(host,"focused") &&
        (false_value(host,"menu_open") || (truth(host,"menu_open") && menu_camera)) &&
        false_value(host,"paused") && !truth(host,"panel_open") && !mhr_gui_capture() && selected!=0;
}
thread_local bool dispatching=false;
thread_local bool shutdown_requested=false;
std::atomic<bool> enabled=false;
std::mutex readers_mutex;
std::vector<std::shared_ptr<MhrWindowReader>> readers;
constexpr uint64_t reset_grace_ns=2000000000ull;
#ifdef MHR_WINDOW_READER_TEST
std::atomic<void(*)()> test_before_lock=nullptr,test_after_process=nullptr;
#endif
#ifdef _WIN32
std::atomic<HWND> game_window=nullptr;
HWND timer_window=nullptr; // Accessed only on the window owner thread.
UINT kick_message() {
    static const UINT message=RegisterWindowMessageW(L"MHRGyro.WindowReader.0.1");
    return message;
}
UINT_PTR timer_id() {return reinterpret_cast<UINT_PTR>(&timer_window);}
BOOL CALLBACK discover(HWND hwnd,LPARAM user) {
    DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
    if(pid==GetCurrentProcessId() && IsWindowVisible(hwnd) && !GetWindow(hwnd,GW_OWNER)) {
        *reinterpret_cast<HWND*>(user)=hwnd;return FALSE;
    }
    return TRUE;
}
HWND find_window() {
    auto hwnd=game_window.load();
    if(!hwnd || !IsWindow(hwnd)) {
        hwnd=nullptr;EnumWindows(discover,reinterpret_cast<LPARAM>(&hwnd));
        if(hwnd)game_window.store(hwnd);
    }
    return hwnd;
}
bool focused(HWND hwnd) {
    return hwnd && IsWindow(hwnd) && !IsIconic(hwnd) && GetAncestor(GetForegroundWindow(),GA_ROOT)==hwnd;
}
void wake() {if(auto hwnd=find_window())PostMessageW(hwnd,kick_message(),0,0);}
#else
void wake() {}
#endif
}

struct MhrWindowReader {
    std::mutex mutex;
    bool closing=false;
    bool hardware=true;
    bool detached=false;
    uint64_t detached_at=0;
    uint32_t resume_count=0;
    uint64_t request_time=0,request_serial=0,processed_serial=0;
    uint64_t snapshot_time=0;
    uint32_t owner_thread=0;
    Value host=table(),commands=table(),snapshot=table();
    Value filter_results=table();
    double yaw=0,pitch=0;
    double recenter_fraction=0;
    void clear_output(){yaw=pitch=recenter_fraction=0;filter_results=table();}
    std::string directory;
    lua_State* vm=nullptr; // Created, used and closed only by window dispatcher.
    int reference=LUA_NOREF;
};

namespace {
void dispose(MhrWindowReader& reader) {
    if(reader.vm)lua_close(reader.vm);
    reader.vm=nullptr;reader.reference=LUA_NOREF;reader.clear_output();
}
bool invoke(MhrWindowReader& reader,int arguments,int results) {
    if(lua_pcall(reader.vm,arguments,results,0)==LUA_OK)return true;
    const char* error=lua_tostring(reader.vm,-1);
    set(reader.snapshot,"reader_error",text(error?error:"Window reader call failed"));
    set(reader.snapshot,"reader_available",boolean(false));
    set(reader.snapshot,"update_result",number(-2));
    reader.clear_output();lua_settop(reader.vm,0);return false;
}
void process(MhrWindowReader& reader,bool window_focused,uint32_t tid) {
#ifdef MHR_WINDOW_READER_TEST
    if(auto hook=test_before_lock.load())hook();
#endif
    std::scoped_lock lock(reader.mutex);
    // The producer timestamps observations under this same lock. Sampling
    // before acquiring it can make a new request newer than `now`; unsigned
    // subtraction would then turn a fresh view into an expired observation.
    const auto now=clock_ns();
    if(reader.closing){dispose(reader);return;}
    if(reader.detached && now-reader.detached_at>=reset_grace_ns) {
        reader.closing=true;dispose(reader);return;
    }
    if(!reader.request_serial)return;
    // SDL acquisition stays on the window thread, never a camera job worker.
    if(reader.owner_thread && reader.owner_thread!=tid) {
        set(reader.snapshot,"reader_error",text("Window owner thread changed"));
        set(reader.snapshot,"reader_available",boolean(false));
        set(reader.snapshot,"update_result",number(-2));reader.clear_output();return;
    }
    const bool stale=now-reader.request_time>100000000;
    if(reader.processed_serial==reader.request_serial && !stale && !reader.detached)return;
    reader.owner_thread=tid;
    if(!reader.vm) {
        reader.vm=luaL_newstate();
        if(!reader.vm) {set(reader.snapshot,"reader_error",text("Cannot allocate reader VM"));return;}
        mhr_register(reader.vm);
        lua_getglobal(reader.vm,"mhr_gyro_native");lua_getfield(reader.vm,-1,"create");lua_remove(reader.vm,-2);
        lua_pushboolean(reader.vm,reader.hardware);lua_pushlstring(reader.vm,reader.directory.data(),reader.directory.size());
        if(!invoke(reader,2,1)){dispose(reader);return;}
        reader.reference=luaL_ref(reader.vm,LUA_REGISTRYINDEX);
    }
    // Keep SDL/Steam discovery alive briefly through a Lua reset. The old
    // producer is gone: no old commands, profile permissions or movement survive.
    auto host=reader.detached?table():reader.host;
    if(stale || reader.detached) {
        // A missed observation does not uninstall the integration. Preserve its
        // view definitions and hook declarations so the configuration cannot
        // disappear/reappear around this timeout; withdraw runtime permission.
        set(host,"camera_allowed",boolean(false));
        set(host,"focused",boolean(false));reader.clear_output();
        if(auto* views=field(host,"contexts");views && views->kind==Value::Table)
            for(auto& entry:views->fields)if(entry.second.kind==Value::Table) {
                set(entry.second,"active",boolean(false));
                set(entry.second,"available",boolean(false));
            }
    } else if(!window_focused) {set(host,"focused",boolean(false));reader.clear_output();}
    lua_rawgeti(reader.vm,LUA_REGISTRYINDEX,reader.reference);
    lua_getfield(reader.vm,-1,"tick");lua_insert(reader.vm,-2);
    push(reader.vm,host);push(reader.vm,reader.commands);
    reader.commands=table();reader.processed_serial=reader.request_serial;
    if(!invoke(reader,3,1))return;
    try {
        size_t budget=100000;auto snapshot=copy(reader.vm,-1,budget);lua_settop(reader.vm,0);
        const double previous_context=numeric(field(reader.snapshot,"active_context"));
        const double context=numeric(field(snapshot,"active_context"));
        if(previous_context!=context || !safe(host) || numeric(field(snapshot,"update_result"))!=0)
            reader.clear_output();
        if(safe(host) && numeric(field(snapshot,"update_result"))==0) {
            const double yaw=numeric(field(snapshot,"yaw_degrees")),pitch=numeric(field(snapshot,"pitch_degrees"));
            if(std::isfinite(yaw) && std::isfinite(pitch)){reader.yaw+=yaw;reader.pitch+=pitch;}
            const double fraction=numeric(field(snapshot,"recenter_fraction"));
            if(std::isfinite(fraction) && fraction>0 && fraction<=1)
                reader.recenter_fraction=1-(1-reader.recenter_fraction)*(1-fraction);
        }
        set(snapshot,"yaw_degrees",number(0));set(snapshot,"pitch_degrees",number(0));
        set(snapshot,"recenter_requested",boolean(false));set(snapshot,"recenter_fraction",number(0));
        if(const auto* results=field(snapshot,"filter_results");results && results->kind==Value::Table) {
            for(const auto& entry:results->fields) {
                if(reader.filter_results.fields.size()>=128) {reader.filter_results=table();break;}
                Value key;key.kind=Value::Integer;key.integer=reader.filter_results.fields.size()+1;
                reader.filter_results.fields.emplace_back(std::move(key),entry.second);
            }
        }
        set(snapshot,"filter_results",table());
        reader.snapshot=std::move(snapshot);
        reader.snapshot_time=now;
    } catch(const std::exception& error) {
        lua_settop(reader.vm,0);reader.clear_output();
        set(reader.snapshot,"reader_error",text(error.what()));set(reader.snapshot,"update_result",number(-2));
    }
}
}

void mhr_enable_window_reader(){enabled.store(true);}
#ifdef MHR_WINDOW_READER_TEST
void mhr_window_reader_test_hooks(void (*before_lock)(),void (*after_process)()) {
    test_before_lock.store(before_lock);test_after_process.store(after_process);
}
#endif
void mhr_set_game_window(void* window){
#ifdef _WIN32
    const auto hwnd=static_cast<HWND>(window);DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
    if(IsWindow(hwnd) && pid==GetCurrentProcessId())game_window.store(hwnd);
#endif
}
bool mhr_window_reader_available(){
#ifdef MHR_WITH_SDL
    return enabled.load();
#else
    return false;
#endif
}
bool mhr_inside_window_reader(){return dispatching;}
std::shared_ptr<MhrWindowReader> mhr_request_window_reader(const char* directory,bool hardware) {
    const std::string wanted_directory=directory?directory:"";
    std::shared_ptr<MhrWindowReader> resumed;
    {
        std::scoped_lock lock(readers_mutex);
        for(const auto& candidate:readers) {
            std::scoped_lock reader_lock(candidate->mutex);
            if(!candidate->closing && candidate->detached && candidate->hardware==hardware &&
                candidate->directory==wanted_directory && clock_ns()-candidate->detached_at<reset_grace_ns) {
                candidate->detached=false;++candidate->resume_count;
                candidate->host=table();candidate->commands=table();candidate->snapshot=table();
                candidate->request_time=candidate->request_serial=candidate->processed_serial=0;
                candidate->snapshot_time=0;
                candidate->clear_output();
                set(candidate->snapshot,"reader_available",boolean(false));
                set(candidate->snapshot,"reader_error",text("Reader resumed; waiting for fresh game state"));
                set(candidate->snapshot,"update_result",number(-2));
                set(candidate->snapshot,"active_context",number(0));
                resumed=candidate;break;
            }
        }
    }
    if(resumed){wake();return resumed;}
    auto reader=std::make_shared<MhrWindowReader>();reader->directory=directory?directory:"";
    reader->hardware=hardware;
    set(reader->snapshot,"reader_available",boolean(false));
    set(reader->snapshot,"reader_error",text("Waiting for the game window thread"));
    set(reader->snapshot,"update_result",number(-2));set(reader->snapshot,"active_context",number(0));
    {std::scoped_lock lock(readers_mutex);readers.push_back(reader);}
    wake();return reader;
}
int mhr_window_reader_tick(lua_State* l,const std::shared_ptr<MhrWindowReader>& reader) {
    const int top=lua_gettop(l);
    try {
        size_t budget=16384;auto host=copy(l,2,budget);auto commands=table();
        if(lua_istable(l,3))for(size_t n=1;n<=lua_rawlen(l,3);++n) {
            lua_rawgeti(l,3,static_cast<lua_Integer>(n));auto command=copy(l,-1,budget);lua_pop(l,1);
            Value key;key.kind=Value::Integer;key.integer=static_cast<lua_Integer>(n);
            commands.fields.emplace_back(std::move(key),std::move(command));
        }
        Value snapshot;
        {
            std::scoped_lock lock(reader->mutex);
            if(reader->closing)throw std::runtime_error("Window reader is closed");
            if(reader->commands.fields.size()+commands.fields.size()>1024)
                throw std::runtime_error("Reader command queue is full");
            const uint32_t expected=active_view(host);
            snapshot=reader->snapshot;
            set(snapshot,"filter_results",reader->filter_results);
            const auto now=clock_ns();
            const bool fresh=reader->snapshot_time && now-reader->snapshot_time<=100000000;
            if(!fresh) {
                // The owner may itself be stalled: its expiry timer cannot run.
                // Keep configuration metadata but withdraw all old input rights.
                set(snapshot,"update_result",number(-2));
                set(snapshot,"suppress_native_right_stick",boolean(false));
                set(snapshot,"suppress_native_right_touchpad",boolean(false));
                set(snapshot,"long_press_filter",table());
                set(snapshot,"filter_results",table());
            }
            if(fresh && safe(host) && expected==numeric(field(snapshot,"active_context"))) {
                set(snapshot,"yaw_degrees",number(reader->yaw));set(snapshot,"pitch_degrees",number(reader->pitch));
                const double fraction=truth(host,"recenter_verified")?reader->recenter_fraction:0;
                set(snapshot,"recenter_requested",boolean(fraction>0));
                set(snapshot,"recenter_fraction",number(fraction));
            } else {set(snapshot,"yaw_degrees",number(0));set(snapshot,"pitch_degrees",number(0));
                set(snapshot,"recenter_requested",boolean(false));set(snapshot,"recenter_fraction",number(0));}
            reader->clear_output();
            for(auto& p:commands.fields) {
                Value key;key.kind=Value::Integer;key.integer=static_cast<lua_Integer>(reader->commands.fields.size()+1);
                reader->commands.fields.emplace_back(std::move(key),std::move(p.second));
            }
            reader->host=std::move(host);reader->request_time=clock_ns();++reader->request_serial;
            set(snapshot,"reader_owner_thread",number(reader->owner_thread));
            set(snapshot,"window_transport",boolean(true));
            set(snapshot,"reader_resume_count",number(reader->resume_count));
        }
        wake();push(l,snapshot);return 1;
    } catch(const std::exception& error) {
        lua_settop(l,top);lua_pushstring(l,error.what());
    }
    return lua_error(l);
}
void mhr_close_window_reader(const std::shared_ptr<MhrWindowReader>& reader) {
    {std::scoped_lock lock(reader->mutex);reader->closing=true;reader->clear_output();}
    wake();
}
void mhr_detach_window_reader(const std::shared_ptr<MhrWindowReader>& reader) {
    {
        std::scoped_lock lock(reader->mutex);
        if(reader->closing)return;
        reader->detached=true;reader->detached_at=clock_ns();
        reader->host=table();reader->commands=table();reader->clear_output();
    }
    mhr_gui_close();
    wake();
}
bool mhr_on_window_message(void* window,unsigned int message,unsigned long long wparam,long long) {
#ifdef _WIN32
    const auto hwnd=static_cast<HWND>(window);DWORD pid=0;
    const DWORD owner=GetWindowThreadProcessId(hwnd,&pid);
    if(!enabled.load() || pid!=GetCurrentProcessId() || owner!=GetCurrentThreadId())return true;
    if(GetAncestor(hwnd,GA_ROOT)!=hwnd || GetWindow(hwnd,GW_OWNER))return true;
    if(const auto bound=game_window.load();bound && bound!=hwnd)return true;
    game_window.store(hwnd);
    const bool ours=message==kick_message() || (message==WM_TIMER && wparam==timer_id());
    bool shutdown=message==WM_DESTROY || message==WM_NCDESTROY;
    // SDL's pump can dispatch messages recursively. Never re-enter a context
    // or destroy its reader while a poll is still on the stack.
    if(dispatching){if(shutdown)shutdown_requested=true;return !ours;}
    if(!ours && !shutdown)return true;
    std::vector<std::shared_ptr<MhrWindowReader>> active;
    {std::scoped_lock lock(readers_mutex);active=readers;}
    {
        struct Guard {Guard(){dispatching=true;shutdown_requested=false;}~Guard(){dispatching=false;}} guard;
        for(auto& reader:active) {
            if(shutdown){std::scoped_lock lock(reader->mutex);reader->closing=true;}
            process(*reader,focused(hwnd),owner);
#ifdef MHR_WINDOW_READER_TEST
            if(auto hook=test_after_process.load())hook();
#endif
        }
        shutdown=shutdown || shutdown_requested;
        if(shutdown_requested)for(auto& reader:active) {
            {std::scoped_lock lock(reader->mutex);reader->closing=true;}
            process(*reader,false,owner);
        }
    }
    bool any=false;
    {
        std::scoped_lock lock(readers_mutex);
        for(auto it=readers.begin();it!=readers.end();) {
            // Keep the object alive until after unlocking, including readers
            // added/closed by Lua after the dispatcher's initial list copy.
            auto reader=*it;
            std::scoped_lock reader_lock(reader->mutex);
            if(reader->closing && !reader->vm)it=readers.erase(it);
            else {any=true;++it;}
        }
    }
    if(shutdown || !any) {
        if(timer_window)KillTimer(timer_window,timer_id());timer_window=nullptr;
        if(shutdown)game_window.store(nullptr);
    } else if(!timer_window && SetTimer(hwnd,timer_id(),10,nullptr))timer_window=hwnd;
    return !ours;
#else
    return true;
#endif
}
int mhr_window_state(lua_State* l) {
    lua_newtable(l);
#ifdef _WIN32
    const auto hwnd=find_window();DWORD pid=0;
    const auto tid=GetWindowThreadProcessId(hwnd,&pid);
    lua_pushboolean(l,pid==GetCurrentProcessId() && focused(hwnd));lua_setfield(l,-2,"focused");
    lua_pushinteger(l,tid);lua_setfield(l,-2,"window_thread");
    lua_pushinteger(l,GetCurrentThreadId());lua_setfield(l,-2,"caller_thread");
#else
    lua_pushboolean(l,false);lua_setfield(l,-2,"focused");
#endif
    {std::scoped_lock lock(readers_mutex);lua_pushinteger(l,static_cast<lua_Integer>(readers.size()));}
    lua_setfield(l,-2,"reader_count");
    return 1;
}
