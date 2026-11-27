#include "fm1/midi.hpp"
#include <cstdio>
#include <vector>

namespace {
// BLE-MIDI v1.0a running-status rules are documented by the upstream transport:
// https://github.com/lathoub/Arduino-BLE-MIDI/blob/master/src/BLEMIDI_Transport.h
// These byte fixtures are original examples of those rules, not hardware captures.
using Messages = std::vector<fm1::MidiMessage>;
void receive(const uint8_t* bytes, std::size_t size, void* context) {
    fm1::MidiMessage message;
    message.size = size;
    for (std::size_t i = 0; i < size; ++i) message.data[i] = bytes[i];
    static_cast<Messages*>(context)->push_back(message);
}
int failures = 0;
void check(bool condition, const char* description) {
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "BLE review failure: %s\n", description);
    }
}
} // namespace

int main() {
    {
        fm1::BleMidiDecoder decoder;
        Messages messages;
        const uint8_t first[]{0x80, 0x80, 0xb0, 7, 64};
        const uint8_t orphan[]{0x80, 0x81, 7, 65};
        decoder.feed(first, sizeof(first), receive, &messages);
        decoder.feed(orphan, sizeof(orphan), receive, &messages);
        check(messages.size() == 1, "running status leaked across BLE packet boundary");
    }
    {
        fm1::BleMidiDecoder decoder;
        Messages messages;
        const uint8_t common[]{
            0x80, 0x80, 0xb0, 7, 64, 0x81, 0xf2, 1, 2, 0x82, 7, 65,
        };
        decoder.feed(common, sizeof(common), receive, &messages);
        check(messages.size() == 3 && messages.back().data[0] == 0xb0 &&
              messages.back().data[2] == 65,
              "system-common message cancelled valid timestamped running status");
    }
    {
        fm1::BleMidiDecoder decoder;
        Messages messages;
        const uint8_t realTime[]{
            0x80, 0x80, 0xb0, 7, 64, 0x81, 0xf8, 0x82, 7, 65,
        };
        decoder.feed(realTime, sizeof(realTime), receive, &messages);
        check(messages.size() == 3 && messages[1].data[0] == 0xf8 &&
              messages.back().data[0] == 0xb0,
              "timestamped running status after clock was not decoded");
    }
    {
        fm1::BleMidiDecoder decoder;
        Messages messages;
        const uint8_t missingTimestamp[]{
            0x80, 0x80, 0xb0, 7, 64, 0x81, 0xf8, 7, 65,
        };
        decoder.feed(missingTimestamp, sizeof(missingTimestamp), receive, &messages);
        check(messages.size() == 2,
              "running status resumed after a system message without mandatory timestamp");
    }
    {
        fm1::BleMidiDecoder decoder;
        Messages messages;
        const uint8_t sharedTimestamp[]{0x80, 0x80, 0xb0, 7, 64, 10, 32, 74, 90};
        decoder.feed(sharedTimestamp, sizeof(sharedTimestamp), receive, &messages);
        check(messages.size() == 3 && messages.back().data[1] == 74,
              "same-timestamp ordinary running status stopped working");
    }
    if (failures) return 1;
    std::puts("BLE packet-boundary and interspersed-system-message regressions passed");
}
