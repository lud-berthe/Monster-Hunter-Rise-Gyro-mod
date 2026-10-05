#include "bridge.hpp"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <cstdio>
int main() {
    auto* l=luaL_newstate();luaL_openlibs(l);mhr_register(l);
    const int error=luaL_dostring(l,R"(
      local s=mhr_gyro_native.create(false)
      local host={bindings_verified=true,menu_open=false,paused=false,focused=true,camera_allowed=true,contexts={}}
      for i=1,6 do host.contexts[i]={id=i,label='View '..i,description='Benchmark',priority=i,active=i==1,available=true} end
      for _,menu in ipairs({true,false,true,false}) do
        host.include_menu=menu
        for i=1,30 do s:tick(host,{}) end
        collectgarbage('collect')
        local samples={}
        for i=1,500 do
          local start=mhr_gyro_native.now_ns()
          local snapshot=s:tick(host,{})
          samples[i]=(mhr_gyro_native.now_ns()-start)/1e6
          assert(snapshot.update_result==0 and snapshot.active_context==1)
        end
        table.sort(samples);local total=0
        for _,v in ipairs(samples) do total=total+v end
        print(string.format('include_menu=%s mean_ms=%.4f p95_ms=%.4f p99_ms=%.4f max_ms=%.4f',tostring(menu),total/#samples,samples[475],samples[495],samples[500]))
      end
      s:close()
    )");
    if(error)std::fprintf(stderr,"%s\n",lua_tostring(l,-1));lua_close(l);return error?1:0;
}
