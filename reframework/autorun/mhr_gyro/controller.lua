-- Engine-independent orchestration, tested with synthetic host contexts.
local Controller = {}
Controller.__index = Controller
local mandatory = {"menu_open", "paused", "focused", "camera_allowed"}
local suppression_channels={
    {cap="stick_suppression_verified",available="stick_suppression_available",callback="suppress_right_stick",
        owner="suppression_callback",request="suppress_native_right_stick"},
    {cap="touchpad_suppression_verified",available="touchpad_suppression_available",callback="suppress_right_touchpad",
        owner="touchpad_suppression_callback",request="suppress_native_right_touchpad"},
}
local function integer(value, low, high)
    return type(value)=="number" and value==value and value>=low and value<=high and value==math.floor(value)
end
function Controller.new(native, profile)
    local self=setmetatable({native=native, profile=profile, commands={{op="initialize"}}, panel_open=false,
        previous_f10=false, snapshot={}, error=nil, session=nil}, Controller)
    if profile.bind_controller then profile.bind_controller(self) end
    return self
end
function Controller:queue(command) self.commands[#self.commands+1]=command end
function Controller:key(down)
    if self.native.gui_available==true then return end -- GyroLib owns its configured shortcut.
    if down and not self.previous_f10 then self.panel_open=not self.panel_open end
    self.previous_f10=down
end
-- Keep the exact callback that applied suppression: a replacement profile may
-- no longer expose it. Failed releases remain pending for the next frame.
function Controller:release_channel(channel)
    if not self[channel.owner] then return true end
    local ok,err=pcall(self[channel.owner],false)
    if ok then self[channel.owner]=nil
    else self.error=tostring(err) end
    return ok
end
function Controller:release_stick()
    local released=true
    for _,channel in ipairs(suppression_channels) do
        if not self:release_channel(channel) then released=false end
    end
    return released
end
function Controller:step()
    if not self.session then
        local hardware=self.profile.sdl_thread_verified==true or
            (self.profile.reader_enabled==true and self.native.window_transport_available==true)
        self.session=self.native.create(hardware,
            self.profile.settings_directory or "reframework/data")
    end
    local panel_open=self.panel_open
    if self.native.gui_available==true then
        local ok,gui=pcall(self.native.gui_state)
        panel_open=not ok or type(gui)~="table" or type(gui.capture)~="number" or gui.capture~=0
    end
    local host={bindings_verified=false, menu_open=true, paused=true, focused=false,
        camera_allowed=false, panel_open=panel_open, stick_suppression_verified=false, contexts={}}
    if not self.error and self.profile.verified == true then
        local ok, state=pcall(self.profile.read_state)
        local valid=ok and type(state)=="table" and type(self.profile.apply_camera)=="function"
        if valid then
            self.view_observation={active_view=state.active_view,camera_type=state.camera_type,
                camera_mode=state.camera_mode,rotation_type=state.rotation_type,
                menu_open=state.menu_open,paused=state.paused,camera_allowed=state.camera_allowed}
            for _,id in ipairs(mandatory) do
                if type(state[id])~="boolean" then valid=false end
            end
        end
        if valid then
            for _,id in ipairs(mandatory) do host[id]=state[id] end
            host.bindings_verified=true
            for _,channel in ipairs(suppression_channels) do
                host[channel.cap]=self.profile[channel.cap]==true and state[channel.available]~=false
                    and type(self.profile[channel.callback])=="function"
            end
            host.recenter_verified=self.profile.recenter_verified==true and type(self.profile.recenter)=="function"
            host.long_press_blocking_verified=state.long_press_blocking_available==true
                and type(self.profile.accept_snapshot)=="function"
            host.suspend_long_press_blocking=not host.long_press_blocking_verified or state.menu_open or state.paused
            local seen={}
            for _,context in ipairs(self.profile.contexts or {}) do
                if context.verified==true and type(context.read_active)=="function"
                    and integer(context.id,1,0xffffffff) and not seen[context.id]
                    and type(context.label)=="string" and #context.label>0
                    and type(context.description)=="string"
                    and integer(context.priority or 0,-0x80000000,0x7fffffff) then
                    seen[context.id]=true
                    local read_ok,active=pcall(context.read_active,state)
                    local available=read_ok and type(active)=="boolean"
                    host.contexts[#host.contexts+1]={id=context.id,label=context.label,
                        description=context.description,priority=context.priority or 0,
                        active=available and active or false,available=available,
                        camera_in_menu=context.camera_in_menu==true}
                end
            end
        else self.error="Game profile did not supply all required camera safety states" end
    end
    local callbacks={}
    for _,channel in ipairs(suppression_channels) do
        callbacks[channel.owner]=host[channel.cap] and self.profile[channel.callback] or nil
        if self[channel.owner] and self[channel.owner]~=callbacks[channel.owner] then
            if not self:release_channel(channel) then host.bindings_verified=false end
        end
    end
    if not host.bindings_verified then
        for _,channel in ipairs(suppression_channels) do host[channel.cap]=false;callbacks[channel.owner]=nil end
        self:release_stick()
    end
    local commands=self.commands
    self.commands={}
    self.snapshot=self.session:tick(host,commands)
    if self.profile.accept_snapshot then self.profile.accept_snapshot(self.snapshot) end
    -- Reject stale window-thread output from a different or no-longer-active
    -- profile, including requests to suppress input or recenter the camera.
    local selected
    for _,view in ipairs(host.contexts) do
        if view.active and view.available and (not selected or view.priority>selected.priority
            or (view.priority==selected.priority and view.id<selected.id)) then selected=view end
    end
    local camera_safe=host.bindings_verified and host.camera_allowed and host.focused
        and (not host.menu_open or (selected and selected.camera_in_menu)) and not host.paused and not host.panel_open
        and selected and selected.id==self.snapshot.active_context
        and integer(self.snapshot.active_context,1,0xffffffff) and self.snapshot.update_result==0
    for _,channel in ipairs(suppression_channels) do
      local suppression_callback=callbacks[channel.owner]
      if suppression_callback then
        local suppress=camera_safe and self.snapshot[channel.request]==true
        -- Retain before calling: even a callback that throws may have applied it.
        if suppress then self[channel.owner]=suppression_callback end
        local ok,err=pcall(suppression_callback,suppress,self.snapshot.active_context,self.snapshot.flick_input)
        if not ok then self.error=tostring(err); camera_safe=false; self:release_stick()
        elseif not suppress then self[channel.owner]=nil end
      end
    end
    if camera_safe then
        local yaw,pitch=self.snapshot.yaw_degrees,self.snapshot.pitch_degrees
        local finite=type(yaw)=="number" and type(pitch)=="number" and yaw==yaw and pitch==pitch
            and math.abs(yaw)<math.huge and math.abs(pitch)<math.huge
        if finite and (yaw~=0 or pitch~=0) then
            local ok,err=pcall(self.profile.apply_camera,yaw,pitch)
            if not ok then self.error=tostring(err); self:release_stick() end
        end
        if not self.error and host.recenter_verified and self.snapshot.recenter_requested==true then
            local ok,err=pcall(self.profile.recenter)
            if not ok then self.error=tostring(err);self:release_stick() end
        end
    end
end
return Controller
