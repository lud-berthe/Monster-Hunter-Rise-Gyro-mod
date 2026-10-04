#pragma once
#include <gyrolib/gyrolib.h>
#include <vector>
#include <string>
#include <cmath>

// The author's preset is compiled into the mod; no distributed INI is needed.
// GyroLib still owns capture, restoration, the menu action and persistence.
// Call before loading player settings, once per native context.
inline int mhr_capture_recommended_settings(gl_context* ctx) {
    if(gl_has_recommended_settings(ctx))return GL_OK;
    if(!ctx || *gl_get_settings_path(ctx))return GL_INVALID;
    // Startup can precede verified game bindings. Temporarily register the six
    // stable IDs for the snapshot, without advertising any active game view.
    const char* labels[]={"Free camera","Menu camera","Wirebug aim","Weapon aim","Ballista","Cannon"};
    std::vector<uint32_t> added;
    int result=GL_OK;
    for(uint32_t id=1;id<=6 && result==GL_OK;++id) {
        bool exists=false;
        for(uint32_t n=0;n<gl_gameplay_context_count(ctx);++n) {
            gl_gameplay_context known{};
            if(gl_get_gameplay_context(ctx,n,&known)==GL_OK && known.id==id)exists=true;
        }
        if(exists)continue;
        const gl_gameplay_context view{id,labels[id-1],"",0};
        result=gl_register_gameplay_context(ctx,&view);
        if(result==GL_OK) {
            added.push_back(id);
            result=gl_set_gameplay_context_output_target(ctx,id,GL_OUTPUT_CAMERA);
        }
    }
    // Explicit values keep the authored recommendation stable across SDK updates.
    struct Setting { const char* suffix; double value; };
    static constexpr Setting common[]={
        {"sensitivity_x",2.5},
        {"sensitivity_y",2.5},
        {"gyro.space",0},
        {"gyro.local_angle_degrees",0},
        {"gyro.local_roll_percent",100},
        {"activation.button",0},
        {"activation.touchpad",0},
        {"activation.stick_touch",0},
        {"activation.grip_touch",0},
        {"activation.stick_deflection",0},
        {"activation.stick_threshold",0.2},
        {"activation.block_long_press",0},
        {"gyro.invert_x",0},
        {"gyro.invert_y",0},
        {"gyro.smoothing_ms",0},
        {"gyro.acceleration",0},
        {"flick.mode",0},
        {"flick.duration_ms",150},
        {"gyro.smoothing_threshold_dps",5},
        {"gyro.tightening_dps",0},
        {"gyro.fast_sensitivity_x",2.5},
        {"gyro.fast_sensitivity_y",2.5},
        {"gyro.slow_threshold_dps",5},
        {"gyro.fast_threshold_dps",75},
        {"flick.style",0},
        {"flick.smoothing_ms",30},
        {"flick.snap",0},
        {"flick.snap_strength",1},
        {"flick.forward_deadzone_degrees",0},
        {"flick.duration_exponent",0},
        {"camera.recenter_button",0},
        {"gyro.zoom_compensation",0},
        {"activation.trigger",0},
        {"activation.trigger_threshold",0.2},
        {"flick.smoothing_threshold_dps",45},
        {"flick.stick_release_threshold",0.65},
        {"flick.stick_start_threshold",0.9},
        {"flick.touchpad_release_threshold",0.2},
        {"flick.touchpad_start_threshold",0.35},
        {"gyro.temporary_invert_axes",2},
        {"gyro.trackball_axes",2},
        {"gyro.trackball_decay",1},
        {"activation.temporary_invert",0},
        {"activation.trackball",0},
        {"gyro.invert_roll",0},
    };
    if(result==GL_OK)result=gl_setting_set(ctx,"calibration.automatic",0);
    for(uint32_t id=1;id<=6 && result==GL_OK;++id) {
        const auto prefix="context."+std::to_string(id)+".";
        result=gl_set_context_parent(ctx,id,0);
        for(const auto& setting:common) {
            if(result!=GL_OK)break;
            result=gl_setting_set(ctx,(prefix+setting.suffix).c_str(),setting.value);
        }
        if(result==GL_OK)result=gl_setting_set(ctx,(prefix+"gyro.activation").c_str(),id<=2?GL_GYRO_OFF:GL_ALWAYS);
        // Setters may have dependent effects (e.g. acceleration presets).
        // Refuse to publish a recommendation if those changed an authored value.
        for(const auto& setting:common) {
            if(result!=GL_OK)break;
            double actual{};
            result=gl_setting_get(ctx,(prefix+setting.suffix).c_str(),&actual);
            if(result==GL_OK && std::abs(actual-setting.value)>1e-8)result=GL_INVALID;
        }
    }
    if(result==GL_OK)result=gl_capture_recommended_settings(ctx);
    for(auto id:added)gl_unregister_gameplay_context(ctx,id);
    return result;
}
