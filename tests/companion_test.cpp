#include "fm1/companion.hpp"
#include "fm1/midi.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace fm1;
namespace {
CompanionPacket decoded(const MidiMessage& message) {
    CompanionPacket packet;
    assert(message.size && validMidi(message.data.data(), message.size));
    assert(decodeCompanion(message.data.data(), message.size, packet));
    return packet;
}
bool receive(CompanionSession& session, const MidiMessage& message, uint32_t now) {
    return session.receive(message.data.data(), message.size, now);
}
void ready(CompanionSession& session, CompanionInstrument instrument, uint32_t id, uint32_t now) {
    session.begin(instrument, id, now);
    const auto request = session.discover(now);
    assert(decoded(request).payload[0] == static_cast<unsigned>(instrument));
    session.sent(request, now);
    const auto* profile = companionProfileFor(instrument);
    assert(receive(session, companionIdentity({instrument, 1, 0, profile->capabilities, CompanionSource::NativeEngine}, id, 0), now));
    assert(session.ready() && !session.pending());
}
CompanionSnapshot stateFor(CompanionInstrument instrument) {
    const auto* profile = companionProfileFor(instrument);
    CompanionSnapshot state;
    state.validMask = profile->parameterMask;
    for (std::size_t i=0;i<kCompanionParameters;++i)
        state.values[i] = companionParameterNormalized(profile->parameters[i], profile->parameters[i].initial);
    state.note = 60; state.meterCount = 6;
    state.meters = {{1, 2, 3, 4, 5, 127}}; state.outputPeak = 37;
    return state;
}
void profiles() {
    assert(!companionProfileFor(nullptr) && !companionProfileFor("sloop") && !companionProfileFor(CompanionInstrument::Unknown));
    for (auto instrument : {CompanionInstrument::ZaaggenZ, CompanionInstrument::Xenoresonator}) {
        const auto* p = companionProfileFor(instrument);
        assert(p && validateCompanionProfile(*p));
        assert(!p->physicalMidiVerified);
        assert(companionProfileFor(p->id) == p);
        for (std::size_t i=0;i<8;++i) if (p->parameterMask & (1u<<i)) {
            const auto& parameter = p->parameters[i];
            assert(companionParameterValue(parameter, -100) == parameter.minimum);
            assert(companionParameterValue(parameter, 1000) == parameter.maximum);
            assert(companionParameterNormalized(parameter, parameter.minimum-1) == 0);
            assert(companionParameterNormalized(parameter, parameter.maximum+1) == 127);
            if (parameter.kind == ParameterKind::Enumeration)
                for (int v=parameter.minimum;v<=parameter.maximum;++v)
                    assert(companionParameterValue(parameter, companionParameterNormalized(parameter,v)) == v);
        }
        auto invalid=*p; invalid.major=2; assert(!validateCompanionProfile(invalid));
        invalid=*p; invalid.parameters[0].id=nullptr; assert(!validateCompanionProfile(invalid));
        invalid=*p; invalid.parameters[1].id=invalid.parameters[0].id; assert(!validateCompanionProfile(invalid));
        invalid=*p; invalid.parameters[1].cc=invalid.parameters[0].cc; assert(!validateCompanionProfile(invalid));
        invalid=*p; invalid.parameters[0].maximum=-1; assert(!validateCompanionProfile(invalid));
        invalid=*p; invalid.parameters[3].choiceCount=33; assert(!validateCompanionProfile(invalid));
        invalid=*p; invalid.noteMaximum=255; assert(!validateCompanionProfile(invalid));
        invalid=*p; invalid.presetCount=33; assert(!validateCompanionProfile(invalid));
    }
    char text[32];
    const auto& character=companionProfileFor(CompanionInstrument::ZaaggenZ)->parameters[3];
    formatCompanionParameter(character, 127, text, sizeof text); assert(!std::strcmp(text,"Harmonic Rip"));
    formatCompanionParameter(character, -1, text, sizeof text); assert(!std::strcmp(text,"?"));
    char tiny[2]={'x','x'}; formatCompanionParameter(character,127,tiny,sizeof tiny); assert(tiny[1]=='\0');
}
void malformed() {
    const auto good=companionState(stateFor(CompanionInstrument::ZaaggenZ),0xabcdef,123);
    assert(good.size==38);
    const auto packet=decoded(good);
    assert(packet.session==0xabcdef && packet.sequence==123);
    CompanionSnapshot parsed;
    assert(parseCompanionState(packet,parsed) && parsed.validMask==255 && parsed.meters[5]==127 && parsed.outputPeak==37);
    for (std::size_t n=0;n<good.size;++n) {
        CompanionPacket sentinel; sentinel.session=42;
        assert(!decodeCompanion(good.data.data(),n,sentinel));
        assert(sentinel.session==42);
    }
    for (std::size_t n=1;n+1<good.size;++n) {
        auto bad=good; bad.data[n]|=0x80;
        CompanionPacket target; assert(!decodeCompanion(bad.data.data(),bad.size,target));
    }
    auto bad=good; bad.data[13]++; CompanionPacket target;
    assert(!decodeCompanion(bad.data.data(),bad.size,target));
    assert(!decodeCompanion(nullptr,38,target));
    bad=good; bad.data[2]='F'; assert(!decodeCompanion(bad.data.data(),bad.size,target));
    bad=good; bad.data[6]=99; assert(!decodeCompanion(bad.data.data(),bad.size,target));
    auto invalid=packet; invalid.session=0; assert(!encodeCompanion(invalid).size);
    invalid=packet; invalid.sequence=16384; assert(!encodeCompanion(invalid).size);
    invalid=packet; invalid.payload[0]=128; assert(!encodeCompanion(invalid).size);
    invalid=packet; invalid.payload[12]=4; assert(!parseCompanionState(invalid,parsed));
    invalid=packet; invalid.payload[13]=7; assert(!parseCompanionState(invalid,parsed));
    invalid=packet; invalid.payload[1]=2; assert(!parseCompanionState(invalid,parsed));
}
void sessions() {
    CompanionSession session;
    assert(!session.setParameter(0,64).size && !session.note(60,100).size && !session.panic().size);
    session.begin(CompanionInstrument::ZaaggenZ,5,100);
    const auto discover=session.discover(100);
    assert(!session.discover(101).size);
    session.tick(10000); assert(session.pending() && !session.timedOut()); // Not sent yet.
    session.sent(discover,10000); session.tick(10999); assert(session.pending());
    session.tick(11000); assert(!session.pending() && session.timedOut() && !session.ready());
    assert(session.discover(11001).size); session.cancelPending(); assert(!session.pending());
    ready(session,CompanionInstrument::ZaaggenZ,25,200);
    assert(!session.discover(200).size);
    assert(!session.setParameter(8,64).size && !session.setParameter(0,128).size && !session.setPreset(6).size);
    auto req=session.requestState(220); session.sent(req,220);
    auto state=stateFor(CompanionInstrument::ZaaggenZ);
    auto reply=companionState(state,25,decoded(req).sequence);
    assert(receive(session,reply,225) && session.stateFresh(1700) && !session.stateFresh(1800));
    assert(!receive(session,reply,226)); // Duplicate response cannot revive freshness.
    req=session.setParameter(0,110);
    assert(decoded(req).payload[0]==0 && decoded(req).payload[1]==110);
    auto later=session.setParameter(1,20);
    state.values[0]=110; state.values[1]=20;
    assert(!receive(session,companionState(state,25,decoded(req).sequence),230));
    assert(receive(session,companionState(state,25,decoded(later).sequence),231));
    assert(session.snapshot().values[0]==110 && session.lastReplySequence()==decoded(later).sequence);
    auto stale=companionState(state,25,session.sequence());
    ready(session,CompanionInstrument::ZaaggenZ,26,300);
    assert(!receive(session,stale,301) && !session.stateFresh(301));
    req=session.requestState(310); session.sent(req,310);
    reply=companionState(state,26,decoded(req).sequence);
    reply.data[4]=2; assert(!receive(session,reply,311));
    assert(!receive(session,companionState(state,27,decoded(req).sequence),312));
    state.playing=true; assert(!receive(session,companionState(state,26,decoded(req).sequence),313));
    state.playing=false; assert(receive(session,companionState(state,26,decoded(req).sequence),314));
    ready(session,CompanionInstrument::Xenoresonator,33,400);
    assert(!session.note(35,100).size && !session.note(85,100).size && session.note(36,127).size);
    assert(!session.setParameter(4,64).size && !session.setPreset(4).size);
    req=session.requestState(401); state=stateFor(CompanionInstrument::Xenoresonator); state.validMask=255;
    assert(!receive(session,companionState(state,33,decoded(req).sequence),402));
    state.validMask=15; state.note=35; assert(!receive(session,companionState(state,33,decoded(req).sequence),403));
    state.note=60; state.preset=4; assert(!receive(session,companionState(state,33,decoded(req).sequence),404));
    state.preset=3; assert(receive(session,companionState(state,33,decoded(req).sequence),405));
}
void negotiationAndWrap() {
    CompanionSession session;
    session.begin(CompanionInstrument::ZaaggenZ,42,0);
    const auto discover=session.discover(0); session.sent(discover,0);
    auto identity=CompanionIdentity{CompanionInstrument::ZaaggenZ,1,0,CompanionNotes,CompanionSource::NativeEngine};
    identity.instrument=CompanionInstrument::Xenoresonator;
    assert(!receive(session,companionIdentity(identity,42,0),1));
    identity.instrument=CompanionInstrument::ZaaggenZ; identity.profileMajor=2;
    assert(!receive(session,companionIdentity(identity,42,0),2));
    identity.profileMajor=1; identity.capabilities=CompanionPattern;
    assert(!receive(session,companionIdentity(identity,42,0),3));
    identity.capabilities=CompanionNotes; assert(receive(session,companionIdentity(identity,42,0),4));
    assert(!session.setParameter(0,64).size && !session.setPreset(0).size && session.note(60,100).size);
    ready(session,CompanionInstrument::ZaaggenZ,43,5);
    auto state=stateFor(CompanionInstrument::ZaaggenZ);
    for (unsigned n=0;n<20000;++n) {
        const auto request=session.setParameter(0,n%128);
        state.values[0]=n%128;
        assert(receive(session,companionState(state,43,decoded(request).sequence),n+10));
    }
    assert(companionSequenceAfter(1,16383) && !companionSequenceAfter(16383,1));
    assert(!companionSequenceAfter(1,1) && !companionSequenceAfter(8192,0));
    session.begin(CompanionInstrument::ZaaggenZ,44,0xfffffff0u);
    const auto pending=session.discover(0xfffffff0u); session.sent(pending,0xfffffff0u);
    session.tick(999); assert(session.timedOut());
}
void endpointRetirement() {
    for (const auto code : {CompanionError::Version, CompanionError::Session}) {
        CompanionSession session; ready(session,CompanionInstrument::Xenoresonator,91,0);
        auto query=session.requestState(1);
        assert(receive(session,companionState(stateFor(CompanionInstrument::Xenoresonator),91,decoded(query).sequence),2));
        assert(session.stateFresh(2));
        query=session.requestState(3); session.sent(query,3);
        CompanionPacket error; error.op=CompanionOp::Error; error.session=91;
        error.sequence=decoded(query).sequence; error.size=1; error.payload[0]=static_cast<uint8_t>(code);
        // A retired, unrelated or older session cannot disable this one.
        error.session=90; assert(!receive(session,encodeCompanion(error),4) && session.ready());
        error.session=91; error.sequence--; assert(!receive(session,encodeCompanion(error),4) && session.ready());
        error.sequence=decoded(query).sequence;
        assert(!receive(session,encodeCompanion(error),4));
        assert(!session.ready() && !session.pending() && !session.stateFresh(4));
        assert(!session.note(48,100).size && !session.setParameter(0,127).size && !session.setPreset(0).size);
        assert(session.identity().source==CompanionSource::Unknown);
        session.tick(2000); assert(!session.pending() && !session.timedOut());
        ready(session,CompanionInstrument::Xenoresonator,92,2001);
        assert(session.note(48,100).size); // fresh discovery, never a queued replay
    }
    CompanionSession session; ready(session,CompanionInstrument::Xenoresonator,93,0);
    const auto query=session.setParameter(0,127);
    CompanionPacket error;error.op=CompanionOp::Error;error.session=93;
    error.sequence=decoded(query).sequence;error.size=1;error.payload[0]=static_cast<uint8_t>(CompanionError::Range);
    assert(!receive(session,encodeCompanion(error),1) && session.ready());
    assert(session.note(48,100).size); // one refused value does not revoke identity
}
void independentRelease() {
    CompanionReleaseTracker tracker;
    CompanionSession session;
    ready(session,CompanionInstrument::ZaaggenZ,71,0);
    tracker.sent(companionDiscover(CompanionInstrument::ZaaggenZ,71));
    assert(!tracker.needsRelease() && !tracker.panicMessage().size);
    const auto sounding=session.note(60,100); tracker.sent(sounding);
    assert(tracker.needsRelease());
    // Several controller commands may already exist, but none was transmitted.
    for (unsigned n=0;n<40;++n) session.setParameter(n%8,n%128);
    const auto emergency=tracker.panicMessage();
    assert(decoded(emergency).sequence==decoded(sounding).sequence+1);
    tracker.sent(emergency); assert(!tracker.needsRelease());
    const auto controllerPanic=session.panic(); tracker.sent(controllerPanic);
    assert(companionSequenceAfter(decoded(controllerPanic).sequence,decoded(emergency).sequence));
    const auto next=session.note(64,110); tracker.sent(next); assert(tracker.needsRelease());
    // Replayed handshake and other-session traffic cannot retire old release duty.
    tracker.sent(companionDiscover(CompanionInstrument::ZaaggenZ,71));
    tracker.sent(companionDiscover(CompanionInstrument::Xenoresonator,99));
    assert(decoded(tracker.panicMessage()).session==71);
    assert(decoded(tracker.panicMessage()).sequence==decoded(next).sequence+1);
    tracker.reset(); assert(!tracker.needsRelease() && !tracker.panicMessage().size);
    // If no command was queued between the last Note and both Panic paths,
    // duplicate Panics have the same sequence and are harmless at the endpoint.
    ready(session,CompanionInstrument::ZaaggenZ,72,0);
    tracker.sent(companionDiscover(CompanionInstrument::ZaaggenZ,72));
    tracker.sent(session.note(60,100));
    const auto first=tracker.panicMessage(), second=session.panic();
    assert(decoded(first).sequence==decoded(second).sequence);
    tracker.sent(first); tracker.sent(second); assert(!tracker.needsRelease());
    // Lease/version recovery performs a complete transport reconnect. A wire
    // handshake alone must not steal old release duty; the closed epoch releases
    // first, then resets the worker tracker before admitting a new session.
    tracker.reset(); ready(session,CompanionInstrument::Xenoresonator,80,0);
    tracker.sent(companionDiscover(CompanionInstrument::Xenoresonator,80));
    tracker.sent(session.note(48,110));
    tracker.sent(companionDiscover(CompanionInstrument::Xenoresonator,81));
    assert(decoded(tracker.panicMessage()).session==80);
    tracker.sent(tracker.panicMessage()); tracker.reset();
    ready(session,CompanionInstrument::Xenoresonator,81,1);
    tracker.sent(companionDiscover(CompanionInstrument::Xenoresonator,81));
    const auto recoveredNote=session.note(60,100);tracker.sent(recoveredNote);
    tracker.sent(companionDiscover(CompanionInstrument::Xenoresonator,80));
    const auto recoveredPanic=decoded(tracker.panicMessage());
    assert(recoveredPanic.session==81 && recoveredPanic.sequence==decoded(recoveredNote).sequence+1);
    tracker.sent(encodeCompanion(recoveredPanic)); assert(!tracker.needsRelease());
}
}
int main() {
    profiles(); malformed(); sessions(); negotiationAndWrap(); endpointRetirement(); independentRelease();
    std::cout << "companion metadata, codec, negotiation, session, freshness and wrap tests passed\n";
}
