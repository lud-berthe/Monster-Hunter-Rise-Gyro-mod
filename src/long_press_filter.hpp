#pragma once
#include <gyrolib/gyrolib.h>
#include <string>
struct MhrLongPressConfig {
    bool enabled=false;
    uint64_t device=0;
    uint32_t view=0,button=0;
};
inline double mhr_view_setting(gl_context* ctx,uint32_t view,const char* name) {
    double value=0;const auto id="context."+std::to_string(view)+"."+name;
    gl_setting_get_effective(ctx,id.c_str(),&value);return value;
}
inline MhrLongPressConfig mhr_long_press_config(gl_context* ctx,int update_result) {
    MhrLongPressConfig config;
    config.device=gl_get_selected_device(ctx);config.view=gl_get_active_gameplay_context(ctx);
    config.button=static_cast<uint32_t>(mhr_view_setting(ctx,config.view,"activation.button"));
    const auto mode=mhr_view_setting(ctx,config.view,"gyro.activation");
    config.enabled=update_result==GL_OK && config.device && config.view &&
        (gl_get_host_capabilities(ctx)&GL_HOST_LONG_PRESS_BLOCKING) && config.button>=1 && config.button<=32 &&
        (mode==double(GL_HOLD) || mode==double(GL_HOLD_DISABLE)) &&
        mhr_view_setting(ctx,config.view,"activation.block_long_press")!=0;
    return config;
}
struct MhrLongPressEvent {
    uint64_t timestamp_ns=0,device=0;
    uint32_t view=0,key=0,event=0;
    bool eligible=false,native_hold=true;
};
struct MhrLongPressDecision {int result=GL_INVALID;bool current=false;};
inline MhrLongPressDecision mhr_filter_event(gl_context* ctx,const MhrLongPressConfig& config,uint64_t now,const MhrLongPressEvent& event) {
    if(event.key>=32 || event.event>GL_REPEAT)return {};
    const bool current=config.enabled && config.view==event.view && config.device==event.device &&
        config.button==event.key+1 && event.timestamp_ns<=now && now-event.timestamp_ns<100000000;
    return {gl_filter_event(ctx,event.key,event.event,event.timestamp_ns,current && event.eligible,
        !current || event.native_hold),current};
}
