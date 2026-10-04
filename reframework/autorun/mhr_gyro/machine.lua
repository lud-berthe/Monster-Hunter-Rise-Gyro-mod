-- Siege weapons consume CameraOperation themselves before deriving their
-- camera from the weapon's orientation. Feed their shared radian input once.
local Machine={}
Machine.__index=Machine
local function finite(v) return type(v)=="number" and v==v and math.abs(v)<math.huge end
function Machine.new(bindings)
    return setmetatable({bindings=bindings,frame=0,applied=0,dropped=0},Machine)
end
function Machine:begin_frame() self.frame=self.frame+1 end
function Machine:queue(state,yaw,pitch)
    if not finite(yaw) or not finite(pitch) then error("Invalid siege camera delta") end
    if self.error then error(self.error) end
    if state.active_view~=5 and state.active_view~=6 then error("Not a siege view") end
    local pending=self.pending
    if not pending or pending.view~=state.active_view or pending.input~=state.input or self.frame-pending.frame>1 then
        pending={view=state.active_view,input=state.input,frame=self.frame,yaw=0,pitch=0}
        self.pending=pending
    end
    -- Age belongs to the oldest queued delta. Repeated queue calls must not
    -- keep unconsumed movement alive while ReflectInput is absent.
    pending.yaw=pending.yaw+yaw;pending.pitch=pending.pitch+pitch
end
function Machine:apply(input)
    local pending=self.pending
    if not pending then return end
    self.pending=nil -- Never replay a failed or no-longer-safe request.
    local state=self.bindings:read_state()
    if pending.input~=input or state.input~=input or state.active_view~=pending.view
        or self.frame-pending.frame>1 or not self.bindings:camera_ready(state) then
        self.dropped=self.dropped+1;return
    end
    local value=input:get_field("_CameraOperation")
    if not value or not finite(value.x) or not finite(value.y) then error("Siege camera operation unavailable") end
    local x,y=value.x+pending.pitch*math.pi/180,value.y+pending.yaw*math.pi/180
    input:set_field("_CameraOperation",Vector2f.new(x,y))
    self.applied=self.applied+1
    self.last={view=pending.view,frame=self.frame,before={x=value.x,y=value.y},after={x=x,y=y}}
end
function Machine:install()
    local ok,err=pcall(function()
        local td=sdk.find_type_definition("snow.camera.CameraInput")
        local method=td and td:get_method("ReflectInput")
        if not method then error("Siege camera input hook unavailable") end
        sdk.hook(method,function(args)
            local storage=thread.get_hook_storage()
            local stack=storage.mhr_gyro_machine or {};storage.mhr_gyro_machine=stack
            local valid,input=pcall(sdk.to_managed_object,args[2])
            stack[#stack+1]=valid and input or false
            return sdk.PreHookResult.CALL_ORIGINAL
        end,function(retval)
            local stack=thread.get_hook_storage().mhr_gyro_machine
            local input=stack and table.remove(stack)
            if input then
                local success,failure=pcall(self.apply,self,input)
                if not success then self.error=tostring(failure);self.pending=nil end
            end
            return retval
        end)
    end)
    if not ok then self.error=tostring(err) end
end
function Machine:diagnostics()
    return {applied=self.applied,dropped=self.dropped,error=self.error,last=self.last}
end
return Machine
