#include "fm1/ui.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace fm1 {
namespace {
// Fixed 5x7 instrument font; bytes are columns, top pixel first.
// Labels use uppercase; case-sensitive setup credentials retain their exact case.
void glyph(char input, uint8_t out[5], bool preserveCase) {
  const char c = preserveCase?input:static_cast<char>(std::toupper(static_cast<unsigned char>(input)));
  uint64_t bits = 0;
  switch (c) {
    case 'A': bits = 0x7e1111117eULL; break;
    case 'B': bits = 0x364949497fULL; break;
    case 'C': bits = 0x224141413eULL; break;
    case 'D': bits = 0x3e4141417fULL; break;
    case 'E': bits = 0x414949497fULL; break;
    case 'F': bits = 0x010909097fULL; break;
    case 'G': bits = 0x7a4941413eULL; break;
    case 'H': bits = 0x7f0808087fULL; break;
    case 'I': bits = 0x00417f4100ULL; break;
    case 'J': bits = 0x013f414020ULL; break;
    case 'K': bits = 0x412214087fULL; break;
    case 'L': bits = 0x404040407fULL; break;
    case 'M': bits = 0x7f020c027fULL; break;
    case 'N': bits = 0x7f1008047fULL; break;
    case 'O': bits = 0x3e4141413eULL; break;
    case 'P': bits = 0x060909097fULL; break;
    case 'Q': bits = 0x5e2151413eULL; break;
    case 'R': bits = 0x462919097fULL; break;
    case 'S': bits = 0x3149494946ULL; break;
    case 'T': bits = 0x01017f0101ULL; break;
    case 'U': bits = 0x3f4040403fULL; break;
    case 'V': bits = 0x1f2040201fULL; break;
    case 'W': bits = 0x3f4038403fULL; break;
    case 'X': bits = 0x6314081463ULL; break;
    case 'Y': bits = 0x0304780403ULL; break;
    case 'Z': bits = 0x4345495161ULL; break;
    case 'a': bits = 0x7854545420ULL; break;
    case 'b': bits = 0x384444487fULL; break;
    case 'c': bits = 0x2044444438ULL; break;
    case 'd': bits = 0x7f48444438ULL; break;
    case 'e': bits = 0x1854545438ULL; break;
    case 'f': bits = 0x0201097e08ULL; break;
    case 'g': bits = 0x3e5252520cULL; break;
    case 'h': bits = 0x780404087fULL; break;
    case 'i': bits = 0x00407d4400ULL; break;
    case 'j': bits = 0x003d444020ULL; break;
    case 'k': bits = 0x004428107fULL; break;
    case 'l': bits = 0x00407f4100ULL; break;
    case 'm': bits = 0x780418047cULL; break;
    case 'n': bits = 0x780404087cULL; break;
    case 'o': bits = 0x3844444438ULL; break;
    case 'p': bits = 0x081414147cULL; break;
    case 'q': bits = 0x7c18141408ULL; break;
    case 'r': bits = 0x080404087cULL; break;
    case 's': bits = 0x2054545448ULL; break;
    case 't': bits = 0x2040443f04ULL; break;
    case 'u': bits = 0x7c2040403cULL; break;
    case 'v': bits = 0x1c2040201cULL; break;
    case 'w': bits = 0x3c4030403cULL; break;
    case 'x': bits = 0x4428102844ULL; break;
    case 'y': bits = 0x3c5050500cULL; break;
    case 'z': bits = 0x444c546444ULL; break;
    case '0': bits = 0x3e4549513eULL; break;
    case '1': bits = 0x00407f4200ULL; break;
    case '2': bits = 0x4649515162ULL; break;
    case '3': bits = 0x3649494122ULL; break;
    case '4': bits = 0x107f121418ULL; break;
    case '5': bits = 0x3945454527ULL; break;
    case '6': bits = 0x3049494a3cULL; break;
    case '7': bits = 0x0305097101ULL; break;
    case '8': bits = 0x3649494936ULL; break;
    case '9': bits = 0x1e29494906ULL; break;
    case '-': bits = 0x0808080808ULL; break;
    case '_': bits = 0x4040404040ULL; break;
    case '.': bits = 0x0060600000ULL; break;
    case ':': bits = 0x0036360000ULL; break;
    case '/': bits = 0x0204081020ULL; break;
    case '\\': bits = 0x2010080402ULL; break;
    case '+': bits = 0x08083e0808ULL; break;
    case '<': bits = 0x0022140800ULL; break;
    case '>': bits = 0x0008142200ULL; break;
    case '!': bits = 0x00005f0000ULL; break;
    case '?': bits = 0x0609510102ULL; break;
    case '%': bits = 0x6338140e63ULL; break;
    case '=': bits = 0x1414141414ULL; break;
    case '[': bits = 0x0041417f00ULL; break;
    case ']': bits = 0x007f414100ULL; break;
    case '(': bits = 0x0041221c00ULL; break;
    case ')': bits = 0x001c224100ULL; break;
    case '#': bits = 0x147f147f14ULL; break;
    case '*': bits = 0x14083e0814ULL; break;
    case ',': bits = 0x0020503000ULL; break;
    case '\'': bits = 0x0000030000ULL; break;
    case '"': bits = 0x0003000300ULL; break;
    case ' ': break;
    default: bits = 0x7f4141417fULL; break;
  }
  for (int column = 0; column < 5; ++column)
    out[column] = static_cast<uint8_t>((bits >> (column * 8)) & 0x7fu);
}
void fill(Canvas& canvas, int x, int y, int w, int h, uint16_t color) {
  const int left = std::max(0, x), top = std::max(0, y);
  const int right = std::min(kScreenWidth, x + w), bottom = std::min(kScreenHeight, y + h);
  if (right > left && bottom > top) canvas.fillRect(left, top, right-left, bottom-top, color);
}
}
int textWidth(const char* text, int scale, std::size_t maxChars) {
  if (!text || scale <= 0) return 0;
  std::size_t count = 0;
  while (count < maxChars && text[count]) ++count;
  return count == 0 ? 0 : static_cast<int>(count) * 6 * scale - scale;
}
void drawText(Canvas& canvas, int x, int y, const char* text,
              uint16_t color, int scale, std::size_t maxChars, bool preserveCase) {
  if (!text || scale < 1 || scale > 4) return;
  for (std::size_t index = 0; index < maxChars && text[index]; ++index) {
    uint8_t columns[5] = {};
    glyph(text[index], columns, preserveCase);
    // Horizontal runs avoid a separate SPI operation for every lit pixel.
    for (int row = 0; row < 7; ++row) {
      int start = -1;
      for (int column = 0; column <= 5; ++column) {
        const bool lit = column < 5 && (columns[column] & (1u << row));
        if (lit && start < 0) start = column;
        if (!lit && start >= 0) {
          fill(canvas,x+start*scale,y+row*scale,(column-start)*scale,scale,color);
          start = -1;
        }
      }
    }
    x += 6 * scale;
    if (x >= kScreenWidth) break;
  }
}
void drawPanel(Canvas& canvas, Rect rect, uint16_t color, uint16_t stroke) {
  if (rect.w < 4 || rect.h < 4) return;
  fill(canvas,rect.x+2,rect.y,rect.w-4,rect.h,stroke);
  fill(canvas,rect.x,rect.y+2,rect.w,rect.h-4,stroke);
  fill(canvas,rect.x+2,rect.y+1,rect.w-4,rect.h-2,color);
  fill(canvas,rect.x+1,rect.y+2,rect.w-2,rect.h-4,color);
}
namespace {
constexpr Rect kProfile{8,31,142,24}, kTrack{158,31,100,24}, kBank{266,31,46,24};
constexpr Rect kPlay{8,160,98,36}, kNote{111,160,98,36}, kPanic{214,160,98,36};
constexpr Rect kPad{8,60,218,136}, kXYNote{234,60,78,42}, kXYPanic{234,108,78,32};
constexpr Rect kConnect{8,125,148,30}, kConfigure{164,125,148,30};
constexpr Rect kChannelMinus{69,165,28,25}, kChannelPlus{126,165,28,25};
constexpr Rect kProgramMinus{225,165,28,25}, kProgramPlus{284,165,28,25};
constexpr Rect kSetupBack{8,201,304,31};
Rect controlRect(int i) { return {8+(i%2)*156,60+(i/2)*48,148,44}; }
Rect mixRect(int i) { return {8+i*78,60,70,136}; }
Rect tabRect(int i) { return {i*80,206,80,34}; }
Rect modeRect(int i) { return {8+i*104,60,96,31}; }
int clampMidi(int value) { return std::max(0,std::min(127,value)); }
int scalePosition(int position,int start,int length) {
  return length <= 1 ? 0 : clampMidi((position-start)*127/(length-1));
}
const char* originName(ValueOrigin origin) {
  switch(origin) {
    case ValueOrigin::Unknown: return "UNKNOWN";
    case ValueOrigin::Pending: return "QUEUED";
    case ValueOrigin::Sent: return "SENT";
    case ValueOrigin::Device: return "DEVICE";
    case ValueOrigin::Demo: return "DEMO";
  }
  return "UNKNOWN";
}
uint16_t originColor(ValueOrigin origin) {
  if(origin==ValueOrigin::Device) return theme::accent;
  if(origin==ValueOrigin::Demo) return theme::amber;
  if(origin==ValueOrigin::Pending) return theme::blue;
  return theme::muted;
}
void centerText(Canvas& canvas,Rect rect,const char* label,uint16_t color,
                int scale=1,std::size_t maxChars=30) {
  drawText(canvas,rect.x+(rect.w-textWidth(label,scale,maxChars))/2,
           rect.y+(rect.h-7*scale)/2,label,color,scale,maxChars);
}
void button(Canvas& canvas,Rect rect,const char* label,bool enabled=true,
            bool active=false,uint16_t tint=theme::accent) {
  drawPanel(canvas,rect,active?theme::panelActive:theme::panel,active?tint:theme::line);
  centerText(canvas,rect,label,enabled?(active?tint:theme::text):theme::muted,
             rect.w>=70?2:1,rect.w>=70?8:6);
}
void valueText(Canvas& canvas,int x,int y,const ControlView& value,int scale=2) {
  char buffer[8];
  if(value.value<0) std::snprintf(buffer,sizeof(buffer),"---");
  else std::snprintf(buffer,sizeof(buffer),"%03d",clampMidi(value.value));
  drawText(canvas,x,y,buffer,value.enabled?theme::text:theme::muted,scale,3);
}
void drawHeader(Canvas& canvas,const UiModel& model) {
  drawText(canvas,9,8,"FM-1",theme::text,2);
  drawText(canvas,67,13,"CYDAUX",theme::accent,1);
  const char* state="OFFLINE"; uint16_t tint=theme::muted;
  if(model.transport==TransportKind::Demo) {state="DEMO";tint=theme::amber;}
  else if(model.connection==ConnectionState::Connecting) {state="LINKING";tint=theme::blue;}
  else if(model.connection==ConnectionState::Failed) {state="LINK ERROR";tint=theme::danger;}
  else if(model.connection==ConnectionState::Connected) {
    state=model.mockPeer?"MOCK LINK":(model.transport==TransportKind::Ble?"BLE LIVE":"WIFI LIVE");
    tint=model.mockPeer?theme::amber:theme::accent;
  }
  drawPanel(canvas,{218,6,94,18},theme::panel,tint);
  centerText(canvas,{218,6,94,18},state,tint);
  drawPanel(canvas,kProfile,theme::panel);
  drawText(canvas,14,39,model.firmware,theme::text,1,19);
  drawText(canvas,137,39,">",theme::accent);
  drawPanel(canvas,kTrack,theme::panel);
  char text[32];
  std::snprintf(text,sizeof(text),"T%u  CH%02u >",static_cast<unsigned>(model.trackIndex+1),
                static_cast<unsigned>(model.midiChannel));
  centerText(canvas,kTrack,text,theme::text);
  drawPanel(canvas,kBank,theme::panelActive,theme::accent);
  std::snprintf(text,sizeof(text),"%u/2",static_cast<unsigned>(model.controlBank+1));
  centerText(canvas,kBank,text,theme::accent);
}
void drawControls(Canvas& canvas,const UiModel& model) {
  for(int i=0;i<4;++i) {
    const Rect rect=controlRect(i);
    const auto& control=model.controls[static_cast<std::size_t>(i)];
    drawPanel(canvas,rect,theme::panel);
    drawText(canvas,rect.x+7,rect.y+7,control.enabled?control.label:"UNAVAILABLE",
             control.enabled?theme::muted:theme::line,1,15);
    drawText(canvas,rect.x+7,rect.y+22,control.enabled?originName(control.origin):"--",
             originColor(control.origin),1,8);
    valueText(canvas,rect.x+105,rect.y+20,control);
    fill(canvas,rect.x+7,rect.y+37,134,3,theme::line);
    if(control.value>=0&&control.enabled)
      fill(canvas,rect.x+7,rect.y+37,std::max(1,clampMidi(control.value)*134/127),3,
           originColor(control.origin));
  }
  button(canvas,kPlay,model.playing?"STOP":"PLAY",model.transportSupported,model.playing,
          model.playingKnown?theme::accent:theme::amber);
  button(canvas,kNote,model.noteHeld?"HELD":"NOTE",true,model.noteHeld);
  button(canvas,kPanic,"PANIC",true,false,theme::danger);
  drawText(canvas,10,198,"DRAG VALUES    HOLD NOTE TO AUDITION",theme::muted,1,50);
}
void drawMix(Canvas& canvas,const UiModel& model) {
  const int first=(model.trackIndex/4)*4;
  for(int column=0;column<4;++column) {
    const int track=first+column; const Rect rect=mixRect(column);
    const bool selected=track==model.trackIndex;
    drawPanel(canvas,rect,selected?theme::panelActive:theme::panel,selected?theme::accent:theme::line);
    if(track>=model.tracks||track>=static_cast<int>(kMaxTracks)) {
      centerText(canvas,rect,"--",theme::line,2);continue;
    }
    const auto& value=model.mix[static_cast<std::size_t>(track)];
    const char* label=model.trackNames[static_cast<std::size_t>(track)];
    drawText(canvas,rect.x+5,rect.y+7,label?label:"TRACK",selected?theme::accent:theme::muted,1,10);
    valueText(canvas,rect.x+17,rect.y+23,value);
    drawText(canvas,rect.x+5,rect.y+43,originName(value.origin),originColor(value.origin),1,10);
    fill(canvas,rect.x+33,rect.y+58,4,56,theme::line);
    if(value.value>=0) {
      const int amount=clampMidi(value.value)*55/127;
      fill(canvas,rect.x+33,rect.y+113-amount,4,amount+1,originColor(value.origin));
      drawPanel(canvas,{rect.x+15,rect.y+110-amount,40,8},originColor(value.origin),originColor(value.origin));
    }
    char text[16];std::snprintf(text,sizeof(text),"TRACK %d",track+1);
    drawText(canvas,rect.x+7,rect.y+122,text,theme::muted);
  }
  drawText(canvas,10,198,"DRAG LEVELS    TAP A TRACK NAME",theme::muted,1,50);
}
void drawXY(Canvas& canvas,const UiModel& model) {
  drawPanel(canvas,kPad,theme::panelActive);
  for(int q=1;q<4;++q) {
    fill(canvas,kPad.x+q*(kPad.w-2)/4,kPad.y+2,1,kPad.h-4,theme::line);
    fill(canvas,kPad.x+2,kPad.y+q*(kPad.h-2)/4,kPad.w-4,1,theme::line);
  }
  const bool enabled=model.xyEnabled;
  if(enabled&&model.xyOriginX!=ValueOrigin::Unknown&&model.xyOriginY!=ValueOrigin::Unknown) {
    const int x=kPad.x+6+model.xyX*(kPad.w-13)/127;
    const int y=kPad.y+6+(127-model.xyY)*(kPad.h-13)/127;
    const auto color=model.xyOriginX==model.xyOriginY?originColor(model.xyOriginX):theme::muted;
    fill(canvas,x-8,y,17,1,color);fill(canvas,x,y-8,1,17,color);
    drawPanel(canvas,{x-3,y-3,7,7},color,color);
  } else centerText(canvas,kPad,enabled?"TOUCH TO SET":"UNAVAILABLE",theme::muted);
  button(canvas,kXYNote,model.noteHeld?"HELD":"NOTE",true,model.noteHeld);
  button(canvas,kXYPanic,"PANIC",true,false,theme::danger);
  auto axis=[&](const char* name,uint8_t value,ValueOrigin origin,int top){
    char text[18];
    if(origin==ValueOrigin::Unknown) std::snprintf(text,sizeof(text),"%s ---",name);
    else std::snprintf(text,sizeof(text),"%s %03u",name,static_cast<unsigned>(value));
    drawText(canvas,241,top,text,enabled?theme::text:theme::muted,1);
    drawText(canvas,241,top+11,originName(origin),originColor(origin),1,8);
  };
  axis("X",model.xyX,model.xyOriginX,147);
  axis("Y",model.xyY,model.xyOriginY,173);
  char label[60];
  std::snprintf(label,sizeof(label),"X: %.15s  Y: %.15s",model.xyLabelX,model.xyLabelY);
  drawText(canvas,10,198,label,theme::muted,1,50);
}
void drawNotice(Canvas& canvas,const char* notice) {
  if(!notice)return;
  char first[51]={},second[51]={};
  const std::size_t length=std::strlen(notice),take=std::min<std::size_t>(50,length);
  std::memcpy(first,notice,take);
  if(length>take)std::memcpy(second,notice+take,std::min<std::size_t>(50,length-take));
  drawText(canvas,10,99,first,theme::muted);
  drawText(canvas,10,111,second,theme::muted);
}
void drawLink(Canvas& canvas,const UiModel& model) {
  button(canvas,modeRect(0),"DEMO",true,model.transport==TransportKind::Demo,theme::amber);
  button(canvas,modeRect(1),"BLE",model.bleSupported,model.transport==TransportKind::Ble);
  button(canvas,modeRect(2),"WIFI",true,model.transport==TransportKind::Wifi);
  drawNotice(canvas,model.notice);
  const bool linked=model.connection==ConnectionState::Connected||model.connection==ConnectionState::Connecting;
  button(canvas,kConnect,linked?"UNLINK":"CONNECT",model.transport!=TransportKind::Demo);
  button(canvas,kConfigure,"SETUP");
  drawText(canvas,8,174,"MIDI CH",theme::muted);
  button(canvas,kChannelMinus,"-");button(canvas,kChannelPlus,"+");
  char text[32];
  std::snprintf(text,sizeof(text),"%02u",static_cast<unsigned>(model.midiChannel));
  centerText(canvas,{98,165,27,25},text,theme::text);
  drawText(canvas,166,174,"PROGRAM",theme::muted);
  button(canvas,kProgramMinus,"-",model.programSupported);
  button(canvas,kProgramPlus,"+",model.programSupported);
  std::snprintf(text,sizeof(text),"%03u",static_cast<unsigned>(model.program+1));
  centerText(canvas,{254,165,29,25},text,model.programSupported?theme::text:theme::muted);
  std::snprintf(text,sizeof(text),"TX %lu  RX %lu",static_cast<unsigned long>(model.transmitted),
                static_cast<unsigned long>(model.received));
  drawText(canvas,10,198,text,theme::muted,1,30);
  const char* state=model.syncSupported?(model.playingKnown?"STATE READY":"STATE WAIT"):"MIDI CONTROL";
  const auto stateColor=model.syncSupported?(model.playingKnown?theme::accent:theme::blue):theme::muted;
  drawText(canvas,234,198,state,stateColor,1,14);
}
void drawSetup(Canvas& canvas,const UiModel& model) {
  drawText(canvas,12,12,"DEVICE SETUP",theme::accent,2);
  drawText(canvas,12,42,"CONNECT PHONE OR COMPUTER TO WIFI:",theme::muted);
  drawPanel(canvas,{8,58,304,51},theme::panel);
  drawText(canvas,16,67,model.setupSsid,theme::text,2,23,true);
  drawText(canvas,16,91,"PASSWORD:",theme::muted);
  drawText(canvas,77,88,model.setupPassword,theme::amber,2,15,true);
  drawText(canvas,12,123,"THEN OPEN:",theme::muted);
  drawText(canvas,12,143,"HTTP://192.168.4.1",theme::text,2);
  drawText(canvas,12,173,"SAVE WIFI / BRIDGE / BLE SETTINGS.",theme::muted);
  drawText(canvas,12,185,std::strcmp(model.setupSsid,"CYDaux-DEMO")==0?
           "SIMULATED: NO ACCESS POINT":"RETURN HERE AFTER SAVING SETTINGS.",theme::muted);
  button(canvas,kSetupBack,"BACK");
}
}
void Ui::render(Canvas& canvas,const UiModel& model) const {
  canvas.fillRect(0,0,kScreenWidth,kScreenHeight,theme::background);
  if(model.setupActive){drawSetup(canvas,model);return;}
  drawHeader(canvas,model);
  switch(model.page){
    case Page::Play:drawControls(canvas,model);break;
    case Page::Mix:drawMix(canvas,model);break;
    case Page::XY:drawXY(canvas,model);break;
    case Page::Link:drawLink(canvas,model);break;
  }
  static constexpr const char* labels[4]={"PLAY","MIX","XY","LINK"};
  fill(canvas,0,205,kScreenWidth,1,theme::line);
  for(int i=0;i<4;++i){
    const bool active=i==static_cast<int>(model.page);const Rect rect=tabRect(i);
    if(active)fill(canvas,rect.x+12,rect.y,rect.w-24,2,theme::accent);
    centerText(canvas,rect,labels[i],active?theme::accent:theme::muted,2);
  }
}
void Ui::emit(UiAction action) const {if(callback_)callback_(context_,action);}
void Ui::cancelTouch(){
  if(gesture_==Gesture::Note)emit({ActionType::NoteOff});
  gesture_=Gesture::None;lastA_=lastB_=-1;
}
void Ui::drag(int x,int y){
  if(gesture_==Gesture::Control){
    const int value=scalePosition(x,capture_.x+7,capture_.w-14);
    if(value!=lastA_)emit({ActionType::SetControl,static_cast<int16_t>(index_),static_cast<int16_t>(value)});
    lastA_=value;
  }else if(gesture_==Gesture::Level){
    const int value=127-scalePosition(y,capture_.y+58,56);
    if(value!=lastA_)emit({ActionType::SetLevel,static_cast<int16_t>(index_),static_cast<int16_t>(value)});
    lastA_=value;
  }else if(gesture_==Gesture::XY){
    const int a=scalePosition(x,capture_.x+6,capture_.w-12);
    const int b=127-scalePosition(y,capture_.y+6,capture_.h-12);
    if(a!=lastA_||b!=lastB_)emit({ActionType::SetXY,static_cast<int16_t>(a),static_cast<int16_t>(b)});
    lastA_=a;lastB_=b;
  }
}
void Ui::touch(TouchPhase phase,int x,int y,const UiModel& model){
  if(phase==TouchPhase::Cancel){cancelTouch();return;}
  if(phase!=TouchPhase::Down&&(page_!=model.page||profile_!=model.profileIndex||
     track_!=model.trackIndex||bank_!=model.controlBank||transport_!=model.transport||
     connection_!=model.connection||setup_!=model.setupActive)){cancelTouch();return;}
  if(phase==TouchPhase::Move){drag(x,y);return;}
  if(phase==TouchPhase::Up){
    // The release sample can be newer than the last coalesced move event.
    drag(x,y);
    if(gesture_==Gesture::Button&&capture_.contains(x,y))emit(button_);
    cancelTouch();return;
  }
  cancelTouch();page_=model.page;profile_=model.profileIndex;track_=model.trackIndex;
  bank_=model.controlBank;transport_=model.transport;connection_=model.connection;setup_=model.setupActive;
  auto captureButton=[&](Rect rect,UiAction action,bool enabled=true){
    if(!enabled||!rect.contains(x,y))return false;
    capture_=rect;button_=action;gesture_=Gesture::Button;return true;
  };
  if(model.setupActive){captureButton(kSetupBack,{ActionType::Configure});return;}
  for(int i=0;i<4;++i)
    if(captureButton(tabRect(i),{ActionType::SetPage,static_cast<int16_t>(i)}))return;
  if(captureButton(kProfile,{ActionType::NextProfile},model.profiles>1))return;
  if(captureButton(kTrack,{ActionType::SelectTrack,
      static_cast<int16_t>(model.tracks?(model.trackIndex+1)%model.tracks:0)},model.tracks>1))return;
  if(captureButton(kBank,{ActionType::NextControlBank}))return;
  if(model.page==Page::Play){
    for(int i=0;i<4;++i){
      if(controlRect(i).contains(x,y)&&model.controls[static_cast<std::size_t>(i)].enabled){
        capture_=controlRect(i);index_=i;gesture_=Gesture::Control;drag(x,y);return;
      }
    }
    if(captureButton(kPlay,{ActionType::TogglePlay},model.transportSupported))return;
    if(captureButton(kPanic,{ActionType::Panic}))return;
    if(kNote.contains(x,y)){gesture_=Gesture::Note;emit({ActionType::NoteOn});return;}
  }else if(model.page==Page::Mix){
    const int first=(model.trackIndex/4)*4;
    for(int column=0;column<4;++column){
      const int track=first+column;if(track>=model.tracks||track>=static_cast<int>(kMaxTracks))continue;
      const Rect rect=mixRect(column);
      if(captureButton({rect.x,rect.y,rect.w,22},{ActionType::SelectTrack,static_cast<int16_t>(track)}))return;
      if(rect.contains(x,y)&&model.mix[static_cast<std::size_t>(track)].enabled){
        capture_=rect;index_=track;gesture_=Gesture::Level;drag(x,y);return;
      }
    }
  }else if(model.page==Page::XY){
    if(kPad.contains(x,y)&&model.xyEnabled){
      capture_=kPad;gesture_=Gesture::XY;drag(x,y);return;
    }
    if(captureButton(kXYPanic,{ActionType::Panic}))return;
    if(kXYNote.contains(x,y)){gesture_=Gesture::Note;emit({ActionType::NoteOn});return;}
  }else if(model.page==Page::Link){
    if(captureButton(modeRect(0),{ActionType::SelectTransport,0}))return;
    if(captureButton(modeRect(1),{ActionType::SelectTransport,1},model.bleSupported))return;
    if(captureButton(modeRect(2),{ActionType::SelectTransport,2}))return;
    const bool linked=model.connection==ConnectionState::Connected||model.connection==ConnectionState::Connecting;
    if(captureButton(kConnect,{linked?ActionType::Disconnect:ActionType::Connect},
                      model.transport!=TransportKind::Demo))return;
    if(captureButton(kConfigure,{ActionType::Configure}))return;
    if(captureButton(kChannelMinus,{ActionType::ChannelDelta,-1}))return;
    if(captureButton(kChannelPlus,{ActionType::ChannelDelta,1}))return;
    if(captureButton(kProgramMinus,{ActionType::ProgramDelta,-1},model.programSupported))return;
    if(captureButton(kProgramPlus,{ActionType::ProgramDelta,1},model.programSupported))return;
  }
}
} // namespace fm1
