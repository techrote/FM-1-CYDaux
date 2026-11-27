#pragma once
#include "fm1/companion.hpp"
#include "zg/engine.h"
extern "C" {
#include "xeno.h"
}
#include <array>
#include <cstddef>
#include <cstdint>

namespace fm1 {
// Host-only adapter around the actual, source-pinned portable instrument cores.
// It is deliberately absent from the ESP32 firmware. No FM-1 CPU is emulated.
class VirtualInstrument {
public:
    static constexpr std::size_t kMaximumFrames = 4096;
    static constexpr std::size_t kReplyCapacity = 8;
    bool select(const char* profileId);
    void disconnect();
    void panic();
    bool receive(const MidiMessage&);
    bool popReply(MidiMessage&);
    bool render(int16_t* destination, std::size_t frames);
    CompanionSnapshot snapshot() const;
    bool available() const { return profile_ != nullptr; }
    bool ready() const { return session_ != 0; }
    uint32_t sampleRate() const;
    uint32_t generation() const { return generation_; }
    uint64_t renderedFrames() const { return renderedFrames_; }
    uint64_t rejected() const { return rejected_; }
    uint32_t queueOverflows() const { return queueOverflows_; }
    uint16_t peak() const { return peak_; }
    double rms() const { return rms_; }
    const char* name() const;
    const char* mode() const;
    const char* sourceRevision() const;
private:
    const CompanionProfile* profile_ = nullptr;
    zg_engine zaag_{};
    Xeno xeno_{};
    std::array<MidiMessage, kReplyCapacity> replies_{};
    std::array<uint8_t, kCompanionMeters> meters_{};
    std::array<int32_t, ZG_BLOCK * ZG_STEM_COUNT> stems_{};
    std::size_t replyHead_ = 0, replySize_ = 0;
    uint32_t session_ = 0, generation_ = 0, queueOverflows_ = 0;
    uint16_t lastSequence_ = 0, revision_ = 0, peak_ = 0, statePeak_ = 0;
    uint64_t renderedFrames_ = 0, rejected_ = 0;
    uint8_t preset_ = 0, note_ = 60;
    bool gate_ = false, hasSequence_ = false;
    double rms_ = 0;
    bool putReply(const MidiMessage&);
    void error(const CompanionPacket&, CompanionError);
    void stateReply(const CompanionPacket&);
    bool setParameter(unsigned index, unsigned normalized);
    bool setPreset(unsigned index);
    void note(unsigned note, unsigned velocity);
    bool legacyMidi(const MidiMessage&);
};
} // namespace fm1
