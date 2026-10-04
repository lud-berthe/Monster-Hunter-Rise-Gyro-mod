#include "bridge.hpp"
#include "window_reader.hpp"
#include "native_gui.hpp"
#include "long_press_filter.hpp"
#include "steam_session.hpp"
#include "recommended_settings.hpp"
#include <gyrolib/gyrolib.h>
#include <gyrolib/steam.h>
#ifdef MHR_WITH_SDL
#include <gyrolib/sdl.h>
#endif
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <chrono>
#include <cstring>
#include <thread>
#include <cmath>
#include <new>
#include <limits>
#include <string>
#include <utility>
#include <filesystem>

namespace {
constexpr const char* key="MHRGyro.Session.v1";
struct Session {
    gl_context* ctx=nullptr;
    gl_steam_runtime* steam=nullptr;
    uint64_t steam_frame=0;
#ifdef MHR_WITH_SDL
    gl_sdl* reader=nullptr;
#endif
    std::thread::id owner;
    bool closed=false;
    bool hardware_requested=false;
    bool recenter_supported=false,recenter_pending=false;
    int last_command_result=GL_OK;
    int recommended_settings_result=GL_UNAVAILABLE;
    bool recommended_settings_attempted=false;
    std::string settings_directory;
    std::shared_ptr<MhrWindowReader> window_reader;
    std::shared_ptr<MhrGui> gui;
    std::string reader_error;
};
uint64_t now_ns() { return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
void number(lua_State* l,const char* k,double v) { lua_pushnumber(l,v); lua_setfield(l,-2,k); }
void boolean(lua_State* l,const char* k,bool v) { lua_pushboolean(l,v); lua_setfield(l,-2,k); }
void string(lua_State* l,const char* k,const char* v) { lua_pushstring(l,v?v:""); lua_setfield(l,-2,k); }
void integer(lua_State* l,const char* k,uint64_t v) { lua_pushinteger(l,static_cast<lua_Integer>(v));lua_setfield(l,-2,k); }
lua_Integer read_integer(lua_State* l,int index,const char* k,lua_Integer fallback=0) {
    lua_getfield(l,index,k);int valid=0;
    const auto v=lua_type(l,-1)==LUA_TNUMBER?lua_tointegerx(l,-1,&valid):fallback;
    lua_pop(l,1);return valid?v:fallback;
}
bool read_bool(lua_State* l,int index,const char* k,bool fallback) {
    lua_getfield(l,index,k); bool v=lua_isboolean(l,-1)?lua_toboolean(l,-1)!=0:fallback; lua_pop(l,1); return v;
}
Session* session(lua_State* l) { return static_cast<Session*>(luaL_checkudata(l,1,key)); }
void close_session(Session* s,bool script_reset=false) {
    if(s->closed) return;
    if(s->window_reader){
        if(script_reset)mhr_detach_window_reader(s->window_reader);
        else mhr_close_window_reader(s->window_reader);
        s->window_reader.reset();
    }
    // Detach while the owner context is alive. The renderer completes shutdown
    // and destruction separately; no DX12 objects die on the Lua/window thread.
    if(!mhr_gui_detach(s->gui))return;
    s->gui.reset();
#ifdef MHR_WITH_SDL
    if(s->reader) gl_sdl_destroy(s->reader);
    s->reader=nullptr;
#endif
    gl_steam_runtime_destroy(s->steam);s->steam=nullptr;
    gl_destroy(s->ctx); s->ctx=nullptr; s->closed=true;
}
int close(lua_State* l) { close_session(session(l)); return 0; }
int gc(lua_State* l) { auto* s=session(l); close_session(s,true); s->~Session(); return 0; }
int create(lua_State* l) {
    const bool hardware=lua_toboolean(l,1)!=0;
    auto* s=new(lua_newuserdatauv(l,sizeof(Session),0)) Session{};
    luaL_setmetatable(l,key);
    s->owner=std::this_thread::get_id();
    s->hardware_requested=hardware;
    if(lua_isstring(l,2)) s->settings_directory=lua_tostring(l,2);
    if(hardware && !mhr_inside_window_reader()) {
        if(mhr_window_reader_available()) {
            s->window_reader=mhr_request_window_reader(s->settings_directory.c_str());
            return 1;
        }
        s->reader_error="Window-thread acquisition service is unavailable";
    }
    s->ctx=gl_create(GL_ABI_VERSION);
    if(!s->ctx) return luaL_error(l,"GyroLib ABI/create failed");
    // Tests omit the directory: no implicit files beside the SDK or game.
    // SDL initialization is explicit, so tests and read-only profile inspection
    // can operate without probing hardware.
#ifdef MHR_WITH_SDL
    if(hardware && mhr_inside_window_reader()) s->reader=gl_sdl_create(s->ctx,0);
#endif
    if(hardware && mhr_inside_window_reader()) {
        s->steam=gl_steam_runtime_create(s->ctx);
        s->gui=mhr_gui_attach(s->ctx);
    }
    return 1;
}
void commands(lua_State* l,Session* s,int index) {
    if(!lua_istable(l,index)) return;
    auto count=lua_rawlen(l,index);
    for(size_t i=1;i<=count;++i) {
        lua_rawgeti(l,index,static_cast<lua_Integer>(i));
        if(!lua_istable(l,-1)) { lua_pop(l,1); continue; }
        lua_getfield(l,-1,"op"); const char* op=lua_tostring(l,-1); lua_pop(l,1);
        lua_getfield(l,-1,"id"); const char* id=lua_tostring(l,-1);
        if(op && std::strcmp(op,"set")==0) {
            lua_getfield(l,-2,"value");
            int numeric=0; double value=lua_tonumberx(l,-1,&numeric);
            s->last_command_result=id && numeric && std::isfinite(value)?gl_setting_set(s->ctx,id,value):GL_INVALID;
            lua_pop(l,1);
        } else if(op && std::strcmp(op,"action")==0) s->last_command_result=id?gl_action(s->ctx,id):GL_INVALID;
        else if(op && std::strcmp(op,"recenter")==0) s->last_command_result=gl_request_recenter(s->ctx);
        else if(op && std::strcmp(op,"reset")==0) s->last_command_result=gl_action(s->ctx,"settings.reset");
        else if(op && std::strcmp(op,"initialize")==0) {
            if(!s->settings_directory.empty() && !s->recommended_settings_attempted) {
                s->recommended_settings_attempted=true;
                s->recommended_settings_result=mhr_capture_recommended_settings(s->ctx);
            }
            std::error_code directory_error;
            if(!s->settings_directory.empty())
                std::filesystem::create_directories(std::filesystem::u8path(s->settings_directory),directory_error);
            s->last_command_result=s->settings_directory.empty()?GL_UNAVAILABLE:directory_error?GL_IO_ERROR:
                gl_initialize_settings(s->ctx,s->settings_directory.c_str(),
                    (s->settings_directory+"/mhr_gyro_settings.ini").c_str());
        }
        lua_pop(l,2);
    }
}
bool context_id(lua_State* l,int table,uint32_t& id) {
    if(!lua_istable(l,table)) return false;
    lua_getfield(l,table,"id");
    int numeric=0; const double value=lua_tonumberx(l,-1,&numeric); lua_pop(l,1);
    if(!numeric || !std::isfinite(value) || value<1 || value>4294967295.0 || value!=std::floor(value)) return false;
    id=static_cast<uint32_t>(value); return true;
}
bool listed_context(lua_State* l,int array,uint32_t wanted) {
    if(!lua_istable(l,array)) return false;
    for(size_t n=1;n<=lua_rawlen(l,array);++n) {
        lua_rawgeti(l,array,static_cast<lua_Integer>(n)); uint32_t id=0;
        const bool found=context_id(l,-1,id)&&id==wanted; lua_pop(l,1);
        if(found)return true;
    }
    return false;
}
void sync_contexts(lua_State* l,Session* s,bool verified) {
    lua_getfield(l,2,"contexts"); const int array=lua_gettop(l);
    const bool have_contexts=verified && lua_istable(l,array);
    // Reverse removal preserves indices. The core retains saved values by stable ID.
    for(uint32_t n=gl_gameplay_context_count(s->ctx);n>0;--n) {
        gl_gameplay_context descriptor{};
        if(gl_get_gameplay_context(s->ctx,n-1,&descriptor)==GL_OK &&
            (!have_contexts || !listed_context(l,array,descriptor.id)))
            gl_unregister_gameplay_context(s->ctx,descriptor.id);
    }
    if(have_contexts) for(size_t n=1;n<=lua_rawlen(l,array);++n) {
        lua_rawgeti(l,array,static_cast<lua_Integer>(n)); const int entry=lua_gettop(l);
        uint32_t id=0;
        if(!context_id(l,entry,id)) {lua_pop(l,1);continue;}
        lua_getfield(l,entry,"label"); const char* label=lua_tostring(l,-1);
        lua_getfield(l,entry,"description"); const char* description=lua_tostring(l,-1);
        lua_getfield(l,entry,"priority"); int numeric=0;
        const double priority=lua_tonumberx(l,-1,&numeric);
        if(label && *label && description && numeric && std::isfinite(priority) &&
            priority>=std::numeric_limits<int32_t>::min() && priority<=std::numeric_limits<int32_t>::max() && priority==std::floor(priority)) {
            gl_gameplay_context descriptor{id,label,description,static_cast<int32_t>(priority)};
            bool unchanged=false;
            for(uint32_t k=0;k<gl_gameplay_context_count(s->ctx);++k) {
                gl_gameplay_context known{};
                if(gl_get_gameplay_context(s->ctx,k,&known)==GL_OK && known.id==id) {
                    unchanged=known.priority==descriptor.priority && known.label && known.description &&
                        std::strcmp(known.label,label)==0 && std::strcmp(known.description,description)==0;
                    break;
                }
            }
            const auto registered=unchanged?GL_OK:gl_register_gameplay_context(s->ctx,&descriptor);
            if(registered!=GL_OK) s->last_command_result=registered;
            if(registered==GL_OK) {
                gl_set_gameplay_context_output_target(s->ctx,id,GL_OUTPUT_CAMERA);
                gl_set_gameplay_context_camera_in_menu(s->ctx,id,read_bool(l,entry,"camera_in_menu",false));
                gl_set_gameplay_context_state(s->ctx,id,read_bool(l,entry,"active",false),read_bool(l,entry,"available",false));
            }
        } else {
            gl_unregister_gameplay_context(s->ctx,id);
        }
        lua_pop(l,4); // priority, description, label, entry
    }
    lua_pop(l,1);
}
void push_setting(lua_State* l,Session* s,const gl_setting_info& v) {
    lua_newtable(l);
    string(l,"id",v.id); string(l,"label",v.label); string(l,"description",v.description); string(l,"unit",v.unit);
    number(l,"type",v.type); number(l,"value",v.value); number(l,"minimum",v.minimum);
    // Menu values may fall back when the device/hook is unavailable. Preserve
    // the user's actual preference separately in diagnostics.
    double preference=0;
    if(gl_setting_get(s->ctx,v.id,&preference)==GL_OK) number(l,"preference_value",preference);
    number(l,"maximum",v.maximum); number(l,"step",v.step); number(l,"default_value",v.default_value);
    boolean(l,"visible",v.visible!=0); boolean(l,"available",v.available!=0);
    lua_newtable(l);
    for(uint32_t n=0;n<gl_menu_choice_count(s->ctx,v.id);++n) {
        gl_choice c{}; if(gl_choice_at(s->ctx,v.id,n,&c)!=GL_OK) continue;
        lua_newtable(l); string(l,"label",c.label); number(l,"value",c.value); boolean(l,"available",c.available!=0);
        lua_rawseti(l,-2,n+1);
    }
    lua_setfield(l,-2,"choices");
}
int tick(lua_State* l) {
    auto* s=session(l); luaL_checktype(l,2,LUA_TTABLE);
    if(s->closed) return luaL_error(l,"Session is closed");
    if(s->owner!=std::this_thread::get_id()) return luaL_error(l,"GyroLib must run on its creating thread");
    if(s->window_reader)return mhr_window_reader_tick(l,s->window_reader);
    // Host contexts and native-hook capabilities are never inferred from hardware.
    const bool verified=read_bool(l,2,"bindings_verified",false);
    uint32_t capabilities=0;
    // A fail-closed default menu=true is not evidence of a menu observer.
    lua_getfield(l,2,"menu_open");
    const bool menu_observed=verified && lua_isboolean(l,-1);
    lua_pop(l,1);
    if(menu_observed) capabilities|=GL_HOST_MENU_STATE;
    if(verified && read_bool(l,2,"stick_suppression_verified",false))
        capabilities|=GL_HOST_NATIVE_STICK_SUPPRESSION;
    if(verified && read_bool(l,2,"touchpad_suppression_verified",false))
        capabilities|=GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION;
    if(verified && read_bool(l,2,"long_press_blocking_verified",false))
        capabilities|=GL_HOST_LONG_PRESS_BLOCKING;
    gl_set_host_capabilities(s->ctx,capabilities);
    const bool recenter_supported=verified && read_bool(l,2,"recenter_verified",false);
    if(recenter_supported!=s->recenter_supported){
        s->recenter_supported=recenter_supported;
        gl_set_recenter_callback(s->ctx,recenter_supported?+[](void* user){
            static_cast<Session*>(user)->recenter_pending=true;
        }:nullptr,s);
    }
    s->recenter_pending=false;
    sync_contexts(l,s,verified);
    commands(l,s,3);
    gl_host_state host{}; // Legacy aiming/alt_fire fields remain zero and unused.
    host.menu_open=read_bool(l,2,"menu_open",true);
    host.paused=read_bool(l,2,"paused",true);
    host.focused=read_bool(l,2,"focused",false);
    host.camera_allowed=verified && read_bool(l,2,"camera_allowed",false);
    host.suspend_long_press_blocking=!(capabilities&GL_HOST_LONG_PRESS_BLOCKING) ||
        read_bool(l,2,"suspend_long_press_blocking",true);
    host.steam_gyro_output=0; // Cannot reliably infer Steam's mouse/stick gyro config.
    if(!s->gui)gl_set_panel_open(s->ctx,read_bool(l,2,"panel_open",false));
    // Core gates unsupported modes without overwriting their persisted settings.

    const auto now=now_ns();
    gl_output out{};
    int32_t update_result=GL_OK;
    int32_t steam_result=GL_UNAVAILABLE;
    const char* steam_session_error=s->steam?mhr_prepare_steam_input(now):"";
#ifdef MHR_WITH_SDL
    if(s->reader) {
        update_result=gl_sdl_pump_events(s->reader);
        if(update_result==GL_OK)update_result=gl_sdl_poll(s->reader,now);
        // Steam is supplemental. Its absence/errors must not interrupt healthy SDL.
        if(s->steam)steam_result=gl_steam_runtime_poll(s->steam,now,++s->steam_frame);
        if(update_result==GL_OK)update_result=mhr_gui_update(s->gui,s->ctx,now,&host,&out);
        else if(s->gui)mhr_gui_process(s->gui);
        if(update_result==GL_OK)gl_sdl_apply_feedback(s->reader);
    } else
#endif
    {
        if(s->steam)steam_result=gl_steam_runtime_poll(s->steam,now,++s->steam_frame);
        update_result=mhr_gui_update(s->gui,s->ctx,now,&host,&out);
    }
    gl_diagnostics d{}; gl_get_diagnostics(s->ctx,&d);
    lua_newtable(l);
    const auto tap=mhr_long_press_config(s->ctx,update_result);
    lua_newtable(l);
    boolean(l,"enabled",tap.enabled);integer(l,"device",tap.device);
    integer(l,"view",tap.view);integer(l,"button",tap.button);integer(l,"timestamp_ns",now);
    lua_setfield(l,-2,"long_press_filter");
    lua_newtable(l);const auto results=lua_gettop(l);
    // Game command events are copied to this owner thread. Their original
    // monotonic timestamps preserve the SDK's threshold across dispatcher lag.
    if(lua_istable(l,3)) for(size_t i=1,emitted=0;i<=lua_rawlen(l,3);++i) {
        lua_rawgeti(l,3,static_cast<lua_Integer>(i));
        if(lua_istable(l,-1)) {
            lua_getfield(l,-1,"op");const char* op=lua_tostring(l,-1);
            const bool filter=op && std::strcmp(op,"filter")==0;lua_pop(l,1);
            if(filter) {
                const auto key=read_integer(l,-1,"key",-1),event=read_integer(l,-1,"event",-1);
                const auto stamp=read_integer(l,-1,"timestamp_ns",-1);
                MhrLongPressDecision decision;
                if(key>=0 && key<32 && event>=GL_PRESS && event<=GL_REPEAT && stamp>=0) {
                    const MhrLongPressEvent input{static_cast<uint64_t>(stamp),static_cast<uint64_t>(read_integer(l,-1,"device")),
                        static_cast<uint32_t>(read_integer(l,-1,"view")),static_cast<uint32_t>(key),static_cast<uint32_t>(event),
                        read_bool(l,-1,"eligible",false),read_bool(l,-1,"native_hold",true)};
                    decision=mhr_filter_event(s->ctx,tap,now,input);
                }
                lua_newtable(l);integer(l,"token",read_integer(l,-2,"token"));
                number(l,"event",event);number(l,"decision",decision.result);
                boolean(l,"current",decision.current);lua_rawseti(l,results,++emitted);
            }
        }
        lua_pop(l,1);
    }
    lua_setfield(l,-2,"filter_results");
    number(l,"yaw_degrees",out.yaw_degrees); number(l,"pitch_degrees",out.pitch_degrees);
    boolean(l,"recenter_requested",update_result==GL_OK && s->recenter_pending);
    boolean(l,"suppress_native_right_stick",out.suppress_native_right_stick!=0);
    boolean(l,"suppress_native_right_touchpad",update_result==GL_OK && gl_suppress_native_right_touchpad(s->ctx)!=0);
    gl_flick_input flick{};
    if(gl_get_flick_input(s->ctx,&flick)==GL_OK) {
        lua_newtable(l);
        number(l,"available",flick.available);number(l,"touching",flick.touching);
        number(l,"stick_x",flick.stick_x);number(l,"stick_y",flick.stick_y);
        number(l,"touchpad_x",flick.touchpad_x);number(l,"touchpad_y",flick.touchpad_y);
        lua_setfield(l,-2,"flick_input");
    }
    number(l,"last_command_result",s->last_command_result);
    number(l,"update_result",update_result);
    number(l,"settings_save_result",gl_get_settings_save_result(s->ctx));
    number(l,"recommended_settings_result",s->recommended_settings_result);
    number(l,"active_context",gl_get_active_gameplay_context(s->ctx));
    number(l,"steam_result",steam_result);
    string(l,"steam_session_error",steam_session_error);
    string(l,"steam_error",s->steam?gl_steam_runtime_error(s->steam):"Steam reader disabled");
    number(l,"source",out.source); number(l,"accepted_samples",static_cast<double>(d.accepted_samples));
    number(l,"rejected_samples",static_cast<double>(d.rejected_samples));
    number(l,"dropped_samples",static_cast<double>(d.dropped_samples));
    for(const auto& entry:{std::make_pair("calibrated_dps",d.calibrated_dps),std::make_pair("gravity",d.gravity)}) {
        lua_newtable(l);number(l,"x",entry.second.x);number(l,"y",entry.second.y);number(l,"z",entry.second.z);
        lua_setfield(l,-2,entry.first);
    }
    number(l,"calibration_state",d.calibration_state); number(l,"calibration_seconds_remaining",d.calibration_seconds_remaining);
    boolean(l,"stationary",d.stationary!=0);
    boolean(l,"bindings_verified",verified);
    number(l,"host_capabilities",gl_get_host_capabilities(s->ctx));
    number(l,"gui_capture",mhr_gui_capture());
#ifdef MHR_WITH_SDL
    boolean(l,"reader_available",s->reader!=nullptr);
    string(l,"reader_error",!s->reader_error.empty()?s->reader_error.c_str():
        s->hardware_requested?gl_sdl_error(s->reader):"Reader disabled by diagnostic profile");
#else
    boolean(l,"reader_available",false);
#endif
    lua_newtable(l);
    for(uint32_t i=0;i<gl_endpoint_count(s->ctx);++i) {
        gl_endpoint e{}; if(gl_get_endpoint(s->ctx,i,&e)!=GL_OK) continue;
        lua_newtable(l); string(l,"name",e.name); number(l,"source",e.source);
        boolean(l,"connected",e.connected!=0); boolean(l,"gyro",e.caps.gyro!=0);
        boolean(l,"motion_available",gl_endpoint_motion_available(s->ctx,e.id)!=0);
        gl_input_metrics metrics{};
        if(gl_get_input_metrics(s->ctx,e.id,&metrics)==GL_OK) {
            number(l,"report_hz",metrics.report_hz);number(l,"sample_age_ns",static_cast<double>(metrics.sample_age_ns));
        }
        lua_rawseti(l,-2,i+1);
    }
    lua_setfield(l,-2,"endpoints");
    lua_newtable(l);
    for(uint32_t i=0;i<gl_menu_setting_count(s->ctx);++i) {
        gl_setting_info v{}; if(gl_setting_at(s->ctx,i,&v)!=GL_OK) continue;
        push_setting(l,s,v); lua_rawseti(l,-2,i+1);
    }
    lua_setfield(l,-2,"settings");
    lua_newtable(l);
    for(uint32_t i=0;i<gl_menu_shared_setting_count(s->ctx);++i) {
        gl_setting_info v{}; if(gl_menu_shared_setting_at(s->ctx,i,&v)!=GL_OK) continue;
        push_setting(l,s,v); lua_rawseti(l,-2,i+1);
    }
    lua_setfield(l,-2,"shared_settings");
    lua_newtable(l);
    for(uint32_t i=0;i<gl_menu_tab_count(s->ctx);++i) {
        gl_menu_tab tab{}; if(gl_menu_tab_at(s->ctx,i,&tab)!=GL_OK) continue;
        lua_newtable(l);
        lua_pushinteger(l,static_cast<lua_Integer>(tab.id)); lua_setfield(l,-2,"id");
        number(l,"context_id",tab.context_id); boolean(l,"active",tab.active!=0); boolean(l,"available",tab.available!=0);
        string(l,"label",tab.label); string(l,"description",tab.description);
        lua_newtable(l);
        for(uint32_t j=0;j<gl_menu_tab_setting_count(s->ctx,tab.id);++j) {
            gl_setting_info v{}; if(gl_menu_tab_setting_at(s->ctx,tab.id,j,&v)!=GL_OK) continue;
            push_setting(l,s,v); lua_rawseti(l,-2,j+1);
        }
        lua_setfield(l,-2,"settings"); lua_rawseti(l,-2,i+1);
    }
    lua_setfield(l,-2,"tabs");
    return 1;
}
}
void mhr_register(lua_State* l) {
    if(luaL_newmetatable(l,key)) {
        lua_pushcfunction(l,gc); lua_setfield(l,-2,"__gc");
        lua_newtable(l); lua_pushcfunction(l,tick); lua_setfield(l,-2,"tick");
        lua_pushcfunction(l,close); lua_setfield(l,-2,"close"); lua_setfield(l,-2,"__index");
    }
    lua_pop(l,1);
    lua_newtable(l); lua_pushcfunction(l,create); lua_setfield(l,-2,"create");
    lua_pushcfunction(l,+[](lua_State* l){lua_pushinteger(l,static_cast<lua_Integer>(now_ns()));return 1;});
    lua_setfield(l,-2,"now_ns");
    lua_pushcfunction(l,mhr_window_state);lua_setfield(l,-2,"window_state");
    lua_pushcfunction(l,mhr_gui_state);lua_setfield(l,-2,"gui_state");
    lua_pushcfunction(l,mhr_gui_set_open);lua_setfield(l,-2,"gui_set_open");
    boolean(l,"gui_available",mhr_gui_available());
    boolean(l,"window_transport_available",mhr_window_reader_available());
    number(l,"camera_integration_version",4);
    number(l,"abi_version",GL_ABI_VERSION); lua_setglobal(l,"mhr_gyro_native");
}
