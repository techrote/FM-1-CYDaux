#include "fm1/midi.hpp"
namespace fm1 {
uint8_t midiMessageSize(uint8_t status) {
    if (status < 0x80) return 0;
    if (status < 0xf0) return (status & 0xe0) == 0xc0 ? 2 : 3;
    switch (status) {
        case 0xf1: case 0xf3: return 2;
        case 0xf2: return 3;
        case 0xf6: case 0xf8: case 0xfa: case 0xfb: case 0xfc:
        case 0xfe: case 0xff: return 1;
        default: return 0;
    }
}
bool validMidi(const uint8_t* p, std::size_t n) {
    if (!p || !n || n > kMaxMidiBytes) return false;
    if (p[0] == 0xf0) {
        if (n < 2 || p[n-1] != 0xf7) return false;
        for (std::size_t i=1; i+1<n; ++i) if (p[i] & 0x80) return false;
        return true;
    }
    if (midiMessageSize(p[0]) != n) return false;
    for (std::size_t i=1; i<n; ++i) if (p[i] & 0x80) return false;
    return true;
}
std::size_t encodeBleMidi(const MidiMessage& m, uint16_t time, uint8_t* out, std::size_t cap) {
    if (!out || !validMidi(m.data.data(),m.size) || m.data[0]==0xf0 || cap<m.size+2u) return 0;
    out[0]=0x80|((time>>7)&0x3f); out[1]=0x80|(time&0x7f);
    for (std::size_t i=0; i<m.size; ++i) out[i+2]=m.data[i];
    return m.size+2;
}
void BleMidiDecoder::reset() {} // Running status is local to each BLE packet.
void BleMidiDecoder::feed(const uint8_t* p,std::size_t n,Receiver receiver,void* ctx) {
    if (!p || !receiver || n<3 || (p[0]&0xc0)!=0x80) return;
    std::size_t i=1;
    uint8_t running=0;
    bool timestampRequired=true;
    while (i<n) {
        if (p[i]&0x80) {
            ++i; // TimestampLow, not a MIDI status.
            if (i>=n) return;
        } else if (timestampRequired) return;
        uint8_t status=p[i];
        if (status&0x80) {
            ++i;
            if (status==0xf0 || status==0xf7) return; // BLE SysEx is not in this subset.
            if (status<0xf0) running=status;
            // Unlike a serial MIDI byte stream, BLE System Common and Real-Time
            // preserve this packet's preceding channel running status.
        } else status=running;
        const uint8_t needed=midiMessageSize(status);
        if (!needed) return;
        uint8_t message[3]{status,0,0};
        for (uint8_t b=1;b<needed;++b) {
            if (i>=n || (p[i]&0x80)) return;
            message[b]=p[i++];
        }
        receiver(message,needed,ctx);
        // Ordinary running messages may share a timestamp. Resuming after any
        // System Common or Real-Time message requires a new timestamp byte.
        timestampRequired=status>=0xf0;
    }
}
} // namespace fm1
