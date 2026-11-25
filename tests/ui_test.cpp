#include "fm1/ui.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>
namespace {
void require(bool condition,const char* message){
  if(!condition){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
struct Recording{
  std::vector<fm1::UiAction> actions;
  static void dispatch(void* context,fm1::UiAction action){
    static_cast<Recording*>(context)->actions.push_back(action);
  }
};
struct CheckedCanvas:fm1::Canvas{
  int calls=0;
  void fillRect(int x,int y,int w,int h,uint16_t)override{
    require(x>=0&&y>=0&&w>0&&h>0&&x+w<=320&&y+h<=240,"render primitives stay inside display");
    ++calls;
  }
};
}
int main(){
  fm1::UiModel model;
  model.firmware="MELODEE";model.profiles=4;model.tracks=6;
  model.transportSupported=true;model.programSupported=true;model.bleSupported=true;
  for(auto& control:model.controls)control={"RESONANCE",64,fm1::ValueOrigin::Demo,true};
  for(auto& control:model.mix)control={"LEVEL",64,fm1::ValueOrigin::Demo,true};
  for(auto& name:model.trackNames)name="SYNTH 1";
  Recording recording;fm1::Ui ui(Recording::dispatch,&recording);CheckedCanvas canvas;
  for(int page=0;page<4;++page){model.page=static_cast<fm1::Page>(page);ui.render(canvas,model);}
  require(canvas.calls>100,"all four pages render geometry");
  fm1::drawText(canvas,-12,-4,"CLIPPED",fm1::theme::text,4);
  fm1::drawText(canvas,310,236,"CLIPPED",fm1::theme::text,4);
  model.page=fm1::Page::Play;
  ui.touch(fm1::TouchPhase::Down,160,177,model);
  require(recording.actions.size()==1&&recording.actions.back().type==fm1::ActionType::NoteOn,
          "note starts immediately on press");
  ui.touch(fm1::TouchPhase::Move,500,400,model);ui.touch(fm1::TouchPhase::Up,500,400,model);
  require(recording.actions.size()==2&&recording.actions.back().type==fm1::ActionType::NoteOff,
          "release outside screen releases note");
  recording.actions.clear();
  ui.touch(fm1::TouchPhase::Down,20,80,model);ui.touch(fm1::TouchPhase::Move,300,40,model);
  ui.touch(fm1::TouchPhase::Up,300,40,model);
  require(recording.actions.size()==2,"slider drag emits only changed values");
  for(const auto action:recording.actions)
    require(action.type==fm1::ActionType::SetControl&&action.a==0,"drag never becomes navigation");
  require(recording.actions.back().b==127,"slider clamps at endpoint");
  recording.actions.clear();
  ui.touch(fm1::TouchPhase::Down,30,40,model);
  require(recording.actions.empty(),"navigation activates on release");
  ui.touch(fm1::TouchPhase::Up,300,230,model);
  require(recording.actions.empty(),"dragging away cancels navigation");
  ui.touch(fm1::TouchPhase::Down,30,40,model);ui.touch(fm1::TouchPhase::Up,30,40,model);
  require(recording.actions.size()==1&&recording.actions[0].type==fm1::ActionType::NextProfile,
          "profile tap advances exactly once");
  recording.actions.clear();
  ui.touch(fm1::TouchPhase::Down,160,177,model);model.trackIndex=1;
  ui.touch(fm1::TouchPhase::Move,160,177,model);
  require(recording.actions.size()==2&&recording.actions.back().type==fm1::ActionType::NoteOff,
          "context change releases held note");
  ui.touch(fm1::TouchPhase::Up,160,177,model);
  require(recording.actions.size()==2,"context cancellation does not duplicate release");
  recording.actions.clear();model.page=fm1::Page::XY;
  ui.touch(fm1::TouchPhase::Down,8,60,model);ui.touch(fm1::TouchPhase::Move,225,195,model);
  ui.touch(fm1::TouchPhase::Up,225,195,model);
  require(recording.actions.size()==2&&recording.actions.front().a==0&&
          recording.actions.front().b==127&&recording.actions.back().a==127&&
          recording.actions.back().b==0,"XY covers full MIDI range");
  recording.actions.clear();model.page=fm1::Page::Link;model.bleSupported=false;
  ui.touch(fm1::TouchPhase::Down,155,75,model);ui.touch(fm1::TouchPhase::Up,155,75,model);
  require(recording.actions.empty(),"unsupported BLE cannot be selected");
  model.programSupported=false;
  ui.touch(fm1::TouchPhase::Down,298,178,model);ui.touch(fm1::TouchPhase::Up,298,178,model);
  require(recording.actions.empty(),"unsupported program changes do not emit");
  model.page=fm1::Page::Mix;model.trackIndex=4;
  ui.touch(fm1::TouchPhase::Down,40,113,model);ui.touch(fm1::TouchPhase::Move,40,173,model);
  ui.touch(fm1::TouchPhase::Up,40,173,model);
  require(!recording.actions.empty()&&recording.actions.back().type==fm1::ActionType::SetLevel&&
          recording.actions.back().a==4,"mixer second bank addresses correct track");
  recording.actions.clear();
  ui.touch(fm1::TouchPhase::Down,280,130,model);ui.touch(fm1::TouchPhase::Up,280,130,model);
  require(recording.actions.empty(),"unused mixer slots do not emit");
  model.setupActive=true;ui.render(canvas,model);
  ui.touch(fm1::TouchPhase::Down,30,40,model);ui.touch(fm1::TouchPhase::Up,30,40,model);
  require(recording.actions.empty(),"setup modal suppresses ordinary navigation");
  ui.touch(fm1::TouchPhase::Down,150,217,model);ui.touch(fm1::TouchPhase::Up,150,217,model);
  require(recording.actions.size()==1&&recording.actions[0].type==fm1::ActionType::Configure,
          "setup Back toggles configuration portal");
  std::cout<<"UI tests: PASS (bounds, capture, release, capabilities, mixer, setup)\n";
}
