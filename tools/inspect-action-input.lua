-- Read-only input metadata. Does not install hooks or change game settings.
local inspect=dofile("reframework/autorun/mhr_gyro/inspect.lua")
local report={schema=1,types={}}
local names={"snow.Pad","snow.Pad.Device","snow.Pad.Button","via.hid.GamePadButton",
    "snow.StmInputManager","snow.StmPlayerInput","snow.player.PlayerInput",
    "snow.player.PlayerInput.CommandButton2","snow.StmInputManager.PL_INPUT",
    "snow.StmPlInputData","snow.StmInputManager.InGameInputDevice"}
local seen={}
local i=1
while i<=#names and i<=70 do
    local name=names[i];i=i+1
    if not seen[name] then
        seen[name]=true
        local ok,value=pcall(inspect.inspect_type,name)
        report.types[#report.types+1]=ok and value or {name=name,error=tostring(value)}
        if ok and value.fields then
            for _,field in ipairs(value.fields) do
                if field.type:find("Input") or field.type:find("BitSetFlag") then
                    if not seen[field.type] then names[#names+1]=field.type end
                end
            end
        end
    end
end
json.dump_file("mhr_gyro_action_input.json",report)
