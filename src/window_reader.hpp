#pragma once
#include <memory>
struct lua_State;
struct MhrWindowReader;
void mhr_enable_window_reader();
void mhr_set_game_window(void*);
bool mhr_window_reader_available();
bool mhr_on_window_message(void*,unsigned int,unsigned long long,long long);
std::shared_ptr<MhrWindowReader> mhr_request_window_reader(const char* settings_directory,bool hardware=true);
int mhr_window_reader_tick(lua_State*,const std::shared_ptr<MhrWindowReader>&);
void mhr_close_window_reader(const std::shared_ptr<MhrWindowReader>&);
// Lua GC may transfer the owner-thread backend to the next script lifetime.
// Detached readers have no camera permission and expire after a short grace.
void mhr_detach_window_reader(const std::shared_ptr<MhrWindowReader>&);
int mhr_window_state(lua_State*);
// Only the verified window-thread dispatcher may create a direct SDL session.
bool mhr_inside_window_reader();
#ifdef MHR_WINDOW_READER_TEST
void mhr_window_reader_test_hooks(void (*before_lock)(),void (*after_process)());
#endif
