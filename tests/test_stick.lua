local saved_sdk,saved_thread,saved_vector=sdk,thread,Vector2f
Vector2f={new=function(x,y) return {x=x,y=y} end}
local Stick=require("mhr_gyro/stick")
local function pad()
    local p={_AnalogR={x=.6,y=-.2},_AnalogRraw={x=.7,y=-.3},_AnalogRUI={x=.8,y=-.4},_AnalogL={x=.2,y=.1},_PadOn=123}
    p.get_field=function(self,key) return self[key] end
    p.set_field=function(self,key,value) self[key]=value end
    return p
end
local a,b=pad(),pad()
local input={get_field=function(_,key) return key=="_PadOriginal" and a or b end}
local state={input=input,active_view=1,mouse_camera=false}
local safe=true
local binding={read_state=function() return state end,camera_ready=function() return safe end}
local stick=Stick.new(binding)
local pre,post,storage
storage={}
thread={get_hook_storage=function() return storage end}
sdk={find_type_definition=function(name)
    assert(name=="snow.camera.CameraInput")
    return {get_method=function(_,name) assert(name=="ReflectInput");return "method" end}
end,to_managed_object=function(x) return x end,PreHookResult={CALL_ORIGINAL=7},
hook=function(method,before,after) assert(method=="method");pre,post=before,after end}
stick:install();assert(not stick.error and not stick.ready)
assert(pre({[2]=input})==7);assert(post(91)==91 and stick.ready)
local function untouched()
    for _,p in ipairs({a,b}) do
        assert(p._AnalogR.x==.6 and p._AnalogR.y==-.2 and p._AnalogRraw.x==.7)
        assert(p._AnalogRUI.x==.8 and p._AnalogL.x==.2 and p._PadOn==123)
    end
end
untouched()
stick:request(true,1);stick:begin_frame();assert(pre({[2]=input})==7)
for _,p in ipairs({a,b}) do
    assert(p._AnalogR.x==0 and p._AnalogRraw.y==0)
    assert(p._AnalogRUI.x==.8 and p._AnalogL.x==.2 and p._PadOn==123)
end
-- Nested calls use a stack and restore the outer suppression first.
pre({[2]=input});post(92);assert(a._AnalogR.x==0)
post(93);untouched();assert(stick.suppressed==2)
assert(stick.refilled==0)
-- Diagnostic distinguishes engine refilling its pad from suppression working.
pre({[2]=input});a._AnalogR={x=.9,y=0};post(94);untouched()
assert(stick.refilled==1 and stick.last_suppression.after_original[1].x==.9)
-- A mouse-owned camera lever must remain intact with flick enabled. Losing the
-- source observation also preserves input and withdraws suppression support.
state.mouse_camera=true;pre({[2]=input});untouched();post(94)
assert(stick.mouse_preserved==1 and stick.ready)
state.mouse_camera=nil;pre({[2]=input});untouched();post(94);assert(not stick.ready)
state.mouse_camera=false;pre({[2]=input});assert(a._AnalogR.x==0);post(94);untouched();assert(stick.ready)
for _,view in ipairs({2,3,4}) do
    state.active_view=view;stick:request(true,view);pre({[2]=input});assert(a._AnalogR.x==0);post(94);untouched()
end
state.active_view=1;pre({[2]=input});untouched();post(1) -- stale view request
stick:request(true,1);safe=false;pre({[2]=input});untouched();post(1);safe=true
stick:begin_frame();stick:begin_frame();pre({[2]=input});untouched();post(1) -- stale frame
stick:request(true,1);pre({[2]={}});untouched();post(1) -- another camera
stick:request(false);pre({[2]=input});untouched();post(1)
-- A setter throwing after mutation must still restore every saved field.
local failed=false
b.set_field=function(self,key,value)
    self[key]=value
    if not failed then failed=true;error("synthetic setter failure") end
end
stick:request(true,1);pre({[2]=input});post(1);untouched()
assert(stick.error and not stick.ready and not stick.request_view)
sdk,thread,Vector2f=saved_sdk,saved_thread,saved_vector
print("MHR stick: camera-only scope, nested hooks, release, context/focus/stale gates and failure restoration passed")
