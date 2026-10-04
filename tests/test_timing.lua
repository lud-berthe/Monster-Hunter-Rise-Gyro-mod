-- Passive hooks must forward native execution even when observations fail.
local saved={sdk=sdk,re=re,thread=thread}
local installed,pre_entries,post_entries={},{},{}
local storage={}
sdk={PreHookResult={CALL_ORIGINAL=7},
    find_type_definition=function() return {get_method=function(_,name) return name end} end,
    hook=function(method,pre,post) installed[method]={pre=pre,post=post} end,
    to_managed_object=function(value) return value end,to_float=function(value) return value end}
re={on_pre_application_entry=function(name,fn) pre_entries[name]=fn end,
    on_application_entry=function(name,fn) post_entries[name]=fn end}
thread={get_id=function() return 42 end,get_hook_storage=function() return storage end}
local Timing=dofile(root.."/reframework/autorun/mhr_gyro/timing.lua")
local trace=Timing.new()
local camera={x=0,y=0,call=function(self,method)
    assert(method=="get_CameraAngleTarget")
    return {x=self.x,y=self.y}
end}
local args={[2]=camera,[3]=0.2}
local native_return={}
assert(installed.setTurnAngXTarget.pre(args)==7)
assert(args[2]==camera and args[3]==0.2)
camera.x=0.2 -- The original game method, outside the observer.
assert(installed.setTurnAngXTarget.post(native_return)==native_return)
assert(trace.report.methods.setTurnAngXTarget.argument_matches==1)
pre_entries.UpdateHID(); post_entries.UpdateHID()
assert(trace.report.entries.UpdateHID.pre["42"]==1)
assert(trace.report.entries.UpdateHID.post["42"]==1)
local broken={[2]={call=function() error("unreadable camera") end},[3]=0.1}
assert(installed.setTurnAngYTarget.pre(broken)==7)
assert(installed.setTurnAngYTarget.post(native_return)==native_return)
assert(trace.report.errors["setTurnAngYTarget pre"])
trace.active=false
local sequence=trace.sequence
assert(installed.setTurnAngXTarget.pre(args)==7)
assert(installed.setTurnAngXTarget.post(native_return)==native_return)
assert(trace.sequence==sequence)
sdk,re,thread=saved.sdk,saved.re,saved.thread
print("MHR timing: native calls, arguments and returns survive passive trace failures")
