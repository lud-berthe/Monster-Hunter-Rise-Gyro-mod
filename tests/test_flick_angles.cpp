#include <gyrolib/gyrolib.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)
static void run(double degrees, unsigned hz, double sensitivity, unsigned mode=GL_FLICK_ON) {
    auto* c=gl_create(GL_ABI_VERSION);CHECK(c);
    const gl_gameplay_context view{1,"Angle contract","Synthetic camera",0};
    CHECK(gl_register_gameplay_context(c,&view)==GL_OK);
    gl_endpoint pad{};pad.id=pad.physical_id=1;pad.connected=1;
    pad.source=GL_SOURCE_SDL;pad.caps.sticks=pad.caps.touchpads=GL_RIGHT;
    CHECK(gl_register_endpoint(c,&pad)==GL_OK);
    CHECK(gl_set_host_capabilities(c,GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION)==GL_OK);
    CHECK(gl_setting_set(c,"context.1.flick.mode",mode)==GL_OK);
    CHECK(gl_setting_set(c,"context.1.flick.duration_ms",150)==GL_OK);
    CHECK(gl_setting_set(c,"context.1.sensitivity_x",sensitivity)==GL_OK);
    gl_host_state host{};host.focused=host.camera_allowed=1;
    uint64_t now=1000000000,step=1000000000/hz;
    const double radians=degrees*3.14159265358979323846/180;
    double total=0,held=0;
    for(unsigned frame=0;frame<2*hz;++frame){
        now+=step;
        gl_flick_input input{};input.timestamp_ns=now;input.available=GL_FLICK_INPUT_STICK|GL_FLICK_INPUT_TOUCHPAD;
        if(frame>=5){
            input.stick_x=input.touchpad_x=static_cast<float>(std::sin(radians));
            input.stick_y=input.touchpad_y=static_cast<float>(std::cos(radians));
            input.touching=1;
        }
        CHECK(gl_submit_flick_input(c,1,&input)==GL_OK);
        CHECK(gl_set_gameplay_context_state(c,1,1,1)==GL_OK);
        gl_output output{};CHECK(gl_update(c,now,&host,&output)==GL_OK);
        CHECK(output.suppress_native_right_stick==(mode!=GL_FLICK_TOUCHPAD));
        CHECK(gl_suppress_native_right_touchpad(c)==(mode!=GL_FLICK_ON));
        total+=output.yaw_degrees;
        if(frame>hz)held+=std::abs(output.yaw_degrees);
        CHECK(output.pitch_degrees==0);
    }
    std::printf("SDK flick: mode=%u input=%g deg rate=%u Hz sensitivity=%g output=%.8f deg held=%.8f deg\n",mode,degrees,hz,sensitivity,total,held);
    CHECK(std::abs(total-degrees*(mode==GL_FLICK_BOTH?2:1))<.0002);CHECK(held<.0001);
    gl_destroy(c);
}
int main(){try{
    for(double degrees:{-135.,-90.,-45.,45.,90.,135.,180.})
        for(unsigned hz:{30u,60u,144u,250u})for(double sensitivity:{0.,2.5,20.})run(degrees,hz,sensitivity);
    for(auto mode:{GL_FLICK_TOUCHPAD,GL_FLICK_BOTH})
        for(double degrees:{-90.,90.,180.})for(unsigned hz:{30u,60u,144u,250u})run(degrees,hz,2.5,mode);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
