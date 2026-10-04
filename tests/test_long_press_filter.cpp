#include "long_press_filter.hpp"
#include <cstdio>
#include <stdexcept>
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string("tap:")+std::to_string(__LINE__)+": " #x);}while(0)
int main() try {
    auto* c=gl_create(GL_ABI_VERSION);CHECK(c);
    const gl_gameplay_context view{1,"Camera","Memory-only input filter",0};
    CHECK(gl_register_gameplay_context(c,&view)==GL_OK);
    gl_endpoint endpoint{};endpoint.id=1;endpoint.physical_id=123;endpoint.source=GL_SOURCE_SDL;
    endpoint.connected=1;endpoint.caps={0xffffffff,0,0,0,0,1,1};
    CHECK(gl_register_endpoint(c,&endpoint)==GL_OK);
    CHECK(gl_set_host_capabilities(c,GL_HOST_LONG_PRESS_BLOCKING)==GL_OK);
    gl_host_state host{};host.focused=host.camera_allowed=1;
    uint64_t now=1000000000;
    auto tick=[&] {
        now+=1000000;gl_sample sample{now,now,{0,0,0},{0,1,0}};gl_controls controls{};controls.timestamp_ns=now;
        CHECK(gl_submit_sample(c,1,&sample)==GL_OK);CHECK(gl_submit_controls(c,1,&controls)==GL_OK);
        CHECK(gl_set_gameplay_context_state(c,1,1,1)==GL_OK);gl_output output{};
        CHECK(gl_update(c,now,&host,&output)==GL_OK);
    };
    tick();tick();tick();
    CHECK(gl_setting_set(c,"context.1.gyro.activation",GL_HOLD_DISABLE)==GL_OK);
    CHECK(gl_setting_set(c,"context.1.activation.button",3)==GL_OK);
    CHECK(gl_setting_set(c,"context.1.activation.block_long_press",1)==GL_OK);tick();
    auto config=mhr_long_press_config(c,GL_OK);CHECK(config.enabled && config.button==3 && config.device==123);
    auto event=[&](unsigned phase,uint64_t time,bool eligible=true,bool hold=false,uint64_t delay=10000000) {
        return mhr_filter_event(c,config,time+delay,{time,123,1,2,phase,eligible,hold});
    };
    CHECK(event(GL_PRESS,now).result==GL_SUPPRESS);
    CHECK(event(GL_REPEAT,now+100000000).result==GL_SUPPRESS);
    CHECK(event(GL_RELEASE,now+199999999).result==GL_EMIT_TAP);
    CHECK(event(GL_PRESS,now+1000000000).result==GL_SUPPRESS);
    CHECK(event(GL_RELEASE,now+1200000000).result==GL_SUPPRESS);
    CHECK(event(GL_PRESS,now+2000000000,false).result==GL_FORWARD);
    CHECK(event(GL_RELEASE,now+2050000000,false).result==GL_FORWARD);
    CHECK(event(GL_PRESS,now+3000000000,true,true).result==GL_FORWARD);
    CHECK(event(GL_RELEASE,now+3050000000,true,true).result==GL_FORWARD);
    CHECK(event(GL_PRESS,now+4000000000).result==GL_SUPPRESS);
    CHECK(event(GL_RELEASE,now+4050000000,true,false,100000000).result==GL_SUPPRESS); // stale delivery cancels
    CHECK(event(GL_PRESS,now+5000000000).result==GL_SUPPRESS);
    auto changed=config;changed.device=456;
    auto decision=mhr_filter_event(c,changed,now+5060000000,{now+5050000000,123,1,2,GL_RELEASE,true,false});
    CHECK(!decision.current && decision.result==GL_SUPPRESS);
    CHECK(event(GL_PRESS,now+6000000000).result==GL_SUPPRESS);
    gl_set_panel_open(c,1);
    CHECK(event(GL_RELEASE,now+6050000000).result==GL_SUPPRESS);
    gl_set_panel_open(c,0);
    host.menu_open=1;tick();CHECK(event(GL_PRESS,now).result==GL_FORWARD);CHECK(event(GL_RELEASE,now+1).result==GL_FORWARD);
    gl_destroy(c);std::puts("Rise SDK tap filtering: threshold, queued timestamps, native holds, stale delivery, device and panel cancellation passed");
    return 0;
} catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
