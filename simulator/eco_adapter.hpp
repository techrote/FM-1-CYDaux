#pragma once
extern "C" {
#include "ecophon.h"
}
#include <array>
#include <cstddef>
#include <cstdint>

namespace fm1 {
// Native-only snapshot. Slot indices are storage slots, not individual IDs.
// No CY1/MIDI encoding, remote state, or physical-device claim.
struct EcoAgentView {
    uint8_t alive, patch, species, trait;
    uint16_t energy, envelope;
};
struct EcoView {
    uint32_t ticks, births, deaths, gifts, moves, consumed;
    uint8_t fertility, sharing, pressure, regime, preset;
    bool frozen, running;
    std::array<uint16_t,ECO_PATCHES> resource;
    std::array<EcoAgentView,ECO_AGENTS> agents;
};
// The simulator serializes all calls. No allocation, blocking, or DSP on CYD.
class EcoAdapter {
public:
    static constexpr std::size_t kMaximumFrames=4096;
    EcoAdapter(){ reset(1,0); }
    bool reset(uint32_t seed,unsigned preset);
    bool controls(unsigned fertility,unsigned sharing,unsigned pressure,unsigned frozen);
    // XY only changes fertility and pressure; retain sharing and freeze.
    bool xy(unsigned fertility,unsigned pressure);
    void start();
    void stop();
    void disconnect(){stop();}
    bool render(int16_t* pcm,std::size_t frames);
    EcoView snapshot() const;
private:
    Eco state_{};
    uint8_t preset_=0;
    bool running_=false;
};
}
