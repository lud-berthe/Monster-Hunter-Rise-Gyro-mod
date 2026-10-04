local saved={json=json,log=log,time=os.time}
local CameraTest=dofile(root.."/reframework/autorun/mhr_gyro/camera_test.lua")
local open=true
local calls,saves=0,{}
local clock_time=100
os.time=function() return clock_time end
local camera={call=function() return {x=0.1,y=0.2} end}
local binding={read_state=function()
    return {camera_allowed=true,focused=true,menu_open=open,paused=false,camera=camera,normal_view=true}
end,apply_camera=function(_,yaw,pitch)
    assert(yaw==5 and pitch==3);calls=calls+1;return {expected={x=0.1,y=0.2}}
end}
json={dump_file=function(path,report)
    assert(path=="mhr_gyro_camera_test.json" and report.schema==2 and report.key_observed)
    assert(not report.state or (not report.state.camera and not report.state.input))
    saves[#saves+1]={stage=report.stage,attempted=report.action_attempted,samples=#report.samples}
    return true
end}
log={info=function() end,error=function(message) error(message) end}
local test=CameraTest.new(binding)
test:key(true);test:key(true)
assert(#saves==1 and saves[1].stage=="requested" and not saves[1].attempted and calls==0)
test:step({},false);assert(calls==0 and not test.spent)
assert(test.report.stage=="blocked" and test.report.blocked_reasons[1]=="Game menu is open or unknown")
test:key(false);open=false;test:key(true);test:step({},true)
assert(calls==0 and not test.spent)
assert(test.report.blocked_reasons[1]=="REFramework or gyro panel is open")
test:step({},false);assert(calls==0,"Closing the overlay must not replay a blocked request")
test:key(false);test:key(true);test:step({},nil)
assert(calls==0 and test.report.stage=="blocked","Unknown overlay state must block")
test:key(false);test:key(true);test:step({},false)
assert(calls==1 and test.spent)
assert(test.report.stage=="recording" and test.report.action_attempted and test.report.write_succeeded)
local before=#saves
for _=1,70 do test:key(false);test:key(true);test:step({},false) end
assert(calls==1 and #saves==before+1 and test.frames==60 and saves[#saves].stage=="complete")
local expired=CameraTest.new(binding)
expired:key(true);clock_time=103;expired:key(false);expired:step({},false)
assert(expired.report.stage=="expired" and not expired.pending and not expired.spent and calls==1)
local delayed=CameraTest.new(binding)
delayed:key(true);clock_time=106;delayed:step({},false)
assert(delayed.report.stage=="expired" and calls==1)
local failed=CameraTest.new({read_state=binding.read_state,apply_camera=function() error("setter failure") end})
failed:key(true);failed:step({},false)
assert(failed.spent and failed.report.stage=="failed" and failed.report.action_attempted)
failed:key(false);failed:key(true);failed:step({},false)
assert(failed.key_requests==1 and calls==1)
local unreadable=CameraTest.new({read_state=function() error("state unavailable") end})
unreadable:key(true);unreadable:step({},false)
assert(unreadable.report.stage=="blocked" and unreadable.report.blocked_reasons[1]:find("state unavailable"))
local transition=CameraTest.new({read_state=binding.read_state,apply_camera=function() return nil end})
transition:key(true);transition:step({},false)
assert(transition.report.stage=="blocked" and not transition.report.write_succeeded and transition.frames==0)
assert(test:describe().late_update_frames>60)
json,log,os.time=saved.json,saved.log,saved.time
print("MHR camera test: key/callback diagnostics, blocked reasons, expiry and one bounded action passed")
