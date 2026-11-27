#pragma once
#include "fm1/profile.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace fm1 {
// Experimental CYDaux endpoint protocol. Implemented by the native evaluator
// and the separate VIBEWARE USB target adapter; not by unmodified Fune or the
// legacy firmware profiles. Emulation is separate from physical qualification.
constexpr uint8_t kCompanionMajor = 1;
constexpr uint8_t kCompanionMinor = 0;
constexpr uint32_t kCompanionSessionMask = 0x0fffffff;
constexpr uint16_t kCompanionSequenceMask = 0x3fff;
constexpr uint32_t kCompanionTimeoutMs = 1000;
constexpr std::size_t kCompanionParameters = 8;
constexpr std::size_t kCompanionMeters = 6;
constexpr std::size_t kCompanionPresets = 8;
constexpr std::size_t kCompanionMaxPayload = 48;

enum class CompanionInstrument : uint8_t { Unknown = 0, ZaaggenZ = 1, Xenoresonator = 2 };
enum class CompanionSource : uint8_t { Unknown = 0, NativeEngine = 1, EmulatedFirmware = 2, PhysicalDevice = 3 };
enum CompanionCapability : uint16_t {
    CompanionNotes = 1u << 0, CompanionParameters = 1u << 1,
    CompanionPresets = 1u << 2, CompanionXY = 1u << 3,
    CompanionTelemetry = 1u << 4, CompanionTransport = 1u << 5,
    CompanionPattern = 1u << 6,
};
enum class ParameterKind : uint8_t { Continuous, Enumeration, Switch, ReadOnly };
struct CompanionParameter {
    const char* id;
    const char* label;
    ParameterKind kind;
    int16_t minimum, maximum, initial;
    uint8_t cc;
    const char* unit;
    const char* const* choices;
    uint8_t choiceCount;
    bool writable;
};
struct CompanionProfile {
    CompanionInstrument instrument;
    const char* id;
    const char* version;
    uint8_t major, minor;
    uint16_t capabilities;
    std::array<CompanionParameter, kCompanionParameters> parameters;
    uint8_t parameterMask;
    const char* const* presetNames;
    uint8_t presetCount;
    uint8_t noteMinimum, noteMaximum;
    const char* sourceRevision;
    bool physicalMidiVerified;
};
const CompanionProfile* companionProfileFor(const char* id);
const CompanionProfile* companionProfileFor(CompanionInstrument);
bool validateCompanionProfile(const CompanionProfile&);
int companionParameterValue(const CompanionParameter&, int normalized);
uint8_t companionParameterNormalized(const CompanionParameter&, int semantic);
void formatCompanionParameter(const CompanionParameter&, int normalized, char* out, std::size_t size);

enum class CompanionOp : uint8_t {
    Discover = 1, Identity = 2, GetState = 3, State = 4,
    SetParameter = 5, SetPreset = 6, Note = 7, Panic = 8, Error = 9,
};
enum class CompanionError : uint8_t { Malformed = 1, Version = 2, Session = 3, Unsupported = 4, Range = 5, Sequence = 6 };
struct CompanionPacket {
    CompanionOp op = CompanionOp::Discover;
    uint8_t major = kCompanionMajor, minor = kCompanionMinor;
    uint32_t session = 0;
    uint16_t sequence = 0;
    std::array<uint8_t, kCompanionMaxPayload> payload{};
    uint8_t size = 0;
};
struct CompanionIdentity {
    CompanionInstrument instrument = CompanionInstrument::Unknown;
    uint8_t profileMajor = 1, profileMinor = 0;
    uint16_t capabilities = 0;
    CompanionSource source = CompanionSource::Unknown;
};
struct CompanionSnapshot {
    uint8_t validMask = 0;
    std::array<uint8_t, kCompanionParameters> values{}; // normalized MIDI domain
    uint8_t preset = 0, note = 60;
    bool gate = false, playing = false;
    uint8_t meterCount = 0;
    std::array<uint8_t, kCompanionMeters> meters{};
    uint8_t outputPeak = 0; // Actual PCM peak since previous snapshot, independently scaled 0..127.
    uint16_t revision = 0;
};
MidiMessage encodeCompanion(const CompanionPacket&);
bool decodeCompanion(const uint8_t*, std::size_t, CompanionPacket&);
MidiMessage companionDiscover(CompanionInstrument, uint32_t session, uint16_t sequence = 0);
MidiMessage companionIdentity(const CompanionIdentity&, uint32_t session, uint16_t sequence);
bool parseCompanionIdentity(const CompanionPacket&, CompanionIdentity&);
MidiMessage companionState(const CompanionSnapshot&, uint32_t session, uint16_t sequence);
bool parseCompanionState(const CompanionPacket&, CompanionSnapshot&);
bool companionSequenceAfter(uint16_t newer, uint16_t older);

// One outstanding read request; controls remain no-replay mutations. The
// controller uses a new nonzero session on every connection/profile change.
// Endpoint source is a declaration, never proof of physical qualification.
class CompanionSession {
public:
    void begin(CompanionInstrument, uint32_t session, uint32_t now);
    void reset();
    MidiMessage discover(uint32_t now);
    MidiMessage requestState(uint32_t now);
    MidiMessage setParameter(uint8_t parameter, uint8_t value);
    MidiMessage setPreset(uint8_t preset);
    MidiMessage note(uint8_t note, uint8_t velocity);
    MidiMessage panic();
    bool receive(const uint8_t*, std::size_t, uint32_t now);
    void sent(const MidiMessage&, uint32_t now);
    void cancelPending();
    void tick(uint32_t now);
    bool ready() const { return ready_; }
    bool pending() const { return pending_; }
    bool timedOut() const { return timedOut_; }
    bool stateFresh(uint32_t now, uint32_t maximumAge = 1500) const;
    uint32_t session() const { return session_; }
    uint16_t lastReplySequence() const { return lastReply_; }
    uint16_t sequence() const { return sequence_; }
    const CompanionIdentity& identity() const { return identity_; }
    const CompanionSnapshot& snapshot() const { return snapshot_; }
    uint32_t rejected() const { return rejected_; }
private:
    CompanionInstrument instrument_ = CompanionInstrument::Unknown;
    uint32_t session_ = 0, requestAt_ = 0, receivedAt_ = 0, rejected_ = 0;
    uint16_t sequence_ = 0, requestSequence_ = 0, lastReply_ = 0;
    bool ready_ = false, pending_ = false, requestSent_ = false, timedOut_ = false, hasSnapshot_ = false, hasReply_ = false;
    CompanionOp pendingOp_ = CompanionOp::Discover;
    CompanionIdentity identity_{};
    CompanionSnapshot snapshot_{};
    MidiMessage command(CompanionOp, const uint8_t*, uint8_t);
};

// Transport-worker safety net: observe only bytes actually accepted by the
// transport. A queue reset must not forget a previously transmitted note.
// This is not discovery or authentication and does not trust reply provenance.
class CompanionReleaseTracker {
public:
    void reset();
    void sent(const MidiMessage&);
    MidiMessage panicMessage() const;
    bool needsRelease() const { return notesTouched_; }
private:
    uint32_t session_ = 0;
    uint16_t sequence_ = 0;
    bool notesTouched_ = false;
};
} // namespace fm1
