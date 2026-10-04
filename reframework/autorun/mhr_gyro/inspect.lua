-- Read-only reflection aid. No hooks, no field writes and no invoked game methods.
local M={}
local function type_name(t) return t and t:get_full_name() or "<unknown>" end
function M.inspect_type(name)
    local t=sdk.find_type_definition(name)
    if not t then return {name=name,present=false} end
    local result={name=name,present=true,methods={},fields={}}
    for _,method in ipairs(t:get_methods()) do
        local m={name=method:get_name(),returns=type_name(method:get_return_type()),parameters={}}
        -- Runtime addresses make read-only inspection of the actual input path
        -- possible without guessing which merged vector contains mouse motion.
        local address_ok,address=pcall(function() return tostring(method:get_function()) end)
        if address_ok then m.address=address end
        for _,parameter in ipairs(method:get_param_types()) do
            m.parameters[#m.parameters+1]=type_name(parameter)
        end
        result.methods[#result.methods+1]=m
    end
    for _,field in ipairs(t:get_fields()) do
        local entry={name=field:get_name(),type=type_name(field:get_type()),
            static=field:is_static(),literal=field:is_literal()}
        local offset_ok,offset=pcall(function() return field:get_offset_from_base() end)
        if offset_ok then entry.offset=offset end
        if entry.literal then
            local ok,value=pcall(function() return field:get_data(nil) end)
            if ok and (type(value)=="number" or type(value)=="boolean") then entry.value=value end
        end
        result.fields[#result.fields+1]=entry
    end
    table.sort(result.methods,function(a,b) return a.name<b.name end)
    table.sort(result.fields,function(a,b) return a.name<b.name end)
    return result
end
function M.capture()
    -- These roots are evidenced in public scripts/SDK. Any additional type name
    -- below is discovered from current runtime field metadata, never guessed.
    local result={profile="unverified",schema=2,types={}}
    -- Camera references were found in the first live report. UI/input/game
    -- singleton names were found in the same session's REFramework log.
    local names={"snow.CameraManager","snow.player.PlayerManager","via.Transform",
        "snow.camera.PlayerCamera","snow.camera.CameraInput","snow.GameCamera",
        "snow.Pad","snow.StmPlayerInput",
        "snow.Pad.Device","snow.camera.PlayerCameraActionBallista","snow.camera.PlayerCameraActionCannon",
        "snow.stage.huntingMachine.Hm00Base",
        "snow.camera.PlayerCameraActionCtrl.ActionId",
        "snow.gui.GuiManager","snow.SnowGameManager","snow.StmInputManager",
        "snow.gui.fsm.startmenu.GuiStartMenuFsmManager",
        "snow.camera.PlayerCamera.CameraParam","snow.camera.PlayerCameraActionCtrl",
        "snow.camera.PlayerCamera.RotationType","snow.camera.CameraInput.CameraMode",
        "snow.CameraManager.CameraType","snow.camera.CameraUtility.MinMax"}
    local seen,queued={},{}
    for _,name in ipairs(names) do queued[name]=true end
    local function discover(name)
        local low=name:lower()
        if low:sub(1,5)=="snow." and (low:find("camera") or low:find("input") or low:find("menu")
            or low:find("ballista") or low:find("cannon") or low:find("huntingmachine"))
            and not queued[name] then
            queued[name]=true; names[#names+1]=name
        end
    end
    local index=1
    while index<=#names and #result.types<128 do
        local name=names[index]; index=index+1
        if not seen[name] then
            seen[name]=true
            local ok,data=pcall(M.inspect_type,name)
            result.types[#result.types+1]=ok and data or {name=name,error=tostring(data)}
            if ok and data.fields then
                for _,field in ipairs(data.fields) do
                    discover(field.type)
                end
            end
            if ok and data.methods then
                for _,method in ipairs(data.methods) do discover(method.returns) end
            end
        end
    end
    result.truncated=index<=#names
    return result
end
return M
