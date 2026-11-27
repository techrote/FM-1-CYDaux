#include "virtual_instrument.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

namespace {
fm1::MidiMessage request(fm1::CompanionOp op, uint32_t session, uint16_t sequence,
                         std::initializer_list<uint8_t> values = {}) {
    fm1::CompanionPacket packet;
    packet.op = op; packet.session = session; packet.sequence = sequence;
    for (auto value : values) packet.payload[packet.size++] = value;
    return fm1::encodeCompanion(packet);
}
void drain(fm1::VirtualInstrument& instrument) {
    fm1::MidiMessage reply;
    while (instrument.popReply(reply)) {}
}
void negotiate(fm1::VirtualInstrument& instrument, const char* id, uint32_t session) {
    assert(instrument.select(id));
    const auto* profile = fm1::companionProfileFor(id);
    assert(instrument.receive(fm1::companionDiscover(profile->instrument, session, 1)));
    fm1::MidiMessage message; fm1::CompanionPacket packet; fm1::CompanionIdentity identity;
    assert(instrument.popReply(message));
    assert(fm1::decodeCompanion(message.data.data(), message.size, packet));
    assert(fm1::parseCompanionIdentity(packet, identity));
    assert(identity.source == fm1::CompanionSource::NativeEngine);
    assert(identity.instrument == profile->instrument);
}
uint64_t checksum(const int16_t* data, std::size_t count) {
    uint64_t result = 1469598103934665603ull;
    for (std::size_t i = 0; i < count; ++i) {
        result ^= static_cast<uint16_t>(data[i]); result *= 1099511628211ull;
    }
    return result;
}
}
int main() {
    using namespace fm1;
    std::array<int16_t, VirtualInstrument::kMaximumFrames> pcm{};
    VirtualInstrument endpoint;
    assert(!endpoint.select("unsupported"));
    assert(!endpoint.receive(request(CompanionOp::Note, 1, 1, {60,100})));
    assert(endpoint.render(pcm.data(), pcm.size()));
    assert(std::all_of(pcm.begin(), pcm.end(), [](int16_t s){return s==0;}));
    assert(!endpoint.render(pcm.data(), VirtualInstrument::kMaximumFrames + 1));
    assert(!endpoint.render(nullptr, 1));

    for (const auto id : {"zaaggenz-native", "xenoresonator-native"}) {
        negotiate(endpoint, id, 7);
        assert(endpoint.receive(request(CompanionOp::Note, 7, 2, {60,100})));
        drain(endpoint);
        assert(endpoint.render(pcm.data(), pcm.size()));
        assert(std::any_of(pcm.begin(), pcm.end(), [](int16_t s){return s!=0;}));
        assert(endpoint.snapshot().gate);
        const auto before = endpoint.snapshot();
        assert(!endpoint.receive(request(CompanionOp::SetParameter, 8, 3, {0,0})));
        assert(endpoint.snapshot().values == before.values);
        assert(!endpoint.receive(request(CompanionOp::Note, 6, 3, {60,0})));
        assert(endpoint.snapshot().gate);
        assert(!endpoint.receive(request(CompanionOp::SetParameter, 7, 2, {0,0})));
        assert(endpoint.snapshot().values == before.values);
        // A replayed discovery must not rewind the mutation sequence.
        const auto* profile = companionProfileFor(id);
        endpoint.receive(companionDiscover(profile->instrument, 7, 1));
        assert(!endpoint.receive(request(CompanionOp::SetParameter, 7, 2, {0,0})));
        assert(endpoint.snapshot().values == before.values);
        drain(endpoint);
        assert(!endpoint.receive(request(CompanionOp::SetPreset, 7, 3, {127})));
        assert(!endpoint.receive(request(CompanionOp::SetParameter, 7, 4, {127,127})));
        drain(endpoint);
        assert(endpoint.receive(request(CompanionOp::Note, 7, 5, {64,100})));
        assert(endpoint.receive(request(CompanionOp::Note, 7, 6, {60,0})));
        assert(endpoint.snapshot().gate && endpoint.snapshot().note == 64);
        assert(endpoint.receive(request(CompanionOp::Panic, 7, 7)));
        assert(!endpoint.snapshot().gate);
        assert(endpoint.render(pcm.data(), pcm.size()));
        assert(std::all_of(pcm.begin(), pcm.end(), [](int16_t s){return s==0;}));
        endpoint.disconnect();
        assert(!endpoint.ready());
        assert(!endpoint.receive(request(CompanionOp::Note, 7, 8, {60,100})));
    }

    // Independent transport release must match the actual endpoint's serial
    // ordering. A duplicate Panic is rejected and must not clear note duty.
    // Exercise both real source-pinned engines and actual silence after the
    // generated emergency Panic, not merely a tracker-only assertion.
    for (const auto id : {"zaaggenz-native", "xenoresonator-native"}) {
        negotiate(endpoint, id, 21);
        CompanionReleaseTracker tracker;
        const auto* profile = companionProfileFor(id);
        tracker.sent(companionDiscover(profile->instrument, 21, 1));
        const auto attack = request(CompanionOp::Note, 21, 2, {60,100});
        assert(endpoint.receive(attack)); tracker.sent(attack);
        drain(endpoint);
        assert(endpoint.render(pcm.data(), pcm.size()));
        assert(std::any_of(pcm.begin(),pcm.end(),[](int16_t s){return s!=0;}));
        const auto duplicatePanic = request(CompanionOp::Panic, 21, 2);
        assert(!endpoint.receive(duplicatePanic)); tracker.sent(duplicatePanic);
        assert(endpoint.snapshot().gate && tracker.needsRelease());
        const auto emergency = tracker.panicMessage();
        assert(emergency.size && endpoint.receive(emergency)); tracker.sent(emergency);
        assert(!tracker.needsRelease() && !endpoint.snapshot().gate);
        const auto duplicateNote = request(CompanionOp::Note, 21, 3, {60,100});
        assert(!endpoint.receive(duplicateNote)); tracker.sent(duplicateNote);
        assert(!tracker.needsRelease());
        assert(endpoint.render(pcm.data(),pcm.size()));
        assert(std::all_of(pcm.begin(),pcm.end(),[](int16_t s){return s==0;}));
    }

    // Every native preset independently yields a different actual waveform.
    for (const auto id : {"zaaggenz-native", "xenoresonator-native"}) {
        const auto* profile = companionProfileFor(id);
        std::array<uint64_t,8> hashes{};
        for (unsigned p=0;p<profile->presetCount;++p) {
            negotiate(endpoint,id,11);
            assert(endpoint.receive(request(CompanionOp::SetPreset,11,2,{static_cast<uint8_t>(p)})));
            assert(endpoint.receive(request(CompanionOp::Note,11,3,{48,100})));
            assert(endpoint.render(pcm.data(),pcm.size()));
            hashes[p]=checksum(pcm.data(),pcm.size());
            for(unsigned previous=0;previous<p;++previous)assert(hashes[previous]!=hashes[p]);
            assert(std::any_of(pcm.begin(),pcm.end(),[](int16_t s){return s!=0;}));
        }
    }

    // Third consumer uses the actual Orbit engine: gesture X/Y, three
    // materials, gate ownership, stale release, deterministic audio and silence.
    std::array<uint64_t,3> orbitHashes{};
    for (uint8_t p=0;p<3;++p) {
        negotiate(endpoint,"grimoire-native",31);
        assert(endpoint.sampleRate()==ORBIT_RATE);
        assert(endpoint.receive(request(CompanionOp::SetPreset,31,2,{p})));
        assert(endpoint.receive(request(CompanionOp::SetParameter,31,3,{0,20})));
        assert(endpoint.receive(request(CompanionOp::SetParameter,31,4,{1,90})));
        assert(endpoint.receive(request(CompanionOp::Note,31,5,{48,100})));
        drain(endpoint);
        assert(endpoint.render(pcm.data(),pcm.size()));
        orbitHashes[p]=checksum(pcm.data(),pcm.size());
        const auto state=endpoint.snapshot();
        assert(state.validMask==3 && state.values[0]==20 && state.values[1]==90);
        assert(state.gate && state.meterCount==2 && state.meters[0]>0 && state.outputPeak>0);
        for(uint8_t prev=0;prev<p;++prev)assert(orbitHashes[p]!=orbitHashes[prev]);
        assert(endpoint.receive(request(CompanionOp::Note,31,6,{49,0})));
        assert(endpoint.snapshot().gate);
        assert(endpoint.receive(request(CompanionOp::Note,31,7,{48,0})));
        assert(!endpoint.snapshot().gate);
        assert(endpoint.receive(request(CompanionOp::Panic,31,8)));
        assert(endpoint.render(pcm.data(),pcm.size()));
        assert(std::all_of(pcm.begin(),pcm.end(),[](int16_t s){return s==0;}));
        endpoint.disconnect();
        assert(!endpoint.receive(request(CompanionOp::Note,31,9,{48,100})));
    }

    // Bounded outgoing state: saturation never allocates or prevents panic.
    negotiate(endpoint,"xenoresonator-native",13);
    for(uint16_t sequence=2;sequence<200;++sequence)
        endpoint.receive(request(CompanionOp::SetParameter,13,sequence,{0,static_cast<uint8_t>(sequence%128)}));
    assert(endpoint.queueOverflows()>0);
    assert(endpoint.receive(request(CompanionOp::Panic,13,200)));
    assert(!endpoint.snapshot().gate);
    unsigned replies=0;MidiMessage reply;
    while(endpoint.popReply(reply))++replies;
    assert(replies==VirtualInstrument::kReplyCapacity);
    assert(endpoint.receive(request(CompanionOp::GetState,13,201)));
    assert(endpoint.popReply(reply));

    // Raw MIDI is a local-only convenience and is refused before negotiation.
    endpoint.select("xenoresonator-native");
    MidiMessage note;note.size=3;note.data[0]=0x90;note.data[1]=48;note.data[2]=100;
    assert(!endpoint.receive(note));
    // Ring wrap is a genuine serial-number wrap, not stale replay.
    assert(endpoint.receive(companionDiscover(CompanionInstrument::Xenoresonator,14,16382)));
    drain(endpoint);
    assert(endpoint.receive(request(CompanionOp::Note,14,16383,{48,100})));
    assert(endpoint.receive(request(CompanionOp::Note,14,0,{48,0})));
    assert(!endpoint.snapshot().gate);

    std::cout << "Actual native cores: presets, PCM, stale sessions, discovery replay, sequence wrap, saturation and panic PASS\n";
}
