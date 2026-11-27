#include "fm1/controller.hpp"
#include "fm1/midi.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "transport review failure: %s\n", message);
        std::exit(1);
    }
}

// A tiny output observer models note lifetimes, rather than inspecting the
// controller's queue implementation. This catches lost release messages.
struct Instrument {
    std::array<std::array<bool, 128>, 16> notes{};
    void accept(const fm1::MidiMessage& message) {
        if (message.size != 3 || message.data[0] >= 0xf0) return;
        const auto channel = message.data[0] & 15;
        const auto kind = message.data[0] & 0xf0;
        if (kind == 0x90) notes[channel][message.data[1]] = message.data[2] != 0;
        if (kind == 0x80) notes[channel][message.data[1]] = false;
        if (kind == 0xb0 && (message.data[1] == 120 || message.data[1] == 123))
            notes[channel].fill(false);
    }
};

void connectSkiff(fm1::Controller& controller) {
    const int index = fm1::profileIndex("skiff");
    require(index >= 0, "Skiff fixture profile missing");
    controller.selectProfile(static_cast<std::size_t>(index));
    controller.dispatch({fm1::ActionType::SelectTransport, static_cast<int16_t>(fm1::TransportKind::Wifi)});
    controller.setConnection(fm1::ConnectionState::Connected);
}

void drain(fm1::Controller& controller, Instrument& instrument) {
    fm1::MidiMessage message;
    unsigned count = 0;
    while (controller.popMidi(message)) {
        require(++count < 128, "output queue did not remain bounded");
        require(fm1::validMidi(message.data.data(), message.size), "invalid MIDI left controller");
        instrument.accept(message);
        controller.didSend(message);
    }
}

void releaseSurvivesQueuePressure() {
    fm1::Controller controller;
    connectSkiff(controller);
    Instrument instrument;
    // An unrelated channel must not be silenced by the controller's recovery.
    instrument.notes[15][60] = true;
    controller.dispatch({fm1::ActionType::NoteOn});
    drain(controller, instrument);
    require(instrument.notes[0][60], "initial note was not delivered");

    // The old channel's note-off is queued, then a second channel's note-on.
    // Fill the queue before releasing that second note. Clearing the queue to
    // fit only the newest note-off used to strand the first sounding note.
    controller.dispatch({fm1::ActionType::SelectTrack, 1});
    controller.dispatch({fm1::ActionType::NoteOn});
    for (int i = 0; i < 64; ++i)
        controller.dispatch({fm1::ActionType::ProgramDelta, 1});
    controller.dispatch({fm1::ActionType::NoteOff});
    drain(controller, instrument);
    require(!instrument.notes[0][60], "queue pressure lost the earlier channel's note release");
    require(!instrument.notes[1][60], "second channel remained sounding after release");
    require(instrument.notes[15][60], "recovery silenced an unrelated channel");
}

void panicDoesNotLeaveDiscardedControlsPending() {
    fm1::Controller controller;
    connectSkiff(controller);
    controller.dispatch({fm1::ActionType::SetControl, 0, 90});
    require(controller.model().controls[0].origin == fm1::ValueOrigin::Pending,
            "test control was not queued");
    controller.dispatch({fm1::ActionType::Panic});
    require(controller.model().controls[0].origin != fm1::ValueOrigin::Pending,
            "panic discarded a control but left it permanently Pending");
}
} // namespace

int main() {
    releaseSurvivesQueuePressure();
    panicDoesNotLeaveDiscardedControlsPending();
    std::puts("transport release and pending-state regressions passed");
}
