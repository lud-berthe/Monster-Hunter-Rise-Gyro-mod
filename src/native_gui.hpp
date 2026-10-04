#pragma once
#include <memory>
#include <cstdint>
struct gl_context;
struct gl_host_state;
struct gl_output;
struct lua_State;
struct MhrGui;
struct MhrGuiRenderer {int type=0;void* device=nullptr;void* swapchain=nullptr;void* queue=nullptr;};
using MhrGuiLog=void(*)(const char*,...);
void mhr_gui_enable(MhrGuiLog=nullptr);
bool mhr_gui_available();
std::shared_ptr<MhrGui> mhr_gui_attach(gl_context*);
int mhr_gui_process(const std::shared_ptr<MhrGui>&);
int mhr_gui_update(const std::shared_ptr<MhrGui>&,gl_context*,uint64_t,const gl_host_state*,gl_output*);
bool mhr_gui_detach(const std::shared_ptr<MhrGui>&);
void mhr_gui_present(const MhrGuiRenderer&);
bool mhr_gui_reset();
bool mhr_gui_message(void*,unsigned int,uint64_t,int64_t);
uint32_t mhr_gui_capture();
void mhr_gui_close();
int mhr_gui_state(lua_State*);
int mhr_gui_set_open(lua_State*);
