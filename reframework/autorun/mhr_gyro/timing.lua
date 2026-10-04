-- Passive native-call trace: preserve arguments, original calls and return values.
local Timing={}
Timing.__index=Timing
local entries={"UpdateHID","UpdateBehavior","LateUpdateBehavior","UpdateCamera",
    "LockScene","BeginRendering","EndRendering"}
local methods={
    {name="setTurnAngXTarget",axis="x"},
    {name="setTurnAngYTarget",axis="y"},
    {name="updateCamera"},
    {name="lateUpdate"},
}
local function finite(v)
    return type(v)=="number" and v==v and math.abs(v)<math.huge
end
local function angles(camera)
    if not camera then return nil end
    local a=camera:call("get_CameraAngleTarget")
    if not a or not finite(a.x) or not finite(a.y) then return nil end
    return {x=a.x,y=a.y}
end
local function wrapped(v) return (v+math.pi)%(2*math.pi)-math.pi end
function Timing:protect(key,fn)
    local ok,value=pcall(fn)
    if not ok then self.report.errors[key]=tostring(value) end
    return ok and value or nil
end
function Timing:event(kind,name,phase)
    local tid=thread.get_id()
    local id=tostring(tid)
    local bucket=self.report[kind][name]
    if not bucket then bucket={pre={},post={}}; self.report[kind][name]=bucket end
    bucket[phase][id]=(bucket[phase][id] or 0)+1
    self.sequence=self.sequence+1
    if #self.report.order<128 then
        self.report.order[#self.report.order+1]={sequence=self.sequence,frame=self.frames,
            thread_id=tid,kind=kind,name=name,phase=phase}
    end
    return bucket
end
function Timing:install_hook(td,spec)
    local method=td:get_method(spec.name)
    if not method then self.report.errors[spec.name]="Method unavailable"; return end
    local key="mhr_gyro_timing_"..spec.name
    sdk.hook(method,function(args)
        if self.active then self:protect(spec.name.." pre",function()
            local bucket=self:event("methods",spec.name,"pre")
            local camera=sdk.to_managed_object(args[2])
            local observation={before=angles(camera),camera=camera}
            if spec.axis then
                observation.argument=sdk.to_float(args[3])
                if finite(observation.argument) then
                    bucket.argument_min=math.min(bucket.argument_min or observation.argument,observation.argument)
                    bucket.argument_max=math.max(bucket.argument_max or observation.argument,observation.argument)
                end
            end
            thread.get_hook_storage()[key]=observation
        end) end
        return sdk.PreHookResult.CALL_ORIGINAL
    end,function(retval)
        if self.active then self:protect(spec.name.." post",function()
            local bucket=self:event("methods",spec.name,"post")
            local observation=thread.get_hook_storage()[key]
            if not observation then return end
            local after=angles(observation.camera)
            if spec.axis and after and finite(observation.argument) then
                local residual=math.abs(wrapped(after[spec.axis]-observation.argument))
                bucket.comparisons=(bucket.comparisons or 0)+1
                bucket.argument_matches=(bucket.argument_matches or 0)+(residual<0.00001 and 1 or 0)
                bucket.maximum_wrapped_residual=math.max(bucket.maximum_wrapped_residual or 0,residual)
            end
            bucket.observations=bucket.observations or {}
            if #bucket.observations<32 then
                bucket.observations[#bucket.observations+1]={frame=self.frames,thread_id=thread.get_id(),
                    argument=observation.argument,before=observation.before,after=after}
            end
            thread.get_hook_storage()[key]=nil
        end) end
        return retval
    end)
end
function Timing.new()
    local self=setmetatable({active=true,frames=0,sequence=0,started=os.time(),
        status="Camera/thread trace: recording automatically for 15 seconds",
        report={schema=1,observation_only=true,entries={},methods={},order={},errors={}}},Timing)
    for _,entry in ipairs(entries) do
        re.on_pre_application_entry(entry,function()
            if self.active then self:protect(entry.." pre",function() self:event("entries",entry,"pre") end) end
        end)
        re.on_application_entry(entry,function()
            if self.active then self:protect(entry.." post",function() self:event("entries",entry,"post") end) end
        end)
    end
    self:protect("hook setup",function()
        local td=sdk.find_type_definition("snow.camera.PlayerCamera")
        if not td then error("PlayerCamera type unavailable") end
        for _,spec in ipairs(methods) do self:install_hook(td,spec) end
    end)
    return self
end
function Timing:step()
    if not self.active then return end
    self.frames=self.frames+1
    self:protect("render callback",function() self:event("entries","on_frame","post") end)
    if os.difftime(os.time(),self.started)<15 and self.frames<1200 then return end
    self.active=false
    self.report.duration_seconds=os.difftime(os.time(),self.started)
    self.report.render_frames=self.frames
    local ok,saved=pcall(json.dump_file,"mhr_gyro_camera_trace.json",self.report)
    if ok and saved==true then
        self.status="Camera/thread trace saved: mhr_gyro_camera_trace.json"
        log.info("MHRGyro: passive camera/thread trace saved to reframework/data/mhr_gyro_camera_trace.json")
    else
        self.status="Camera/thread trace save failed: "..tostring(saved)
        log.error("MHRGyro: "..self.status)
    end
end
return Timing
