#include "fm1/controller.hpp"
#include <cassert>
#include <cstdio>

using namespace fm1;

int main() {
    Controller c;
    assert(c.model().xyOriginX==ValueOrigin::Demo && c.model().xyOriginY==ValueOrigin::Demo);
    c.dispatch({ActionType::SetXY,23,109});
    assert(c.model().xyX==23 && c.model().xyY==109);
    assert(c.model().controls[0].value==23 && c.model().controls[1].value==109);
    c.dispatch({ActionType::NextControlBank});
    c.dispatch({ActionType::SetControl,0,5});
    assert(c.model().xyX==23 && c.model().xyY==109); // Axes always use parameters 0/1.
    c.dispatch({ActionType::SelectTrack,1});
    assert(c.model().xyX==profileAt(0).parameters[0].initial);
    assert(c.model().xyY==profileAt(0).parameters[1].initial);
    c.dispatch({ActionType::SelectTrack,0});
    assert(c.model().xyX==23 && c.model().xyY==109);

    c.dispatch({ActionType::SelectTransport,2});
    c.setConnection(ConnectionState::Connected);
    assert(c.model().xyOriginX==ValueOrigin::Unknown && c.model().xyOriginY==ValueOrigin::Unknown);
    const auto& p=profileAt(0);
    uint8_t xFeedback[]{0xb0,p.parameters[0].cc,44};
    uint8_t yFeedback[]{0xb0,p.parameters[1].cc,88};
    c.receive(xFeedback,3);
    assert(c.model().xyX==44 && c.model().xyOriginX==ValueOrigin::Device);
    assert(c.model().xyOriginY==ValueOrigin::Unknown);
    c.receive(yFeedback,3);
    assert(c.model().xyY==88 && c.model().xyOriginY==ValueOrigin::Device);

    c.dispatch({ActionType::SetXY,31,95});
    assert(c.model().xyX==31 && c.model().xyY==95);
    assert(c.model().xyOriginX==ValueOrigin::Pending && c.model().xyOriginY==ValueOrigin::Pending);
    MidiMessage message;
    assert(c.popMidi(message) && message.data[1]==p.parameters[0].cc && message.data[2]==31);
    c.didSend(message);
    assert(c.model().xyOriginX==ValueOrigin::Sent && c.model().xyOriginY==ValueOrigin::Pending);
    assert(c.popMidi(message) && message.data[1]==p.parameters[1].cc && message.data[2]==95);
    c.didSend(message);
    assert(c.model().xyOriginY==ValueOrigin::Sent && !c.popMidi(message));
    c.dispatch({ActionType::SelectTrack,1});
    assert(c.model().xyOriginX==ValueOrigin::Unknown && c.model().xyOriginY==ValueOrigin::Unknown);
    c.dispatch({ActionType::SelectTrack,0});
    assert(c.model().xyX==31 && c.model().xyY==95);
    c.selectProfile(1);
    assert(c.model().xyOriginX==ValueOrigin::Unknown && c.model().xyOriginY==ValueOrigin::Unknown);
    std::puts("XY: independent axes, bank/track selection, readback and value origins PASS");
}
