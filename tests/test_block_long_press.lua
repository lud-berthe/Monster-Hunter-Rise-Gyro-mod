local LongPressBlocker=require('mhr_gyro/block_long_press')
local now=1000000000
local open=false
local events={}
local blocker=LongPressBlocker.new({now_ns=function() return now end},{},{open=function() return open end})
blocker.send=function(e) events[#events+1]=e end
local state={active_view=1,focused=true,camera_allowed=true,menu_open=false,paused=false}
local config={enabled=true,button=3,device=123,view=1,timestamp_ns=now}
local function snapshot(results)
    config.timestamp_ns=now
    return {update_result=0,long_press_filter=config,filter_results=results or {}}
end
local function pad(on,trg,rel)
    local values={['<mPadOn>k__BackingField']=on,['<mPadTrg>k__BackingField']=trg or 0,['<PadRel>k__BackingField']=rel or 0}
    return {get_field=function(_,k) return values[k] end,set_field=function(_,k,v) values[k]=v end},values
end
blocker:accept(snapshot())
local p,v=pad(64,64);blocker:apply(p,state,0)
assert(v['<mPadOn>k__BackingField']==0 and #events==1 and events[1].event==0)
now=now+50000000
p,v=pad(64);blocker:accept(snapshot());blocker:apply(p,state,0);assert(v['<mPadOn>k__BackingField']==0 and #events==1)
p,v=pad(0,0,64);blocker:apply(p,state,0);assert(events[2].event==1 and v['<PadRel>k__BackingField']==0)
blocker:accept(snapshot({{token=1,event=1,current=true,decision=2}}))
p,v=pad(0);blocker:apply(p,state,0);assert(v['<mPadTrg>k__BackingField']==64 and blocker.emitted==1)
p,v=pad(0);blocker:apply(p,state,0);assert(v['<PadRel>k__BackingField']==64)
blocker:accept(snapshot());p,v=pad(0);blocker:apply(p,state,0);assert(v['<mPadTrg>k__BackingField']==0)
-- Long hold: the SDK's suppress verdict emits no action.
p=pad(64,64);blocker:apply(p,state,0);now=now+300000000;blocker:accept(snapshot())
p=pad(0,0,64);blocker:apply(p,state,0)
blocker:accept(snapshot({{token=2,event=1,current=true,decision=1}}))
p,v=pad(0);blocker:apply(p,state,0);assert(v['<mPadTrg>k__BackingField']==0 and blocker.emitted==1)
-- UI opened during a deferred press: no blocker, no held input leaks on close.
p=pad(64,64);blocker:apply(p,state,0);open=true
p,v=pad(64);blocker:apply(p,state,0);assert(v['<mPadOn>k__BackingField']==0)
open=false;p,v=pad(64);blocker:apply(p,state,0);assert(v['<mPadOn>k__BackingField']==0)
p=pad(0,0,64);blocker:apply(p,state,0)
blocker:accept(snapshot({{token=3,event=1,current=true,decision=2}}))
p,v=pad(0);blocker:apply(p,state,0);assert(v['<mPadTrg>k__BackingField']==0)
-- Native holds / nonselected buttons / keyboard command fields stay intact.
local count=#events;p,v=pad(64,64);blocker:apply(p,state,64)
assert(v['<mPadOn>k__BackingField']==64 and #events==count)
p,v=pad(32,32);blocker:apply(p,state,0);assert(v['<mPadTrg>k__BackingField']==32)
-- A new view/device cannot consume a previous view's delayed blocker.
p=pad(64,64);blocker:apply(p,state,0);p=pad(0,0,64);blocker:apply(p,state,0)
config={enabled=true,button=3,device=456,view=1,timestamp_ns=now}
blocker:accept(snapshot({{token=4,event=1,current=true,decision=2}}))
p,v=pad(0);blocker:apply(p,state,0);assert(v['<mPadTrg>k__BackingField']==0)
-- Forward remains a native hold, with a single restored press edge.
p=pad(64,64);blocker:apply(p,state,0)
local token=events[#events].token
blocker:accept(snapshot({{token=token,event=0,current=true,decision=0}}))
p,v=pad(64);blocker:apply(p,state,0)
assert(v['<mPadOn>k__BackingField']==64 and v['<mPadTrg>k__BackingField']==64)
p,v=pad(64);blocker:apply(p,state,0);assert(v['<mPadTrg>k__BackingField']==0)
p,v=pad(0,0,64);blocker:apply(p,state,0);assert(v['<PadRel>k__BackingField']==64)
blocker:accept(snapshot({{token=token,event=1,current=true,decision=0}}))
p,v=pad(0);blocker:apply(p,state,0);assert(v['<mPadTrg>k__BackingField']==0)
-- Lost transport verdicts expire; a late result cannot replay a previous tap.
p=pad(64,64);blocker:apply(p,state,0);p=pad(0,0,64);blocker:apply(p,state,0)
local lost=events[#events].token
assert(blocker.pending[lost])
now=now+100000001
blocker:accept(snapshot({{token=lost,event=1,current=true,decision=2}}))
assert(next(blocker.pending)==nil and #blocker.replays==0)
p,v=pad(0);blocker:apply(p,state,0);assert(v['<mPadTrg>k__BackingField']==0)
print('MHR blocker adapter: deferred tap, hold, stale device/view, lost verdict expiry, UI and native holds passed')
