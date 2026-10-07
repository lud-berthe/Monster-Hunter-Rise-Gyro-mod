local Bindings=require("mhr_gyro/bindings")
local Stick=require("mhr_gyro/stick")
local Machine=require("mhr_gyro/machine")
local Capture=require("mhr_gyro/input_capture")
local LongPressBlocker=require("mhr_gyro/block_long_press")
local bindings=Bindings.new(mhr_gyro_native,function() return reframework:is_drawing_ui() end)
local stick=Stick.new(bindings)
local machine=Machine.new(bindings)
local capture=Capture.new(mhr_gyro_native)
local blocker=LongPressBlocker.new(mhr_gyro_native,bindings,capture)
local controller
-- Installation contract: Steam Input's right touchpad is set to None.
-- GyroLib reads the physical pad directly. With no native camera output on
-- that channel, suppression is already satisfied; never mask the real mouse
-- or the independent right stick for a touchpad-only flick request.
local touchpad_native_output_disabled=true
local touchpad_request
local function view(id,label,description,priority,field,menu)
    return {id=id,label=label,description=description,priority=priority,verified=true,
        camera_in_menu=menu==true,read_active=function(state)
            if type(state[field])~="boolean" then return nil end
            return state[field]
        end}
end
return {
    id="mhr-steam-17920865-views-v3",
    verified=true,
    reader_enabled=true, -- Diagnostic acquisition via the native window-thread service.
    sdl_thread_verified=false,
    stick_suppression_verified=true,recenter_verified=true,
    touchpad_suppression_verified=touchpad_native_output_disabled,
    contexts={
        view(1,"Free camera","Look around without aiming.",0,"normal_view"),
        view(2,"Menu camera","Camera and input settings while a menu is open.",30,"menu_view",true),
        view(3,"Wirebug aim","Aim the Wirebug.",20,"wire_view"),
        view(4,"Weapon aim","Aim with any weapon.",10,"weapon_view"),
        view(5,"Ballista","Aim the ballista.",25,"ballista_view"),
        view(6,"Cannon","Aim the cannon.",25,"cannon_view")},
    read_state=function()
        local state=bindings:read_state()
        state.stick_suppression_available=stick.ready and not stick.error
        state.long_press_blocking_available=blocker.ready==true and not blocker.error
        state.touchpad_suppression_available=touchpad_native_output_disabled
            and (mhr_gyro_native.camera_integration_version or 0)>=3
        return state
    end,
    apply_camera=function(yaw,pitch)
        local state=bindings:read_state()
        if state.active_view==5 or state.active_view==6 then
            if bindings:camera_ready(state) then return machine:queue(state,yaw,pitch) end
            return
        end
        return bindings:apply_camera(yaw,pitch)
    end,
    recenter=function(fraction) return bindings:recenter(fraction) end,
    suppress_right_stick=function(enabled,id) return stick:request(enabled,id) end,
    suppress_right_touchpad=function(enabled,id)
        if not touchpad_native_output_disabled then error("Disable Steam Input right-touchpad output before using touchpad flick") end
        touchpad_request=enabled and id or nil
    end,
    settings_directory="reframework/data", -- Explicit: never write into the SDK.
    bind_controller=function(value) controller=value end,
    accept_snapshot=function(snapshot) blocker:accept(snapshot) end,
    status="F10: Settings | F9: Diagnostics",
    install=function(tick)
        capture:install()
        blocker:install(function(command) controller:queue(command) end)
        stick:install()
        machine:install()
        re.on_pre_application_entry("UpdateHID",function() stick:begin_frame();machine:begin_frame() end)
        re.on_pre_application_entry("LateUpdateBehavior",tick)
        re.on_script_reset(function() stick:request(false);machine.pending=nil;touchpad_request=nil end)
    end,
    diagnostic_state=function()
        local result=stick:diagnostics();result.machine=machine:diagnostics()
        result.input_capture=capture:diagnostics()
        result.block_long_press=blocker:diagnostics()
        result.touchpad_request=touchpad_request;result.touchpad_native_output_disabled=touchpad_native_output_disabled
        return result
    end,
    -- Required: read_state() -> menu_open, paused, focused, camera_allowed booleans.
    -- Required: apply_camera(yaw_degrees,pitch_degrees), positive right/up, no extra dt.
    -- Required for output: contexts entries with persistent uint32 id, localized label and
    -- description, integer priority, verified=true, read_active(state) -> boolean.
    -- A missing/nonboolean/error result marks that context temporarily unavailable.
    -- Optional: suppress_right_stick(enabled), plus stick_suppression_verified=true.
    -- install(tick): once per camera update after resolved commands, on the owner thread.
}
