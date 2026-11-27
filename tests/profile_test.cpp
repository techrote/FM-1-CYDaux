#include "fm1/profile.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
void check(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "profile/protocol: " << message << '\n';
        std::exit(1);
    }
}
using Bytes = std::vector<uint8_t>;
Bytes infoFixture() {
    // Synthetic wire fixture from the pinned serializer; not a hardware capture.
    return {0xf0, 0x7d, 0x46, 0x4c, 0x01, 0x46, 0x45, 0x4c, 0x55, 0x43, 0x43, 0x41, 0x20, 0x53, 0x4c, 0x4f, 0x4f, 0x50, 0x20, 0x32, 0x2e, 0x38, 0x2e, 0x35, 0x00, 0x0c, 0x47, 0x32, 0x40, 0x3f, 0x41, 0x4e, 0x41, 0x4c, 0x4f, 0x47, 0x00, 0x44, 0x49, 0x47, 0x49, 0x54, 0x41, 0x4c, 0x00, 0x50, 0x48, 0x41, 0x53, 0x45, 0x00, 0x4c, 0x4f, 0x46, 0x49, 0x00, 0x53, 0x41, 0x4d, 0x50, 0x4c, 0x45, 0x00, 0x56, 0x4f, 0x49, 0x43, 0x45, 0x00, 0x54, 0x52, 0x49, 0x4f, 0x00, 0x57, 0x48, 0x45, 0x45, 0x4c, 0x00, 0x47, 0x52, 0x41, 0x49, 0x4e, 0x00, 0x46, 0x4d, 0x36, 0x00, 0x50, 0x48, 0x59, 0x53, 0x00, 0x4e, 0x4f, 0x49, 0x53, 0x45, 0x00, 0x04, 0x0c, 0xf7};
}
Bytes liveFixture() {
    // Upstream ui_pages_test.c counts 50 bytes including op, not after it.
    return {0xf0, 0x7d, 0x46, 0x4c, 0x4d, 0x00, 0x01, 0x01, 0x78, 0x40, 0x01, 0x00, 0x00, 0x00, 0x05, 0x02, 0x02, 0x02, 0x00, 0x22, 0x00, 0x00, 0x00, 0x01, 0x01, 0x03, 0x10, 0x4b, 0x07, 0x20, 0x58, 0x00, 0x40, 0x00, 0x0b, 0x10, 0x60, 0x00, 0x00, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0xf7};
}
void replaceName(Bytes& frame, const std::string& name) {
    const auto end = std::find(frame.begin() + 5, frame.end(), uint8_t(0));
    frame.erase(frame.begin() + 5, end);
    frame.insert(frame.begin() + 5, name.begin(), name.end());
}
void checkInfoRejected(const Bytes& bytes, const char* message) {
    uint8_t version = 99;
    bool sloop = true;
    check(!fm1::parseEditorInfo(bytes.data(), bytes.size(), version, sloop), message);
    check(version == 0 && !sloop, "rejected INFO clears capability outputs");
}
void checkLiveRejected(const Bytes& bytes, const char* message) {
    fm1::SloopLiveState state;
    state.bpm = 211;
    state.mutes = 6;
    state.steps[0] = 9;
    check(!fm1::parseSloopLive(bytes.data(), bytes.size(), state), message);
    check(state.bpm == 211 && state.mutes == 6 && state.steps[0] == 9,
          "rejected LIVE must not partially mutate state");
}
void testProfiles() {
    check(fm1::profileCount() == 7, "four legacy profiles plus three negotiated native endpoints");
    check(fm1::profileIndex(nullptr) == -1 && fm1::profileIndex("unknown") == -1,
          "unknown profile id");
    const uint8_t expected[4][8] = {
        {74, 71, 73, 72, 7, 10, 75, 5},
        {74, 71, 73, 72, 7, 10, 91, 94},
        {74, 71, 73, 72, 7, 10, 91, 94},
        {16, 17, 18, 19, 20, 21, 22, 23}
    };
    for (std::size_t p = 0; p < fm1::profileCount(); ++p) {
        const auto& profile = fm1::profileAt(p);
        check(fm1::profileIndex(profile.id) == int(p), "profile id round-trip");
        check(profile.tracks > 0 && profile.tracks <= fm1::kMaxTracks, "track bound");
        for (std::size_t t = 0; t < profile.tracks; ++t)
            check(profile.channels[t] >= 1 && profile.channels[t] <= 16 &&
                  profile.trackNames[t] && *profile.trackNames[t], "human MIDI channels");
        for (std::size_t v = 0; v < fm1::kProfileControls; ++v) {
            if (p < 4) check(profile.parameters[v].cc == expected[p][v], "audited legacy CC map");
            check(profile.parameters[v].initial <= 127, "demo initial range");
        }
    }
    check(&fm1::profileAt(999) == &fm1::profileAt(0), "safe out-of-range lookup");
    check(fm1::profileAt(0).directBle && !fm1::profileAt(0).transport &&
          !fm1::profileAt(0).programChange, "Melodee capabilities");
    check(fm1::profileAt(1).channels[3] == 10 && fm1::profileAt(1).sloopLive &&
          !fm1::profileAt(1).transport && fm1::profileAt(1).programChange,
          "SLOOP drums and capability-gated LIVE");
    check(fm1::profileAt(1).parameterMask[3] == 0xf1 &&
          fm1::profileAt(1).parameterMask[0] == 0xff,
          "SLOOP drums exclude resonance, attack and release");
    check(fm1::profileAt(0).parameterMask[0] == 0xff &&
          fm1::profileAt(2).parameterMask[5] == 0xff &&
          fm1::profileAt(3).parameterMask[1] == 0xff, "ordinary track control mask");
    check(fm1::profileAt(2).tracks == 6 && !fm1::profileAt(2).transport &&
          fm1::profileAt(2).programChange, "Skiff transport needs a clock source");
    check(fm1::profileAt(3).tracks == 2 && fm1::profileAt(3).channels[0] == 2 &&
          fm1::profileAt(3).channels[1] == 3 && fm1::profileAt(3).transport &&
          !fm1::profileAt(3).programChange, "X0X is explicitly the 303 pair");
    for (std::size_t p = 1; p < fm1::profileCount(); ++p)
        check(!fm1::profileAt(p).directBle, "do not advertise unimplemented BLE");
}
void testInfo() {
    const auto good = infoFixture();
    uint8_t version = 0;
    bool sloop = false;
    check(fm1::parseEditorInfo(good.data(), good.size(), version, sloop) &&
          version == 12 && sloop, "current SLOOP INFO");
    for (std::size_t n = 0; n < good.size(); ++n)
        checkInfoRejected(Bytes(good.begin(), good.begin() + n), "truncated INFO");
    check(!fm1::parseEditorInfo(nullptr, 255, version, sloop), "null INFO");
    auto bad = good;
    bad[1] = 0x7e;
    checkInfoRejected(bad, "wrong manufacturer envelope");
    bad = good;
    bad[4] = 77;
    checkInfoRejected(bad, "wrong INFO command");
    bad = good;
    bad[6] = 0x80;
    checkInfoRejected(bad, "status inside INFO body");
    bad = good;
    replaceName(bad, "NOT SLOOP 2.8.5");
    check(fm1::parseEditorInfo(bad.data(), bad.size(), version, sloop) && !sloop,
          "substring is not a SLOOP identity");
    bad = good;
    replaceName(bad, "FELUCCA 1.0");
    check(fm1::parseEditorInfo(bad.data(), bad.size(), version, sloop) && !sloop,
          "shared FL envelope does not identify SLOOP");
    bad = good;
    replaceName(bad, "FELUCCA SLOOP 2.x");
    checkInfoRejected(bad, "malformed SLOOP version");
    bad = good;
    replaceName(bad, "FELUCCA SLOOP 2.6");
    checkInfoRejected(bad, "protocol 12 predates claimed firmware");
    bad[bad.size() - 2] = 11;
    check(fm1::parseEditorInfo(bad.data(), bad.size(), version, sloop) &&
          sloop && version == 11, "older SLOOP remains controller-only");
    bad = good;
    replaceName(bad, "FELUCCA SLOOP 2.7");
    check(fm1::parseEditorInfo(bad.data(), bad.size(), version, sloop) && sloop,
          "two-component upstream release version");
    bad = good;
    replaceName(bad, "FELUCCA SLOOP 2.8.5 BETA");
    check(fm1::parseEditorInfo(bad.data(), bad.size(), version, sloop) && sloop,
          "bounded BETA build identity");
    bad = good;
    replaceName(bad, std::string(25, 'A'));
    checkInfoRejected(bad, "overlong version string");
    bad = good;
    const auto nameEnd = std::find(bad.begin() + 5, bad.end(), uint8_t(0));
    const std::size_t counts = std::size_t(nameEnd - bad.begin()) + 1;
    bad[counts] = 13;
    checkInfoRejected(bad, "engine count mismatch");
    bad = good;
    bad[counts + 4] = 62;
    checkInfoRejected(bad, "parameter base mismatch");
    bad = good;
    bad[bad.size() - 3] = 6;
    check(fm1::parseEditorInfo(bad.data(), bad.size(), version, sloop) && !sloop,
          "LIVE layout requires four tracks");
    bad = good;
    bad.insert(bad.end() - 1, 0);
    checkInfoRejected(bad, "unexpected INFO tail");
    bad = good;
    bad.erase(bad.end() - 2);
    check(fm1::parseEditorInfo(bad.data(), bad.size(), version, sloop) &&
          version == 0, "legacy INFO without protocol cannot enable LIVE");
}
void testLive() {
    const auto good = liveFixture();
    fm1::SloopLiveState state;
    check(good.size() == 56, "serializer frame length");
    check(fm1::parseSloopLive(good.data(), good.size(), state), "current LIVE state");
    check(state.playing && state.recording && state.bpm == 120 && state.section == 1 &&
          state.mutes == 5 && state.solos == 2 && state.selectedTrack == 2,
          "global LIVE offsets");
    const std::array<uint8_t, 4> steps{{3, 7, 0, 11}}, lengths{{16, 32, 64, 16}},
                                 meters{{75, 88, 0, 96}};
    check(state.steps == steps && state.lengths == lengths && state.meters == meters,
          "all four LIVE track triplets");
    for (std::size_t n = 0; n < good.size(); ++n)
        checkLiveRejected(Bytes(good.begin(), good.begin() + n), "truncated LIVE");
    check(!fm1::parseSloopLive(nullptr, good.size(), state), "null LIVE");
    auto bad = good;
    bad.insert(bad.end() - 1, 0);
    checkLiveRejected(bad, "reject documentation's off-by-one state length");
    bad = good;
    bad[5] = 2;
    checkLiveRejected(bad, "unrequested LIVE operation");
    bad = good;
    bad[6 + 11] = 4;
    checkLiveRejected(bad, "track index outside four tracks");
    bad = good;
    bad[6 + 8] = 16;
    checkLiveRejected(bad, "invalid mute mask");
    bad = good;
    bad[6 + 1] = 16;
    checkLiveRejected(bad, "invalid secondary flags");
    bad = good;
    bad[6 + 20] = 0;
    checkLiveRejected(bad, "zero pattern length");
    bad = good;
    bad[6 + 19] = 64;
    checkLiveRejected(bad, "step outside supported pattern");
    bad = good;
    bad[6 + 21] = 128;
    checkLiveRejected(bad, "non-seven-bit meter");
    for (int bpm : {40, 240}) {
        auto frame = good;
        frame[8] = uint8_t((bpm + 8192) & 127);
        frame[9] = uint8_t((bpm + 8192) >> 7);
        check(fm1::parseSloopLive(frame.data(), frame.size(), state) &&
              state.bpm == bpm, "valid tempo boundary");
    }
    for (int bpm : {39, 241}) {
        auto frame = good;
        frame[8] = uint8_t((bpm + 8192) & 127);
        frame[9] = uint8_t((bpm + 8192) >> 7);
        checkLiveRejected(frame, "invalid tempo boundary");
    }
}
void testRequests() {
    auto info = fm1::editorInfoRequest();
    check(info.size == 6 && info.data[4] == 1 && info.data[5] == 0xf7, "INFO request");
    auto state = fm1::sloopLiveRequest(0);
    check(state.size == 7 && state.data[4] == 77 && state.data[5] == 0 &&
          state.data[6] == 0xf7, "LIVE read request");
    for (int value : {0, 1}) {
        auto play = fm1::sloopLiveRequest(1, value);
        check(play.size == 8 && play.data[5] == 1 && play.data[6] == value &&
              play.data[7] == 0xf7, "explicit transport request");
    }
    check(fm1::sloopLiveRequest(0, 0).size == 0 &&
          fm1::sloopLiveRequest(1).size == 0 &&
          fm1::sloopLiveRequest(1, 2).size == 0 &&
          fm1::sloopLiveRequest(2, 0).size == 0 &&
          fm1::sloopLiveRequest(255).size == 0, "unsupported LIVE mutations are unavailable");
}
} // namespace

int main() {
    testProfiles();
    testInfo();
    testLive();
    testRequests();
    std::cout << "profile/protocol tests passed\n";
    return 0;
}
