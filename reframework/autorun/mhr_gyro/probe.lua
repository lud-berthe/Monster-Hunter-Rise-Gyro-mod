-- Bounded observation only: read fields/getters; never set fields or call actions.
local Probe={}
Probe.__index=Probe
local primitive_types={
    ["System.Boolean"]=true,["System.Single"]=true,["System.Double"]=true,
    ["System.Int32"]=true,["System.UInt32"]=true,["System.Int16"]=true,
    ["System.UInt16"]=true,["System.Int64"]=true,["System.UInt64"]=true,
    ["System.Byte"]=true,["System.SByte"]=true,
}
local function scalar(value)
    if type(value)=="boolean" then return value end
    if type(value)=="number" and value==value and math.abs(value)<math.huge then return value end
end
local function read(errors,key,fn)
    local ok,value=pcall(fn)
    if not ok then errors[key]=tostring(value); return nil end
    return value
end
local function field(object,name)
    return object and object:get_field(name) or nil
end
local function vector(value,size)
    if not value then return nil end
    local result={x=scalar(value.x),y=scalar(value.y)}
    if size==3 then result.z=scalar(value.z) end
    return result
end
-- Inspect query results separately from cached fields. A failed/missing query
-- stays absent with an error; it must never masquerade as a closed menu.
local menu_queries={
    gui={"IsStartMenuAndSubmenuOpen","IsPlayerAllInputDisable","isOpenPauseWindow",
        "isOpenOptionWindow","isOpenTalkWindow","isOpenDialog","isOpenLobbyMapWindow",
        "get_IsStartManuPause"},
    start_menu={"isOpenStartMenuWindow","get_isOpenStartMenu","get_isStartMenuUpdate"},
}
function Probe:menu_queries(object,errors,key)
    local result={present=object~=nil}
    if not object then return result end
    for _,method in ipairs(menu_queries[key] or {}) do
        result[method]=read(errors,key.."."..method,function()
            local value=object:call(method)
            if type(value)~="boolean" then error("Expected a boolean menu query result") end
            return value
        end)
    end
    return result
end
function Probe.new()
    return setmetatable({recording=false,previous_key=false,status="F9: record camera states for 30 seconds",
        fields={},samples={},frame=0},Probe)
end
function Probe:primitives(object,errors,key)
    if not object then return {present=false} end
    local td=object:get_type_definition()
    local name=td:get_full_name()
    if not self.fields[name] then
        local fields={}
        for _,f in ipairs(td:get_fields()) do
            local ft=f:get_type()
            if ft and not f:is_static() and not f:get_name():match("^_GS_")
                and (primitive_types[ft:get_full_name()] or ft:is_enum()) then
                fields[#fields+1]=f:get_name()
            end
        end
        self.fields[name]=fields
    end
    local result={present=true,type=name}
    for _,name in ipairs(self.fields[name]) do
        result[name]=scalar(read(errors,key.."."..name,function() return object:get_field(name) end))
    end
    return result
end
function Probe:snapshot(elapsed)
    local errors={}
    local result={elapsed_seconds=elapsed,frame=self.frame,errors=errors}
    local manager=read(errors,"CameraManager",function() return sdk.get_managed_singleton("snow.CameraManager") end)
    result.manager=read(errors,"manager fields",function() return self:primitives(manager,errors,"manager") end)
    local camera=read(errors,"PlayerCamera",function() return field(manager,"_RefPlayerCameraBehavior") end)
    local input=read(errors,"CameraInput",function() return field(manager,"_RefCameraInput") end)
    local game_camera=read(errors,"GameCamera",function() return field(manager,"<_RefGameCameraBehavior>k__BackingField") end)
    result.player_camera=read(errors,"camera fields",function() return self:primitives(camera,errors,"camera") end)
    result.input=read(errors,"input fields",function() return self:primitives(input,errors,"input") end)
    result.game_camera=read(errors,"game camera fields",function() return self:primitives(game_camera,errors,"game_camera") end)
    if camera then
        result.camera_action=read(errors,"camera action",function()
            return self:primitives(field(camera,"_ActionCtrl"),errors,"camera_action")
        end)
        for key,method in pairs({angle="get_CameraAngle",target_angle="get_CameraAngleTarget",
            camera_position="get__CameraPos",look_at_position="get__LookAtPos"}) do
            result[key]=read(errors,key,function() return vector(camera:call(method),3) end)
        end
    end
    if input then
        result.camera_operation=read(errors,"camera_operation",function() return vector(field(input,"_CameraOperation"),2) end)
        result.camera_pads={}
        for _,name in ipairs({"_PadOriginal","_PadArrange","_PadOriginalNoMask"}) do
            result.camera_pads[name]=read(errors,name,function()
                local pad=field(input,name)
                return {right=vector(field(pad,"_AnalogR"),2),raw=vector(field(pad,"_AnalogRraw"),2)}
            end)
        end
    end
    for key,name in pairs({gui="snow.gui.GuiManager",game="snow.SnowGameManager",
        system_input="snow.StmInputManager",start_menu="snow.gui.fsm.startmenu.GuiStartMenuFsmManager"}) do
        local object=read(errors,key.." singleton",function() return sdk.get_managed_singleton(name) end)
        result[key]=read(errors,key,function()
            return self:primitives(object,errors,key)
        end)
        if menu_queries[key] then
            result[key.."_queries"]=self:menu_queries(object,errors,key)
        end
        if key=="system_input" and object then
            result.mouse_move=read(errors,"mouse_move",function() return vector(object:call("getMouseMovePosition"),2) end)
        end
    end
    result.reframework_ui=read(errors,"reframework_ui",function() return reframework:is_drawing_ui() end)
    result.thread_id=read(errors,"thread_id",function() return thread.get_id() end)
    result.reader=self.reader_snapshot
    result.camera_test=self.camera_test
    result.gyro_camera_output_enabled=self.gyro_camera_output_enabled==true
    return result
end
function Probe:finish()
    self.recording=false
    local report={schema=2,observation_only=true,duration_seconds=os.difftime(os.time(),self.started),samples=self.samples}
    local ok,saved=pcall(json.dump_file,"mhr_gyro_state_probe.json",report)
    if ok and saved==true then
        self.status="Observation saved: "..#self.samples.." samples"
        log.info("MHRGyro: state observation saved to reframework/data/mhr_gyro_state_probe.json ("..#self.samples.." samples)")
    else
        self.status="Observation save failed: "..tostring(saved)
        log.error("MHRGyro: "..self.status)
    end
end
function Probe:key(down)
    if down and not self.previous_key then
        if self.recording then self:finish()
        else
            self.started=os.time(); self.samples={}; self.frame=0; self.recording=true
            self.status="Recording camera states; F9 stops early"
            log.info("MHRGyro: 30-second read-only state observation started")
        end
    end
    self.previous_key=down
end
function Probe:step()
    if not self.recording then return end
    self.frame=self.frame+1
    local elapsed=os.difftime(os.time(),self.started)
    if self.frame%5==0 then self.samples[#self.samples+1]=self:snapshot(elapsed) end
    if elapsed>=30 or #self.samples>=1200 then self:finish() end
end
return Probe
