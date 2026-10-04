-- Capture Rise's resolved input, after acquisition. SDL/Win32 input used by
-- GyroLib's settings remains untouched. No persistent game input mode is set.
local Capture={}
Capture.__index=Capture
function Capture.new(native)
    return setmetatable({native=native,pad_latched=false,keys_latched=false,frames=0},Capture)
end
function Capture:open()
    local ok,state=pcall(self.native.gui_state)
    return ok and type(state)=="table" and type(state.capture)=="number" and state.capture~=0
end
function Capture:pad(device)
    local held=false
    for _,name in ipairs({"hard","sys","app"}) do
        local pad=device:get_field(name)
        held=held or (pad and pad:get_field("_on")~=0)
    end
    self.pad_latched=self:open() or (self.pad_latched and held)
    if not self.pad_latched then return end
    for _,name in ipairs({"hard","sys","app"}) do
        local pad=device:get_field(name)
        if pad then pad:call("clear") end
    end
end
local function zero_vector(object,name)
    local value=object:get_field(name)
    value.x=0;value.y=0;object:set_field(name,value)
end
function Capture:input(device)
    local open=self:open()
    if not open and not self.keys_latched then return end
    local held=device:get_field("_mos_on")~=0 or device:get_field("_pad_on")~=0
    local keys=device:get_field("_kbd_on")
    if keys then
        for i=0,keys:get_size()-1 do held=held or keys:get_element(i)==true end
    end
    self.keys_latched=open or held
    if not self.keys_latched then return end
    -- inputDisable clears keyboard, mouse buttons and repeat timers. It can
    -- preserve background pads, so explicitly clear the game-owned pad cache.
    device:call("inputDisable")
    for _,name in ipairs({"_pad_on","_pad_oldon","_pad_trg","_pad_rel",
        "_mos_WheelDelta","_mos_SmoothWheelDelta","_mos_SmoothWheelDeltaStack"}) do
        device:set_field(name,0)
    end
    for _,name in ipairs({"_pad_al","_pad_ar","_mos_MovePosition"}) do zero_vector(device,name) end
    for _,name in ipairs({"_app_input","_sys_input","_hard_input"}) do
        local input=device:get_field(name)
        if input then input:call("inputClear") end
    end
    local player=device:get_field("_pl_input")
    if player then
        player:call("clearAll")
        for _,name in ipairs({"_al","_al_sub","_ar","distMouse"}) do zero_vector(player,name) end
    end
    self.frames=self.frames+1
end
function Capture:install()
    if self.native.gui_available~=true then return end
    for _,entry in ipairs({{"snow.Pad",self.pad},{"snow.StmInputManager.InGameInputDevice",self.input}}) do
        local kind=sdk.find_type_definition(entry[1])
        local method=kind and kind:get_method("update")
        if not method then self.error="Input capture hook unavailable: "..entry[1];return end
        local slot="mhr_gyro_capture_"..entry[1]
        local callback=entry[2]
        sdk.hook(method,function(args)
            thread.get_hook_storage()[slot]=sdk.to_managed_object(args[2])
        end,function(retval)
            local storage=thread.get_hook_storage()
            local object=storage[slot];storage[slot]=nil
            if object then
                local ok,err=pcall(callback,self,object)
                if not ok then self.error=tostring(err) end
            end
            return retval
        end,true)
    end
    self.installed=true
end
function Capture:diagnostics()
    return {installed=self.installed==true,error=self.error,frames=self.frames,
        pad_latched=self.pad_latched,keys_latched=self.keys_latched}
end
return Capture
