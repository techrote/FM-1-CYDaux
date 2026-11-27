#include "fm1/companion.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace fm1 {
namespace {
constexpr uint16_t kCapabilities = CompanionNotes | CompanionParameters | CompanionPresets |
                                  CompanionXY | CompanionTelemetry;
const char* const kCharacters[] = {"Bloom Bark", "Formant Snarl", "Upper Chop", "Split Maul", "Crushed Teeth", "Harmonic Rip"};
const char* const kZaagPresets[] = {"Native Bark", "Throat Walk", "Flea Circuit", "Iron Bellows", "Crushed Teeth Native", "Wire Lattice"};
const char* const kMaterials[] = {"Ceramic", "Woodglass", "Alien metal", "Membrane"};
constexpr CompanionParameter continuous(const char* id, const char* label, uint8_t cc, int16_t initial) {
    return {id, label, ParameterKind::Continuous, 0, 127, initial, cc, "", nullptr, 0, true};
}
constexpr CompanionParameter unused() {
    return {"", "", ParameterKind::ReadOnly, 0, 0, 0, 0, "", nullptr, 0, false};
}
const CompanionProfile kProfiles[] = {
    {CompanionInstrument::ZaaggenZ, "zaaggenz-native", "1.0", 1, 0, kCapabilities,
     {{continuous("grit", "Grit", 16, 90), continuous("vowel", "Vowel", 17, 60),
       continuous("motion", "Motion", 18, 42),
       {"character", "Character", ParameterKind::Enumeration, 0, 5, 0, 19, "", kCharacters, 6, true},
       continuous("attack", "Attack", 20, 5), continuous("release", "Release", 21, 35),
       continuous("body", "Body", 22, 20), continuous("sub", "Sub", 23, 40)}},
     0xff, kZaagPresets, 6, 0, 127, "ac21d089e7afce6bc7adc555deb0ce90b56a39cd", false},
    {CompanionInstrument::Xenoresonator, "xenoresonator-native", "1.0", 1, 0, kCapabilities,
     {{continuous("excitation", "Excite", 16, 76), continuous("damping", "Damping", 17, 38),
       continuous("coupling", "Coupling", 18, 48),
       {"material", "Material", ParameterKind::Enumeration, 0, 3, 0, 19, "", kMaterials, 4, true},
       unused(), unused(), unused(), unused()}},
     0x0f, kMaterials, 4, 36, 84, "fa27a317bb116f2afb62a710876f96314756f814", false},
};
bool textValid(const char* s, std::size_t maximum, bool allowEmpty = false) {
    if (!s || (!allowEmpty && !s[0])) return false;
    for (std::size_t i = 0; i <= maximum; ++i) {
        const unsigned char ch = static_cast<unsigned char>(s[i]);
        if (!ch) return true;
        if (ch < 32 || ch > 126) return false;
    }
    return false;
}
bool operationValid(CompanionOp op, uint8_t size) {
    switch (op) {
        case CompanionOp::Discover: return size == 1;
        case CompanionOp::Identity: return size == 6;
        case CompanionOp::GetState: case CompanionOp::Panic: return size == 0;
        case CompanionOp::State: return size == 23;
        case CompanionOp::SetParameter: case CompanionOp::Note: return size == 2;
        case CompanionOp::SetPreset: case CompanionOp::Error: return size == 1;
    }
    return false;
}
bool instrumentValid(CompanionInstrument instrument) { return companionProfileFor(instrument) != nullptr; }
uint16_t read14(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (uint16_t(p[1]) << 7u)); }
void write14(uint8_t* p, uint16_t value) { p[0] = value & 127u; p[1] = (value >> 7u) & 127u; }
CompanionPacket packetFor(CompanionOp op, uint32_t session, uint16_t sequence) {
    CompanionPacket packet;
    packet.op = op; packet.session = session; packet.sequence = sequence;
    return packet;
}
}

const CompanionProfile* companionProfileFor(const char* id) {
    if (!id) return nullptr;
    for (const auto& profile : kProfiles) if (std::strcmp(id, profile.id) == 0) return &profile;
    return nullptr;
}
const CompanionProfile* companionProfileFor(CompanionInstrument instrument) {
    for (const auto& profile : kProfiles) if (profile.instrument == instrument) return &profile;
    return nullptr;
}
bool validateCompanionProfile(const CompanionProfile& profile) {
    if (!instrumentValid(profile.instrument) || !textValid(profile.id, 31) ||
        !textValid(profile.version, 15) || !textValid(profile.sourceRevision, 40) ||
        profile.major != kCompanionMajor || profile.minor > 127 || !profile.parameterMask ||
        (profile.capabilities & ~uint16_t(127)) || profile.noteMinimum > profile.noteMaximum ||
        profile.noteMaximum > 127 || !profile.presetNames || !profile.presetCount || profile.presetCount > kCompanionPresets)
        return false;
    for (unsigned i = 0; i < profile.presetCount; ++i)
        if (!textValid(profile.presetNames[i], 31)) return false;
    for (std::size_t i = 0; i < profile.parameters.size(); ++i) {
        if (!(profile.parameterMask & (1u << i))) continue;
        const auto& p = profile.parameters[i];
        if (!textValid(p.id, 31) || !textValid(p.label, 15) || !textValid(p.unit, 11, true) ||
            p.minimum > p.maximum || p.initial < p.minimum || p.initial > p.maximum || p.cc > 119 ||
            static_cast<unsigned>(p.kind) > static_cast<unsigned>(ParameterKind::ReadOnly) ||
            (p.kind == ParameterKind::ReadOnly && p.writable)) return false;
        if (p.kind == ParameterKind::Enumeration) {
            if (!p.choices || !p.choiceCount || p.choiceCount > 32 || p.minimum != 0 ||
                p.maximum != p.choiceCount - 1) return false;
            for (unsigned k = 0; k < p.choiceCount; ++k) if (!textValid(p.choices[k], 31)) return false;
        } else if (p.choiceCount || p.choices) return false;
        if (p.kind == ParameterKind::Switch && (p.minimum != 0 || p.maximum != 1)) return false;
        for (std::size_t k = 0; k < i; ++k) if (profile.parameterMask & (1u << k)) {
            if (!std::strcmp(p.id, profile.parameters[k].id) || p.cc == profile.parameters[k].cc) return false;
        }
    }
    return true;
}
int companionParameterValue(const CompanionParameter& p, int normalized) {
    normalized = std::max(0, std::min(127, normalized));
    if (p.minimum >= p.maximum) return p.minimum;
    return p.minimum + ((int32_t(p.maximum) - p.minimum) * normalized + 63) / 127;
}
uint8_t companionParameterNormalized(const CompanionParameter& p, int semantic) {
    if (p.minimum >= p.maximum) return 0;
    semantic = std::max(int(p.minimum), std::min(int(p.maximum), semantic));
    const int32_t range = int32_t(p.maximum) - p.minimum;
    return static_cast<uint8_t>(((semantic - p.minimum) * 127 + range / 2) / range);
}
void formatCompanionParameter(const CompanionParameter& p, int normalized, char* out, std::size_t size) {
    if (!out || !size) return;
    if (normalized < 0) { std::snprintf(out, size, "?"); return; }
    const int value = companionParameterValue(p, normalized);
    if (p.kind == ParameterKind::Enumeration && p.choices && value >= 0 && value < p.choiceCount)
        std::snprintf(out, size, "%s", p.choices[value]);
    else if (p.kind == ParameterKind::Switch) std::snprintf(out, size, "%s", value ? "On" : "Off");
    else std::snprintf(out, size, "%d%s%s", value, p.unit && p.unit[0] ? " " : "", p.unit ? p.unit : "");
}

MidiMessage encodeCompanion(const CompanionPacket& packet) {
    MidiMessage message;
    if (!packet.session || packet.session > kCompanionSessionMask || packet.sequence > kCompanionSequenceMask ||
        packet.major > 127 || packet.minor > 127 || packet.size > kCompanionMaxPayload ||
        !operationValid(packet.op, packet.size)) return message;
    for (std::size_t i = 0; i < packet.size; ++i) if (packet.payload[i] > 127) return message;
    auto& d = message.data;
    d[0] = 0xf0; d[1] = 0x7d; d[2] = 'C'; d[3] = 'Y';
    d[4] = packet.major; d[5] = packet.minor; d[6] = static_cast<uint8_t>(packet.op);
    for (unsigned i = 0; i < 4; ++i) d[7+i] = (packet.session >> (i*7u)) & 127u;
    write14(d.data()+11, packet.sequence); d[13] = packet.size;
    std::copy_n(packet.payload.begin(), packet.size, d.begin()+14);
    d[14+packet.size] = 0xf7;
    message.size = static_cast<uint16_t>(15+packet.size);
    return message;
}
bool decodeCompanion(const uint8_t* data, std::size_t size, CompanionPacket& output) {
    if (!data || size < 15 || size > 15+kCompanionMaxPayload || data[0] != 0xf0 ||
        data[1] != 0x7d || data[2] != 'C' || data[3] != 'Y' || data[size-1] != 0xf7 ||
        size != std::size_t(15+data[13])) return false;
    for (std::size_t i = 1; i+1 < size; ++i) if (data[i] > 127) return false;
    CompanionPacket packet;
    packet.major = data[4]; packet.minor = data[5]; packet.op = static_cast<CompanionOp>(data[6]);
    packet.size = data[13];
    if (!operationValid(packet.op, packet.size)) return false;
    for (unsigned i = 0; i < 4; ++i) packet.session |= uint32_t(data[7+i]) << (i*7u);
    if (!packet.session) return false;
    packet.sequence = read14(data+11);
    std::copy_n(data+14, packet.size, packet.payload.begin());
    output = packet;
    return true;
}
MidiMessage companionDiscover(CompanionInstrument instrument, uint32_t session, uint16_t sequence) {
    if (!instrumentValid(instrument)) return {};
    auto packet = packetFor(CompanionOp::Discover, session, sequence);
    packet.payload[0] = static_cast<uint8_t>(instrument); packet.size = 1;
    return encodeCompanion(packet);
}
MidiMessage companionIdentity(const CompanionIdentity& identity, uint32_t session, uint16_t sequence) {
    if (!instrumentValid(identity.instrument) || identity.profileMajor > 127 || identity.profileMinor > 127 ||
        identity.capabilities > kCompanionSequenceMask || identity.source == CompanionSource::Unknown ||
        static_cast<unsigned>(identity.source) > static_cast<unsigned>(CompanionSource::PhysicalDevice)) return {};
    auto packet = packetFor(CompanionOp::Identity, session, sequence); packet.size = 6;
    packet.payload[0] = static_cast<uint8_t>(identity.instrument); packet.payload[1] = identity.profileMajor;
    packet.payload[2] = identity.profileMinor; write14(packet.payload.data()+3, identity.capabilities);
    packet.payload[5] = static_cast<uint8_t>(identity.source);
    return encodeCompanion(packet);
}
bool parseCompanionIdentity(const CompanionPacket& packet, CompanionIdentity& output) {
    if (packet.op != CompanionOp::Identity || packet.size != 6) return false;
    CompanionIdentity identity;
    identity.instrument = static_cast<CompanionInstrument>(packet.payload[0]);
    identity.profileMajor = packet.payload[1]; identity.profileMinor = packet.payload[2];
    identity.capabilities = read14(packet.payload.data()+3);
    identity.source = static_cast<CompanionSource>(packet.payload[5]);
    if (!instrumentValid(identity.instrument) || identity.source == CompanionSource::Unknown ||
        static_cast<unsigned>(identity.source) > static_cast<unsigned>(CompanionSource::PhysicalDevice)) return false;
    output = identity;
    return true;
}
MidiMessage companionState(const CompanionSnapshot& state, uint32_t session, uint16_t sequence) {
    if (state.meterCount > kCompanionMeters || state.preset > 127 || state.note > 127 ||
        state.revision > kCompanionSequenceMask) return {};
    auto packet = packetFor(CompanionOp::State, session, sequence); packet.size = 23;
    write14(packet.payload.data(), state.validMask);
    std::copy(state.values.begin(), state.values.end(), packet.payload.begin()+2);
    packet.payload[10] = state.preset; packet.payload[11] = state.note;
    packet.payload[12] = (state.gate ? 1 : 0) | (state.playing ? 2 : 0);
    packet.payload[13] = state.meterCount;
    std::copy(state.meters.begin(), state.meters.end(), packet.payload.begin()+14);
    packet.payload[20] = state.outputPeak;
    write14(packet.payload.data()+21, state.revision);
    return encodeCompanion(packet);
}
bool parseCompanionState(const CompanionPacket& packet, CompanionSnapshot& output) {
    if (packet.op != CompanionOp::State || packet.size != 23 || packet.payload[1] > 1 ||
        packet.payload[12] > 3 || packet.payload[13] > kCompanionMeters) return false;
    CompanionSnapshot state;
    state.validMask = static_cast<uint8_t>(read14(packet.payload.data()));
    std::copy_n(packet.payload.begin()+2, state.values.size(), state.values.begin());
    state.preset = packet.payload[10]; state.note = packet.payload[11];
    state.gate = (packet.payload[12] & 1) != 0; state.playing = (packet.payload[12] & 2) != 0;
    state.meterCount = packet.payload[13];
    std::copy_n(packet.payload.begin()+14, state.meters.size(), state.meters.begin());
    state.outputPeak = packet.payload[20];
    state.revision = read14(packet.payload.data()+21);
    output = state;
    return true;
}
bool companionSequenceAfter(uint16_t newer, uint16_t older) {
    if (newer > kCompanionSequenceMask || older > kCompanionSequenceMask) return false;
    const uint16_t delta = (newer-older) & kCompanionSequenceMask;
    return delta != 0 && delta < 0x2000;
}
void CompanionSession::reset() { *this = CompanionSession{}; }
void CompanionSession::begin(CompanionInstrument instrument, uint32_t session, uint32_t now) {
    reset();
    if (!instrumentValid(instrument) || !session || session > kCompanionSessionMask) return;
    instrument_ = instrument; session_ = session; requestAt_ = now;
}
MidiMessage CompanionSession::discover(uint32_t now) {
    if (!session_ || pending_ || ready_) return {};
    pending_ = true; requestSent_ = false; timedOut_ = false; requestAt_ = now;
    pendingOp_ = CompanionOp::Discover; requestSequence_ = sequence_;
    return companionDiscover(instrument_, session_, sequence_);
}
MidiMessage CompanionSession::command(CompanionOp op, const uint8_t* data, uint8_t size) {
    if (!ready_ || size > kCompanionMaxPayload) return {};
    sequence_ = (sequence_+1) & kCompanionSequenceMask;
    if (!sequence_) sequence_ = 1;
    auto packet = packetFor(op, session_, sequence_); packet.size = size;
    if (size) std::copy_n(data, size, packet.payload.begin());
    return encodeCompanion(packet);
}
MidiMessage CompanionSession::requestState(uint32_t now) {
    if (!ready_ || pending_) return {};
    auto message = command(CompanionOp::GetState, nullptr, 0);
    if (message.size) {
        pending_ = true; requestSent_ = false; pendingOp_ = CompanionOp::GetState; requestSequence_ = sequence_;
        requestAt_ = now; timedOut_ = false;
    }
    return message;
}
MidiMessage CompanionSession::setParameter(uint8_t parameter, uint8_t value) {
    const auto* profile = companionProfileFor(instrument_);
    if (!profile || !(identity_.capabilities & CompanionParameters) || parameter >= kCompanionParameters ||
        !(profile->parameterMask & (1u << parameter)) || !profile->parameters[parameter].writable || value > 127) return {};
    const uint8_t data[] = {parameter, value};
    return command(CompanionOp::SetParameter, data, 2);
}
MidiMessage CompanionSession::setPreset(uint8_t preset) {
    const auto* profile = companionProfileFor(instrument_);
    if (!profile || !(identity_.capabilities & CompanionPresets) || preset >= profile->presetCount) return {};
    return command(CompanionOp::SetPreset, &preset, 1);
}
MidiMessage CompanionSession::note(uint8_t noteNumber, uint8_t velocity) {
    const auto* profile = companionProfileFor(instrument_);
    if (!profile || !(identity_.capabilities & CompanionNotes) || velocity > 127 ||
        noteNumber < profile->noteMinimum || noteNumber > profile->noteMaximum) return {};
    const uint8_t data[] = {noteNumber, velocity};
    return command(CompanionOp::Note, data, 2);
}
MidiMessage CompanionSession::panic() { return command(CompanionOp::Panic, nullptr, 0); }
bool CompanionSession::receive(const uint8_t* data, std::size_t size, uint32_t now) {
    CompanionPacket packet;
    if (!decodeCompanion(data, size, packet) || !session_ || packet.session != session_ ||
        packet.major != kCompanionMajor || packet.minor > kCompanionMinor) { ++rejected_; return false; }
    if (packet.op == CompanionOp::Identity) {
        CompanionIdentity identity;
        const auto* profile = companionProfileFor(instrument_);
        if (!pending_ || pendingOp_ != CompanionOp::Discover || packet.sequence != requestSequence_ ||
            !parseCompanionIdentity(packet, identity) || identity.instrument != instrument_ || !profile ||
            identity.profileMajor != profile->major || identity.profileMinor > profile->minor ||
            (identity.capabilities & ~profile->capabilities)) { ++rejected_; return false; }
        identity_ = identity; ready_ = true; pending_ = false; timedOut_ = false;
        lastReply_ = packet.sequence; hasReply_ = true;
        return true;
    }
    if (!ready_ || packet.sequence != sequence_ ||
        (hasReply_ && !companionSequenceAfter(packet.sequence, lastReply_))) { ++rejected_; return false; }
    if (packet.op == CompanionOp::State) {
        CompanionSnapshot state;
        const auto* profile = companionProfileFor(instrument_);
        if (!parseCompanionState(packet, state) || !profile || (state.validMask & ~profile->parameterMask) ||
            state.preset >= profile->presetCount || state.note < profile->noteMinimum || state.note > profile->noteMaximum ||
            (!(identity_.capabilities & CompanionTelemetry) && state.meterCount) ||
            (!(identity_.capabilities & CompanionTransport) && state.playing)) { ++rejected_; return false; }
        snapshot_ = state; receivedAt_ = now; hasSnapshot_ = true;
        lastReply_ = packet.sequence; hasReply_ = true; pending_ = false; timedOut_ = false;
        return true;
    }
    if (packet.op == CompanionOp::Error && packet.payload[0] >= 1 && packet.payload[0] <= 6) {
        lastReply_ = packet.sequence; hasReply_ = true; pending_ = false;
        ++rejected_; return false;
    }
    ++rejected_; return false;
}
void CompanionSession::sent(const MidiMessage& message, uint32_t now) {
    CompanionPacket packet;
    if (pending_ && !requestSent_ && decodeCompanion(message.data.data(), message.size, packet) &&
        packet.session == session_ && packet.sequence == requestSequence_ && packet.op == pendingOp_) {
        requestSent_ = true; requestAt_ = now;
    }
}
void CompanionSession::cancelPending() { pending_ = false; requestSent_ = false; }
void CompanionSession::tick(uint32_t now) {
    if (pending_ && requestSent_ && uint32_t(now-requestAt_) >= kCompanionTimeoutMs) {
        pending_ = false; timedOut_ = true;
        if (pendingOp_ == CompanionOp::Discover) ready_ = false;
    }
}
bool CompanionSession::stateFresh(uint32_t now, uint32_t maximumAge) const {
    return ready_ && hasSnapshot_ && uint32_t(now-receivedAt_) <= maximumAge;
}
void CompanionReleaseTracker::reset() { *this = CompanionReleaseTracker{}; }
void CompanionReleaseTracker::sent(const MidiMessage& message) {
    CompanionPacket packet;
    if (!decodeCompanion(message.data.data(), message.size, packet) ||
        packet.major != kCompanionMajor || packet.minor != kCompanionMinor) return;
    if (packet.op == CompanionOp::Discover && !session_) {
        session_ = packet.session; sequence_ = packet.sequence;
    }
    if (packet.session != session_) return;
    const bool current = packet.sequence == sequence_ || companionSequenceAfter(packet.sequence, sequence_);
    if (!current) return;
    sequence_ = packet.sequence;
    if (packet.op == CompanionOp::Note && packet.payload[1]) notesTouched_ = true;
    else if (packet.op == CompanionOp::Panic) notesTouched_ = false;
}
MidiMessage CompanionReleaseTracker::panicMessage() const {
    if (!session_ || !notesTouched_) return {};
    uint16_t sequence = (sequence_ + 1) & kCompanionSequenceMask;
    if (!sequence) sequence = 1;
    return encodeCompanion(packetFor(CompanionOp::Panic, session_, sequence));
}
} // namespace fm1
