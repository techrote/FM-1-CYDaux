#include "fm1/performance.hpp"
#include <algorithm>
#include <limits>

namespace fm1 {
namespace {
bool validIndex(int index) { return index >= 0 && index < static_cast<int>(kPerformanceSteps); }
bool due(uint32_t now, uint32_t deadline) {
    // Unsigned arithmetic remains defined across the millis() wrap. A caller
    // must tick at least once per 2^31 ms (24 days), as on the main UI loop.
    return now - deadline < 0x80000000u;
}
uint8_t clampedByte(int value, int low, int high) {
    return static_cast<uint8_t>(std::max(low, std::min(high, value)));
}
}

PerformanceSequencer::PerformanceSequencer() {
    static constexpr uint8_t notes[kPerformanceSteps] =
        {48, 55, 60, 58, 48, 63, 55, 58, 51, 58, 63, 60, 46, 53, 58, 65};
    for (std::size_t i = 0; i < kPerformanceSteps; ++i) {
        view_.steps[i].note = notes[i];
        view_.steps[i].velocity = static_cast<uint8_t>(i % 4 == 0 ? 110 : 82);
        view_.steps[i].enabled = i % 2 == 0 || i == 3 || i == 11;
    }
}
void PerformanceSequencer::setTempo(int bpm) {
    const auto next=static_cast<uint16_t>(std::max(30, std::min(300, bpm)));
    timeRemainder_=static_cast<uint16_t>(uint32_t(timeRemainder_)*next/view_.bpm);
    view_.bpm=next;
}
void PerformanceSequencer::select(int index) {
    if (validIndex(index)) view_.selected = static_cast<uint8_t>(index);
}
void PerformanceSequencer::toggle(int index) {
    if (validIndex(index)) view_.steps[static_cast<std::size_t>(index)].enabled =
        !view_.steps[static_cast<std::size_t>(index)].enabled;
}
void PerformanceSequencer::setNote(int index, int note) {
    if (validIndex(index)) view_.steps[static_cast<std::size_t>(index)].note = clampedByte(note, 0, 127);
}
void PerformanceSequencer::setVelocity(int index, int velocity) {
    if (validIndex(index)) view_.steps[static_cast<std::size_t>(index)].velocity = clampedByte(velocity, 0, 127);
}
void PerformanceSequencer::setGate(int index, int percent) {
    if (validIndex(index)) view_.steps[static_cast<std::size_t>(index)].gate = clampedByte(percent, 1, 100);
}
void PerformanceSequencer::setLength(int length) {
    view_.length = clampedByte(length, 1, static_cast<int>(kPerformanceSteps));
}
void PerformanceSequencer::start(uint32_t now, uint8_t channel) {
    if (channel < 1 || channel > 16) return;
    channel_ = channel;
    view_.running = true;
    view_.playhead = 255;
    nextStepAt_ = now;
    timeRemainder_ = 0;
    // Restarting must release the original pitch/channel before a new attack.
    releaseRequired_ = noteActive_;
}
bool PerformanceSequencer::release(Output output, void* context) {
    if (!noteActive_) { releaseRequired_ = false; return true; }
    if (!output || !output(context, static_cast<uint8_t>(0x80u | (heldChannel_ - 1)), heldNote_, 0))
        return false;
    noteActive_ = false;
    releaseRequired_ = false;
    return true;
}
void PerformanceSequencer::stop(Output output, void* context) {
    view_.running = false;
    view_.playhead = 255;
    releaseRequired_ = noteActive_;
    release(output, context);
}
void PerformanceSequencer::abandonAfterPanic() {
    view_.running = false;
    view_.playhead = 255;
    noteActive_ = releaseRequired_ = false;
}
void PerformanceSequencer::tick(uint32_t now, Output output, void* context) {
    if (noteActive_ && (releaseRequired_ || !view_.running || due(now, noteOffAt_))) {
        if (!release(output, context)) return;
    }
    if (!view_.running || !due(now, nextStepAt_)) return;

    // Keep fractional milliseconds so tempos such as 137 BPM do not drift
    // upwards through per-step integer rounding. At most one note is emitted
    // even when an arbitrary number of elapsed periods are skipped.
    const uint32_t elapsed=now-nextStepAt_;
    const uint32_t skipped=static_cast<uint32_t>((
        (uint64_t(elapsed)+1u)*view_.bpm-timeRemainder_-1u)/15000u);
    const uint64_t previousNumerator=uint64_t(timeRemainder_)+uint64_t(skipped)*15000u;
    const uint32_t stepStart=nextStepAt_+static_cast<uint32_t>(previousNumerator/view_.bpm);
    const uint64_t nextNumerator=previousNumerator+15000u;
    nextStepAt_+=static_cast<uint32_t>(nextNumerator/view_.bpm);
    timeRemainder_=static_cast<uint16_t>(nextNumerator%view_.bpm);
    const uint32_t duration=nextStepAt_-stepStart;
    const uint32_t next = view_.playhead == 255 ? 0 : static_cast<uint32_t>(view_.playhead) + 1;
    view_.playhead = static_cast<uint8_t>((next + skipped) % view_.length);
    view_.skipped += std::min(skipped, std::numeric_limits<uint32_t>::max() - view_.skipped);

    // Never replay a backlog of notes after the UI/transport stalled. At most
    // the current step is admitted; a late step's gate ends at its scheduled
    // boundary, not a fresh full gate extending into the following step.
    const auto& step = view_.steps[view_.playhead];
    const uint32_t gate = std::max(1u, duration * step.gate / 100u);
    const uint32_t gateEnd = nextStepAt_ - duration + gate;
    if (!step.enabled || step.velocity == 0 || due(now, gateEnd) || !output) return;
    if (noteActive_ && !release(output, context)) return;
    if (!output(context, static_cast<uint8_t>(0x90u | (channel_ - 1)), step.note, step.velocity)) return;
    heldChannel_ = channel_;
    heldNote_ = step.note;
    noteOffAt_ = gateEnd;
    noteActive_ = true;
}
} // namespace fm1
