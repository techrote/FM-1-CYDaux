#include "fm1/ui.hpp"
#include <array>
#include <cassert>
#include <climits>
#include <cstdio>
#include <iostream>
#include <vector>

namespace {
struct Recording {
    std::vector<fm1::UiAction> actions;
    static void send(void* context, fm1::UiAction action) {
        static_cast<Recording*>(context)->actions.push_back(action);
    }
    void clear() { actions.clear(); }
    std::size_t count(fm1::ActionType type) const {
        std::size_t result=0;for(const auto& action:actions)if(action.type==type)++result;return result;
    }
};
struct CheckedCanvas:fm1::Canvas {
    uint64_t hash=1469598103934665603ULL;
    unsigned calls=0;
    void fillRect(int x,int y,int w,int h,uint16_t color) override {
        assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=320&&y+h<=240);
        for(const int part:{x,y,w,h,static_cast<int>(color)})
            hash=(hash^static_cast<uint64_t>(part))*1099511628211ULL;
        ++calls;
    }
};
void tap(fm1::Ui& ui,const fm1::UiModel& model,int x,int y) {
    ui.touch(fm1::TouchPhase::Down,x,y,model);
    ui.touch(fm1::TouchPhase::Up,x,y,model);
}
}
int main() {
    using namespace fm1;
    UiModel model;
    model.companionEnabled=true;model.companionReady=true;model.nativeSupported=true;
    model.notesSupported=true;model.sequenceSupported=true;model.programSupported=true;
    model.xyTouchGate=true;model.transport=TransportKind::Native;model.connection=ConnectionState::Connected;
    model.xyOriginX=model.xyOriginY=ValueOrigin::Native;
    model.profileIndex=4;model.profiles=6;model.presetCount=6;model.presetIndex=2;
    model.firmware="ZAAGGENZ";model.sourceLabel="Actual native engine / no hardware attached";
    model.telemetryAvailable=true;model.telemetryOrigin=ValueOrigin::Native;model.meterCount=6;
    model.telemetryMeters={12,80,127,30,66,109};model.outputPeak=109;
    model.presetNames={"BLOOM BARK","FORMANT SNARL","UPPER CHOP","SPLIT MAUL","CRUSHED TEETH","HARMONIC RIP","", ""};
    for(auto& control:model.controls){control.label="CHARACTER";control.value=76;control.origin=ValueOrigin::Native;
        std::snprintf(control.display,sizeof(control.display),"Formant Snarl");}
    PerformanceSequencer sequence;model.sequence=sequence.view();
    Recording recording;Ui ui(Recording::send,&recording);
    std::array<uint64_t,3> hashes{};
    for(unsigned theme=0;theme<3;++theme){
        model.themeIndex=theme;
        CheckedCanvas canvas;
        for(unsigned page=0;page<6;++page){model.page=static_cast<Page>(page);ui.render(canvas,model);}
        model.page=Page::XY;model.fullscreenXY=true;ui.render(canvas,model);model.fullscreenXY=false;
        hashes[theme]=canvas.hash;assert(canvas.calls>1000);
    }
    assert(hashes[0]!=hashes[1]&&hashes[1]!=hashes[2]);

    // The same finger gates a note and controls both axes, including final
    // coalesced release coordinates and release outside the screen.
    model.page=Page::XY;
    ui.touch(TouchPhase::Down,30,100,model);
    assert(recording.actions.size()==2 && recording.actions[0].type==ActionType::NoteOn &&
           recording.actions[1].type==ActionType::SetXY);
    ui.touch(TouchPhase::Move,150,120,model);
    ui.touch(TouchPhase::Up,INT_MAX,INT_MIN,model);
    assert(recording.actions[recording.actions.size()-2].type==ActionType::SetXY);
    assert(recording.actions[recording.actions.size()-2].a==127&&recording.actions[recording.actions.size()-2].b==127);
    assert(recording.actions.back().type==ActionType::NoteOff && recording.count(ActionType::NoteOff)==1);
    recording.clear();
    ui.touch(TouchPhase::Down,30,100,model);ui.cancelTouch();ui.cancelTouch();
    assert(recording.count(ActionType::NoteOn)==1&&recording.count(ActionType::NoteOff)==1);

    // Every existing context boundary retires the captured gate. A replacement
    // pointer-down also releases the prior finger before its own new attack.
    for(unsigned boundary=0;boundary<7;++boundary){
        recording.clear();UiModel before=model;
        ui.touch(TouchPhase::Down,40,120,model);
        switch(boundary){
            case 0:model.profileIndex++;break;
            case 1:model.page=Page::Link;break;
            case 2:model.connection=ConnectionState::Offline;break;
            case 3:model.trackIndex++;break;
            case 4:model.controlBank^=1;break;
            case 5:model.fullscreenXY=true;break;
            case 6:model.setupActive=true;break;
        }
        ui.touch(TouchPhase::Move,50,110,model);ui.touch(TouchPhase::Up,50,110,model);
        assert(recording.count(ActionType::NoteOn)==1&&recording.count(ActionType::NoteOff)==1);
        model=before;
    }
    recording.clear();ui.touch(TouchPhase::Down,40,120,model);ui.touch(TouchPhase::Down,80,120,model);
    assert(recording.count(ActionType::NoteOn)==2&&recording.count(ActionType::NoteOff)==1);
    ui.touch(TouchPhase::Cancel,0,0,model);assert(recording.count(ActionType::NoteOff)==2);
    recording.clear();model.xyTouchGate=false;tap(ui,model,40,120);
    assert(recording.count(ActionType::NoteOn)==0&&recording.count(ActionType::NoteOff)==0);
    model.xyTouchGate=true;model.notesSupported=false;recording.clear();tap(ui,model,40,120);
    assert(recording.count(ActionType::NoteOn)==0);model.notesSupported=true;

    // Fullscreen gates, exits and panic remain reachable with the same renderer.
    model.fullscreenXY=true;recording.clear();tap(ui,model,10,230);
    assert(recording.count(ActionType::NoteOn)==1&&recording.count(ActionType::NoteOff)==1);
    recording.clear();tap(ui,model,25,15);assert(recording.actions.size()==1&&recording.actions[0].type==ActionType::ToggleFullscreen);
    recording.clear();tap(ui,model,280,15);assert(recording.actions.size()==1&&recording.actions[0].type==ActionType::Panic);
    model.fullscreenXY=false;

    // Companion tabs keep legacy enum values while exposing the extra pages.
    recording.clear();tap(ui,model,185,225);
    assert(recording.actions.size()==1&&recording.actions[0].type==ActionType::SetPage&&recording.actions[0].a==4);
    recording.clear();tap(ui,model,240,225);
    assert(recording.actions.size()==1&&recording.actions[0].a==5);
    recording.clear();tap(ui,model,295,225);
    assert(recording.actions.size()==1&&recording.actions[0].a==3);

    model.page=Page::Sequence;recording.clear();tap(ui,model,22,70);
    assert(recording.actions.size()==2&&recording.actions[0].type==ActionType::SelectStep&&
           recording.actions[1].type==ActionType::ToggleStep&&recording.actions[1].a==0);
    recording.clear();ui.touch(TouchPhase::Down,22,70,model);
    ui.touch(TouchPhase::Move,300,40,model);ui.touch(TouchPhase::Up,300,40,model);
    assert(recording.count(ActionType::SelectStep)==1&&recording.count(ActionType::ToggleStep)==0);
    assert(recording.actions.back().type==ActionType::SetStepNote&&recording.actions.back().a==0&&
           recording.actions.back().b==model.sequence.steps[0].note+7);
    recording.clear();model.sequence.selected=5;
    ui.touch(TouchPhase::Down,125,180,model);ui.touch(TouchPhase::Up,500,180,model);
    assert(recording.actions.back().type==ActionType::SetStepVelocity&&recording.actions.back().a==5&&recording.actions.back().b==127);
    recording.clear();ui.touch(TouchPhase::Down,225,180,model);ui.touch(TouchPhase::Up,-500,180,model);
    assert(recording.actions.back().type==ActionType::SetStepGate&&recording.actions.back().b==1);
    model.sequenceSupported=false;recording.clear();tap(ui,model,20,70);tap(ui,model,40,140);
    assert(recording.actions.empty());model.sequenceSupported=true;
    model.sequence.length=4;recording.clear();tap(ui,model,300,100);assert(recording.actions.empty());

    model.page=Page::Presets;recording.clear();tap(ui,model,200,140);
    assert(recording.actions.size()==1&&recording.actions[0].type==ActionType::SelectPreset&&recording.actions[0].a==5);
    recording.clear();tap(ui,model,30,175);assert(recording.actions.empty());
    model.presetCount=0;recording.clear();tap(ui,model,30,70);assert(recording.actions.empty());

    model.page=Page::Link;model.nativeSupported=false;recording.clear();tap(ui,model,280,75);
    assert(recording.actions.empty());model.nativeSupported=true;tap(ui,model,280,75);
    assert(recording.actions.size()==1&&recording.actions[0].type==ActionType::SelectTransport&&recording.actions[0].a==3);

    // Corrupted presentation fields cannot index outside bounded model arrays.
    model.meterCount=255;model.sequence.selected=255;model.sequence.length=255;model.presetCount=255;
    CheckedCanvas hostile;
    for(unsigned page=0;page<6;++page){model.page=static_cast<Page>(page);ui.render(hostile,model);}
    std::cout << "Performance UI: PASS (gated XY, context release, full screen, presets, sequence capture, bounds, themes)\n";
}
