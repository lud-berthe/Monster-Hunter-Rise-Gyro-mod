local ReaderProbe={}
ReaderProbe.__index=ReaderProbe
function ReaderProbe.describe(snapshot,window)
    local result={window=window}
    for _,key in ipairs({"reader_available","reader_error","reader_owner_thread","reader_resume_count","window_transport",
        "steam_result","steam_error","steam_session_error","source","accepted_samples","rejected_samples","dropped_samples","update_result",
        "active_context","bindings_verified","host_capabilities","settings_save_result",
        "yaw_degrees","pitch_degrees","recenter_requested","suppress_native_right_stick","suppress_native_right_touchpad",
        "flick_input","long_press_filter","gui_capture","calibration_state","stationary","calibrated_dps","gravity","endpoints"}) do
        result[key]=snapshot[key]
    end
    return result
end
function ReaderProbe.new(camera_output_enabled)
    return setmetatable({started=os.time(),frame=0,samples={},done=false,
        camera_output_enabled=camera_output_enabled==true,
        status="Sensor diagnostic: recording automatically for 15 seconds"},ReaderProbe)
end
function ReaderProbe:step(description)
    if self.done then return end
    self.frame=self.frame+1
    if self.frame%5==0 and #self.samples<256 then self.samples[#self.samples+1]=description end
    if os.difftime(os.time(),self.started)<15 and self.frame<1200 then return end
    self.done=true
    local ok,saved=pcall(json.dump_file,"mhr_gyro_reader_probe.json",{
        schema=1,camera_output_enabled=self.camera_output_enabled,duration_seconds=os.difftime(os.time(),self.started),samples=self.samples})
    if ok and saved==true then
        self.status="Sensor diagnostic saved: mhr_gyro_reader_probe.json"
        log.info("MHRGyro: sensor diagnostic saved to reframework/data/mhr_gyro_reader_probe.json")
    else self.status="Sensor diagnostic save failed: "..tostring(saved);log.error("MHRGyro: "..self.status) end
end
return ReaderProbe
