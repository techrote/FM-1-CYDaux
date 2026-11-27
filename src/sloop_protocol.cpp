#include "fm1/profile.hpp"
#include <cstring>

namespace fm1 {
namespace {
constexpr uint8_t kInfo = 1;
constexpr uint8_t kLive = 77;
constexpr std::size_t kLiveFrameBytes = 56; // op + 49 state bytes, not 50 after op.

bool envelope(const uint8_t* data, std::size_t size, uint8_t command) {
    if (!data || size < 6 || size > kMaxMidiBytes ||
        data[0] != 0xf0 || data[1] != 0x7d || data[2] != 0x46 ||
        data[3] != 0x4c || data[4] != command || data[size - 1] != 0xf7)
        return false;
    for (std::size_t i = 1; i + 1 < size; ++i)
        if (data[i] > 127) return false;
    return true;
}

bool readString(const uint8_t* data, std::size_t end, std::size_t& pos,
                std::size_t maximum, const char*& value) {
    const std::size_t start = pos;
    while (pos < end && data[pos] != 0) {
        if (pos - start >= maximum || data[pos] < 32 || data[pos] > 126)
            return false;
        ++pos;
    }
    if (pos == end || pos == start) return false;
    value = reinterpret_cast<const char*>(data + start);
    ++pos;
    return true;
}

bool versionNumber(const char*& at, unsigned& value) {
    if (*at < '0' || *at > '9') return false;
    value = 0;
    while (*at >= '0' && *at <= '9') {
        value = value * 10u + static_cast<unsigned>(*at++ - '0');
        if (value > 999u) return false;
    }
    return true;
}

bool sloopVersion(const char* name, unsigned& major, unsigned& minor) {
    // The audited serializer prepends FELUCCA to FELUCCA_VERSION.
    constexpr char prefix[] = "FELUCCA SLOOP ";
    if (std::strncmp(name, prefix, sizeof(prefix) - 1) != 0) return false;
    const char* at = name + sizeof(prefix) - 1;
    unsigned patch = 0;
    if (!versionNumber(at, major) || *at++ != '.' || !versionNumber(at, minor))
        return false;
    if (*at == '.') {
        ++at;
        if (!versionNumber(at, patch)) return false;
    }
    return major != 0 && (*at == 0 || std::strcmp(at, " BETA") == 0);
}

MidiMessage request(uint8_t command) {
    MidiMessage message;
    message.data[0] = 0xf0;
    message.data[1] = 0x7d;
    message.data[2] = 0x46;
    message.data[3] = 0x4c;
    message.data[4] = command;
    return message;
}
} // namespace

MidiMessage editorInfoRequest() {
    auto message = request(kInfo);
    message.data[5] = 0xf7;
    message.size = 6;
    return message;
}

bool parseEditorInfo(const uint8_t* data, std::size_t size,
                     uint8_t& protocol, bool& isSloop) {
    protocol = 0;
    isSloop = false;
    if (!envelope(data, size, kInfo)) return false;
    std::size_t pos = 5;
    const std::size_t end = size - 1;
    const char* name = nullptr;
    if (!readString(data, end, pos, 24, name) || end - pos < 5) return false;
    const uint8_t engines = data[pos++];
    const uint8_t parameters = data[pos++];
    const uint8_t globals = data[pos++];
    const uint8_t steps = data[pos++];
    const uint8_t engineBase = data[pos++];
    if (engines == 0 || engines > 32 || parameters < 8 || globals == 0 ||
        steps == 0 || steps > 64 || unsigned(engineBase) + 8u != parameters)
        return false;
    for (uint8_t i = 0; i < engines; ++i) {
        const char* engineName = nullptr;
        if (!readString(data, end, pos, 8, engineName)) return false;
    }
    const std::size_t remaining = end - pos;
    if (remaining > 2) return false;
    uint8_t tracks = 0;
    uint8_t parsedProtocol = 0;
    if (remaining >= 1) {
        tracks = data[pos++];
        if (tracks == 0 || tracks > kMaxTracks) return false;
    }
    if (remaining == 2) {
        parsedProtocol = data[pos++];
        if (parsedProtocol < 5) return false;
    }
    unsigned major = 0, minor = 0;
    const bool identity = sloopVersion(name, major, minor);
    constexpr char sloopPrefix[] = "FELUCCA SLOOP ";
    if (!identity && std::strncmp(name, sloopPrefix, sizeof(sloopPrefix) - 1) == 0)
        return false; // A SLOOP-looking identity needs a well-formed version.
    if (identity && parsedProtocol >= 12 &&
        !(major > 2 || (major == 2 && minor >= 7)))
        return false; // LIVE did not exist before SLOOP 2.7.
    protocol = parsedProtocol;
    isSloop = identity && tracks == 4 && steps == 64;
    return true;
}

MidiMessage sloopLiveRequest(uint8_t operation, int argument) {
    // This project exposes only a read and explicit play/stop.
    // Toggle, record, section-store, and other mutations are intentionally absent.
    if ((operation == 0 && argument != -1) ||
        (operation == 1 && argument != 0 && argument != 1) || operation > 1)
        return {};
    auto message = request(kLive);
    message.data[5] = operation;
    std::size_t pos = 6;
    if (operation == 1) message.data[pos++] = static_cast<uint8_t>(argument);
    message.data[pos++] = 0xf7;
    message.size = static_cast<uint16_t>(pos);
    return message;
}

bool parseSloopLive(const uint8_t* data, std::size_t size, SloopLiveState& result) {
    if (size != kLiveFrameBytes || !envelope(data, size, kLive) || data[5] > 1)
        return false;
    const uint8_t* state = data + 6;
    const int bpm = int(state[2]) + (int(state[3]) << 7) - 8192;
    if (state[1] > 15 || bpm < 40 || bpm > 240 ||
        state[4] > 4 || state[5] > 4 || state[6] > 8 || state[7] >= 8 ||
        state[8] > 15 || state[9] > 15 || state[10] > 15 ||
        state[11] >= 4 || state[12] > 27 || state[18] > 15 || state[32] > 3)
        return false;
    SloopLiveState parsed;
    parsed.playing = (state[0] & 1u) != 0;
    // Includes armed recording and a free take, matching the LIVE status flags.
    parsed.recording = state[10] != 0 || (state[1] & 9u) != 0 || (state[0] & 64u) != 0;
    parsed.bpm = static_cast<uint16_t>(bpm);
    parsed.section = state[4]; // Wire convention: 0 none; 1..4 are A..D.
    parsed.mutes = state[8];
    parsed.solos = state[9];
    parsed.selectedTrack = state[11];
    for (std::size_t i = 0; i < 4; ++i) {
        const std::size_t at = 19 + 3 * i;
        if (state[at] >= 64 || state[at + 1] == 0 || state[at + 1] > 64)
            return false;
        parsed.steps[i] = state[at];
        parsed.lengths[i] = state[at + 1];
        parsed.meters[i] = state[at + 2];
    }
    result = parsed; // A rejected frame never partially overwrites prior state.
    return true;
}
} // namespace fm1
