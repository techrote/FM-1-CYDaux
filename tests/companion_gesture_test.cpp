#include "fm1/controller.hpp"
#include "fm1/ui.hpp"
#include "virtual_instrument.hpp"
#include <array>
#include <cassert>
#include <iostream>

namespace {
struct Rig {
    fm1::Controller controller;
    fm1::VirtualInstrument engine;
    fm1::Ui ui;
    Rig():ui(dispatch,this){}
    static void dispatch(void* context,fm1::UiAction action) {
        static_cast<Rig*>(context)->controller.dispatch(action);
    }
    void drain() {
        fm1::MidiMessage message,reply;
        unsigned budget=100;
        while(controller.popMidi(message)){
            assert(budget--);assert(engine.receive(message));controller.didSend(message);
            while(engine.popReply(reply))controller.receive(reply.data.data(),reply.size);
        }
    }
    void connect(const char* profile="xenoresonator-native") {
        controller.setNativeAvailable(true);
        controller.selectProfile(fm1::profileIndex(profile));
        controller.dispatch({fm1::ActionType::SelectTransport,3});
        fm1::UiAction action{fm1::ActionType::Refresh};while(controller.takePlatformAction(action)){}
        assert(engine.select(profile));
        controller.setConnection(fm1::ConnectionState::Connected);drain();
        controller.dispatch({fm1::ActionType::SetPage,static_cast<int16_t>(fm1::Page::XY)});
        assert(controller.model().xyTouchGate&&controller.model().notesSupported);
    }
};
void switchingLifecycle() {
    using namespace fm1;
    Rig rig;rig.connect();
    uint64_t audibleSamples=0;
    std::array<int16_t,128> audio{};
    for(unsigned iteration=0;iteration<1000;++iteration){
        rig.controller.dispatch({ActionType::SetPage,static_cast<int16_t>(Page::XY)});
        rig.ui.touch(TouchPhase::Down,40,100,rig.controller.model());rig.drain();
        assert(rig.engine.snapshot().gate);
        assert(rig.engine.render(audio.data(),audio.size()));
        for(const auto sample:audio)audibleSamples+=sample!=0;
        const char* current=profileAt(rig.controller.model().profileIndex).id;
        switch(iteration%4){
            case 0:{
                // Replace an instrument while the old surface is captured.
                const char* next=rig.controller.model().profileIndex==profileIndex("xenoresonator-native")?
                    "zaaggenz-native":"xenoresonator-native";
                rig.engine.disconnect();assert(!rig.engine.snapshot().gate);rig.connect(next);break;
            }
            case 1:
                // Offline and Connected can both arrive before pollTouch. The
                // visible context ends up identical, but this is a new epoch.
                rig.engine.disconnect();rig.controller.setConnection(ConnectionState::Offline);
                rig.connect(current);break;
            case 2:
                rig.controller.dispatch({ActionType::SelectTransport,0});rig.engine.disconnect();
                rig.controller.setConnection(ConnectionState::Offline);rig.connect(current);break;
            case 3:
                // Include reload of the same preset identity: index comparison
                // alone must not permit the old gesture to edit the new patch.
                rig.controller.dispatch({ActionType::SelectPreset,static_cast<int16_t>(rig.controller.model().presetIndex)});
                rig.drain();break;
        }
        const auto expected=rig.engine.snapshot().values;
        rig.ui.touch(TouchPhase::Move,220,70,rig.controller.model());
        rig.ui.touch(TouchPhase::Up,220,70,rig.controller.model());rig.drain();
        assert(!rig.engine.snapshot().gate&&!rig.controller.model().noteHeld);
        assert(rig.engine.snapshot().values==expected);

        rig.controller.dispatch({ActionType::ToggleSequence});
        rig.controller.tick(iteration*300+1);rig.drain();
        assert(rig.controller.model().sequence.running);
        assert(rig.engine.render(audio.data(),audio.size()));
        rig.engine.disconnect();rig.controller.setConnection(ConnectionState::Offline);
        assert(!rig.controller.model().sequence.running&&!rig.controller.model().telemetryAvailable);
        rig.connect(current);
        assert(!rig.engine.snapshot().gate&&!rig.controller.model().sequence.running);
        assert(rig.engine.rejected()==0&&rig.engine.queueOverflows()==0);
    }
    assert(audibleSamples>1000);
}
void endpointLeaseLoss() {
    using namespace fm1;
    for(const auto profile:{"zaaggenz-native","xenoresonator-native"})
    for(const bool sequence:{false,true})
    for(const auto reason:{CompanionError::Session,CompanionError::Version}){
        Rig rig;rig.connect(profile);
        if(sequence){
            rig.controller.dispatch({ActionType::ToggleSequence});rig.controller.tick(1);
        }else rig.ui.touch(TouchPhase::Down,190,100,rig.controller.model());
        rig.drain();
        assert(rig.engine.snapshot().gate);
        assert(sequence?rig.controller.model().sequence.running:rig.controller.model().noteHeld);
        const auto oldState=rig.engine.snapshot();
        const auto epoch=rig.controller.model().interactionEpoch;

        // A real endpoint restarts while the transport remains connected. Its
        // new instance must refuse our next command in the old lease. For the
        // version case the wire fixture presents an incompatible major, as
        // after a firmware/protocol change; the error is still generated by
        // the actual native endpoint, not fabricated by the controller test.
        rig.controller.setControl(0,110);
        MidiMessage command,reply;
        assert(rig.controller.popMidi(command));
        CompanionPacket original;assert(decodeCompanion(command.data.data(),command.size,original));
        rig.engine.disconnect();assert(rig.engine.select(profile));
        auto wire=command;
        if(reason==CompanionError::Version){auto packet=original;packet.major=2;wire=encodeCompanion(packet);}
        assert(!rig.engine.receive(wire));
        rig.controller.didSend(command);
        assert(rig.engine.popReply(reply));
        CompanionPacket error;assert(decodeCompanion(reply.data.data(),reply.size,error));
        assert(error.op==CompanionOp::Error&&error.payload[0]==static_cast<uint8_t>(reason));
        rig.controller.receive(reply.data.data(),reply.size);
        const auto& retired=rig.controller.model();
        assert(retired.connection==ConnectionState::Connected); // Wire survived; identity did not.
        assert(!retired.companionReady&&!retired.notesSupported&&!retired.sequenceSupported&&!retired.xyEnabled);
        assert(!retired.noteHeld&&!retired.sequence.running&&!retired.telemetryAvailable);
        assert(retired.interactionEpoch!=epoch);
        for(const auto& value:retired.controls)
            assert(value.value==-1&&value.origin==ValueOrigin::Unknown&&!value.enabled);
        assert(retired.xyOriginX==ValueOrigin::Unknown&&retired.xyOriginY==ValueOrigin::Unknown);
        assert(!rig.controller.popMidi(command));

        // Losing a lease does not authorize mutation replay or automatic
        // rediscovery, even when the UI/clock continues producing activity.
        rig.controller.dispatch({ActionType::NoteOn,60,100});
        rig.controller.dispatch({ActionType::SetXY,127,127});
        rig.controller.dispatch({ActionType::SelectPreset,1});
        rig.controller.dispatch({ActionType::ToggleSequence});
        rig.controller.tick(5000);
        assert(!rig.controller.popMidi(command));
        const auto stale=companionState(oldState,original.session,original.sequence);
        rig.controller.receive(stale.data.data(),stale.size);
        assert(!rig.controller.model().companionReady&&!rig.controller.model().telemetryAvailable);
        std::array<int16_t,128> silent{};assert(rig.engine.render(silent.data(),silent.size()));
        for(const auto sample:silent)assert(sample==0);

        rig.controller.dispatch({ActionType::Refresh});
        assert(rig.controller.popMidi(command));
        CompanionPacket discovery;assert(decodeCompanion(command.data.data(),command.size,discovery));
        assert(discovery.op==CompanionOp::Discover&&discovery.session&&discovery.session!=original.session);
        assert(!rig.controller.model().companionReady);
        assert(rig.engine.receive(command));rig.controller.didSend(command);
        while(rig.engine.popReply(reply))rig.controller.receive(reply.data.data(),reply.size);
        rig.drain();
        assert(rig.controller.model().companionReady&&rig.controller.model().notesSupported);
        assert(!rig.controller.model().noteHeld&&!rig.controller.model().sequence.running&&!rig.engine.snapshot().gate);
        const auto values=rig.engine.snapshot().values;
        // A gesture captured before retirement cannot operate the recovered
        // lease, even though profile/page/transport all look identical again.
        rig.ui.touch(TouchPhase::Move,220,70,rig.controller.model());
        rig.ui.touch(TouchPhase::Up,220,70,rig.controller.model());rig.drain();
        assert(rig.engine.snapshot().values==values&&!rig.engine.snapshot().gate);
        rig.ui.touch(TouchPhase::Down,90,110,rig.controller.model());rig.drain();
        assert(rig.engine.snapshot().gate);
        rig.ui.touch(TouchPhase::Up,90,110,rig.controller.model());rig.drain();
        assert(!rig.engine.snapshot().gate);
    }
}
}
int main() {
    using namespace fm1;
    Rig actual,expected;actual.connect();expected.connect();
    actual.ui.touch(TouchPhase::Down,195,100,actual.controller.model());
    const int x=actual.controller.model().xyX,y=actual.controller.model().xyY;
    // The initial touch's excitation/damping must be applied before strike.
    // Xenoresonator snapshots excitation into its noise-burst length on note-on,
    // so reversing these operations changes the real generated waveform.
    expected.controller.dispatch({ActionType::SetXY,static_cast<int16_t>(x),static_cast<int16_t>(y)});
    expected.controller.dispatch({ActionType::NoteOn});
    actual.drain();expected.drain();
    std::array<int16_t,2048> heard{},reference{};
    assert(actual.engine.render(heard.data(),heard.size()));
    assert(expected.engine.render(reference.data(),reference.size()));
    assert(heard==reference);
    bool audible=false;for(const auto sample:heard)audible|=sample!=0;assert(audible);
    actual.ui.touch(TouchPhase::Up,195,100,actual.controller.model());actual.drain();
    assert(!actual.engine.snapshot().gate&&!actual.controller.model().noteHeld);
    actual.ui.touch(TouchPhase::Down,100,100,actual.controller.model());actual.drain();
    assert(actual.engine.snapshot().gate);
    actual.ui.cancelTouch();actual.drain();assert(!actual.engine.snapshot().gate);

    // A preceding response cannot overwrite a later admitted gesture intent,
    // even if the reply arrives while that new edit is queued or already sent.
    auto checkDelayed=[&](bool sentFirst){
        actual.controller.setControl(0,10);
        MidiMessage old,newer,reply;
        assert(actual.controller.popMidi(old));assert(actual.engine.receive(old));actual.controller.didSend(old);
        actual.controller.setControl(0,100);
        if(sentFirst){assert(actual.controller.popMidi(newer));assert(actual.engine.receive(newer));actual.controller.didSend(newer);}
        assert(actual.engine.popReply(reply));actual.controller.receive(reply.data.data(),reply.size);
        assert(actual.controller.model().controls[0].value==100);
        assert(actual.controller.model().controls[0].origin==(sentFirst?ValueOrigin::Sent:ValueOrigin::Pending));
        if(!sentFirst){assert(actual.controller.popMidi(newer));assert(actual.engine.receive(newer));actual.controller.didSend(newer);}
        assert(actual.engine.popReply(reply));actual.controller.receive(reply.data.data(),reply.size);
        assert(actual.controller.model().controls[0].value==100&&actual.controller.model().controls[0].origin==ValueOrigin::Native);
    };
    checkDelayed(false);checkDelayed(true);
    switchingLifecycle();
    endpointLeaseLoss();
    std::cout<<"Companion gestures: PASS (actual strike ordering/audio, release, delayed readback, 1000 held/sequence transitions, endpoint lease retirement/recovery)\n";
}
