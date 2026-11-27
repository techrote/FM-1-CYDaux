#include "fm1/performance.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using Event = std::array<uint8_t, 3>;
struct Wire {
    std::vector<Event> events;
    bool accept = true;
    static bool send(void* context, uint8_t status, uint8_t a, uint8_t b) {
        auto& self = *static_cast<Wire*>(context);
        if (!self.accept) return false;
        self.events.push_back({status, a, b});
        return true;
    }
};
void allEnabled(fm1::PerformanceSequencer& seq) {
    for (int i = 0; i < 16; ++i) {
        if (!seq.view().steps[i].enabled) seq.toggle(i);
        seq.setNote(i, 48 + i);
        seq.setVelocity(i, 100);
        seq.setGate(i, 65);
    }
}
}
int main() {
    fm1::PerformanceSequencer seq;
    Wire wire;
    allEnabled(seq);
    seq.start(0, 3);
    seq.tick(0, Wire::send, &wire);
    assert(wire.events.size() == 1 && wire.events[0] == (Event{0x92, 48, 100}));
    seq.tick(80, Wire::send, &wire);
    assert(wire.events.size() == 1);
    seq.tick(81, Wire::send, &wire);
    assert(wire.events.size() == 2 && wire.events.back() == (Event{0x82, 48, 0}));
    seq.tick(125, Wire::send, &wire);
    assert(wire.events.size() == 3 && wire.events.back() == (Event{0x92, 49, 100}));
    seq.setNote(1, 70); // Editing a ringing step must not change its release pitch.
    wire.accept = false;
    seq.stop(Wire::send, &wire);
    assert(seq.releasePending() && !seq.view().running);
    seq.tick(500, Wire::send, &wire);
    assert(seq.releasePending() && wire.events.size() == 3);
    wire.accept = true;
    seq.tick(501, Wire::send, &wire);
    assert(!seq.releasePending() && wire.events.back() == (Event{0x82, 49, 0}));

    // A held release prevents restart attacks until the transport recovers.
    seq.start(1000, 1); seq.tick(1000, Wire::send, &wire);
    wire.accept = false;
    seq.start(1001, 16); seq.tick(1001, Wire::send, &wire);
    assert(wire.events.back()[0] == 0x90);
    wire.accept = true; seq.tick(1002, Wire::send, &wire);
    assert(wire.events[wire.events.size()-2] == (Event{0x80, 48, 0}));
    assert(wire.events.back() == (Event{0x9f, 48, 100}));

    // Long stalls emit one release and at most one new attack, not a MIDI burst.
    const auto count = wire.events.size();
    seq.tick(2501, Wire::send, &wire);
    assert(wire.events.size() <= count + 2 && seq.view().skipped == 11);
    assert(seq.view().playhead == 12);
    seq.stop(Wire::send, &wire);

    // Wrap-safe clock scheduling and gate order.
    wire.events.clear(); allEnabled(seq); seq.setTempo(120);
    seq.start(0xfffffff0u, 2); seq.tick(0xfffffff0u, Wire::send, &wire);
    seq.tick(0x40u, Wire::send, &wire);
    assert(wire.events.size() == 1);
    seq.tick(0x41u, Wire::send, &wire);
    assert(wire.events.size() == 2 && wire.events.back()[0] == 0x81);
    seq.tick(0x6du, Wire::send, &wire);
    assert(wire.events.size() == 3 && seq.view().playhead == 1);
    seq.stop(Wire::send, &wire);

    // A tick arriving after this step's gate does not play a short late blip.
    wire.events.clear(); seq.start(0, 1); seq.tick(100, Wire::send, &wire);
    assert(wire.events.empty());
    seq.tick(125, Wire::send, &wire); assert(wire.events.size() == 1);
    seq.stop(Wire::send, &wire);

    seq.setTempo(-1); assert(seq.view().bpm == 30);
    seq.setTempo(1000); assert(seq.view().bpm == 300);
    seq.setLength(0); assert(seq.view().length == 1);
    seq.setLength(300); assert(seq.view().length == 16);
    seq.setNote(0, -10); seq.setVelocity(0, 400); seq.setGate(0, 0);
    assert(seq.view().steps[0].note == 0 && seq.view().steps[0].velocity == 127 && seq.view().steps[0].gate == 1);
    const auto before = seq.view().steps;
    seq.setNote(16, 42); seq.setNote(-1, 42); seq.toggle(100); seq.select(16);
    for (int i=0;i<16;++i) assert(before[i].note == seq.view().steps[i].note && before[i].enabled == seq.view().steps[i].enabled);
    seq.start(0, 0); assert(!seq.view().running);
    seq.start(0, 17); assert(!seq.view().running);

    // Non-integral sixteenth periods retain exact average tempo. A simple
    // 15000 / BPM integer timer would run fast and add extra attacks here.
    wire.events.clear();allEnabled(seq);seq.setTempo(137);
    for(int i=0;i<16;++i)seq.setGate(i,100);
    seq.start(0,1);
    for(uint32_t t=0;t<=60000;++t)seq.tick(t,Wire::send,&wire);
    std::size_t attacks=0;
    for(const auto& e:wire.events)if((e[0]&0xf0)==0x90)++attacks;
    assert(attacks==549); // Initial attack + 137 beats * four steps in 60 s.
    seq.stop(Wire::send,&wire);

    // Exercise 400,000 elapsed steps with bounded output and valid MIDI bytes.
    allEnabled(seq); seq.setTempo(300); seq.start(0, 16);
    for (uint32_t t=0;t<20000000;t+=137) {
        const auto n=wire.events.size(); seq.tick(t, Wire::send, &wire);
        assert(wire.events.size() <= n+2);
    }
    seq.stop(Wire::send, &wire);
    assert(!seq.releasePending());
    for (const auto& e:wire.events) assert(e[1]<128 && e[2]<128 && (e[0]==0x9f || e[0]==0x8f || e[0]==0x90 || e[0]==0x80));
    std::cout << "Performance sequencer: PASS (gate, backpressure, stalls, wraps, edits, bounds, endurance)\n";
}
