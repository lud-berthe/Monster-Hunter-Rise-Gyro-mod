local CameraTest={}
CameraTest.__index=CameraTest
local function describe_state(state)
    -- Never serialize managed camera/input userdata into a diagnostic report.
    local result={}
    for _,name in ipairs({"menu_open","paused","focused","camera_allowed","normal_view",
        "menu_state_observed","overlay_open","window","objects","gui_queries","blocking_fields",
        "camera_type","camera_mode","rotation_type","errors"}) do result[name]=state[name] end
    return result
end
function CameraTest.new(bindings)
    return setmetatable({bindings=bindings,previous_key=false,pending=false,spent=false,frames=0,
        late_update_frames=0,key_requests=0,
        status="F8: one camera test (+5 degrees yaw, +3 degrees pitch)"},CameraTest)
end
function CameraTest:key(down,reader)
    if down and not self.previous_key and not self.spent then
        self.pending=true
        self.requested_at=os.time()
        self.key_requests=self.key_requests+1
        self.frames=0
        self.report={schema=2,stage="requested",key_observed=true,action_attempted=false,
            gyro_camera_output_enabled=false,yaw_degrees=5,pitch_degrees=3,
            write_succeeded=false,key_requests=self.key_requests,
            late_update_frames_at_request=self.late_update_frames,reader_at_request=reader,samples={}}
        -- Record the key on the render callback even if the engine callback never
        -- runs. Reading or changing the game camera stays on LateUpdateBehavior.
        self:save("F8 received; waiting for the camera update")
    end
    self.previous_key=down
    if self.pending and os.difftime(os.time(),self.requested_at)>=3 then
        self.pending=false
        self.report.stage="expired"
        self.report.blocked_reasons={"LateUpdateBehavior did not consume the request within 3 seconds"}
        self:save("Camera test expired; camera update callback unavailable")
    end
end
function CameraTest:describe()
    return {key_requests=self.key_requests,late_update_frames=self.late_update_frames,
        pending=self.pending,spent=self.spent,recorded_frames=self.frames,status=self.status,
        stage=self.report and self.report.stage or "idle"}
end
function CameraTest:save(status)
    local ok,saved=pcall(json.dump_file,"mhr_gyro_camera_test.json",self.report)
    if ok and saved==true then
        self.status=status
        log.info("MHRGyro: "..status.."; report: reframework/data/mhr_gyro_camera_test.json")
    else self.status="Camera test save failed: "..tostring(saved);log.error("MHRGyro: "..self.status) end
end
function CameraTest:step(reader,overlay_open,overlay)
    self.late_update_frames=self.late_update_frames+1
    if self.pending then
        self.pending=false
        self.report.late_update_frames_at_attempt=self.late_update_frames
        self.report.reader_at_start=reader
        self.report.overlay=overlay or {open=overlay_open==true}
        local ok,state=pcall(function() return self.bindings:read_state() end)
        local reasons={}
        if not ok then reasons[#reasons+1]="Camera state read failed: "..tostring(state)
        else
            self.report.state=describe_state(state)
            if overlay_open~=false then reasons[#reasons+1]="REFramework or gyro panel is open" end
            if state.camera_allowed~=true then reasons[#reasons+1]="Normal camera state unavailable or blocked" end
            if state.focused~=true then reasons[#reasons+1]="Game window is not focused" end
            if state.menu_open~=false then reasons[#reasons+1]="Game menu is open or unknown" end
            if state.paused~=false then reasons[#reasons+1]="Game is paused or pause state is unknown" end
        end
        if #reasons>0 then
            self.report.stage="blocked"
            self.report.blocked_reasons=reasons
            self:save("Camera test blocked: "..table.concat(reasons,"; "))
            return
        end
        if os.difftime(os.time(),self.requested_at)>=3 then
            self.report.stage="expired"
            self.report.blocked_reasons={"Camera test request is older than 3 seconds"}
            self:save("Camera test expired; press F8 again")
            return
        end
        -- One bounded action per script lifetime, even if a setter throws after
        -- applying part of the rotation. Native camera updates remain untouched.
        self.spent=true
        self.report.action_attempted=true
        local applied,result=pcall(function() return self.bindings:apply_camera(5,3) end)
        if applied and result==nil then
            self.report.stage="blocked"
            self.report.write_succeeded=false
            self.report.blocked_reasons={"Camera state changed before the write"}
            self:save("Camera test blocked by a gameplay transition")
            return
        end
        self.report.write_succeeded=applied
        self.report.result=applied and result or nil
        self.report.error=not applied and tostring(result) or nil
        self.report.stage=applied and "recording" or "failed"
        self:save(applied and "Camera test applied; recording persistence for 60 frames"
            or "Camera test setter failed: "..tostring(result))
    end
    if not self.report or not self.report.write_succeeded or self.frames>=60 then return end
    self.frames=self.frames+1
    local ok,sample=pcall(function()
        local state=self.bindings:read_state()
        local sample={frame=self.frames,reader=reader,normal_view=state.normal_view,
            camera_allowed=state.camera_allowed,menu_open=state.menu_open,focused=state.focused}
        if state.camera then
            local target=state.camera:call("get_CameraAngleTarget")
            local actual=state.camera:call("get_CameraAngle")
            sample.target={x=target.x,y=target.y};sample.actual={x=actual.x,y=actual.y}
        end
        if state.input then
            local operation=state.input:get_field("_CameraOperation")
            if operation then sample.native_operation={x=operation.x,y=operation.y} end
        end
        return sample
    end)
    self.report.samples[#self.report.samples+1]=ok and sample or {frame=self.frames,error=tostring(sample)}
    if self.frames==60 then
        self.report.stage="complete"
        self:save("Camera test saved: mhr_gyro_camera_test.json")
    end
end
return CameraTest
