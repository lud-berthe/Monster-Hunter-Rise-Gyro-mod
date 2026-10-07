-- Rise-specific camera conventions and resolved view observations.
local Bindings={}
Bindings.__index=Bindings
local gui_queries={"IsStartMenuAndSubmenuOpen","IsPlayerAllInputDisable","isOpenPauseWindow",
    "isOpenOptionWindow","isOpenTalkWindow","isOpenDialog","isOpenLobbyMapWindow","get_IsStartManuPause"}
local function finite(v) return type(v)=="number" and v==v and math.abs(v)<math.huge end
local function wrap(v) return (v+math.pi)%(2*math.pi)-math.pi end
local function read(errors,key,fn)
    local ok,value=pcall(fn)
    if not ok then errors[key]=tostring(value);return nil end
    return value
end
local function boolean(errors,key,fn)
    return read(errors,key,function()
        local value=fn()
        if type(value)~="boolean" then error("Expected boolean state") end
        return value
    end)
end
function Bindings.new(native,overlay_observer)
    return setmetatable({native=native,overlay_observer=overlay_observer},Bindings)
end
function Bindings:read_state()
    local errors={}
    local state={menu_open=true,paused=true,focused=false,camera_allowed=false,
        menu_state_observed=false,errors=errors,objects={},blocking_fields={}}
    local window=read(errors,"window",function() return self.native.window_state() end)
    state.window=window
    state.focused=window and window.focused==true or false
    state.overlay_open=boolean(errors,"REFramework overlay",self.overlay_observer or function() return false end)
    local manager=read(errors,"manager",function() return sdk.get_managed_singleton("snow.CameraManager") end)
    local gui=read(errors,"gui",function() return sdk.get_managed_singleton("snow.gui.GuiManager") end)
    local players=read(errors,"players",function() return sdk.get_managed_singleton("snow.player.PlayerManager") end)
    state.objects.manager=manager~=nil
    state.objects.gui=gui~=nil
    state.objects.players=players~=nil
    if not gui then return state end
    local queries={}
    state.gui_queries=queries
    local menus_known=true
    for _,method in ipairs(gui_queries) do
        queries[method]=boolean(errors,method,function() return gui:call(method) end)
        if type(queries[method])~="boolean" then menus_known=false end
    end
    if not menus_known then return state end
    state.menu_state_observed=true
    state.menu_open=false
    for _,method in ipairs(gui_queries) do state.menu_open=state.menu_open or queries[method] end
    state.paused=queries.isOpenPauseWindow or queries.get_IsStartManuPause
    -- The menu profile also owns input policy when its camera is unavailable.
    -- Only observed UI state selects it; unknown states retain no active view.
    state.menu_view=state.menu_open
    state.active_view=state.menu_view and 2 or 0
    if next(errors) or not manager or not players then return state end
    local camera=read(errors,"camera",function() return manager:get_field("_RefPlayerCameraBehavior") end)
    local input=read(errors,"input",function() return manager:get_field("_RefCameraInput") end)
    local game_camera=read(errors,"game_camera",function() return manager:get_field("<_RefGameCameraBehavior>k__BackingField") end)
    local player=read(errors,"player",function() return players:call("findMasterPlayer") end)
    state.objects.camera=camera~=nil
    state.objects.input=input~=nil
    state.objects.game_camera=game_camera~=nil
    state.objects.player=player~=nil
    if next(errors) or not camera or not input or not game_camera or not player then return state end
    local kind=read(errors,"camera type",function() return manager:get_field("_NowCameraType") end)
    local mode=read(errors,"camera mode",function() return input:get_field("_CameraMode") end)
    local rotation=read(errors,"rotation type",function() return camera:get_field("_RotationType") end)
    state.camera_type=kind
    state.camera_mode=mode
    state.rotation_type=rotation
    local blocked=false
    for _,entry in ipairs({
        {manager,"_IsFastTravelDemo"},{manager,"_IsQuestStartDemo"},{manager,"_IsPlayerDie"},
        {manager,"_IsTentIn"},{manager,"_IsTransportNekotaku"},
        {game_camera,"_IsDisableCamera"},{game_camera,"_IsActivePhotoCamera"},{game_camera,"_IsCampOperation"},
        {input,"_IgnoreAllInput"},{input,"_IgnoreCameraOperation"},{input,"_IsDemo"},
        {camera,"_IsComboLock"},{camera,"_IsLookAtLock"}}) do
        local value=boolean(errors,entry[2],function() return entry[1]:get_field(entry[2]) end)
        state.blocking_fields[entry[2]]=value
        blocked=blocked or value~=false
    end
    if next(errors) or type(kind)~="number" or type(mode)~="number" or type(rotation)~="number" then return state end
    -- Aim is shared by wire and weapon actions; the resolved wire reticle
    -- distinguishes them. Enum values are from the runtime metadata capture.
    local wire=boolean(errors,"wire aim",function() return gui:call("isDisplayedHunterWireAimUI") end)
    if next(errors) then return state end
    local machine_kind=mode==3 and read(errors,"machine camera kind",function()
        return input:get_field("_OperationSpeedDataKind")
    end) or nil
    -- Runtime CameraOperationSpeedDataKind: Cannon=1, BallistaNormal=2,
    -- BallistaScope=3. Other machine cameras remain outside these profiles.
    local ballista=mode==3 and (machine_kind==2 or machine_kind==3)
    local cannon=mode==3 and machine_kind==1
    state.machine_kind=machine_kind
    local supported=kind==1 and (mode==0 or mode==1 or mode==2 or ballista or cannon)
        and (rotation==0 or rotation==2)
    local free=mode==0 and rotation==0 and not wire
    local aiming=(mode==1 or mode==2) and (rotation==0 or rotation==2)
    local ui_blocks=queries.isOpenOptionWindow or queries.isOpenTalkWindow or queries.isOpenDialog
    local eligible=supported and not blocked
    state.normal_view=eligible and free and not state.menu_open
    state.wire_view=eligible and aiming and wire and not state.menu_open
    state.weapon_view=eligible and aiming and not wire and not state.menu_open
    state.ballista_view=eligible and ballista and not state.menu_open
    state.cannon_view=eligible and cannon and not state.menu_open
    state.active_view=state.menu_view and 2 or state.ballista_view and 5 or state.cannon_view and 6
        or state.wire_view and 3 or state.weapon_view and 4 or state.normal_view and 1 or 0
    state.camera_in_menu=state.menu_view
    state.camera_allowed=eligible and not ui_blocks and state.overlay_open==false and state.active_view~=0
    state.camera=camera
    state.input=input
    -- Rise can route mouse-look through the same camera-pad analog fields.
    -- This is the resolved mouse-lever flag, not the last displayed device icon.
    -- Missing observation only withdraws stick masking, not gyro camera output.
    state.mouse_camera=boolean(errors,"mouse camera source",function()
        local system_input=sdk.get_managed_singleton("snow.StmInputManager")
        if not system_input then error("StmInputManager unavailable") end
        return system_input:get_field("<isMouseLever>k__BackingField")
    end)
    return state
end
function Bindings:apply_camera(yaw_degrees,pitch_degrees)
    if not finite(yaw_degrees) or not finite(pitch_degrees) then error("Nonfinite camera delta") end
    local state=self:read_state()
    if not self:camera_ready(state) then
        -- Focus/UI/view state may change after the controller's observation.
        -- Discard this delta; a normal transition must not latch a fatal error.
        return nil
    end
    local camera=state.camera
    local angle=camera:call("get_CameraAngleTarget")
    local limits=camera:call("get_CameraAngleXLimit")
    local minimum,maximum=limits:get_field("Min"),limits:get_field("Max")
    if not angle or not finite(angle.x) or not finite(angle.y) or not finite(minimum) or not finite(maximum)
        or minimum>maximum or minimum<-math.pi or maximum>math.pi then
        error("Camera angle or radian pitch limits unavailable")
    end
    local before={x=angle.x,y=angle.y}
    local pitch=math.max(minimum,math.min(maximum,angle.x+pitch_degrees*math.pi/180))
    -- GyroLib yaw is right-positive; Rise's camera yaw decreases to the right.
    local yaw=wrap(angle.y-yaw_degrees*math.pi/180)
    camera:call("setTurnAngXTarget",pitch)
    camera:call("setTurnAngYTarget",yaw)
    local after=camera:call("get_CameraAngleTarget")
    return {before=before,expected={x=pitch,y=yaw},after={x=after.x,y=after.y},
        limits={minimum=minimum,maximum=maximum}}
end
function Bindings:camera_ready(state)
    if not state.camera_allowed or not state.focused or state.paused or state.overlay_open~=false
        or (state.menu_open and not state.camera_in_menu) then return false end
    if self.native.gui_available then
        local ok,gui=pcall(self.native.gui_state)
        if not ok or type(gui)~="table" or gui.capture~=0 then return false end
    end
    return true
end
function Bindings:recenter(fraction)
    if not finite(fraction) or fraction<=0 or fraction>1 then error("Invalid recenter fraction") end
    local state=self:read_state()
    if not self:camera_ready(state) then return end
    local limits=state.camera:call("get_CameraAngleXLimit")
    local low,high=limits:get_field("Min"),limits:get_field("Max")
    if not finite(low) or not finite(high) or low>high or low<-math.pi or high>math.pi then
        error("Camera radian pitch limits unavailable")
    end
    local angle=state.camera:call("get_CameraAngleTarget")
    if not angle or not finite(angle.x) then error("Camera pitch unavailable") end
    local level=math.max(low,math.min(high,0))
    local pitch=fraction==1 and level or angle.x+(level-angle.x)*fraction
    state.camera:call("setTurnAngXTarget",math.max(low,math.min(high,pitch)))
end
return Bindings
