local Capture=require('mhr_gyro/input_capture')
local open=false
local capture=Capture.new({gui_state=function() return {capture=open and 7 or 0} end})
local function object(fields)
    fields.get_field=function(self,k) assert(self[k]~=nil,k);return self[k] end
    fields.set_field=function(self,k,v) assert(self[k]~=nil,k);self[k]=v end
    return fields
end
local function pad()
    return object({_on=32,clears=0,call=function(self,name)
        assert(name=='clear');self.clears=self.clears+1;self._on=0
    end})
end
local p=object({app=pad(),hard=pad(),sys=pad()})
capture:pad(p);assert(p.app._on==32 and p.app.clears==0)
open=true;capture:pad(p);assert(p.app._on==0 and p.hard.clears==1 and p.sys.clears==1)
open=false;p.hard._on=32;capture:pad(p);assert(p.hard._on==0 and capture.pad_latched)
capture:pad(p);assert(not capture.pad_latched)
p.app._on=32;capture:pad(p);assert(p.app._on==32) -- new press after neutral
local function ui() return {clears=0,call=function(self,n) assert(n=='inputClear');self.clears=self.clears+1 end} end
local player=object({_al={x=1,y=2},_al_sub={x=1,y=2},_ar={x=1,y=2},distMouse={x=1,y=2},
    call=function(self,n) assert(n=='clearAll');self.cleared=true end})
local keys={[0]=false,[1]=true,get_size=function() return 2 end,get_element=function(self,i) return self[i] end}
local device=object({_kbd_on=keys,_mos_on=1,_pad_on=32,_pad_oldon=32,_pad_trg=32,_pad_rel=0,
    _mos_WheelDelta=1,_mos_SmoothWheelDelta=1,_mos_SmoothWheelDeltaStack=1,
    _pad_al={x=1,y=2},_pad_ar={x=1,y=2},_mos_MovePosition={x=1,y=2},
    _pl_input=player,_app_input=ui(),_sys_input=ui(),_hard_input=ui(),call=function(self,n)
        assert(n=='inputDisable');keys[1]=false;self._mos_on=0
    end})
open=true;capture:input(device)
assert(device._pad_on==0 and player.cleared and player._ar.x==0 and player.distMouse.y==0)
assert(device._app_input.clears==1 and device._sys_input.clears==1 and device._hard_input.clears==1)
assert(device._mos_MovePosition.x==0 and device._mos_SmoothWheelDelta==0)
open=false;keys[1]=true;capture:input(device);assert(capture.keys_latched)
capture:input(device);assert(not capture.keys_latched)
local before=capture.frames;capture:input(device);assert(capture.frames==before)
print('MHR input capture: gamepad, keyboard, mouse, UI caches and held-close quarantine passed')
