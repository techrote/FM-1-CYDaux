#pragma once
extern "C" {
#include "chrono.h"
}
#include <array>
#include <cstdint>
#include <cstddef>

namespace fm1 {
// Host-only bounded semantic adapter. No CY1 or MIDI mapping yet.
// All calls must be serialized by the simulator owner, never by an audio ISR.
struct ChronoLaneView {
    uint8_t length, step, cycle;
    uint16_t mask;
    uint32_t clock, ticks, hits;
};
struct ChronoView {
    uint16_t bpm;
    int8_t warp;
    uint8_t variation, preset;
    bool playing;
    std::array<ChronoLaneView, CM_LANES> lane;
};
struct ChronoPattern {
    unsigned length, mask;
};
class ChronoAdapter {
public:
    static constexpr unsigned kMaximumFrames=4096;
    ChronoAdapter() { reset(1,0); }
    bool reset(uint32_t seed, unsigned preset);
    bool controls(unsigned bpm,int warp,unsigned variation);
    bool edit(unsigned lane,unsigned length,unsigned mask);
    // Apply all three patterns and controls atomically without rewinding clocks.
    bool scene(unsigned bpm,int warp,unsigned variation,
               const std::array<ChronoPattern,CM_LANES>& patterns);
    void start();
    void stop();
    void disconnect() { stop(); }
    bool render(int16_t* pcm,std::size_t frames);
    ChronoView snapshot() const;
private:
    cm_state state_{};
};
}
