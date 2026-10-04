-- The SDK owns the press duration. This adapter only defers/replays a local
-- player command and discards decisions which belong to an obsolete cycle.
local LongPressBlocker={}
LongPressBlocker.__index=LongPressBlocker
-- SDL_GamepadButton -> snow.Pad.Button, from Rise's runtime enum metadata.
-- Guide, Start/Back and extra physical buttons have no direct gameplay binding.
local masks={[1]=32,[2]=128,[3]=64,[4]=16,[8]=4096,[9]=8192,
    [10]=512,[11]=2048,[12]=1,[13]=2,[14]=4,[15]=8}
local fields={"<mPadOn>k__BackingField","<mPadTrg>k__BackingField","<PadRel>k__BackingField"}
local native_holds={0,1,2,4,5,7,8,9,45}
function LongPressBlocker.new(native,bindings,capture)
    return setmetatable({native=native,bindings=bindings,capture=capture,serial=0,
        snapshot={},blocked=0,emitted=0,suppressed=0,cancelled=0,pending={},replays={}},LongPressBlocker)
end
local function same(a,b)
    return a and b and a.device==b.device and a.view==b.view and a.button==b.button
end
function LongPressBlocker:event(cycle,event,eligible)
    self.send({op="filter",token=cycle.token,key=cycle.config.button-1,event=event,
        timestamp_ns=self.native.now_ns(),view=cycle.config.view,device=cycle.config.device,
        eligible=eligible,native_hold=not eligible})
end
function LongPressBlocker:cancel()
    if self.cycle then
        self:event(self.cycle,1,false)
        self.blocked=self.blocked|self.cycle.mask
        self.cancelled=self.cancelled+1
    end
    self.cycle=nil;self.pending={};self.replays={};self.release=nil
end
function LongPressBlocker:accept(snapshot)
    self.snapshot=snapshot
    local config=snapshot.long_press_filter
    local now=self.native.now_ns()
    for token,pending in pairs(self.pending) do
        if now>=pending.expires_at or not config or not config.enabled or not same(pending.config,config) then
            self.pending[token]=nil
        end
    end
    if self.cycle and (not config or not config.enabled or not same(self.cycle.config,config)) then self:cancel() end
    for _,result in ipairs(snapshot.filter_results or {}) do
        local pending=self.pending[result.token]
        local cycle=self.cycle and self.cycle.token==result.token and self.cycle or pending
        if cycle and result.event==0 and result.current and result.decision==0 then cycle.forward=true end
        if pending and result.token==pending.token and result.event==1 then
            if result.current and (result.decision==2 or (result.decision==0 and pending.forward))
                and config and config.enabled and same(pending.config,config) then
                self.replays[#self.replays+1]=pending
            end
            self.pending[result.token]=nil
        end
    end
end
local function mask(pad,bits)
    for _,field in ipairs(fields) do pad:set_field(field,pad:get_field(field)&~bits) end
end
function LongPressBlocker:apply(pad,state,excluded)
    local config=self.snapshot.long_press_filter
    local on=pad:get_field(fields[1])
    local previous_blocked=self.blocked
    self.blocked=self.blocked&on
    mask(pad,previous_blocked)
    local safe=state.focused and state.camera_allowed and not state.menu_open and not state.paused
        and not self.capture:open() and config and config.enabled and state.active_view==config.view
        and self.snapshot.update_result==0 and self.native.now_ns()-config.timestamp_ns<100000000
    if not safe then
        self:cancel();mask(pad,self.blocked);return
    end
    local bit=masks[config.button]
    if not bit or bit&excluded~=0 then
        self:cancel();mask(pad,self.blocked);return
    end
    if self.release then
        pad:set_field(fields[3],pad:get_field(fields[3])|self.release)
        self.release=nil
    end
    -- Keep native chords/hold commands intact. Cancel an already deferred
    -- cycle without letting its held button suddenly trigger a native action.
    if on&~(bit|131072|262144)~=0 then
        self:cancel();mask(pad,self.blocked);return
    end
    local down=on&bit~=0
    if self.cycle then
        if self.cycle.forward then
            if down and not self.cycle.forwarded then
                pad:set_field(fields[2],pad:get_field(fields[2])|self.cycle.mask)
                self.cycle.forwarded=true
            end
        else mask(pad,self.cycle.mask) end
        if not down then
            self:event(self.cycle,1,true)
            if not self.cycle.forwarded then
                self.cycle.expires_at=self.native.now_ns()+100000000
                self.pending[self.cycle.token]=self.cycle
            end
            self.cycle=nil
        end
    elseif down and on&previous_blocked==0 and pad:get_field(fields[2])&bit~=0 then
        self.serial=self.serial+1
        self.cycle={token=self.serial,mask=bit,config=config}
        self:event(self.cycle,0,true)
        mask(pad,bit);self.suppressed=self.suppressed+1
    end
    if #self.replays>0 then
        local replay=table.remove(self.replays,1)
        if same(replay.config,config) and self.native.now_ns()-replay.config.timestamp_ns<400000000 then
            pad:set_field(fields[1],pad:get_field(fields[1])|replay.mask)
            pad:set_field(fields[2],pad:get_field(fields[2])|replay.mask)
            pad:set_field(fields[3],pad:get_field(fields[3])&~replay.mask)
            self.release=replay.mask;self.emitted=self.emitted+1
        end
    end
end
function LongPressBlocker:install(send)
    self.send=send
    if (self.native.camera_integration_version or 0)<4 then return end
    local kind=sdk.find_type_definition("snow.player.PlayerInput.PlPad")
    local method=kind and kind:get_method("updatePad")
    if not method then self.error="Player command hook unavailable";return end
    sdk.hook(method,function(args)
        thread.get_hook_storage().mhr_gyro_long_press_pad=sdk.to_managed_object(args[2])
    end,function(retval)
        local storage=thread.get_hook_storage()
        local pad=storage.mhr_gyro_long_press_pad;storage.mhr_gyro_long_press_pad=nil
        local ok,err=pcall(function()
            local input=sdk.get_managed_singleton("snow.StmInputManager")
            local device=input and input:get_field("_InGameInputDevice")
            local keys=device and device:get_field("_pl_input")
            local player=keys and keys:get_field("Refinput")
            if not player or player:get_field("<mNow>k__BackingField")~=pad then return end
            local excluded=0
            for _,command in ipairs(native_holds) do excluded=excluded|pad:call("getPadBtn",command) end
            local state=self.bindings:read_state()
            if state.active_view==5 or state.active_view==6 then
                excluded=excluded|pad:call("getPadBtn",54)|pad:call("getPadBtn",55)
            end
            self.excluded=excluded
            self.ready=true
            self:apply(pad,state,excluded)
        end)
        if not ok then self.error=tostring(err);self.ready=false;self:cancel() end
        return retval
    end,true)
end
function LongPressBlocker:diagnostics()
    return {ready=self.ready==true,error=self.error,emitted=self.emitted,suppressed=self.suppressed,
        cancelled=self.cancelled,excluded=self.excluded,
        button=self.snapshot.long_press_filter and self.snapshot.long_press_filter.button}
end
return LongPressBlocker
