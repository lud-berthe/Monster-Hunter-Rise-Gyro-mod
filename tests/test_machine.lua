local Machine=require("mhr_gyro/machine")
local old_vector=Vector2f
Vector2f={new=function(x,y) return {x=x,y=y} end}
local operation={x=.02,y=-.03}
local writes=0
local input={get_field=function(_,name) assert(name=="_CameraOperation");return operation end,
    set_field=function(_,name,value) assert(name=="_CameraOperation");writes=writes+1;operation=value end}
local state={active_view=5,input=input}
local safe=true
local binding={read_state=function() return state end,camera_ready=function() return safe end}
local m=Machine.new(binding)
local function near(a,b) assert(math.abs(a-b)<1e-9) end
m:queue(state,90,10);m:begin_frame();m:apply(input)
near(operation.x,.02+math.pi/18);near(operation.y,-.03+math.pi/2)
m:apply(input);assert(writes==1) -- consume once, preserve native input
state.active_view=6;operation={x=0,y=0}
m:queue(state,-10,-5);m:queue(state,3,1);m:apply(input)
near(operation.x,-4*math.pi/180);near(operation.y,-7*math.pi/180)
for _,reason in ipairs({"view","input","focus","age"}) do
    local n=writes;m:queue(state,90,10)
    if reason=="view" then state.active_view=5
    elseif reason=="input" then state.input={}
    elseif reason=="focus" then safe=false
    else m:begin_frame();m:begin_frame() end
    m:apply(input);assert(writes==n and not m.pending)
    state.active_view=6;state.input=input;safe=true
end
assert(m.dropped==4)
-- Missing ReflectInput for several frames must not accumulate an old turn.
operation={x=0,y=0};m.pending=nil
m:queue(state,90,0);m:begin_frame();m:queue(state,90,0)
m:begin_frame();m:queue(state,1,0);m:apply(input)
near(operation.y,math.pi/180)
assert(not pcall(m.queue,m,state,0/0,0))
m:queue(state,1,1);operation={x=0/0,y=0}
assert(not pcall(m.apply,m,input) and not m.pending)
Vector2f=old_vector
print("MHR siege input: native movement retained, radians, single consumption and stale/context/focus rejection passed")
