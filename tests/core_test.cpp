#include "fm1/controller.hpp"
#include "fm1/midi.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace fm1;
static std::vector<MidiMessage> drain(Controller& c) {
    std::vector<MidiMessage> out; MidiMessage m;
    while (c.popMidi(m)) { out.push_back(m); c.didSend(m); }
    return out;
}
int main() {
    Controller c;
    assert(c.model().transport == TransportKind::Demo);
    c.dispatch({ActionType::SetControl,0,99});
    assert(c.model().controls[0].value == 99 && c.model().controls[0].origin == ValueOrigin::Demo);
    assert(drain(c).empty());
    c.dispatch({ActionType::SelectTransport,1});
    c.dispatch({ActionType::SetControl,0,40});
    assert(c.model().controls[0].value == -1 && drain(c).empty());
    c.setConnection(ConnectionState::Connected);
    c.dispatch({ActionType::SetControl,0,41});
    assert(c.model().controls[0].origin == ValueOrigin::Pending);
    auto messages = drain(c);
    assert(messages.size() == 1 && messages[0].data[1] == 74 && messages[0].data[2] == 41);
    assert(c.model().controls[0].origin == ValueOrigin::Sent);
    uint8_t feedback[]{0xb0,74,87}; c.receive(feedback,3);
    assert(c.model().controls[0].origin == ValueOrigin::Device && c.model().controls[0].value == 87);
    for (int i=0;i<100;++i) c.dispatch({ActionType::SetControl,0,static_cast<int16_t>(i)});
    messages=drain(c); assert(messages.size()==1 && messages[0].data[2]==99);
    c.dispatch({ActionType::NoteOn}); messages=drain(c);
    assert(messages.size()==1 && messages[0].data[0]==0x90);
    c.dispatch({ActionType::SelectTrack,1}); messages=drain(c);
    assert(messages.size()==1 && messages[0].data[0]==0x80); // Original channel released.
    c.dispatch({ActionType::NoteOn});
    c.setConnection(ConnectionState::Offline);
    c.setConnection(ConnectionState::Connected);
    assert(drain(c).empty() && !c.model().noteHeld); // No replay after reconnect.
    assert(c.model().controls[0].origin==ValueOrigin::Unknown);
    c.dispatch({ActionType::SelectTransport,0});
    c.dispatch({ActionType::NextProfile});
    c.dispatch({ActionType::SelectTransport,1});
    c.dispatch({ActionType::Connect});
    assert(c.model().connection!=ConnectionState::Connected);

    const uint8_t malformed[]{0xb0,74,0x80}; assert(!validMidi(malformed,3));
    assert(!validMidi(feedback,2)); assert(!validMidi(nullptr,3));
    const uint8_t clock[]{0xf8}; assert(validMidi(clock,1));
    const uint8_t reserved[]{0xf9}; assert(!validMidi(reserved,1));
    const uint8_t sx[]{0xf0,0x7d,1,2,0xf7}; assert(validMidi(sx,5));
    assert(!validMidi(sx,4));
    MidiMessage m; m.size=3; m.data[0]=0xb0;m.data[1]=74;m.data[2]=81;
    uint8_t bytes[10]{}; assert(encodeBleMidi(m,8191,bytes,sizeof(bytes))==5);
    assert(bytes[0]==0xbf && bytes[1]==0xff && bytes[2]==0xb0);
    assert(encodeBleMidi(m,0,bytes,4)==0);
    std::vector<MidiMessage> decoded;
    auto callback=[](const uint8_t* d,std::size_t n,void* v) {
        MidiMessage e; e.size=n; for (std::size_t i=0;i<n;++i) e.data[i]=d[i];
        static_cast<std::vector<MidiMessage>*>(v)->push_back(e);
    };
    BleMidiDecoder decoder; decoder.feed(bytes,5,callback,&decoded);
    assert(decoded.size()==1 && decoded[0].data[2]==81);
    const uint8_t batch[]{0x80,0x81,0xb0,74,1,0x82,74,2,74,3};
    decoder.feed(batch,sizeof(batch),callback,&decoded);
    assert(decoded.size()==4 && decoded.back().data[2]==3);
    const uint8_t truncated[]{0x80,0x81,0xb0,74};
    decoder.feed(truncated,sizeof(truncated),callback,&decoded);
    assert(decoded.size()==4);
    std::puts("core: state honesty, message bounds, queue coalescing, release and reconnect PASS");
}
