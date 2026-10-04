-- REFramework restores its package path before running frame/UI callbacks.
local names={"mhr_gyro_native","re","reframework","sdk","json","log","imgui"}
local saved={}
for _,name in ipairs(names) do saved[name]=_G[name] end
local old_path,old_cpath=package.path,package.cpath
local old_profile=package.loaded["mhr_gyro/profile"]
local callbacks,captures,errors,entries={},{},{},{}
local key_down,inspect_clicked=false,false
local camera_key=false
local state_reads=0
mhr_gyro_native={camera_integration_version=3,create=function(hardware)
    assert(not hardware)
    return {tick=function()
        return {update_result=0,active_context=0,settings={},shared_settings={},tabs={}}
    end}
end}
re={on_frame=function(fn) callbacks[#callbacks+1]=fn end,
    on_pre_application_entry=function(name,fn) entries[name]=fn end,on_application_entry=function() end}
reframework={is_key_down=function(_,key) return (key==0x79 and key_down) or (key==0x77 and camera_key) end,
    is_drawing_ui=function() return true end}
sdk={find_type_definition=function() return nil end,
    get_managed_singleton=function() state_reads=state_reads+1;return nil end}
json={dump_file=function(path,report)
    captures[#captures+1]={path=path,report=report}
    return true
end}
log={info=function() end,error=function(message) errors[#errors+1]=message end}
imgui=setmetatable({
    begin_window=function() return true end,
    button=function(label)
        return inspect_clicked and label=="Inspect known types (read only)"
    end,
}, {__index=function() return function() return false end end})
-- Exercise diagnostic F8 independently of the now-playable default profile.
package.loaded["mhr_gyro/profile"]={verified=false,contexts={},
    install=function(tick) re.on_frame(tick) end}

package.loaded["mhr_gyro/inspect"]=nil
dofile(frontend_entry or root.."/reframework/autorun/mhr_gyro.lua")
-- Remove autorun from module search, as the real script runner does.
package.path,package.cpath="",""
for _=1,35 do
    for _,callback in ipairs(callbacks) do callback() end
end
assert(#captures==1 and #errors==0,"Automatic capture failed after restoring the package path")
assert(captures[1].path=="mhr_gyro_bindings.json" and #captures[1].report.types>=3)
assert(captures[1].report.types[1].name=="snow.CameraManager")
key_down,inspect_clicked=true,true
for _,callback in ipairs(callbacks) do callback() end
assert(#captures==2 and #errors==0,"Manual capture failed after restoring the package path")
key_down,inspect_clicked,camera_key=false,false,true
for _,callback in ipairs(callbacks) do callback() end
assert(#captures==3 and captures[3].path=="mhr_gyro_camera_test.json")
assert(captures[3].report.stage=="requested" and state_reads==0,
    "Render-side F8 capture must save a request without reading/changing the camera")
assert(entries.LateUpdateBehavior)
entries.LateUpdateBehavior()
assert(#captures==4 and captures[4].report.stage=="blocked" and state_reads==3)
assert(captures[4].report.overlay.reframework_open and not captures[4].report.action_attempted)
assert(#errors==0,"Blocked F8 must report safely with no package search path")
-- The SDK GUI owns its shortcut. The Lua frontend exposes only diagnostics
-- and an explicit open button in REFramework's generated UI.
callbacks,entries,captures,errors={},{},{},{}
local native_ui,open_calls=nil,0
local gui_clicked=true
mhr_gyro_native.gui_available=true
mhr_gyro_native.gui_state=function() return {ready=true,capture=0,error=""} end
mhr_gyro_native.gui_set_open=function(open)
    assert(open==true);open_calls=open_calls+1;return 0
end
re.on_draw_ui=function(fn) assert(not native_ui);native_ui=fn end
imgui.begin_window=function() error("The legacy setting panel must not render with the SDK GUI") end
imgui.button=function(label)
    return (gui_clicked and label=="Open GyroLib settings") or
        (inspect_clicked and label=="Inspect known types (read only)")
end
package.path,package.cpath=old_path,old_cpath
key_down,camera_key,inspect_clicked=true,false,false
dofile(frontend_entry or root.."/reframework/autorun/mhr_gyro.lua")
package.path,package.cpath="",""
for _,callback in ipairs(callbacks) do callback() end
assert(native_ui and open_calls==0,"Lua F10 must not toggle the SDK GUI")
native_ui()
assert(open_calls==1,"The REFramework button must open the native GUI")
gui_clicked,inspect_clicked=false,true
native_ui()
assert(#captures==1 and captures[1].path=="mhr_gyro_bindings.json" and #errors==0,
    "Native-GUI diagnostics must survive package-path restoration")
package.path,package.cpath=old_path,old_cpath
package.loaded["mhr_gyro/profile"]=old_profile
for _,name in ipairs(names) do _G[name]=saved[name] end
print("MHR frontend: inspection, camera callbacks and native-GUI diagnostics survive package-path restoration")
