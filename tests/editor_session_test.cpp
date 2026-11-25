#include "fm1/controller.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace fm1;
using Bytes = std::vector<uint8_t>;
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "editor session: " << message << '\n'; std::exit(1); }
}
Bytes info(const char* name = "FELUCCA SLOOP 2.8.5") {
    Bytes frame{0xf0, 0x7d, 0x46, 0x4c, 1};
    auto string = [&frame](const char* text) {
        while (*text) frame.push_back(static_cast<uint8_t>(*text++));
        frame.push_back(0);
    };
    string(name);
    const uint8_t counts[]{12, 71, 50, 64, 63};
    frame.insert(frame.end(), std::begin(counts), std::end(counts));
    for (const char* engine : {"ANALOG", "DIGITAL", "PHASE", "LOFI", "SAMPLE",
                              "VOICE", "TRIO", "WHEEL", "GRAIN", "FM6", "PHYS", "NOISE"})
        string(engine);
    frame.insert(frame.end(), {4, 12, 0xf7});
    return frame;
}
Bytes live(uint8_t op, bool playing = false, uint16_t bpm = 120) {
    // Source-shaped fixture; the values describe this test, not a physical FM-1.
    Bytes frame(56, 0);
    const uint8_t header[]{0xf0, 0x7d, 0x46, 0x4c, 77, op};
    std::copy(std::begin(header), std::end(header), frame.begin());
    frame[6] = playing ? 1 : 0;
    frame[8] = static_cast<uint8_t>((bpm + 8192) & 127);
    frame[9] = static_cast<uint8_t>((bpm + 8192) >> 7);
    for (std::size_t track = 0; track < 4; ++track) frame[6 + 20 + 3 * track] = 16;
    std::fill(frame.begin() + 6 + 33, frame.end() - 1, uint8_t(72));
    frame.back() = 0xf7;
    return frame;
}
void receive(Controller& c, const Bytes& bytes) { c.receive(bytes.data(), bytes.size()); }
void noMidi(Controller& c, const char* message) {
    MidiMessage out;
    check(!c.popMidi(out), message);
}
MidiMessage onlyMessage(Controller& c, bool sent = true) {
    MidiMessage out;
    check(c.popMidi(out), "expected outgoing message");
    noMidi(c, "only one request may be queued");
    if (sent) c.didSend(out);
    return out;
}
void expectEditor(const MidiMessage& m, uint8_t command, int operation = -1, int argument = -1) {
    check(m.size >= 6 && m.data[0] == 0xf0 && m.data[1] == 0x7d &&
          m.data[2] == 0x46 && m.data[3] == 0x4c && m.data[4] == command &&
          m.data[m.size - 1] == 0xf7, "expected editor envelope");
    if (operation < 0) check(m.size == 6, "INFO has no request payload");
    else {
        check(m.data[5] == operation, "expected LIVE operation");
        check(m.size == (argument < 0 ? 7 : 8), "expected LIVE request size");
        if (argument >= 0) check(m.data[6] == argument, "explicit play/stop value");
    }
}
void connectSloop(Controller& c) {
    c.selectProfile(static_cast<std::size_t>(profileIndex("sloop")));
    c.dispatch({ActionType::SelectTransport, static_cast<int16_t>(TransportKind::Wifi)});
    c.setConnection(ConnectionState::Connected);
}
void ready(Controller& c, bool playing = false) {
    connectSloop(c);
    expectEditor(onlyMessage(c), 1);
    receive(c, info());
    check(c.model().syncSupported && !c.model().playingKnown && !c.model().transportSupported,
          "INFO alone cannot establish transport state");
    expectEditor(onlyMessage(c), 77, 0);
    receive(c, live(0, playing));
    check(c.model().syncSupported && c.model().playingKnown && c.model().transportSupported,
          "valid LIVE reply establishes transport readiness");
}

void testIdentityAndPendingSend() {
    Controller c;
    connectSloop(c);
    receive(c, info());
    check(!c.model().syncSupported, "unsent INFO request cannot accept an unsolicited reply");
    const auto request = onlyMessage(c, false);
    expectEditor(request, 1);
    receive(c, info());
    check(!c.model().syncSupported, "popped but unsent INFO is not pending on the device");
    c.didSend(request);
    receive(c, info());
    check(c.model().syncSupported && !c.model().transportSupported, "sent INFO permits identity only");
    c.dispatch({ActionType::Refresh});
    c.tick(500);
    const auto read = onlyMessage(c, false);
    expectEditor(read, 77, 0);
    receive(c, live(0));
    check(!c.model().playingKnown, "LIVE before send is unsolicited");
    c.didSend(read);
    receive(c, live(1, true));
    check(!c.model().playingKnown, "wrong LIVE operation is ignored");
    auto malformed = live(0);
    malformed[6 + 11] = 4;
    receive(c, malformed);
    check(!c.model().playingKnown, "malformed LIVE cannot establish state");
    c.tick(999);
    c.dispatch({ActionType::Refresh});
    c.dispatch({ActionType::TogglePlay});
    noMidi(c, "outstanding LIVE blocks another editor request");
    receive(c, live(0, false, 133));
    check(c.model().playingKnown && !c.model().playing && c.model().bpm == 133 &&
          c.model().transportSupported, "matching valid LIVE finishes request");
    receive(c, live(0, true, 144));
    check(!c.model().playing && c.model().bpm == 133, "unsolicited LIVE cannot overwrite state");
}

void testReadTimeout() {
    Controller c;
    ready(c);
    c.tick(250);
    expectEditor(onlyMessage(c), 77, 0);
    c.tick(1249);
    noMidi(c, "read is not duplicated before timeout");
    c.tick(1250);
    check(!c.model().playingKnown && !c.model().transportSupported,
          "timeout invalidates playing state");
    expectEditor(onlyMessage(c), 77, 0);
    c.tick(1251);
    noMidi(c, "one recovery poll remains outstanding");
    receive(c, live(0, true, 126));
    check(c.model().playingKnown && c.model().playing && c.model().bpm == 126,
          "read polling recovers from a lost reply");
}

void testPlayTimeoutNeverReplays() {
    Controller c;
    ready(c);
    c.dispatch({ActionType::TogglePlay});
    expectEditor(onlyMessage(c), 77, 1, 1);
    check(!c.model().playing, "sending play does not forge device state");
    for (int i = 0; i < 5; ++i) c.dispatch({ActionType::TogglePlay});
    c.tick(999);
    noMidi(c, "repeated input cannot replay an outstanding play");
    c.tick(1000);
    expectEditor(onlyMessage(c), 77, 0);
    check(!c.model().playingKnown, "timed-out play must reconcile with a read");
    receive(c, live(1, true));
    check(!c.model().playingKnown, "late play reply cannot satisfy a state read");
    receive(c, live(0, true));
    check(c.model().playingKnown && c.model().playing, "read reconciles uncertain play result");
    c.dispatch({ActionType::TogglePlay});
    expectEditor(onlyMessage(c), 77, 1, 0);
    receive(c, live(1, false));
    check(c.model().playingKnown && !c.model().playing, "explicit stop is confirmed by reply");
}

void testInfoTimeoutAndWrongFirmware() {
    Controller c;
    connectSloop(c);
    expectEditor(onlyMessage(c), 1);
    c.tick(1000);
    noMidi(c, "unconfirmed identity does not trigger LIVE polling");
    receive(c, info());
    check(!c.model().syncSupported, "late INFO after timeout is ignored");
    c.dispatch({ActionType::Refresh});
    expectEditor(onlyMessage(c), 1);
    receive(c, info("FELUCCA SKIFF 2.7.1"));
    check(!c.model().syncSupported && !c.model().transportSupported,
          "FL envelope from other firmware does not enable LIVE");
    noMidi(c, "other firmware is controller-only");
    c.dispatch({ActionType::SetControl, 0, 81});
    const auto cc = onlyMessage(c);
    check(cc.size == 3 && cc.data[0] == 0xb0 && cc.data[1] == 74 && cc.data[2] == 81,
          "ordinary MIDI controls still work without editor capability");
}

void testReconnectClearsCapabilities() {
    Controller c;
    ready(c, true);
    c.dispatch({ActionType::TogglePlay});
    expectEditor(onlyMessage(c), 77, 1, 0);
    c.setConnection(ConnectionState::Offline);
    check(!c.model().syncSupported && !c.model().playingKnown && !c.model().transportSupported,
          "disconnect clears capability and state");
    receive(c, live(1, false));
    c.setConnection(ConnectionState::Connected);
    receive(c, live(0, true));
    receive(c, info());
    check(!c.model().syncSupported && !c.model().playingKnown,
          "old replies before new request send do not re-enable capabilities");
    expectEditor(onlyMessage(c), 1);
    receive(c, info());
    expectEditor(onlyMessage(c), 77, 0);
    receive(c, live(0, false));
    check(c.model().syncSupported && c.model().transportSupported && !c.model().playing,
          "fresh handshake restores capabilities");
    c.selectProfile(static_cast<std::size_t>(profileIndex("skiff")));
    check(!c.model().syncSupported && !c.model().playingKnown &&
          c.model().connection == ConnectionState::Offline, "profile switch closes editor session");
    noMidi(c, "profile switch does not replay old editor work");
}

void testPerTrackControlMask() {
    Controller c;
    connectSloop(c);
    expectEditor(onlyMessage(c), 1);
    c.dispatch({ActionType::SelectTrack, 3});
    check(c.model().controls[0].enabled && !c.model().controls[1].enabled &&
          !c.model().controls[2].enabled && !c.model().controls[3].enabled &&
          !c.model().xyEnabled, "SLOOP drum mask reaches the view model");
    for (int control = 1; control < 4; ++control)
        c.dispatch({ActionType::SetControl, static_cast<int16_t>(control), 127});
    c.dispatch({ActionType::SetXY, 100, 100});
    noMidi(c, "disabled drum controls and XY do not emit unsupported CCs");
    c.dispatch({ActionType::SetControl, 0, 88});
    auto cc = onlyMessage(c);
    check(cc.size == 3 && cc.data[0] == 0xb9 && cc.data[1] == 74 && cc.data[2] == 88,
          "drum tone uses supported channel10 filter");
    c.dispatch({ActionType::NextControlBank});
    for (const auto& control : c.model().controls) check(control.enabled, "drum second bank is supported");
    c.dispatch({ActionType::SetLevel, 3, 77});
    cc = onlyMessage(c);
    check(cc.data[0] == 0xb9 && cc.data[1] == 7 && cc.data[2] == 77,
          "drum mix level remains available");
    c.dispatch({ActionType::SelectTrack, 0});
    check(c.model().xyEnabled, "synth track restores XY capability");
}
} // namespace

int main() {
    testIdentityAndPendingSend();
    testReadTimeout();
    testPlayTimeoutNeverReplays();
    testInfoTimeoutAndWrongFirmware();
    testReconnectClearsCapabilities();
    testPerTrackControlMask();
    std::cout << "editor session tests passed\n";
}
