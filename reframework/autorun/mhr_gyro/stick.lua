-- Suppress only the camera-owned stick during ReflectInput. UI, buttons and
-- movement are untouched, and the original values are restored after the call.
local Stick={}
Stick.__index=Stick
local key="mhr_gyro_camera_stick"
local function finite(v) return type(v)=="number" and v==v and math.abs(v)<math.huge end
function Stick.new(bindings)
    return setmetatable({bindings=bindings,frame=0,ready=false,calls=0,suppressed=0,refilled=0,mouse_preserved=0},Stick)
end
function Stick:begin_frame() self.frame=self.frame+1 end
function Stick:request(enabled,id)
    self.request_view=enabled and id or nil
    self.request_frame=self.frame
end
function Stick:diagnostics()
    return {ready=self.ready,calls=self.calls,suppressed=self.suppressed,error=self.error,
        request_view=self.request_view,refilled=self.refilled,last_suppression=self.last_suppression,
        observation_error=self.observation_error,mouse_preserved=self.mouse_preserved}
end
-- Observe the original call before restoring our temporary fields. A nonzero
-- result here establishes that the engine overwrote the masked input itself.
function Stick:finish(changes)
    if #changes>0 then
        local ok,err=pcall(function()
            local after={}
            local refilled=false
            for i,c in ipairs(changes) do
                local value=c.pad:get_field(c.field)
                if not value or not finite(value.x) or not finite(value.y) then error("Invalid post-hook stick") end
                after[i]={field=c.field,x=value.x,y=value.y}
                refilled=refilled or value.x~=0 or value.y~=0
            end
            if refilled then self.refilled=self.refilled+1 end
            self.last_suppression={frame=self.frame,view=self.request_view,after_original=after}
        end)
        if not ok then self.observation_error=tostring(err) end
    end
    self:restore(changes)
end
function Stick:restore(changes)
    local failure
    for i=#changes,1,-1 do
        local c=changes[i]
        local ok,err=pcall(function() c.pad:set_field(c.field,c.value) end)
        if not ok then failure=tostring(err) end
    end
    if failure then self.error=failure;self.ready=false;self:request(false) end
end
function Stick:before(input)
    local changes={}
    local ok,err=pcall(function()
        local state=self.bindings:read_state()
        if not state.input or state.input~=input then return end
        if type(state.mouse_camera)~="boolean" then self.ready=false;return end
        local fields={}
        for _,name in ipairs({"_PadOriginal","_PadArrange"}) do
            local pad=input:get_field(name)
            if not pad then error("Camera pad unavailable: "..name) end
            for _,field in ipairs({"_AnalogR","_AnalogRraw"}) do
                local value=pad:get_field(field)
                if not value or not finite(value.x) or not finite(value.y) then error("Camera stick unavailable: "..field) end
                fields[#fields+1]={pad=pad,field=field,value=Vector2f.new(value.x,value.y)}
            end
        end
        if self.error then return end
        self.ready=true;self.calls=self.calls+1
        if not self.request_view or self.request_view~=state.active_view
            or self.frame-(self.request_frame or -2)>1 or not self.bindings:camera_ready(state) then return end
        if state.mouse_camera then self.mouse_preserved=self.mouse_preserved+1;return end
        for _,field in ipairs(fields) do
            changes[#changes+1]=field
            field.pad:set_field(field.field,Vector2f.new(0,0))
        end
        self.suppressed=self.suppressed+1
    end)
    if not ok then self.error=tostring(err);self.ready=false;self:request(false);self:restore(changes);return {} end
    return changes
end
function Stick:install()
    local ok,err=pcall(function()
        local td=sdk.find_type_definition("snow.camera.CameraInput")
        local method=td and td:get_method("ReflectInput")
        if not method then error("CameraInput.ReflectInput unavailable") end
        sdk.hook(method,function(args)
            local storage=thread.get_hook_storage()
            local stack=storage[key] or {};storage[key]=stack
            local ok,input=pcall(sdk.to_managed_object,args[2])
            stack[#stack+1]=ok and self:before(input) or {}
            return sdk.PreHookResult.CALL_ORIGINAL
        end,function(retval)
            local stack=thread.get_hook_storage()[key]
            if stack and #stack>0 then self:finish(table.remove(stack)) end
            return retval
        end)
    end)
    if not ok then self.error=tostring(err);self.ready=false end
end
return Stick
