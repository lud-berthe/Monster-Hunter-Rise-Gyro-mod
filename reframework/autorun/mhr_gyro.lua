if not mhr_gyro_native then
    log.error("MHRGyro.dll is missing or incompatible. No camera/input changes were made.")
    return
end
local Controller=require("mhr_gyro/controller")
local profile=require("mhr_gyro/profile")
local Inspector=require("mhr_gyro/inspect")
local Probe=require("mhr_gyro/probe")
local Timing=require("mhr_gyro/timing")
local Bindings=require("mhr_gyro/bindings")
local ReaderProbe=require("mhr_gyro/reader_probe")
local CameraTest=require("mhr_gyro/camera_test")
log.info("MHRGyro: diagnostic Lua initialized; inspection module loaded")
local controller=Controller.new(mhr_gyro_native,profile)
local probe=Probe.new()
local timing=profile.trace_camera==true and Timing.new() or nil
local reader_probe=ReaderProbe.new(profile.verified==true)
local camera_test=CameraTest.new(Bindings.new(mhr_gyro_native))
local reader_description={}
local inspection=nil
local inspection_error=nil
local inspection_frames=0
local selected_view=nil
local function capture_inspection()
    local ok,result=pcall(function()
        local report=Inspector.capture()
        if json.dump_file("mhr_gyro_bindings.json",report)~=true then
            error("Could not save mhr_gyro_bindings.json; check REFramework's JSON log")
        end
        return report
    end)
    if ok then
        inspection=result
        inspection_error=nil
        log.info("MHRGyro: inspection saved to reframework/data/mhr_gyro_bindings.json ("..#result.types.." types)")
    else
        inspection_error=tostring(result)
        log.error("MHRGyro: inspection failed: "..inspection_error)
    end
end
if (mhr_gyro_native.camera_integration_version or 0)<3 then
    log.error("MHRGyro: these camera profiles require the matching updated MHRGyro.dll. Restart after installing the complete update.")
    return
end
local function draw_setting(setting)
    if setting.visible then
        local label=setting.label.."##"..setting.id

        if not setting.available then
            imgui.text(setting.label.." (unavailable)")
        elseif setting.type==3 then
            if imgui.button(label) then controller:queue({op="action",id=setting.id}) end
        elseif setting.type==0 then
            local changed,value=imgui.checkbox(label,setting.value~=0)
            if changed then controller:queue({op="set",id=setting.id,value=value and 1 or 0}) end
        elseif setting.type==2 then
            imgui.text(setting.label)
            for _,choice in ipairs(setting.choices or {}) do
                if choice.available then
                    local selected=choice.value==setting.value and "[x] " or "[ ] "
                    if imgui.button(selected..choice.label.."##"..setting.id..choice.value) then
                        controller:queue({op="set",id=setting.id,value=choice.value})
                    end
                    imgui.same_line()
                elseif choice.value==setting.value then
                    imgui.text("[x] "..choice.label.." (unavailable)")
                end
            end
            imgui.new_line()
        else
            local changed,value=imgui.slider_float(label,setting.value,setting.minimum,setting.maximum)
            if changed then
                local step=setting.step>0 and setting.step or 1
                value=math.max(setting.minimum,math.min(setting.maximum,math.floor(value/step+0.5)*step))
                controller:queue({op="set",id=setting.id,value=value})
            end
        end
        if imgui.is_item_hovered() then imgui.set_tooltip(setting.description) end
    end
end
local function draw()
    if mhr_gyro_native.gui_available==true then return end
    if not controller.panel_open then return end
    imgui.set_next_window_size({760,720},4) -- FirstUseEver, host DPI/font applies.
    controller.panel_open=imgui.begin_window("Monster Hunter Rise - GyroLib",true)
    if controller.panel_open then
        imgui.text(profile.status or profile.id)
        imgui.text(probe.status)
        if timing then imgui.text(timing.status) end
        imgui.text(reader_probe.status)
        imgui.text(camera_test.status)
        if controller.error then imgui.text("Adapter stopped: "..controller.error) end
        local snapshot=controller.snapshot
        imgui.text("Reader: "..tostring(snapshot.reader_available).." | Source: "..tostring(snapshot.source)
            .." | Samples: "..tostring(snapshot.accepted_samples))
        imgui.text("Camera bindings verified: "..tostring(snapshot.bindings_verified))
        imgui.text("Update: "..tostring(snapshot.update_result).." | Last command: "..tostring(snapshot.last_command_result))
        imgui.text("Automatic save: "..tostring(snapshot.settings_save_result))
        if snapshot.reader_error and snapshot.reader_error~="" then imgui.text(snapshot.reader_error) end
        if snapshot.steam_error and snapshot.steam_error~="" then imgui.text("Steam: "..snapshot.steam_error) end
        imgui.text("Steam gyro-as-mouse/stick must be disabled manually; automatic detection is incomplete.")
        imgui.text("Flick and Block long press require their verified game input hooks.")
        if imgui.button("Inspect known types (read only)") then
            capture_inspection()
        end
        if inspection then imgui.text("Inspection saved to reframework/data/mhr_gyro_bindings.json") end
        if inspection_error then imgui.text("Inspection failed: "..inspection_error) end
        for _,endpoint in ipairs(snapshot.endpoints or {}) do
            imgui.text(endpoint.name.." | gyro: "..tostring(endpoint.gyro).." | connected: "..tostring(endpoint.connected))
        end
        if imgui.button("Close (F10)") then controller.panel_open=false end
        local tabs=snapshot.tabs or {}
        local selected=nil
        for _,tab in ipairs(tabs) do
            if tab.id==selected_view then selected=tab end
        end
        if not selected and #tabs>0 then selected=tabs[1]; selected_view=selected.id end
        for _,tab in ipairs(tabs) do
            local label=(tab.id==selected_view and "[x] " or "")..tab.label
            if imgui.button(label.."##view"..tab.id) then selected_view=tab.id; selected=tab end
            if imgui.is_item_hovered() then imgui.set_tooltip(tab.description) end
            imgui.same_line()
        end
        if #tabs>0 then imgui.new_line() end
        if selected then
            imgui.text(selected.label..(selected.active and " (active in game)" or ""))
            for _,setting in ipairs(selected.settings or {}) do draw_setting(setting) end
        else
            imgui.text("No verified camera view. Inspect the game bindings before enabling gyro.")
        end
        imgui.separator()
        for _,setting in ipairs(snapshot.shared_settings or {}) do
            draw_setting(setting)
        end
    end
    imgui.end_window()
end
local function draw_native_diagnostics()
    if mhr_gyro_native.gui_available~=true then return end
    imgui.text(profile.status or profile.id)
    imgui.text("GyroLib settings: F10 (or your configured shortcut).")
    local ok,gui=pcall(mhr_gyro_native.gui_state)
    if ok and type(gui)=="table" then
        imgui.text("GUI ready: "..tostring(gui.ready).." | capture: "..tostring(gui.capture))
        if gui.error and gui.error~="" then imgui.text(gui.error) end
    end
    if imgui.button("Open GyroLib settings") then mhr_gyro_native.gui_set_open(true) end
    local snapshot=controller.snapshot
    if snapshot.recommended_settings_result and snapshot.recommended_settings_result<0 then
        imgui.text("Recommended preset unavailable: check plugin and GyroLib versions")
    end
    if imgui.button("Inspect known types (read only)") then capture_inspection() end
    if inspection_error then imgui.text("Inspection failed: "..inspection_error) end
    if controller.error then imgui.text("Adapter stopped: "..controller.error) end
end
profile.install(function()
    local ok,err=pcall(function() controller:step() end)
    if not ok then controller.error=tostring(err) end
end)
-- Observe the image encoding, not whether the monitor supports HDR. The
-- renderer can use 10-bit SDR while the Windows desktop remains in HDR.
local color_last=nil
local function report_color_space()
    if type(mhr_gyro_native.gui_report_color_space)~="function" then return end
    local ok,value=pcall(function()
        local kind=sdk.find_type_definition("via.render.Renderer")
        local instance=sdk.get_native_singleton("via.render.Renderer")
        if not kind or not instance then return nil end
        local config=sdk.call_native_func(instance,kind,"get_RenderConfig")
        if not config then return nil end
        return sdk.call_native_func(config,sdk.find_type_definition("via.render.RenderConfig"),"get_ColorSpace")
    end)
    local color=nil
    if ok and value==3 then color=12
    elseif ok and (value==1 or value==2) then color=0 end
    if tostring(color)~=color_last then
        log.info("MHRGyro GUI: observed DXGI color space="..tostring(color).." (RenderConfig="..tostring(value)..")")
        color_last=tostring(color)
    end
    mhr_gyro_native.gui_report_color_space(color)
end
re.on_frame(function()
    report_color_space()
    controller:key(reframework:is_key_down(0x79))
    if profile.verified~=true then
        camera_test:key(reframework:is_key_down(0x77),reader_description)
        probe.camera_test=camera_test:describe()
        if timing then timing:step() end
    end
    local window_ok,window=pcall(function() return mhr_gyro_native.window_state() end)
    reader_description=ReaderProbe.describe(controller.snapshot,window_ok and window or nil)
    reader_description.view=controller.view_observation
    if profile.diagnostic_state then reader_description.camera_input=profile.diagnostic_state() end
    reader_probe:step(reader_description)
    probe.reader_snapshot=reader_description
    probe.gyro_camera_output_enabled=profile.verified==true
    probe:key(reframework:is_key_down(0x78))
    probe:step()
    -- One metadata-only capture after the diagnostic script starts/reloads.
    -- Camera and controller acquisition remain governed by the profile flags.
    if inspection_frames<30 then
        inspection_frames=inspection_frames+1
        if inspection_frames==30 then capture_inspection() end
    end
end)
-- This entry was observed once per frame on the stable engine thread, before
-- the camera's late-update jobs. The test runs only on a deliberate F8 press.
re.on_pre_application_entry("LateUpdateBehavior",function()
    if profile.verified==true then return end
    local overlay={reframework_open=reframework:is_drawing_ui(),mod_panel_open=controller.panel_open}
    camera_test:step(reader_description,overlay.reframework_open or overlay.mod_panel_open,overlay)
end)
re.on_frame(draw)
if mhr_gyro_native.gui_available==true then re.on_draw_ui(draw_native_diagnostics) end
