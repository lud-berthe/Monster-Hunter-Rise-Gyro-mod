local Controller=require("mhr_gyro/controller")
local calls=0
-- No aiming/alt_fire fields: a camera adapter with a named view is valid.
local state={menu_open=false,paused=false,focused=true,camera_allowed=true}
local received
local reported_active_context=401
local reported_update_result=0
local native={create=function() return {tick=function(_,host,commands)
    received=host
    return {yaw_degrees=1,pitch_degrees=2,suppress_native_right_stick=true,
        active_context=reported_active_context,update_result=reported_update_result,
        settings={},shared_settings={},tabs={}}
end} end}
local function normal_view()
    return {id=401,label="Camera",description="Synthetic normal camera",priority=0,
        verified=true,read_active=function() return true end}
end
local profile={verified=false,contexts={normal_view()},read_state=function() return state end,
    apply_camera=function(y,p) assert(y==1 and p==2); calls=calls+1 end}
local c=Controller.new(native,profile)
c:step(); assert(calls==0 and not received.bindings_verified and #received.contexts==0)
profile.verified=true
c:step(); assert(calls==1 and c.error==nil and #received.contexts==1)
assert(received.aiming==nil and received.alt_fire==nil)
-- Even nonzero native output cannot move the camera without an active view.
profile.contexts={}; reported_active_context=0
c:step(); assert(calls==1 and c.error==nil and #received.contexts==0)
local active=false
profile.contexts={
    {id=401,label="Precision view",description="Synthetic test context",priority=20,
        verified=true,read_active=function() return active end},
    {id=23,label="Zoom",description="Synthetic test context",priority=10,
        verified=true,read_active=function() return true end},
    {id=2,label="Unverified",description="Must not appear",verified=false,read_active=function() return true end}}
reported_active_context=23
c:step(); assert(calls==2 and #received.contexts==2 and received.contexts[1].active==false)
active=true; reported_active_context=401
c:step(); assert(calls==3 and received.contexts[1].active==true and received.contexts[1].available)
profile.contexts[1].read_active=function() return nil end
reported_active_context=23
c:step(); assert(calls==4 and not received.contexts[1].available and not received.contexts[1].active)
assert(c.error==nil) -- Another available active view still permits output.
profile.contexts={profile.contexts[2]}
c:step(); assert(calls==5 and #received.contexts==1 and received.contexts[1].id==23)
profile.contexts={normal_view()}
for _,invalid in ipairs({0,-1,1.5,4294967296,math.huge,0/0,"401"}) do
    reported_active_context=invalid
    c:step(); assert(calls==5 and c.error==nil)
end
reported_active_context=nil; c:step(); assert(calls==5)
reported_active_context=401
reported_update_result=-2; c:step(); assert(calls==5)
reported_update_result=nil; c:step(); assert(calls==5)
reported_update_result=0
c:key(true); c:key(true); assert(c.panel_open)
c:step(); assert(calls==5)
c:key(false); c:key(true); assert(not c.panel_open)
for _,name in ipairs({"menu_open","paused"}) do
    state[name]=true; c:step(); assert(calls==5); state[name]=false
end
state.focused=false; c:step(); assert(calls==5); state.focused=true
state.camera_allowed=false; c:step(); assert(calls==5); state.camera_allowed=true
state.paused=nil; c:step(); assert(calls==5 and c.error~=nil)
-- A missing safety binding disables the adapter, unlike an optional view.
state.paused=false; c:step(); assert(calls==5 and not received.bindings_verified and #received.contexts==0)
local broken={create=function() return {tick=function()
    return {yaw_degrees=0/0,pitch_degrees=2,active_context=401,update_result=0}
end} end}
local b=Controller.new(broken,profile); b:step(); assert(calls==5)
print("MHR controller: named views, availability, active-view safety, F10 and invalid output passed")

-- Suppression ownership survives profile/callback withdrawal and errors.
local function suppression_fixture()
    local history={}
    local p={verified=true,stick_suppression_verified=true,contexts={normal_view()},camera_calls=0,
        read_state=function() return {menu_open=false,paused=false,focused=true,camera_allowed=true} end,
        suppress_right_stick=function(value) history[#history+1]=value end}
    p.apply_camera=function() p.camera_calls=p.camera_calls+1 end
    local controller=Controller.new(native,p)
    controller:step(); assert(history[1]==true and p.camera_calls==1)
    return controller,p,history
end
local sc,sp,history=suppression_fixture()
reported_active_context=0
sc:step(); assert(history[2]==false and sp.camera_calls==1 and sc.suppression_callback==nil)
reported_active_context=401
sc,sp,history=suppression_fixture()
reported_update_result=-2
sc:step(); assert(history[2]==false and sp.camera_calls==1 and sc.suppression_callback==nil)
reported_update_result=0
sc,sp,history=suppression_fixture()
sp.stick_suppression_verified=false; sp.suppress_right_stick=nil
sc:step(); assert(history[2]==false and sc.suppression_callback==nil)
sc,sp,history=suppression_fixture()
sp.verified=false; sc:step(); assert(history[2]==false)
sc,sp,history=suppression_fixture()
sp.read_state=function() error("state failed") end
sc:step(); assert(history[2]==false and sc.error)
sc,sp,history=suppression_fixture()
sp.apply_camera=function() error("camera failed") end
sc:step(); assert(history[#history]==false and sc.error)
sc:step(); assert(not received.bindings_verified)
sc,sp,history=suppression_fixture()
local replacement={}
sp.suppress_right_stick=function(value) replacement[#replacement+1]=value end
sc:step(); assert(history[2]==false and replacement[1]==true)
-- A release failure is protected, blocks new output, and is retried.
local fail_release=true
sp.suppress_right_stick=function(value)
    if not value and fail_release then error("release failed") end
    replacement[#replacement+1]=value
end
sc:step()
sp.stick_suppression_verified=false
sc:step(); assert(sc.error and sc.suppression_callback)
fail_release=false; sc:step(); assert(sc.suppression_callback==nil and replacement[#replacement]==false)
print("MHR controller: stick suppression withdrawal, replacement, error and release retry passed")

-- Touchpad-only flick must not suppress the native stick/mouse, and its
-- independent callback must be released on a context change or withdrawal.
local pad_calls,stick_calls={},{}
local pad_context=401
local pad_host={menu_open=false,paused=false,focused=true,camera_allowed=true}
local pad_native={create=function() return {tick=function(_,host)
    assert(host.touchpad_suppression_verified)
    return {active_context=pad_context,update_result=0,yaw_degrees=0,pitch_degrees=0,
        suppress_native_right_touchpad=true,suppress_native_right_stick=false,flick_input={available=3,touching=1}}
end} end}
local pad_profile={verified=true,contexts={normal_view()},apply_camera=function() end,
    read_state=function() return pad_host end,stick_suppression_verified=true,touchpad_suppression_verified=true,
    suppress_right_stick=function(on) stick_calls[#stick_calls+1]=on end,
    suppress_right_touchpad=function(on,id,input)
        pad_calls[#pad_calls+1]=on
        if on then assert(id==401 and input.available==3) end
    end}
local pc=Controller.new(pad_native,pad_profile)
pc:step();assert(pad_calls[1] and not stick_calls[1])
pad_context=0;pc:step();assert(not pad_calls[2] and not stick_calls[2])
pad_context=401;pc:step();assert(pad_calls[3])
pc:release_stick();assert(not pad_calls[4] and not pc.touchpad_suppression_callback)

-- The native GUI owns F10 and must block camera output independently of the
-- REFramework panel, including a failed/unknown capture observation.
local capture=0
local gui_native={create=native.create,gui_available=true,
    gui_state=function() return {capture=capture} end}
local gui_profile={verified=true,contexts={normal_view()},read_state=function() return state end,
    apply_camera=function() calls=calls+1 end}
local gui_controller=Controller.new(gui_native,gui_profile)
local before=calls
gui_controller:key(true);assert(not gui_controller.panel_open)
gui_controller:step();assert(calls==before+1 and not received.panel_open)
capture=7;gui_controller:step();assert(calls==before+1 and received.panel_open)
capture=nil;gui_controller:step();assert(calls==before+1 and received.panel_open)
gui_native.gui_state=function() error("GUI unavailable") end
gui_controller:step();assert(calls==before+1 and received.panel_open)
print("MHR controller: native shortcut ownership and GUI capture gates passed")

-- Menu opt-in belongs to the selected view. A stale snapshot from the previous
-- profile must not rotate, suppress input or recenter the current camera.
local output_view,request_center=2,true
local yaw_calls,center_calls=0,0
local menu_state={menu_open=true,paused=false,focused=true,camera_allowed=true}
local menu_profile={verified=true,recenter_verified=true,contexts={
    {id=2,label='Menu',description='',priority=30,verified=true,camera_in_menu=true,read_active=function() return true end}},
    read_state=function() return menu_state end,
    apply_camera=function() yaw_calls=yaw_calls+1 end,recenter=function() center_calls=center_calls+1 end}
local menu_native={create=function() return {tick=function()
    return {active_context=output_view,update_result=0,yaw_degrees=1,pitch_degrees=0,recenter_requested=request_center}
end} end}
local menu=Controller.new(menu_native,menu_profile)
menu:step();assert(yaw_calls==1 and center_calls==1)
request_center=false;menu:step();assert(yaw_calls==2 and center_calls==1)
request_center=true;output_view=1;menu:step();assert(yaw_calls==2 and center_calls==1)
output_view=2;menu_profile.contexts[1].camera_in_menu=false;menu:step();assert(yaw_calls==2 and center_calls==1)
menu_profile.contexts[1].camera_in_menu=true;menu_state.paused=true;menu:step();assert(center_calls==1)
menu_state.paused=false;menu_profile.recenter_verified=false;menu:step();assert(yaw_calls==3 and center_calls==1)
print("MHR controller: menu opt-in, exact active profile and recenter delivery passed")
