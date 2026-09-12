#pragma once
/* Host stand-in for platform/tab5/tab5_platform.h so the port's overlay
 * sources compile unchanged off-device. */
#include <cstdint>
#include <cstddef>
#include <chrono>
#define TAB5_TAG "jinyong"
#define TAB5_GAME_WIDTH 640
#define TAB5_GAME_HEIGHT 480
#define TAB5_SD_GAME_ROOT "."
typedef enum { TAB5_PROF_RENDER = 0, TAB5_PROF_BLIT, TAB5_PROF_VSYNC, TAB5_PROF_COUNT } tab5_prof_slot_t;
inline std::uint64_t tab5_clock_us(void) {
    using namespace std::chrono;
    return (std::uint64_t)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}
void tab5_log_memory(const char *stage);
/* No PSRAM off-device; report a large, obviously-synthetic figure so the
 * out-of-memory message path still compiles and can be exercised. */
inline void tab5_mem_psram(std::size_t *free_bytes, std::size_t *largest_block) {
    if (free_bytes != nullptr) { *free_bytes = 0; }
    if (largest_block != nullptr) { *largest_block = 0; }
}
inline void tab5_delay_ms(std::uint32_t) {}
inline void tab5_prof_set_enabled(bool) {}
inline void tab5_prof_add(tab5_prof_slot_t, std::uint64_t) {}
inline void tab5_prof_end_frame(void) {}
