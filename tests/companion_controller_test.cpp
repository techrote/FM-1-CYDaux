#include "fm1/controller.hpp"
#include "virtual_instrument.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
struct Rig {
    fm1::Controller controller;
    fm1::VirtualInstrument engine;
    unsigned emitted=0;
    void drain() {
        fm1::MidiMessage message;
        unsigned guard=0;
        for (;;) {
            bool progress=false;
            while (controller.popMidi(message)) {
                assert(++guard<256);
                engine.receive(message);
                controller.didSend(message);
                ++emitted; progress=true;
            }
            while (engine.popReply(message)) {
                controller.receive(message.data.data(),message.size);
                progress=true;
            }
            if (!progress) break;
        }
    }
    void connect(const char* profile) {
        controller.setNativeAvailable(true);
        controller.selectProfile(static_cast<std::size_t>(fm1::profileIndex(profile)));
        controller.dispatch({fm1::ActionType::SelectTransport,3});
        fm1::UiAction action{fm1::ActionType::Disconnect};
        while (controller.takePlatformAction(action)) {}
        assert(engine.select(profile));
        controller.setConnection(fm1::ConnectionState::Connected);
        drain();
        assert(controller.model().companionReady);
        assert(controller.model().controls[0].origin==fm1::ValueOrigin::Native);
    }
};
void negotiatedAndUnavailable() {
    fm1::Controller device;
    device.selectProfile(static_cast<std::size_t>(fm1::profileIndex("zaaggenz-native")));
    device.dispatch({fm1::ActionType::SelectTransport,3});
    assert(device.model().transport==fm1::TransportKind::Demo); // No host engine on CYD.
    device.dispatch({fm1::ActionType::SelectTransport,2});
    device.setConnection(fm1::ConnectionState::Connected);
    assert(!device.model().controls[0].enabled && !device.model().notesSupported);
    device.dispatch({fm1::ActionType::NoteOn});
    device.dispatch({fm1::ActionType::SetXY,127,127});
    fm1::MidiMessage discover;
    assert(device.popMidi(discover)); device.didSend(discover);
    fm1::MidiMessage other;
    assert(!device.popMidi(other)); // Only discovery, never speculative physical CCs.
    device.tick(1001);
    assert(!device.model().companionReady && !device.model().controls[0].enabled);
}
void rapidInputAndBarriers() {
    Rig rig; rig.connect("zaaggenz-native");
    const auto before=rig.emitted;
    for (int i=0;i<100000;++i)
        rig.controller.dispatch({fm1::ActionType::SetXY,static_cast<int16_t>(i%128),static_cast<int16_t>((i+33)%128)});
    rig.drain();
    assert(rig.emitted-before==2); // Latest coordinates, bounded wire and sequence consumption.
    assert(rig.engine.rejected()==0);
    assert(rig.engine.snapshot().values[0]==99999%128);
    assert(rig.engine.snapshot().values[1]==(99999+33)%128);
    assert(rig.controller.model().controls[0].origin==fm1::ValueOrigin::Native);

    rig.controller.setControl(0,10);
    rig.controller.dispatch({fm1::ActionType::SelectPreset,2});
    rig.controller.setControl(0,127);
    rig.drain();
    assert(rig.engine.snapshot().preset==2);
    assert(rig.engine.snapshot().values[0]==127); // Edit remains after preset.
    assert(rig.engine.rejected()==0);
}
void releaseAndReconnect() {
    Rig rig; rig.connect("xenoresonator-native");
    rig.controller.dispatch({fm1::ActionType::NoteOn,60,100}); rig.drain();
    assert(rig.engine.snapshot().gate);
    for (int i=0;i<1000;++i) {
        rig.controller.dispatch({fm1::ActionType::SelectPreset,static_cast<int16_t>(i%4)});
        rig.controller.dispatch({fm1::ActionType::NoteOn,60,100});
    }
    rig.controller.dispatch({fm1::ActionType::Panic}); rig.drain();
    assert(!rig.engine.snapshot().gate && !rig.controller.model().noteHeld);
    rig.controller.dispatch({fm1::ActionType::ToggleSequence});
    rig.controller.tick(500); rig.drain();
    rig.engine.disconnect(); rig.controller.setConnection(fm1::ConnectionState::Offline);
    assert(!rig.controller.model().sequence.running && !rig.controller.model().companionReady);
    assert(!rig.controller.model().telemetryAvailable);
    rig.connect("xenoresonator-native");
    assert(!rig.engine.snapshot().gate && !rig.controller.model().sequence.running);
}
void boundsAndHonestReadback() {
    Rig rig; rig.connect("zaaggenz-native");
    rig.controller.dispatch({fm1::ActionType::NoteDelta,127});
    rig.controller.dispatch({fm1::ActionType::SetStepNote,0,127});
    rig.engine.disconnect(); rig.controller.setConnection(fm1::ConnectionState::Offline);
    rig.connect("xenoresonator-native");
    assert(rig.controller.model().note==84 && rig.controller.model().sequence.steps[0].note==84);
    rig.controller.dispatch({fm1::ActionType::StepNoteDelta,1000});
    assert(rig.controller.model().sequence.steps[0].note==84);
    rig.controller.dispatch({fm1::ActionType::StepNoteDelta,-1000});
    assert(rig.controller.model().sequence.steps[0].note==36);
    rig.controller.setControl(3,80); rig.drain();
    assert(std::strcmp(rig.controller.model().controls[3].display,"Alien metal")==0);
    rig.controller.tick(1600); // A read has been queued, but no reply has arrived.
    assert(!rig.controller.model().telemetryAvailable);
    rig.drain();
    assert(rig.controller.model().telemetryAvailable);
}
}
int main() {
    negotiatedAndUnavailable(); rapidInputAndBarriers(); releaseAndReconnect(); boundsAndHonestReadback();
    std::puts("companion controller: identity gates, 100000 XY samples, event ordering, panic, reconnect, bounds PASS");
}
