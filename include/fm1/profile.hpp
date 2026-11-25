#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace fm1 {
constexpr std::size_t kMaxTracks = 8;
constexpr std::size_t kProfileControls = 8;
constexpr std::size_t kMaxMidiBytes = 256;
struct MidiMessage { std::array<uint8_t, kMaxMidiBytes> data{}; uint16_t size = 0; };
struct Parameter { const char* name; uint8_t cc; uint8_t initial; };
struct Profile {
    const char* id;
    const char* name;
    const char* description;
    const char* sourceVersion;
    uint8_t tracks;
    std::array<uint8_t, kMaxTracks> channels; // Human MIDI channels, 1..16.
    std::array<const char*, kMaxTracks> trackNames;
    std::array<Parameter, kProfileControls> parameters;
    bool directBle;
    bool transport; // Start/Stop works without this controller generating clock.
    bool sloopLive; // Also requires a SLOOP INFO protocol >= 12 reply.
    bool programChange;
    const char* setupHint;
    // Bit i permits control i on this track; engine-specific availability can be narrower.
    std::array<uint8_t, kMaxTracks> parameterMask{{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
};
std::size_t profileCount();
const Profile& profileAt(std::size_t index);
int profileIndex(const char* id);
struct SloopLiveState {
    bool playing = false, recording = false;
    uint16_t bpm = 120;
    uint8_t section = 0, mutes = 0, solos = 0, selectedTrack = 0;
    std::array<uint8_t, 4> steps{}, lengths{}, meters{};
};
MidiMessage editorInfoRequest();
bool parseEditorInfo(const uint8_t*, std::size_t, uint8_t& protocol, bool& isSloop);
MidiMessage sloopLiveRequest(uint8_t operation, int argument = -1);
bool parseSloopLive(const uint8_t*, std::size_t, SloopLiveState&);
} // namespace fm1
