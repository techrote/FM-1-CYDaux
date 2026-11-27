#include "virtual_instrument.hpp"
#include "fm1/midi.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace fm1 {
namespace {
constexpr unsigned zaagPresets[] = {2, 7, 14, 19, 24, 31};
uint8_t meter(uint32_t value, uint32_t fullScale) {
    return static_cast<uint8_t>(std::min<uint32_t>(127, value * 127u / fullScale));
}
uint32_t magnitude(int32_t value) {
    return value < 0 ? 0u - static_cast<uint32_t>(value) : static_cast<uint32_t>(value);
}
}

bool VirtualInstrument::select(const char* id) {
    disconnect();
    profile_ = companionProfileFor(id);
    preset_ = 0;
    note_ = 60;
    renderedFrames_ = 0;
    rejected_ = 0;
    queueOverflows_ = 0;
    revision_ = 0;
    if (!profile_ || !validateCompanionProfile(*profile_)) {
        profile_ = nullptr;
        return false;
    }
    if (profile_->instrument == CompanionInstrument::ZaaggenZ) {
        zg_engine_init(&zaag_, ZG_SAMPLE_RATE);
    } else if (profile_->instrument == CompanionInstrument::Xenoresonator) {
        xeno_reset(&xeno_, 0x43594431u, 0);
    } else if (profile_->instrument == CompanionInstrument::Grimoire) {
        orbit_reset(&orbit_, 0);
    } else {
        profile_ = nullptr;
        return false;
    }
    setPreset(0);
    return true;
}

void VirtualInstrument::disconnect() {
    panic();
    session_ = 0;
    hasSequence_ = false;
    replyHead_ = 0;
    replySize_ = 0;
}

void VirtualInstrument::panic() {
    gate_ = false;
    meters_.fill(0);
    peak_ = 0;
    statePeak_ = 0;
    rms_ = 0;
    // Panic is immediate silence, whereas a musical note-off retains a tail.
    // Reinitializing only the core's dynamic state also clears recursive filters.
    if (profile_ && profile_->instrument == CompanionInstrument::ZaaggenZ) {
        const zg_patch patch = zaag_.patch;
        zg_engine_init(&zaag_, ZG_SAMPLE_RATE);
        zg_engine_set_patch(&zaag_, &patch);
        zg_engine_all_notes_off(&zaag_);
        zaag_.exciter_envelope = 0;
    } else if (profile_ && profile_->instrument == CompanionInstrument::Xenoresonator) {
        xeno_panic(&xeno_);
    } else if (profile_ && profile_->instrument == CompanionInstrument::Grimoire) {
        orbit_panic(&orbit_);
    }
    ++generation_;
    ++revision_;
}

uint32_t VirtualInstrument::sampleRate() const {
    if (profile_ && profile_->instrument == CompanionInstrument::Grimoire) return ORBIT_RATE;
    return profile_ && profile_->instrument == CompanionInstrument::Xenoresonator ? XENO_RATE : ZG_SAMPLE_RATE;
}
const char* VirtualInstrument::name() const {
    if (!profile_) return "No native engine";
    if (profile_->instrument == CompanionInstrument::Grimoire) return "VIBEWARE Grimoire Orbit Gate";
    return profile_->instrument == CompanionInstrument::ZaaggenZ ? "ZaaggenZ native procedural core" : "VIBEWARE Xenoresonator";
}
const char* VirtualInstrument::mode() const {
    return profile_ ? "native-engine" : "no-audio-engine";
}
const char* VirtualInstrument::sourceRevision() const {
    return profile_ ? profile_->sourceRevision : "";
}

bool VirtualInstrument::putReply(const MidiMessage& message) {
    if (!message.size) return false;
    if (replySize_ == kReplyCapacity) {
        // A saturated consumer never blocks audio or allocates. Refuse new
        // replies; polling/reconnection can reconcile state explicitly.
        ++queueOverflows_;
        return false;
    }
    replies_[(replyHead_ + replySize_) % kReplyCapacity] = message;
    ++replySize_;
    return true;
}
bool VirtualInstrument::popReply(MidiMessage& out) {
    if (!replySize_) return false;
    out = replies_[replyHead_];
    replyHead_ = (replyHead_ + 1) % kReplyCapacity;
    --replySize_;
    return true;
}
void VirtualInstrument::error(const CompanionPacket& request, CompanionError value) {
    CompanionPacket response;
    response.op = CompanionOp::Error;
    response.session = request.session;
    response.sequence = request.sequence;
    response.size = 1;
    response.payload[0] = static_cast<uint8_t>(value);
    putReply(encodeCompanion(response));
    ++rejected_;
}
void VirtualInstrument::stateReply(const CompanionPacket& request) {
    if (putReply(companionState(snapshot(), session_, request.sequence))) {
        // Real interval maxima retain percussion transients between 100ms
        // state polls. Clear only after a snapshot was actually queued.
        meters_.fill(0);
        statePeak_ = 0;
    }
}

bool VirtualInstrument::receive(const MidiMessage& message) {
    if (!available() || message.size > message.data.size() || !validMidi(message.data.data(), message.size)) {
        ++rejected_;
        return false;
    }
    if (message.data[0] != 0xf0) return legacyMidi(message);
    CompanionPacket packet;
    if (!decodeCompanion(message.data.data(), message.size, packet)) {
        ++rejected_;
        return false;
    }
    if (packet.major != kCompanionMajor || packet.minor > kCompanionMinor) {
        error(packet, CompanionError::Version);
        return false;
    }
    if (packet.op == CompanionOp::Discover) {
        if (packet.size != 1 || packet.payload[0] != static_cast<uint8_t>(profile_->instrument)) {
            error(packet, CompanionError::Unsupported);
            return false;
        }
        if (!packet.session || (session_ && packet.session != session_)) {
            error(packet, CompanionError::Session);
            return false;
        }
        if (!session_) {
            session_ = packet.session;
            lastSequence_ = packet.sequence;
            hasSequence_ = true;
        } else if (companionSequenceAfter(packet.sequence, lastSequence_)) {
            lastSequence_ = packet.sequence;
        }
        CompanionIdentity identity;
        identity.instrument = profile_->instrument;
        identity.profileMajor = profile_->major;
        identity.profileMinor = profile_->minor;
        identity.capabilities = profile_->capabilities;
        identity.source = CompanionSource::NativeEngine;
        return putReply(companionIdentity(identity, session_, packet.sequence));
    }
    if (!session_ || packet.session != session_) {
        error(packet, CompanionError::Session);
        return false;
    }
    if (hasSequence_ && !companionSequenceAfter(packet.sequence, lastSequence_)) {
        error(packet, CompanionError::Sequence);
        return false;
    }
    // Consume the sequence even for a refused mutation. Invalid inputs cannot
    // later be replayed after an unrelated state transition.
    lastSequence_ = packet.sequence;
    hasSequence_ = true;
    switch (packet.op) {
    case CompanionOp::GetState:
        if (packet.size) break;
        stateReply(packet);
        return true;
    case CompanionOp::SetParameter:
        if (packet.size != 2) break;
        if (!setParameter(packet.payload[0], packet.payload[1])) {
            error(packet, CompanionError::Range);
            return false;
        }
        stateReply(packet);
        return true;
    case CompanionOp::SetPreset:
        if (packet.size != 1) break;
        if (!setPreset(packet.payload[0])) {
            error(packet, CompanionError::Range);
            return false;
        }
        stateReply(packet);
        return true;
    case CompanionOp::Note:
        if (packet.size != 2) break;
        if (packet.payload[0] < profile_->noteMinimum || packet.payload[0] > profile_->noteMaximum) {
            error(packet, CompanionError::Range);
            return false;
        }
        note(packet.payload[0], packet.payload[1]);
        stateReply(packet);
        return true;
    case CompanionOp::Panic:
        if (packet.size) break;
        panic();
        stateReply(packet);
        return true;
    default:
        error(packet, CompanionError::Unsupported);
        return false;
    }
    error(packet, CompanionError::Malformed);
    return false;
}

bool VirtualInstrument::legacyMidi(const MidiMessage& message) {
    // This is a local virtual transport adapter. It does not establish that
    // these messages are understood by an unmodified physical FM-1 firmware.
    if (!ready()) { ++rejected_; return false; }
    const uint8_t status = message.data[0];
    if ((status & 15u) != 0) { ++rejected_; return false; }
    if (message.size == 3 && ((status & 0xf0u) == 0x90 || (status & 0xf0u) == 0x80)) {
        const auto pitch = message.data[1];
        if (pitch < profile_->noteMinimum || pitch > profile_->noteMaximum) { ++rejected_; return false; }
        note(pitch, (status & 0xf0u) == 0x80 ? 0 : message.data[2]);
        return true;
    }
    if (message.size == 3 && (status & 0xf0u) == 0xb0) {
        const auto cc = message.data[1];
        if (cc == 120 || cc == 123) { panic(); return true; }
        for (unsigned i = 0; i < kCompanionParameters; ++i) {
            if ((profile_->parameterMask & (1u << i)) && profile_->parameters[i].cc == cc)
                return setParameter(i, message.data[2]);
        }
    }
    if (message.size == 2 && (status & 0xf0u) == 0xc0) return setPreset(message.data[1]);
    ++rejected_;
    return false;
}

bool VirtualInstrument::setParameter(unsigned index, unsigned normalized) {
    if (!profile_ || index >= kCompanionParameters || normalized > 127 ||
        !(profile_->parameterMask & (1u << index)) || !profile_->parameters[index].writable) return false;
    const auto value = static_cast<uint8_t>(companionParameterValue(profile_->parameters[index], static_cast<int>(normalized)));
    if (profile_->instrument == CompanionInstrument::ZaaggenZ) {
        zg_patch patch = zaag_.patch;
        switch (index) {
        case 0: patch.grit = value; break;
        case 1: patch.vowel = value; break;
        case 2: patch.motion = value; break;
        case 3: patch.family = value; break;
        case 4: patch.attack = value; break;
        case 5: patch.release = value; break;
        case 6: patch.level[ZG_BODY] = value; break;
        case 7: patch.level[ZG_SUB] = value; break;
        default: return false;
        }
        patch.mode = ZG_NATIVE;
        zg_engine_set_patch(&zaag_, &patch);
    } else if (profile_->instrument == CompanionInstrument::Grimoire) {
        if (index == 0) orbit_xy(&orbit_, value, orbit_.y);
        else if (index == 1) orbit_xy(&orbit_, orbit_.x, value);
        else return false;
    } else {
        switch (index) {
        case 0: xeno_controls(&xeno_, value, xeno_.damping, xeno_.coupling); break;
        case 1: xeno_controls(&xeno_, xeno_.excitation, value, xeno_.coupling); break;
        case 2: xeno_controls(&xeno_, xeno_.excitation, xeno_.damping, value); break;
        case 3: xeno_material(&xeno_, value); break;
        default: return false;
        }
    }
    ++revision_;
    return true;
}

bool VirtualInstrument::setPreset(unsigned index) {
    if (!profile_ || index >= profile_->presetCount) return false;
    panic();
    preset_ = static_cast<uint8_t>(index);
    if (profile_->instrument == CompanionInstrument::ZaaggenZ) {
        zg_engine_set_preset(&zaag_, zaagPresets[index]);
        zg_patch patch = zaag_.patch;
        patch.mode = ZG_NATIVE;
        zg_engine_set_patch(&zaag_, &patch);
        note_ = zaag_.note;
    } else if (profile_->instrument == CompanionInstrument::Grimoire) {
        orbit_reset(&orbit_, index);
        note_ = 36 + orbit_.x * 23u / 127u;
    } else {
        xeno_reset(&xeno_, 0x43594431u, index);
        note_ = xeno_.note;
    }
    ++revision_;
    return true;
}

void VirtualInstrument::note(unsigned pitch, unsigned velocity) {
    if (!velocity && pitch != note_) return; // late release cannot end a newer note
    if (velocity) note_ = static_cast<uint8_t>(pitch);
    gate_ = velocity != 0;
    if (profile_->instrument == CompanionInstrument::ZaaggenZ) {
        if (velocity) zg_engine_note_on(&zaag_, pitch, velocity, 0);
        else zg_engine_note_off(&zaag_, pitch);
    } else if (profile_->instrument == CompanionInstrument::Grimoire) {
        // Note is a captured gesture gate; pitch lives in pitch_x.
        orbit_gate(&orbit_, velocity != 0);
    } else {
        if (velocity) xeno_strike(&xeno_, pitch, velocity);
        else xeno_release(&xeno_);
    }
    ++revision_;
}

CompanionSnapshot VirtualInstrument::snapshot() const {
    CompanionSnapshot result;
    if (!profile_) return result;
    result.validMask = profile_->parameterMask;
    result.note = note_;
    result.gate = gate_;
    result.preset = preset_;
    result.meterCount = kCompanionMeters;
    result.meters = meters_;
    result.outputPeak = meter(statePeak_, 32768);
    result.revision = revision_ & kCompanionSequenceMask;
    if (profile_->instrument == CompanionInstrument::ZaaggenZ) {
        const auto& p = zaag_.patch;
        const uint8_t values[] = {p.grit, p.vowel, p.motion, p.family, p.attack, p.release, p.level[ZG_BODY], p.level[ZG_SUB]};
        for (unsigned i = 0; i < kCompanionParameters; ++i)
            result.values[i] = companionParameterNormalized(profile_->parameters[i], values[i]);
    } else if (profile_->instrument == CompanionInstrument::Grimoire) {
        result.values[0] = orbit_.x;
        result.values[1] = orbit_.y;
        result.meterCount = 2;
    } else {
        const uint8_t values[] = {xeno_.excitation, xeno_.damping, xeno_.coupling, xeno_.material};
        for (unsigned i = 0; i < 4; ++i)
            result.values[i] = companionParameterNormalized(profile_->parameters[i], values[i]);
    }
    return result;
}

bool VirtualInstrument::render(int16_t* destination, std::size_t frames) {
    if (!destination || frames > kMaximumFrames) return false;
    if (!ready()) {
        std::fill(destination, destination + frames, 0);
        peak_ = 0;
        rms_ = 0;
        return true;
    }
    std::array<uint32_t, ZG_LAYER_COUNT> stemPeaks{};
    if (profile_->instrument == CompanionInstrument::ZaaggenZ) {
        for (std::size_t offset = 0; offset < frames; offset += ZG_BLOCK) {
            const auto count = std::min<std::size_t>(ZG_BLOCK, frames - offset);
            zg_engine_render_stems(&zaag_, destination + offset, stems_.data(), count);
            for (std::size_t i = 0; i < count; ++i)
                for (unsigned stem = 0; stem < ZG_LAYER_COUNT; ++stem)
                    stemPeaks[stem] = std::max(stemPeaks[stem], magnitude(stems_[i * ZG_STEM_COUNT + stem]));
        }
        for (unsigned i = 0; i < ZG_LAYER_COUNT; ++i) meters_[i] = std::max(meters_[i], meter(stemPeaks[i], 32768));
    } else if (profile_->instrument == CompanionInstrument::Grimoire) {
        // Bounded actual envelope and energy samples, retaining interval maxima.
        for (std::size_t offset = 0; offset < frames; offset += 64) {
            orbit_render(&orbit_, destination + offset, std::min<std::size_t>(64, frames - offset));
            meters_[0] = std::max(meters_[0], meter(orbit_.env, 32767));
            meters_[1] = std::max(meters_[1], meter(orbit_.energy, 28000));
        }
    } else {
        xeno_render(&xeno_, destination, frames);
        for (unsigned i = 0; i < XENO_MODES; ++i) meters_[i] = std::max(meters_[i], meter(xeno_mode_level(&xeno_, i), 131070));
    }
    uint32_t maximum = 0;
    uint64_t sumSquares = 0;
    for (std::size_t i = 0; i < frames; ++i) {
        const int32_t value = destination[i];
        maximum = std::max(maximum, magnitude(value));
        sumSquares += static_cast<uint64_t>(value * value);
    }
    peak_ = static_cast<uint16_t>(maximum);
    statePeak_ = std::max(statePeak_, peak_);
    rms_ = frames ? std::sqrt(static_cast<double>(sumSquares) / static_cast<double>(frames)) / 32768.0 : 0.0;
    if (profile_->instrument == CompanionInstrument::ZaaggenZ) meters_[5] = std::max(meters_[5], meter(maximum, 32768));
    renderedFrames_ += frames;
    ++revision_;
    return true;
}
} // namespace fm1
