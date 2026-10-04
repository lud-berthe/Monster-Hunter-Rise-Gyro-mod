-- False, unavailable and unreadable menu observations must remain distinct.
local Probe=dofile(root.."/reframework/autorun/mhr_gyro/probe.lua")
local probe=Probe.new()
local queries={}
local object={call=function(_,method)
    queries[#queries+1]=method
    if method=="IsStartMenuAndSubmenuOpen" then return false end
    if method=="IsPlayerAllInputDisable" then return true end
    if method=="isOpenPauseWindow" then error("missing method") end
    return nil
end}
local errors={}
local result=probe:menu_queries(object,errors,"gui")
assert(result.present and result.IsStartMenuAndSubmenuOpen==false)
assert(result.IsPlayerAllInputDisable==true and result.isOpenPauseWindow==nil)
assert(errors["gui.isOpenPauseWindow"]:find("missing method",1,true))
assert(errors["gui.isOpenOptionWindow"]:find("Expected a boolean",1,true))
for _,name in ipairs(queries) do
    assert(not name:match("^set") and not name:match("^open") and not name:match("^close"))
end
local count=#queries
result=probe:menu_queries(nil,{},"gui")
assert(result.present==false and #queries==count)
print("MHR probe: closed menus, missing objects and query failures remain distinct")
