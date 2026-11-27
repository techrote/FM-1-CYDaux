#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace fm1 {

constexpr std::size_t kPerformanceSteps = 16;
struct PerformanceStep {
    uint8_t note = 60;
    uint8_t velocity = 100;
    uint8_t gate = 65; // Percent of one sixteenth note, 1..100.
    bool enabled = false;
};
struct SequenceView {
    std::array<PerformanceStep, kPerformanceSteps> steps{};
    uint16_t bpm = 120;
    uint8_t length = 16, selected = 0, playhead = 255;
    bool running = false;
    uint32_t skipped = 0;
};

// A local, monophonic MIDI performance aid, never a device pattern editor.
// The transport retains its own note-release/session guarantees. The callback
// accepts an event into that transport; it must return false under backpressure.
// No heap allocation, audio rendering or blocking work occurs in this class.
class PerformanceSequencer {
public:
    using Output = bool (*)(void*, uint8_t status, uint8_t data1, uint8_t data2);
    PerformanceSequencer();
    const SequenceView& view() const { return view_; }
    void setTempo(int bpm);
    void select(int index);
    void toggle(int index);
    void setNote(int index, int note);
    void setVelocity(int index, int velocity);
    void setGate(int index, int percent);
    void setLength(int length);
    void start(uint32_t now, uint8_t channel);
    void stop(Output output, void* context);
    void tick(uint32_t now, Output output, void* context);
    // Only after the owner has independently panicked or retired the complete
    // transport session. Ordinary stop() preserves a refused release for retry.
    void abandonAfterPanic();
    bool releasePending() const { return noteActive_; }
private:
    SequenceView view_{};
    uint32_t nextStepAt_ = 0, noteOffAt_ = 0;
    uint16_t timeRemainder_ = 0; // Fractional milliseconds, denominator view_.bpm.
    uint8_t channel_ = 1, heldChannel_ = 1, heldNote_ = 60;
    bool noteActive_ = false, releaseRequired_ = false;
    bool release(Output output, void* context);
};
} // namespace fm1
