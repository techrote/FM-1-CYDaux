#pragma once
#include "fm1/profile.hpp"
#include <cstddef>
#include <cstdint>
namespace fm1 {
bool validMidi(const uint8_t*, std::size_t);
uint8_t midiMessageSize(uint8_t status);
std::size_t encodeBleMidi(const MidiMessage&, uint16_t timestamp, uint8_t*, std::size_t capacity);
class BleMidiDecoder {
public:
    using Receiver = void (*)(const uint8_t*, std::size_t, void*);
    void feed(const uint8_t*, std::size_t, Receiver, void*);
    void reset();
private: uint8_t running_ = 0;
};
} // namespace fm1
