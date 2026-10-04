#include "steam_session.hpp"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {int user=1,count=0,inits=0;bool succeed=true;bool explicit_frames=true;
int get_user(){return user;} void* input(){return &user;}
int handles(void*,uint64_t* h){h[0]=42;return count;}
bool init(void*,bool explicit_run){++inits;explicit_frames=explicit_run;return succeed;}
void* resolve(const char* name){
    if(std::strcmp(name,"SteamAPI_GetHSteamUser")==0)return reinterpret_cast<void*>(&get_user);
    if(std::strcmp(name,"SteamAPI_SteamInput_v005")==0)return reinterpret_cast<void*>(&input);
    if(std::strcmp(name,"SteamAPI_ISteamInput_GetConnectedControllers")==0)return reinterpret_cast<void*>(&handles);
    if(std::strcmp(name,"SteamAPI_ISteamInput_Init")==0)return reinterpret_cast<void*>(&init);
    CHECK(false);return nullptr;
}}
int main(){
    MhrSteamSession s;user=0;s.prepare(1,resolve);CHECK(!s.finished && inits==0);
    user=1;s.prepare(2,resolve);CHECK(inits==0);
    s.prepare(1000000001,resolve);CHECK(s.finished && inits==1 && !explicit_frames);
    s.prepare(2000000001,resolve);CHECK(inits==1);
    MhrSteamSession failed;succeed=false;failed.prepare(1,resolve);CHECK(failed.finished && inits==2);
    failed.prepare(3000000001,resolve);CHECK(inits==2);
    MhrSteamSession borrowed;count=1;borrowed.prepare(1,resolve);CHECK(borrowed.finished && inits==2);
    MhrSteamSession invalid;count=17;invalid.prepare(1,resolve);CHECK(!invalid.finished && inits==2);
    MhrSteamSession missing;missing.prepare(1,[](const char*)->void*{return nullptr;});CHECK(!missing.finished && inits==2);
    std::puts("MHR Steam session: one explicit initialization and existing service passed");
}
