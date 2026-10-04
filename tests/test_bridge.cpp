#include "bridge.hpp"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <cstdio>
int main(int argc,char** argv) {
    if(argc!=2 && argc!=3) return 2;
    lua_State* l=luaL_newstate(); luaL_openlibs(l); mhr_register(l);
    lua_pushstring(l,argv[1]); lua_setglobal(l,"root");
    if(argc==3) {
        lua_pushstring(l,argv[2]);lua_setglobal(l,"frontend_entry");
        const int error=luaL_dostring(l,R"(
            package.path,package.cpath="",""
            dofile(root..'/tests/test_frontend.lua')
            -- Test the real profile and every bundled module as well as the
            -- frontend's synthetic diagnostic profile, with no disk search.
            package.loaded['mhr_gyro/profile']=nil
            local profile=require('mhr_gyro/profile')
            assert(profile.verified and #profile.contexts==6)
            for name in pairs(package.preload) do
                if name:match('^mhr_gyro/') then assert(type(require(name))=='table') end
            end
        )");
        if(error)std::fprintf(stderr,"%s\n",lua_tostring(l,-1));
        lua_close(l);return error?1:0;
    }
    const char* script=R"(
      package.path=root.."/reframework/autorun/?.lua;"..package.path
      local OK,INVALID,UNAVAILABLE=0,-1,-2
      local function row(rows,id)
        for _,v in ipairs(rows) do if v.id==id then return v end end
      end
      local function setting(r,id) return row(r.settings,id) end
      local function tab(r,id)
        for _,v in ipairs(r.tabs) do if v.context_id==id then return v end end
      end
      local function near(value,expected) assert(math.abs(value-expected)<0.001) end
      -- No hardware or persistence directory: every operation stays in memory.
      local s=mhr_gyro_native.create(false)
      local r=s:tick({}, {})
      assert(r.yaw_degrees==0 and r.pitch_degrees==0 and not r.bindings_verified)
      assert(r.reader_available==false and #r.settings>10 and r.host_capabilities==0)
      assert(r.update_result==OK and r.settings_save_result==UNAVAILABLE)
      assert(r.active_context==0 and #r.tabs==0 and #r.shared_settings>0)
      assert(row(r.shared_settings,'settings.reset').visible)
      assert(not setting(r,'settings.save').visible and not setting(r,'settings.save').available)
      assert(not setting(r,'ui.scale').visible)
      for _,v in ipairs(r.settings) do
        assert(not (v.visible and (v.id:match('^gyro%.') or v.id:match('^flick%.') or v.id:match('^activation%.'))))
      end
      r=s:tick({}, {{op='initialize'}})
      assert(r.last_command_result==UNAVAILABLE and r.settings_save_result==UNAVAILABLE)

      local contexts={
        {id=401,label='Precision view',description='Synthetic context',priority=20,active=true,available=true},
        {id=23,label='Zoom',description='Another synthetic context',priority=10,active=false,available=true}}
      local host={bindings_verified=true,contexts=contexts,
        menu_open=false,paused=false,focused=true,camera_allowed=true}
      r=s:tick(host,{})
      assert(#r.tabs==2 and r.active_context==401)
      local precision,zoom=tab(r,401),tab(r,23)
      assert(precision.id==402 and precision.active and precision.available)
      assert(precision.label=='Precision view' and precision.description=='Synthetic context')
      assert(zoom.id==24 and not zoom.active and zoom.available)
      assert(setting(r,'context.401.sensitivity_x').visible)
      assert(#setting(r,'context.401.sensitivity_x').description>0)
      assert(setting(r,'context.401.sensitivity_x').description==row(precision.settings,'context.401.sensitivity_x').description)
      assert(setting(r,'context.23.sensitivity_x').label:find('Zoom',1,true))
      assert(row(precision.settings,'context.401.sensitivity_x').maximum==20)
      assert(row(precision.settings,'context.401.sensitivity_y').maximum==20)
      assert(row(precision.settings,'context.401.sensitivity_y').value==2.5)
      for _,v in ipairs(precision.settings) do assert(v.id:match('^context%.401%.')) end
      for _,v in ipairs(r.shared_settings) do assert(not v.id:match('^context%.')) end
      local expected_spaces={0,3,7,8,6,1,2,4,5}
      local spaces=row(precision.settings,'context.401.gyro.space').choices
      assert(#spaces==#expected_spaces)
      for i,choice in ipairs(spaces) do assert(choice.value==expected_spaces[i]) end

      r=s:tick(host,{{op='set',id='context.401.sensitivity_x',value=4.2},
        {op='set',id='context.401.sensitivity_y',value=1.7},
        {op='set',id='context.401.gyro.space',value=4},
        {op='set',id='context.401.gyro.invert_roll',value=1},
        {op='set',id='context.401.flick.mode',value=2}})
      assert(r.last_command_result==OK and r.settings_save_result==UNAVAILABLE)
      assert(not r.suppress_native_right_stick) -- Unsupported saved flick stays intact but inert.
      assert(setting(r,'context.401.flick.mode').value==0 and not setting(r,'context.401.flick.mode').visible)
      assert(setting(r,'context.401.flick.mode').preference_value==2)
      assert(row(tab(r,401).settings,'context.401.flick.mode').preference_value==2)
      assert(setting(r,'context.23.sensitivity_x').value==2.5)
      assert(setting(r,'context.23.gyro.space').value==0)
      assert(setting(r,'context.23.gyro.invert_roll').value==0)
      assert(setting(r,'context.23.flick.mode').value==0)
      for _,id in ipairs({'gyro.sensitivity_x','gyro.space','gyro.context','flick.mode','activation.button'}) do
        r=s:tick(host,{{op='set',id=id,value=1}})
        assert(r.last_command_result==UNAVAILABLE)
      end
      r=s:tick(host,{{op='set',id='context.401.flick.mode',value=1}})
      assert(r.last_command_result==INVALID and setting(r,'context.401.flick.mode').preference_value==2)
      r=s:tick(host,{{op='set',id='context.401.sensitivity_x',value=20.1}})
      assert(r.last_command_result==INVALID)
      for _,command in ipairs({{op='set',id='context.401.sensitivity_x',value=0/0},
          {op='set',id='context.401.sensitivity_x',value=math.huge},
          {op='set',value=3},{op='action'}}) do
        r=s:tick(host,{{op='set',id='context.23.sensitivity_x',value=2.5},command})
        assert(r.last_command_result==INVALID)
        near(setting(r,'context.401.sensitivity_x').preference_value,4.2)
      end

      host.contexts={contexts[2],contexts[1]}
      contexts[1].label='Renamed precision'
      r=s:tick(host,{})
      near(setting(r,'context.401.sensitivity_x').value,4.2)
      near(setting(r,'context.401.sensitivity_y').value,1.7)
      assert(tab(r,401).id==402 and tab(r,401).label=='Renamed precision')
      host.contexts={}; r=s:tick(host,{})
      assert(#r.tabs==0 and r.active_context==0 and r.yaw_degrees==0 and r.pitch_degrees==0)
      assert(not r.suppress_native_right_stick and not setting(r,'context.401.sensitivity_x'))
      host.contexts=contexts; r=s:tick(host,{})
      assert(tab(r,401).id==402)
      near(setting(r,'context.401.sensitivity_x').value,4.2)
      assert(setting(r,'context.401.flick.mode').value==0)
      assert(setting(r,'context.401.flick.mode').preference_value==2)

      contexts[1].available=false; r=s:tick(host,{})
      assert(#r.tabs==2 and not tab(r,401).available and not tab(r,401).active)
      assert(r.active_context==0 and setting(r,'context.401.sensitivity_x').visible)
      r=s:tick(host,{{op='set',id='context.401.sensitivity_x',value=6.3}})
      near(setting(r,'context.401.sensitivity_x').value,6.3)
      assert(r.active_context==0 and r.yaw_degrees==0 and r.pitch_degrees==0)
      contexts[2].active=true; r=s:tick(host,{})
      assert(r.active_context==23 and tab(r,23).active and not tab(r,401).active)
      contexts[1].available=true; r=s:tick(host,{})
      assert(r.active_context==401 and tab(r,401).active and not tab(r,23).active)
      -- The highest uint32 context ID must not wrap its uint64 tab ID to zero.
      local maximum={id=4294967295,label='Maximum ID',description='Exact integer boundary',
        priority=30,active=true,available=true}
      contexts[3]=maximum; r=s:tick(host,{})
      local boundary=tab(r,4294967295)
      assert(boundary.id==4294967296 and boundary.context_id==4294967295)
      assert(boundary.active and r.active_context==4294967295)
      r=s:tick(host,{{op='set',id='context.4294967295.sensitivity_x',value=8}})
      assert(row(tab(r,4294967295).settings,'context.4294967295.sensitivity_x').value==8)
      contexts[3]=nil; r=s:tick(host,{})
      assert(not tab(r,4294967295) and r.active_context==401)
      contexts[3]=maximum; r=s:tick(host,{})
      assert(tab(r,4294967295).id==4294967296)
      assert(row(tab(r,4294967295).settings,'context.4294967295.sensitivity_x').value==8)

      assert(r.host_capabilities==4)
      host.menu_open=true; r=s:tick(host,{})
      assert(r.host_capabilities==4)
      host.menu_open=nil; r=s:tick(host,{})
      assert(r.host_capabilities==0) -- A safe fallback is not evidence of an observer.
      host.menu_open='invalid'; r=s:tick(host,{})
      assert(r.host_capabilities==0)
      host.menu_open=nil
      host.stick_suppression_verified=true; r=s:tick(host,{})
      assert(r.host_capabilities==1) -- Still no long-press blocking or touchpad hook.
      host.touchpad_suppression_verified=true; r=s:tick(host,{})
      assert(r.host_capabilities==9 and not r.suppress_native_right_touchpad)
      assert(r.flick_input.available==0)
      host.stick_suppression_verified=false; r=s:tick(host,{})
      assert(r.host_capabilities==8) -- Touchpad is independent of the stick hook.
      host.bindings_verified=false; r=s:tick(host,{})
      assert(r.host_capabilities==0 and #r.tabs==0 and r.active_context==0)
      -- Reset includes temporarily unregistered profiles and cannot write a file.
      r=s:tick(host,{{op='reset'}})
      assert(r.settings_save_result==UNAVAILABLE)
      host.bindings_verified=true; r=s:tick(host,{})
      assert(setting(r,'context.401.sensitivity_x').value==2.5)
      assert(setting(r,'context.401.gyro.invert_roll').value==0)
      assert(setting(r,'context.401.flick.mode').value==0)
      assert(setting(r,'context.4294967295.sensitivity_x').value==2.5)
      r=s:tick(host,{{op='action',id='calibration.begin'}})
      assert(r.last_command_result==UNAVAILABLE and r.settings_save_result==UNAVAILABLE)
      s:close(); s:close(); assert(not pcall(function() s:tick({}, {}) end))
      assert(mhr_gyro_native.camera_integration_version==4)
      assert(math.type(mhr_gyro_native.now_ns())=='integer')
      local camera=mhr_gyro_native.create(false)
      local view={id=2,label='Menu camera',description='Synthetic movable background',priority=30,active=true,available=true}
      local menu={bindings_verified=true,menu_open=true,paused=false,focused=true,camera_allowed=true,
        recenter_verified=true,contexts={view}}
      local function request() return camera:tick(menu,{{op='recenter'}}) end
      local result=request();assert(not result.recenter_requested)
      assert(setting(result,'context.2.camera.recenter_button').visible)
      view.camera_in_menu=true
      result=request();assert(result.recenter_requested)
      assert(not camera:tick(menu,{}).recenter_requested) -- consumed once
      for _,flag in ipairs({'paused','panel_open'}) do
        menu[flag]=true;assert(not request().recenter_requested);menu[flag]=false
        assert(not camera:tick(menu,{}).recenter_requested)
      end
      menu.focused=false;assert(not request().recenter_requested);menu.focused=true
      menu.camera_allowed=false;assert(not request().recenter_requested);menu.camera_allowed=true
      view.active=false;assert(not request().recenter_requested);view.active=true
      menu.recenter_verified=false;result=request()
      assert(result.last_command_result==UNAVAILABLE and not result.recenter_requested)
      assert(not setting(result,'context.2.camera.recenter_button').visible)
      camera:close()
      print('MHR bridge: independent views, stable tabs, capabilities and memory-only settings passed')
      dofile(root..'/tests/test_controller.lua')
      dofile(root..'/tests/test_probe.lua')
      dofile(root..'/tests/test_timing.lua')
      dofile(root..'/tests/test_bindings.lua')
      dofile(root..'/tests/test_stick.lua')
      dofile(root..'/tests/test_input_capture.lua')
      dofile(root..'/tests/test_block_long_press.lua')
      dofile(root..'/tests/test_machine.lua')
      dofile(root..'/tests/test_camera_test.lua')
      dofile(root..'/tests/test_frontend.lua')
      for _,name in ipairs({'mhr_gyro.lua','mhr_gyro/profile.lua','mhr_gyro/inspect.lua','mhr_gyro/probe.lua',
        'mhr_gyro/timing.lua','mhr_gyro/bindings.lua','mhr_gyro/camera_test.lua','mhr_gyro/reader_probe.lua','mhr_gyro/stick.lua'}) do
        assert(loadfile(root..'/reframework/autorun/'..name))
      end
    )";
    const int error=luaL_dostring(l,script);
    if(error) std::fprintf(stderr,"%s\n",lua_tostring(l,-1));
    lua_close(l); return error?1:0;
}
