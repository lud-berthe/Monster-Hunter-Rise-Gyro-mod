#include "bridge.hpp"
#include "recommended_settings.hpp"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <cstdio>
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #x);}while(0)
std::string read(const fs::path& path) {std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
struct Files {
    fs::path dir=fs::current_path()/("mhr-recommended-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files(){fs::create_directory(dir);}
    ~Files(){std::error_code error;fs::remove_all(dir,error);}
};
using Context=std::unique_ptr<gl_context,decltype(&gl_destroy)>;
Context context(){return Context(gl_create(GL_ABI_VERSION),gl_destroy);}
int main(int argc,char** argv)try {
    CHECK(argc<=2);Files files;
    const auto player=files.dir/GL_SETTINGS_FILENAME;
    auto c=context();CHECK(c);
    CHECK(mhr_capture_recommended_settings(c.get())==GL_OK);
    CHECK(gl_has_recommended_settings(c.get()));
    CHECK(gl_gameplay_context_count(c.get())==0); // No invented live view during startup.
    CHECK(!*gl_get_settings_path(c.get()));
    for(uint32_t id=1;id<=6;++id) {
        const gl_gameplay_context view{id,"Test view","",0};
        CHECK(gl_register_gameplay_context(c.get(),&view)==GL_OK);
    }
    const auto snapshot=files.dir/"snapshot.ini";
    CHECK(gl_save_settings(c.get(),snapshot.string().c_str())==GL_OK);
    CHECK(gl_set_settings_path(c.get(),"")==GL_OK);
    std::map<std::string,double> expected;
    // An optional external reference verifies migration of the author's original
    // preset. Normal CTest runs need no input file and test the full restoration.
    std::istringstream lines(read(argc==2?fs::path(argv[1]):snapshot));std::string line;
    while(std::getline(lines,line)) {
        const auto eq=line.find('=');if(eq==std::string::npos)continue;
        const auto key=line.substr(0,eq);
        if(key.rfind("context.",0)==0 || key=="calibration.automatic")expected[key]=std::stod(line.substr(eq+1));
    }
    CHECK(expected.size()>=277);
    const auto matches=[&]{for(const auto& [key,wanted]:expected){
        double actual{};CHECK(gl_setting_get(c.get(),key.c_str(),&actual)==GL_OK);
        CHECK(std::abs(actual-wanted)<1e-8);
    }};
    matches();
    CHECK(expected.at("context.1.gyro.activation")==double(GL_GYRO_OFF));
    CHECK(expected.at("context.2.gyro.activation")==double(GL_GYRO_OFF));
    for(int id=3;id<=6;++id)CHECK(expected.at("context."+std::to_string(id)+".gyro.activation")==double(GL_ALWAYS));
    const std::string custom="schema=0.2.0\nui.language=fr\nui.menu_key=F7\ncalibration.automatic=2\ncontext.1.sensitivity_x=7\ncontext.1.gyro.activation=0\ncontext.4.sensitivity_y=8\n";
    {std::ofstream f(player,std::ios::binary);f<<custom;}
    CHECK(gl_initialize_settings(c.get(),files.dir.string().c_str(),nullptr)==GL_OK);
    CHECK(read(player)==custom);double value{};
    CHECK(gl_setting_get(c.get(),"context.1.sensitivity_x",&value)==GL_OK && value==7);
    CHECK(gl_setting_get(c.get(),"context.4.sensitivity_y",&value)==GL_OK && value==8);
    CHECK(mhr_capture_recommended_settings(c.get())==GL_OK); // Must not recapture.
    gl_reset_settings(c.get());
    CHECK(gl_action(c.get(),"settings.recommended")==GL_OK);matches();
    CHECK(std::string(gl_get_language(c.get()))=="fr" && gl_get_menu_key(c.get())==7);
    auto restored=context();const gl_gameplay_context restored_view{1,"Restored","",0};
    CHECK(gl_register_gameplay_context(restored.get(),&restored_view)==GL_OK);
    CHECK(gl_load_settings(restored.get(),player.string().c_str())==GL_OK);
    CHECK(gl_setting_get(restored.get(),"context.1.gyro.activation",&value)==GL_OK && value==double(GL_GYRO_OFF));

    // Full bridge startup before bindings, delayed registration, and repeated
    // initialization exercise the actual session path used by the window VM.
    {std::ofstream f(player,std::ios::binary);f<<custom;}
    auto* lua=luaL_newstate();luaL_openlibs(lua);mhr_register(lua);
    lua_pushstring(lua,files.dir.string().c_str());lua_setglobal(lua,"folder");
    const auto status=luaL_dostring(lua,R"(
        local function row(r,id) for _,v in ipairs(r.settings) do if v.id==id then return v end end end
        local s=mhr_gyro_native.create(false,folder)
        local r=s:tick({},{{op='initialize'}})
        assert(r.last_command_result==0 and r.recommended_settings_result==0 and #r.tabs==0)
        assert(row(r,'settings.recommended').visible)
        local host={bindings_verified=true,contexts={}}
        for id=1,6 do host.contexts[id]={id=id,label='Test',description='',priority=id} end
        r=s:tick(host,{})
        assert(row(r,'context.1.sensitivity_x').preference_value==7)
        assert(row(r,'context.4.sensitivity_y').preference_value==8)
        r=s:tick(host,{{op='set',id='context.1.sensitivity_x',value=9},{op='initialize'}})
        assert(row(r,'context.1.sensitivity_x').preference_value==9)
        r=s:tick({},{{op='reset'},{op='action',id='settings.recommended'}})
        assert(r.last_command_result==0 and #r.tabs==0)
        r=s:tick(host,{})
        for id=1,6 do
            assert(row(r,'context.'..id..'.sensitivity_x').preference_value==2.5)
            assert(row(r,'context.'..id..'.gyro.activation').preference_value==(id<=2 and 6 or 0))
        end
        s:close()
    )");
    const std::string error=status?lua_tostring(lua,-1):"";lua_close(lua);
    if(status)throw std::runtime_error(error);

    // First installation has no author/player INI. The bridge creates its data
    // directory, and GyroLib creates and owns the only persisted settings file.
    const auto fresh=files.dir/"fresh"/"data";
    CHECK(!fs::exists(fresh));
    lua=luaL_newstate();luaL_openlibs(lua);mhr_register(lua);
    lua_pushstring(lua,fresh.string().c_str());lua_setglobal(lua,"folder");
    const auto fresh_status=luaL_dostring(lua,R"(
        local s=mhr_gyro_native.create(false,folder)
        local r=s:tick({},{{op='initialize'}})
        assert(r.last_command_result==0 and r.recommended_settings_result==0)
        s:close()
    )");
    const std::string fresh_error=fresh_status?lua_tostring(lua,-1):"";lua_close(lua);
    if(fresh_status)throw std::runtime_error(fresh_error);
    CHECK(read(fresh/GL_SETTINGS_FILENAME).find("context.2.gyro.activation=6")!=std::string::npos);
    CHECK(std::distance(fs::directory_iterator(fresh),fs::directory_iterator{})==1);
    CHECK(mhr_capture_recommended_settings(nullptr)==GL_INVALID);
    auto late=context();CHECK(gl_load_settings(late.get(),player.string().c_str())==GL_OK);
    CHECK(mhr_capture_recommended_settings(late.get())==GL_INVALID);
    CHECK(!gl_has_recommended_settings(late.get()));
    std::puts("Recommended preset: complete snapshot, native action, persistence, startup and reset passed");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Recommended preset: %s\n",e.what());return 1;}
