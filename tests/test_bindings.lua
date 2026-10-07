local saved_sdk=sdk
local saved_native,saved_reframework,saved_re=mhr_gyro_native,reframework,re
local Bindings=dofile(root.."/reframework/autorun/mhr_gyro/bindings.lua")
local queries={}
local writes={}
local target={x=0.74,y=3.13}
local minimum,maximum=-0.8,0.75
local camera_mode,rotation,camera_type=0,0,1
local machine_kind=0
local mouse_camera=false
local camera={get_field=function(_,name) return name=="_RotationType" and rotation or false end,
    call=function(_,method,value)
        if method=="get_CameraAngleTarget" or method=="get_CameraAngle" then return {x=target.x,y=target.y} end
        if method=="get_CameraAngleXLimit" then return {get_field=function(_,name) return name=="Min" and minimum or maximum end} end
        writes[#writes+1]={method=method,value=value}
        if method=="setTurnAngXTarget" then target.x=value
        elseif method=="setTurnAngYTarget" then target.y=value
        else error("Unexpected action") end
    end}
local input={get_field=function(_,name)
    if name=="_CameraMode" then return camera_mode end
    if name=="_OperationSpeedDataKind" then return machine_kind end
    return false
end}
local game_camera={get_field=function() return false end}
local manager={get_field=function(_,name)
    if name=="_RefPlayerCameraBehavior" then return camera end
    if name=="_RefCameraInput" then return input end
    if name=="<_RefGameCameraBehavior>k__BackingField" then return game_camera end
    if name=="_NowCameraType" then return camera_type end
    return false
end}
local objects={
    ["snow.CameraManager"]=manager,
    ["snow.gui.GuiManager"]={call=function(_,name)
        if queries[name]=="missing" then return nil end
        return queries[name]==true
    end},
    ["snow.player.PlayerManager"]={call=function(_,method) assert(method=="findMasterPlayer");return {} end},
    ["snow.StmInputManager"]={get_field=function(_,field)
        assert(field=="<isMouseLever>k__BackingField");return mouse_camera
    end},
}
sdk={get_managed_singleton=function(name) return objects[name] end}
local focused=true
local binding=Bindings.new({window_state=function() return {focused=focused} end})
local state=binding:read_state()
assert(state.normal_view and state.camera_allowed and state.menu_state_observed and not state.menu_open)
assert(state.objects.player and state.objects.camera and state.window.focused)
assert(state.camera_type==1 and state.camera_mode==0 and state.rotation_type==0)
assert(state.blocking_fields._IsDemo==false and state.gui_queries.isOpenDialog==false)
assert(state.active_view==1)
assert(state.mouse_camera==false)
mouse_camera=true;assert(binding:read_state().mouse_camera==true)
mouse_camera=nil;assert(binding:read_state().camera_allowed and binding:read_state().mouse_camera==nil)
mouse_camera=false
queries.IsStartMenuAndSubmenuOpen=true
state=binding:read_state();assert(state.active_view==2 and state.camera_in_menu and state.menu_view and not state.normal_view)
assert(binding:camera_ready(state))
queries.isOpenPauseWindow=true;assert(not binding:camera_ready(binding:read_state()));queries.isOpenPauseWindow=false
queries.isOpenDialog=true;assert(not binding:read_state().camera_allowed);queries.isOpenDialog=false
-- Menu identity is independent of permission to move its background camera.
for _,query in ipairs({'isOpenOptionWindow','isOpenTalkWindow','isOpenDialog','isOpenPauseWindow'}) do
    queries[query]=true
    state=binding:read_state()
    assert(state.menu_view and state.active_view==2 and not binding:camera_ready(state))
    queries[query]=false
end
local old_manager=objects['snow.CameraManager']
objects['snow.CameraManager']=nil
state=binding:read_state()
assert(state.menu_state_observed and state.menu_view and state.active_view==2 and not state.camera_allowed)
objects['snow.CameraManager']=old_manager
queries.IsStartMenuAndSubmenuOpen=false
camera_mode,rotation=1,2;queries.isDisplayedHunterWireAimUI=true
state=binding:read_state();assert(state.active_view==3 and state.wire_view and not state.weapon_view)
queries.isDisplayedHunterWireAimUI=false
state=binding:read_state();assert(state.active_view==4 and state.weapon_view and not state.wire_view)
camera_mode=2;assert(binding:read_state().active_view==4)
for _,mode in ipairs({3,4,5,99}) do camera_mode=mode;assert(not binding:read_state().camera_allowed) end
camera_mode=3;rotation=0
for _,case in ipairs({{1,6},{2,5},{3,5}}) do
    machine_kind=case[1];state=binding:read_state()
    assert(state.active_view==case[2] and state.camera_allowed)
    assert(not state.normal_view and not state.weapon_view and not state.wire_view)
    assert(state.ballista_view==(case[2]==5) and state.cannon_view==(case[2]==6))
end
for _,kind in ipairs({0,4,5,99,false}) do machine_kind=kind;assert(not binding:read_state().camera_allowed) end
machine_kind=2;camera_mode=0;rotation=0;assert(binding:read_state().active_view==1) -- stale machine type
camera_mode=0;rotation=3;assert(not binding:read_state().camera_allowed)
rotation=0;camera_type=3;assert(not binding:read_state().camera_allowed);camera_type=1
target={x=.4,y=1.2};binding:recenter(1);assert(target.x==0 and target.y==1.2 and #writes==1)
queries.isOpenPauseWindow=true;binding:recenter(1);assert(#writes==1);queries.isOpenPauseWindow=false
minimum=.1;binding:recenter(1);assert(target.x==.1 and target.y==1.2);minimum=-.8
writes={}
target={x=.8,y=1.2};binding:recenter(.25)
assert(math.abs(target.x-.6)<1e-9 and target.y==1.2)
binding:recenter(.5);assert(math.abs(target.x-.3)<1e-9)
binding:recenter(1);assert(target.x==0 and target.y==1.2)
for _,bad in ipairs({0,-1,1.1,0/0,math.huge}) do
    assert(not pcall(function() binding:recenter(bad) end))
end
writes={}
-- GyroLib positive yaw means right. Rise's camera angle decreases for a
-- right turn; check both directions and both crossings of the +/-pi boundary.
for _,case in ipairs({{0,90,-90},{0,-90,90},{-179,5,176},{179,-5,-176}}) do
    target={x=0,y=case[1]*math.pi/180}
    local turn=binding:apply_camera(case[2],0)
    assert(math.abs(target.y-case[3]*math.pi/180)<1e-9,"Incorrect horizontal camera direction or wrap")
    assert(math.abs(turn.expected.y-target.y)<1e-9 and target.x==0)
end
target={x=0.74,y=3.13};writes={}
local result=binding:apply_camera(5,3)
assert(#writes==2 and target.x==maximum)
local expected=3.13-5*math.pi/180
assert(math.abs(target.y-expected)<1e-9 and math.abs(result.expected.y-expected)<1e-9)
queries.isOpenDialog=true
assert(binding:apply_camera(5,3)==nil and #writes==2)
queries.isOpenDialog=false
queries.IsPlayerAllInputDisable="missing"
state=binding:read_state()
assert(not state.menu_state_observed and state.menu_open and not state.camera_allowed)
assert(state.menu_view==nil and state.active_view==nil)
assert(binding:apply_camera(5,3)==nil and #writes==2)
queries.IsPlayerAllInputDisable=false;focused=false
assert(binding:apply_camera(5,3)==nil and #writes==2)
focused=true;maximum=75
assert(not pcall(function() binding:apply_camera(5,3) end) and #writes==2)
maximum=0.75
assert(not pcall(function() binding:apply_camera(0/0,3) end) and #writes==2)
local overlay=false
local guarded=Bindings.new({window_state=function() return {focused=true} end},function() return overlay end)
assert(guarded:read_state().camera_allowed)
overlay=true
assert(guarded:read_state().normal_view and not guarded:read_state().camera_allowed)
assert(guarded:apply_camera(5,3)==nil and #writes==2)
overlay=nil
assert(not guarded:read_state().camera_allowed and guarded:read_state().errors["REFramework overlay"])
overlay=false
mhr_gyro_native={window_state=function() return {focused=true} end,now_ns=function() return 1000000000 end}
reframework={is_drawing_ui=function() return overlay end}
local installed_tick
re={on_pre_application_entry=function(name,fn)
    assert(name=="LateUpdateBehavior" or name=="UpdateHID");if name=="LateUpdateBehavior" then installed_tick=fn end
end,on_script_reset=function() end}
local profile=dofile(root.."/reframework/autorun/mhr_gyro/profile.lua")
assert(profile.verified and profile.reader_enabled and #profile.contexts==6 and profile.contexts[1].id==1)
for id=1,6 do assert(profile.contexts[id].id==id and profile.contexts[id].camera_in_menu==(id==2)) end
assert(profile.contexts[1].read_active(profile.read_state())==true)
local context=profile.contexts[1]
assert(context.read_active({normal_view=false})==false and context.read_active({})==nil)
overlay=true;assert(not profile.read_state().camera_allowed);overlay=false
profile.install(function() end);assert(type(installed_tick)=="function")
-- The real profile and controller apply degrees once and block menu/overlay
-- output even when a synthetic native producer returns a stale nonzero delta.
local Controller=require("mhr_gyro/controller")
local received_host
local producer={window_transport_available=true,create=function(hardware)
    assert(hardware)
    return {tick=function(_,host)
        received_host=host
        return {active_context=1,update_result=0,yaw_degrees=1,pitch_degrees=-2}
    end}
end}
local controller=Controller.new(producer,profile)
local before_pitch,before_yaw=target.x,target.y
controller:step()
assert(not controller.error and received_host.bindings_verified and received_host.contexts[1].id==1)
assert(#writes==4 and math.abs(target.x-(before_pitch-2*math.pi/180))<1e-9)
assert(math.abs(target.y-((before_yaw-math.pi/180+math.pi)%(2*math.pi)-math.pi))<1e-9)
queries.isOpenDialog=true;controller:step();assert(#writes==4)
assert(received_host.bindings_verified and not received_host.camera_allowed)
assert(received_host.contexts[2].active and received_host.contexts[2].available)
queries.isOpenDialog=false;overlay=true;controller:step();assert(#writes==4)
overlay=false
-- A menu can open between the first observation and the actual camera write.
local old_tick=controller.session.tick
controller.session.tick=function(...)
    local result=old_tick(...);queries.isOpenDialog=true;return result
end
controller:step();assert(#writes==4 and not controller.error)
controller.session.tick=old_tick;queries.isOpenDialog=false
controller:step();assert(#writes==6 and not controller.error)
objects["snow.CameraManager"]=nil
assert(not binding:read_state().camera_allowed)
controller:step();assert(#writes==6 and not controller.error)
queries.isOpenDialog=true;controller:step()
assert(#writes==6 and not received_host.camera_allowed)
assert(received_host.contexts[2].active and received_host.contexts[2].available)
sdk,mhr_gyro_native,reframework,re=saved_sdk,saved_native,saved_reframework,saved_re
print("MHR camera binding/profile: radians, normal view, engine callback and menu/focus/overlay gates passed")
