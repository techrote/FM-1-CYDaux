#include "fm1/controller.hpp"
#include "fm1/ui.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
using namespace fm1;
namespace {
struct Raster:Canvas {
    std::array<uint16_t,320*240> pixels{};
    int top=0,rows=240;
    void fillRect(int x,int y,int w,int h,uint16_t color) override {
        assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=320&&y+h<=240);
        for(int row=std::max(y,top);row<std::min(y+h,top+rows);++row)
            std::fill_n(pixels.begin()+row*320+x,w,color);
    }
};
void dispatch(void* p,UiAction a){static_cast<Controller*>(p)->dispatch(a);}
}
int main(){
    Controller c; Ui ui(dispatch,&c);
    auto down=[&](int x,int y){ui.touch(TouchPhase::Down,x,y,c.model());};
    auto up=[&](int x,int y){ui.touch(TouchPhase::Up,x,y,c.model());};
    auto tap=[&](int x,int y){down(x,y);up(x,y);};
    c.dispatch({ActionType::SetPage,static_cast<int16_t>(Page::Link)});
    c.dispatch({ActionType::SelectTransport,static_cast<int16_t>(TransportKind::Wifi)});
    UiAction platform{ActionType::Refresh};
    while(c.takePlatformAction(platform)){}
    c.setConnection(ConnectionState::Connected);
    const auto initial=c.model().profileIndex;
    tap(130,43);
    assert(c.model().page==Page::Profiles&&c.model().profileIndex==initial);
    Raster full,strip;ui.render(full,c.model());
    strip.rows=40;
    for(int y=0;y<240;y+=40){strip.top=y;ui.render(strip,c.model());}
    assert(full.pixels==strip.pixels);
    // A drag ending over another row is not a selection.
    down(20,44);up(180,82);
    assert(c.model().page==Page::Profiles&&c.model().profileIndex==initial);
    tap(20,220);
    assert(c.model().page==Page::Link&&c.model().connection==ConnectionState::Connected);
    assert(!c.takePlatformAction(platform));
    tap(130,43);tap(20,44); // Same profile returns without disconnect.
    assert(c.model().page==Page::Link&&c.model().connection==ConnectionState::Connected);
    assert(!c.takePlatformAction(platform));
    c.dispatch({ActionType::NoteOn,60,100});
    assert(c.model().noteHeld);
    tap(130,43);
    assert(!c.model().noteHeld&&!c.model().sequence.running);
    tap(180,120); // index 5: Xenoresonator
    assert(c.model().page==Page::Link&&c.model().profileIndex==profileIndex("xenoresonator-native"));
    assert(c.model().connection==ConnectionState::Offline&&!c.model().companionReady);
    assert(c.takePlatformAction(platform)&&platform.type==ActionType::Disconnect);
    // Every currently registered profile is reachable from the same picker.
    unsigned choices=0;
    for(unsigned i=0;i<profileCount();++i){
        tap(130,43);
        assert(c.model().page==Page::Profiles);
        while(c.model().profileMenuOffset>i)tap(150,220);
        while(i>=unsigned(c.model().profileMenuOffset)+8)tap(260,220);
        const unsigned slot=i-c.model().profileMenuOffset;
        tap(30+156*(slot%2),45+38*(slot/2));
        assert(c.model().profileIndex==i&&c.model().page==Page::Link);
        assert(c.model().connection==ConnectionState::Offline);
        ++choices;
    }
    // Old quick cycling on the Play page is deliberately preserved.
    c.dispatch({ActionType::SetPage,static_cast<int16_t>(Page::Play)});
    const auto previous=c.model().profileIndex;
    tap(130,43);
    assert(c.model().profileIndex==(previous+1u)%profileCount());
    assert(c.model().page==Page::Play);
    std::cout<<"Profile picker: "<<choices<<" direct choices, pixel-identical strips, capture and explicit reconnect PASS\n";
}
