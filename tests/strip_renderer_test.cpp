#include "fm1/ui.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <vector>

namespace {
struct Frame:fm1::Canvas {
    std::array<uint16_t,320*240> pixels{};
    void fillRect(int x,int y,int w,int h,uint16_t color) override {
        assert(x>=0&&y>=0&&x+w<=320&&y+h<=240&&w>0&&h>0);
        for(int row=y;row<y+h;++row)
            std::fill(pixels.begin()+row*320+x,pixels.begin()+row*320+x+w,color);
    }
};
struct Strips:fm1::Canvas {
    // Mirrors firmware DisplayCanvas without display hardware or SPI timing.
    static constexpr int rows=40;
    std::array<uint16_t,320*rows> pixels{};
    int top=0;
    void fillRect(int x,int y,int w,int h,uint16_t color) override {
        const int left=std::max(0,x),right=std::min(320,x+w);
        const int first=std::max(top,y),last=std::min(top+rows,y+h);
        if(left>=right||first>=last)return;
        for(int row=first;row<last;++row)
            std::fill(pixels.begin()+(row-top)*320+left,pixels.begin()+(row-top)*320+right,color);
    }
};
}
int main() {
    using namespace fm1;
    Ui ui(nullptr);
    UiModel model;
    model.firmware="ZAAGGENZ";model.profileIndex=4;model.companionEnabled=true;
    model.companionReady=true;model.nativeSupported=true;model.notesSupported=true;model.xyTouchGate=true;
    model.sequenceSupported=true;model.programSupported=true;model.connection=ConnectionState::Connected;
    model.transport=TransportKind::Native;model.xyOriginX=model.xyOriginY=ValueOrigin::Native;
    model.telemetryOrigin=ValueOrigin::Native;model.telemetryAvailable=true;model.meterCount=6;
    model.telemetryMeters={10,25,55,80,100,127};model.outputPeak=127;
    model.presetCount=8;model.presetIndex=7;
    for(auto& name:model.presetNames)name="HARMONIC RIP";
    for(auto& c:model.controls){c.label="CHARACTER";c.value=91;c.origin=ValueOrigin::Native;
        std::snprintf(c.display,sizeof(c.display),"CRUSHED TEETH");}
    PerformanceSequencer seq;model.sequence=seq.view();model.sequence.running=true;model.sequence.playhead=15;
    Frame full,assembled;
    Strips strip;
    unsigned scenes=0;
    auto verify=[&] {
        ui.render(full,model);
        for(int y=0;y<240;y+=Strips::rows){
            strip.top=y;ui.render(strip,model);
            std::copy(strip.pixels.begin(),strip.pixels.end(),assembled.pixels.begin()+y*320);
        }
        assert(full.pixels==assembled.pixels);++scenes;
    };
    for(unsigned theme=0;theme<3;++theme){
        model.themeIndex=theme;
        for(unsigned page=0;page<6;++page){model.page=static_cast<Page>(page);verify();}
        model.page=Page::XY;model.fullscreenXY=true;
        for(const auto xy:std::array<std::array<uint8_t,2>,5>{{{0,0},{127,0},{0,127},{127,127},{64,64}}}){
            model.xyX=xy[0];model.xyY=xy[1];verify();
        }
        model.fullscreenXY=false;
    }
    // The Grimoire ornament is deterministic in both renderer paths and
    // must neither consume stale meters nor claim oscillator phase.
    const auto legacyProfile=model.profileIndex;
    model.profileIndex=profileIndex("grimoire-native");
    model.page=Page::XY;model.xyEnabled=true;
    model.xyOriginX=model.xyOriginY=ValueOrigin::Native;
    model.xyX=64;model.xyY=64;model.meterCount=2;
    for(unsigned theme=0;theme<3;++theme){
        model.themeIndex=theme;
        for(bool fullscreen:{false,true}){
            model.fullscreenXY=fullscreen;
            model.telemetryAvailable=true;model.telemetryOrigin=ValueOrigin::Native;
            model.telemetryMeters={0,0,0,0,0,0};verify();
            const auto quiet=full.pixels;
            model.telemetryMeters={127,90,0,0,0,0};verify();
            assert(quiet!=full.pixels);
            model.telemetryAvailable=false;verify();
            const auto stale=full.pixels;
            // In compact mode the global header can still display other state;
            // compare the actual orbit pad, not unrelated header pixels.
            auto sameOrbit=[&]{
                const int left=fullscreen?0:8, top=fullscreen?36:60;
                const int right=fullscreen?320:226, bottom=fullscreen?240:196;
                for(int y=top;y<bottom;++y)
                    assert(std::equal(stale.begin()+y*320+left,stale.begin()+y*320+right,
                                      full.pixels.begin()+y*320+left));
            };
            model.telemetryMeters={0,0,0,0,0,0};verify();
            sameOrbit();
            model.telemetryAvailable=true;model.telemetryOrigin=ValueOrigin::Demo;
            model.telemetryMeters={127,127,0,0,0,0};verify();
            sameOrbit();
            model.telemetryOrigin=ValueOrigin::Native;model.meterCount=1;verify();
            sameOrbit();
            model.meterCount=2;
            for(const auto xy:std::array<std::array<uint8_t,2>,4>{{{0,0},{127,0},{0,127},{127,127}}}){
                model.xyX=xy[0];model.xyY=xy[1];verify();
            }
        }
    }
    model.profileIndex=legacyProfile;model.fullscreenXY=false;
    model.telemetryAvailable=true;model.telemetryOrigin=ValueOrigin::Native;model.meterCount=6;
    model.setupActive=true;std::snprintf(model.setupSsid,sizeof(model.setupSsid),"CYDaux-AbCd");
    std::snprintf(model.setupPassword,sizeof(model.setupPassword),"caseSensitive");verify();
    model.setupActive=false;model.companionEnabled=false;
    for(unsigned page=0;page<4;++page){model.page=static_cast<Page>(page);verify();}
    // Timings are host CPU rasterization only: no screen bus, touch, audio or
    // ESP32 is measured. Preserve this boundary in any reports using output.
    model.companionEnabled=true;model.page=Page::XY;model.fullscreenXY=true;
    std::vector<double> times;times.reserve(120);
    for(int run=0;run<120;++run){
        const auto start=std::chrono::steady_clock::now();
        for(int y=0;y<240;y+=Strips::rows){strip.top=y;ui.render(strip,model);}
        const auto end=std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double,std::micro>(end-start).count());
    }
    std::sort(times.begin(),times.end());
    std::cout<<"Strip renderer: PASS "<<scenes<<" scenes pixel-identical to full framebuffer; strip bytes="
        <<sizeof(strip.pixels)<<" host_CPU_only_us median="<<times[times.size()/2]
        <<" p95="<<times[times.size()*95/100]<<" max="<<times.back()<<'\n';
}
