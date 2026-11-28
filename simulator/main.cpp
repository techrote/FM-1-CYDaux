#include "fm1/controller.hpp"
#include "fm1/ui.hpp"
#include "virtual_instrument.hpp"
#include "chrono_panel.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
class Framebuffer final:public fm1::Canvas {
 public:
  void fillRect(int x,int y,int w,int h,uint16_t color)override {
    const int left=std::max(0,x),right=std::min(320,x+w);
    const int top=std::max(0,y),bottom=std::min(240,y+h);
    for(int row=top;row<bottom;++row)
      for(int column=left;column<right;++column)pixels_[row*320+column]=color;
  }
  void writePpm(const std::string& path)const {
    // Python sends UTF-8 paths; use the native wide path on Windows as well.
    std::ofstream out(std::filesystem::u8path(path),std::ios::binary);
    if(!out)throw std::runtime_error("cannot open frame path");
    out<<"P6\n320 240\n255\n";
    for(const uint16_t pixel:pixels_) {
      const unsigned r=(pixel>>11u)&31u,g=(pixel>>5u)&63u,b=pixel&31u;
      const char rgb[3]={static_cast<char>((r<<3u)|(r>>2u)),
                         static_cast<char>((g<<2u)|(g>>4u)),
                         static_cast<char>((b<<3u)|(b>>2u))};
      out.write(rgb,3);
    }
    if(!out)throw std::runtime_error("cannot write frame");
  }
 private:
  std::array<uint16_t,320*240> pixels_{};
};
std::string quoted(const char* input) {
  std::string output="\"";
  if(input)for(const unsigned char c:std::string(input)) {
    if(c=='"'||c=='\\'){output+='\\';output+=static_cast<char>(c);}
    else if(c=='\n')output+="\\n";
    else if(c=='\r')output+="\\r";
    else if(c=='\t')output+="\\t";
    else if(c>=32)output+=static_cast<char>(c);
  }
  return output+'"';
}
const char* origin(fm1::ValueOrigin value) {
  switch(value) {
    case fm1::ValueOrigin::Unknown:return "unknown";
    case fm1::ValueOrigin::Pending:return "pending";
    case fm1::ValueOrigin::Sent:return "sent";
    case fm1::ValueOrigin::Device:return "device";
    case fm1::ValueOrigin::Demo:return "demo";
    case fm1::ValueOrigin::Native:return "native-engine";
  }
  return "unknown";
}
class Simulator {
 public:
  Simulator():ui_(dispatch,this){controller_.setNativeAvailable(true);}
  bool failed()const{return failed_;}
  bool command(const std::string& input) {
    std::istringstream stream(input);std::string name;stream>>name;
    if(name.empty()||name[0]=='#')return true;
    try {
      if(name=="quit"){std::cout<<"{\"ok\":true,\"quit\":true}"<<std::endl;return false;}
      if(name=="chrono") {
        std::string mode,extra;stream>>mode;
        if((mode!="on"&&mode!="off")||(stream>>extra))throw std::runtime_error("chrono expects on or off");
        if((mode=="on")!=chronoMode_) {
          ui_.cancelTouch();controller_.dispatch({fm1::ActionType::Panic});
          controller_.dispatch({fm1::ActionType::Disconnect});settle();
          chrono_.disconnect();chronoPanel_.cancel();sampleClock_=false;sampleRemainder_=0;
          chronoMode_=mode=="on";++chronoGeneration_;++chronoRevision_;
          if(chronoMode_)chrono_.reset(1,0);
        }
      }else if(chronoMode_ && name!="down"&&name!="move"&&name!="up"&&name!="tap"&&name!="cancel"&&name!="tick"&&name!="state"&&name!="frame"&&name!="audio"&&name!="audio-clock"&&name!="panic"&&name!="disconnect"){
        throw std::runtime_error("Leave native Chronomorph view before using another profile");
      }else if(name=="down"||name=="move"||name=="up"||name=="tap") {
        int x,y;
        if(!(stream>>x>>y)||x< -4096||x>4096||y< -4096||y>4096)
          throw std::runtime_error("touch needs bounded x and y");
        if(chronoMode_) {
          if(name=="tap"){chronoTouch(fm1::TouchPhase::Down,x,y);chronoTouch(fm1::TouchPhase::Up,x,y);}
          else chronoTouch(name=="down"?fm1::TouchPhase::Down:name=="move"?fm1::TouchPhase::Move:fm1::TouchPhase::Up,x,y);
        }else if(name=="tap") {
          ui_.touch(fm1::TouchPhase::Down,x,y,controller_.model());
          ui_.touch(fm1::TouchPhase::Up,x,y,controller_.model());
        }else{
          const auto phase=name=="down"?fm1::TouchPhase::Down:
                            name=="move"?fm1::TouchPhase::Move:fm1::TouchPhase::Up;
          ui_.touch(phase,x,y,controller_.model());
        }
      }else if(name=="cancel") {
        ui_.cancelTouch();chronoPanel_.cancel();
      }else if(name=="tick") {
        uint32_t elapsed=0;
        if(!(stream>>elapsed)||elapsed>60000)throw std::runtime_error("tick needs 0..60000 ms");
        if(!sampleClock_){now_+=elapsed;controller_.tick(now_);}
      }else if(name=="profile") {
        std::string id;stream>>id;const int index=fm1::profileIndex(id.c_str());
        if(index<0)throw std::runtime_error("unknown profile");
        ui_.cancelTouch();controller_.selectProfile(static_cast<std::size_t>(index));
      }else if(name=="page") {
        std::string page;stream>>page;const std::vector<std::string> pages{"play","mix","xy","link","sequence","presets"};
        const auto found=std::find(pages.begin(),pages.end(),page);
        if(found==pages.end())throw std::runtime_error("unknown page");
        ui_.cancelTouch();
        controller_.dispatch({fm1::ActionType::SetPage,static_cast<int16_t>(found-pages.begin())});
      }else if(name=="transport") {
        std::string mode;stream>>mode;const std::vector<std::string> modes{"demo","ble","wifi","native"};
        const auto found=std::find(modes.begin(),modes.end(),mode);
        if(found==modes.end())throw std::runtime_error("unknown transport");
        ui_.cancelTouch();
        controller_.dispatch({fm1::ActionType::SelectTransport,static_cast<int16_t>(found-modes.begin())});
      }else if(name=="connect") {
        controller_.dispatch({fm1::ActionType::Connect});
      }else if(name=="disconnect") {
        if(chronoMode_){stopChrono();sampleClock_=false;}else{ui_.cancelTouch();controller_.dispatch({fm1::ActionType::Disconnect});}
      }else if(name=="panic") {
        if(chronoMode_)stopChrono();else{ui_.cancelTouch();controller_.dispatch({fm1::ActionType::Panic});}
      }else if(name=="preset") {
        unsigned value=0;
        if(!(stream>>value)||value>127)throw std::runtime_error("preset needs 0..127");
        ui_.cancelTouch();controller_.dispatch({fm1::ActionType::SelectPreset,static_cast<int16_t>(value)});
      }else if(name=="parameter") {
        unsigned index=0,value=0;
        if(!(stream>>index>>value)||index>3||value>127)throw std::runtime_error("parameter needs visible index 0..3 and value 0..127");
        controller_.dispatch({fm1::ActionType::SetControl,static_cast<int16_t>(index),static_cast<int16_t>(value)});
      }else if(name=="bank") {
        controller_.dispatch({fm1::ActionType::NextControlBank});
      }else if(name=="note") {
        unsigned pitch=0,velocity=0;
        if(!(stream>>pitch>>velocity)||pitch>127||velocity>127)throw std::runtime_error("note needs pitch/velocity 0..127");
        if(velocity)controller_.dispatch({fm1::ActionType::NoteOn,static_cast<int16_t>(pitch),static_cast<int16_t>(velocity)});
        else controller_.dispatch({fm1::ActionType::NoteOff});
      }else if(name=="sequence") {
        controller_.dispatch({fm1::ActionType::ToggleSequence});
      }else if(name=="audio-clock") {
        std::string mode;stream>>mode;
        if(mode!="on"&&mode!="off")throw std::runtime_error("audio-clock expects on or off");
        sampleClock_=mode=="on";sampleRemainder_=0;
        if(!sampleClock_){if(chronoMode_)stopChrono();else{ui_.cancelTouch();controller_.dispatch({fm1::ActionType::Panic});}}
      }else if(name=="audio") {
        unsigned count=0;std::string path;
        if(!(stream>>count)||count<1||count>fm1::VirtualInstrument::kMaximumFrames)
          throw std::runtime_error("audio needs 1..4096 frames");
        std::getline(stream>>std::ws,path);
        if(path.empty())throw std::runtime_error("audio requires a PCM filename");
        settle();
        // The browser consumes this actual-core PCM. While audio is running,
        // audio frames own the controller clock; HTTP/UI wall time does not
        // double-advance the sequencer. Event quantum <=128 engine samples.
        if(chronoMode_){
          if(!chrono_.render(audio_.data(),count))throw std::runtime_error("Chronomorph render refused");
          chronoFrames_+=count;++chronoRevision_;
        }else for(unsigned offset=0;offset<count;offset+=128) {
          const unsigned chunk=std::min(128u,count-offset);
          if(!instrument_.render(audio_.data()+offset,chunk))throw std::runtime_error("native render refused");
          if(sampleClock_) {
            sampleRemainder_+=static_cast<uint64_t>(chunk)*1000u;
            const auto elapsed=static_cast<uint32_t>(sampleRemainder_/instrument_.sampleRate());
            sampleRemainder_%=instrument_.sampleRate();
            now_+=elapsed;controller_.tick(now_);settle();
          }
        }
        std::ofstream output(std::filesystem::u8path(path),std::ios::binary);
        if(!output)throw std::runtime_error("cannot open PCM path");
        for(unsigned i=0;i<count;++i) {
          const uint16_t sample=static_cast<uint16_t>(audio_[i]);
          const char bytes[2]={static_cast<char>(sample&255u),static_cast<char>(sample>>8u)};
          output.write(bytes,2);
        }
        if(!output)throw std::runtime_error("cannot write PCM");
      }else if(name=="echo") {
        std::string mode;stream>>mode;
        if(mode!="on"&&mode!="off")throw std::runtime_error("echo expects on or off");
        echo_=mode=="on";
      }else if(name=="rx") {
        std::vector<uint8_t> bytes;std::string value;
        while(stream>>value) {
          std::size_t used=0;const unsigned long parsed=std::stoul(value,&used,0);
          if(parsed>255||used!=value.size()||bytes.size()==fm1::kMaxMidiBytes)
            throw std::runtime_error("invalid or oversized MIDI input");
          bytes.push_back(static_cast<uint8_t>(parsed));
        }
        if(bytes.empty())throw std::runtime_error("rx requires MIDI bytes");
        controller_.receive(bytes.data(),bytes.size());
      }else if(name=="frame") {
        std::string path;std::getline(stream>>std::ws,path);
        if(path.empty())throw std::runtime_error("frame requires a PPM filename");
        settle();if(chronoMode_)chronoPanel_.render(frame_,chrono_.snapshot());else ui_.render(frame_,controller_.model());frame_.writePpm(path);
      }else if(name!="state")throw std::runtime_error("unknown simulator command");
      settle();snapshot();
    }catch(const std::exception& error) {
      failed_=true;
      std::cout<<"{\"ok\":false,\"error\":"<<quoted(error.what())<<'}'<<std::endl;
    }
    return true;
  }
 private:
  fm1::Controller controller_;
  fm1::Ui ui_;
  Framebuffer frame_;
  fm1::VirtualInstrument instrument_;
  fm1::ChronoAdapter chrono_;
  fm1::ChronoPanel chronoPanel_;
  bool chronoMode_=false;
  uint32_t chronoGeneration_=0x80000000u;
  uint64_t chronoRevision_=0,chronoFrames_=0;
  void stopChrono(){chrono_.stop();chronoPanel_.cancel();++chronoGeneration_;++chronoRevision_;}
  void chronoTouch(fm1::TouchPhase phase,int x,int y){
    const auto change=chronoPanel_.touch(phase,x,y,chrono_,sampleClock_);
    if(change!=fm1::ChronoPanel::Change::None)++chronoRevision_;
    if(change==fm1::ChronoPanel::Change::Flush)++chronoGeneration_;
  }
  std::array<int16_t,fm1::VirtualInstrument::kMaximumFrames> audio_{};
  bool nativeConnected_=false,sampleClock_=false;
  uint64_t sampleRemainder_=0;
  std::string nativeProfile_;
  uint32_t now_=0;
  bool echo_=true,failed_=false;
  std::deque<fm1::MidiMessage> history_;
  static void dispatch(void* context,fm1::UiAction action) {
    static_cast<Simulator*>(context)->controller_.dispatch(action);
  }
  void settle() {
    const auto& initial=controller_.model();
    if(nativeConnected_&&(initial.transport!=fm1::TransportKind::Native||
       initial.connection!=fm1::ConnectionState::Connected||
       nativeProfile_!=fm1::profileAt(initial.profileIndex).id)) {
      instrument_.disconnect();nativeConnected_=false;
    }
    fm1::UiAction action{fm1::ActionType::Refresh};
    while(controller_.takePlatformAction(action)) {
      if(action.type==fm1::ActionType::Connect&&controller_.model().transport==fm1::TransportKind::Native) {
        nativeProfile_=fm1::profileAt(controller_.model().profileIndex).id;
        if(instrument_.select(nativeProfile_.c_str())) {
          nativeConnected_=true;
          controller_.setConnection(fm1::ConnectionState::Connected,false,"Native engine: actual generated audio, no FM-1 hardware");
        } else controller_.setConnection(fm1::ConnectionState::Failed,false,"This profile has no native audio engine");
      } else if(action.type==fm1::ActionType::Connect&&controller_.model().transport!=fm1::TransportKind::Demo)
        controller_.setConnection(fm1::ConnectionState::Connected,true,
                                  "Simulator mock link: no instrument connected");
      else if(action.type==fm1::ActionType::Disconnect) {
        if(nativeConnected_){instrument_.disconnect();nativeConnected_=false;}
        controller_.setConnection(fm1::ConnectionState::Offline,false,"Disconnected in simulator");
      } else if(action.type==fm1::ActionType::Configure)
        controller_.setSetup(!controller_.model().setupActive,"CYDaux-DEMO","demo1234");
    }
    fm1::MidiMessage message;
    unsigned budget=128;
    while(budget--&&controller_.popMidi(message)) {
      if(controller_.model().transport==fm1::TransportKind::Native) {
        if(nativeConnected_&&instrument_.receive(message))controller_.didSend(message);
        fm1::MidiMessage reply;
        while(instrument_.popReply(reply))controller_.receive(reply.data.data(),reply.size);
      } else {
        controller_.didSend(message);
        if(echo_&&controller_.model().mockPeer&&message.size==3&&
           (message.data[0]&0xf0u)==0xb0u&&message.data[1]<120)
          controller_.receive(message.data.data(),message.size);
      }
      history_.push_back(message);
      if(history_.size()>24)history_.pop_front();
    }
  }
  void snapshot()const {
    const auto& m=controller_.model();const auto& profile=fm1::profileAt(m.profileIndex);
    std::cout<<"{\"ok\":true,\"simulated\":true,\"firmware\":"<<quoted(m.firmware)
      <<",\"profile\":"<<quoted(profile.id)<<",\"description\":"<<quoted(profile.description)
      <<",\"sourceVersion\":"<<quoted(profile.sourceVersion)<<",\"setupHint\":"<<quoted(m.setupHint)
      <<",\"page\":"<<static_cast<unsigned>(m.page)<<",\"transport\":"<<static_cast<unsigned>(m.transport)
      <<",\"connection\":"<<static_cast<unsigned>(m.connection)
      <<",\"mockPeer\":"<<(m.mockPeer?"true":"false")<<",\"setupActive\":"<<(m.setupActive?"true":"false")
      <<",\"track\":"<<static_cast<unsigned>(m.trackIndex)<<",\"tracks\":"<<static_cast<unsigned>(m.tracks)
      <<",\"channel\":"<<static_cast<unsigned>(m.midiChannel)<<",\"program\":"<<static_cast<unsigned>(m.program)
      <<",\"note\":"<<static_cast<unsigned>(m.note)<<",\"velocity\":"<<static_cast<unsigned>(m.velocity)
      <<",\"controlBank\":"<<static_cast<unsigned>(m.controlBank)<<",\"noteHeld\":"<<(m.noteHeld?"true":"false")
      <<",\"xyX\":"<<static_cast<unsigned>(m.xyX)<<",\"xyY\":"<<static_cast<unsigned>(m.xyY)
      <<",\"xyOriginX\":"<<quoted(origin(m.xyOriginX))<<",\"xyOriginY\":"<<quoted(origin(m.xyOriginY))
      <<",\"xyEnabled\":"<<(m.xyEnabled?"true":"false")
      <<",\"playing\":"<<(m.playing?"true":"false")<<",\"playingKnown\":"<<(m.playingKnown?"true":"false")
      <<",\"bleSupported\":"<<(m.bleSupported?"true":"false")
      <<",\"transportSupported\":"<<(m.transportSupported?"true":"false")
      <<",\"programSupported\":"<<(m.programSupported?"true":"false")
      <<",\"nativeSupported\":"<<(m.nativeSupported?"true":"false")
      <<",\"companionEnabled\":"<<(m.companionEnabled?"true":"false")
      <<",\"companionReady\":"<<(m.companionReady?"true":"false")
      <<",\"sequenceSupported\":"<<(m.sequenceSupported?"true":"false")
      <<",\"fullscreenXY\":"<<(m.fullscreenXY?"true":"false")
      <<",\"xyTouchGate\":"<<(m.xyTouchGate?"true":"false")
      <<",\"telemetryAvailable\":"<<(m.telemetryAvailable?"true":"false")
      <<",\"telemetryOrigin\":"<<quoted(origin(m.telemetryOrigin))
      <<",\"outputPeak\":"<<static_cast<unsigned>(m.outputPeak)
      <<",\"meterCount\":"<<static_cast<unsigned>(m.meterCount)
      <<",\"presetIndex\":"<<static_cast<unsigned>(m.presetIndex)
      <<",\"presetName\":"<<quoted(m.presetName)
      <<",\"sourceLabel\":"<<quoted(m.sourceLabel)
      <<",\"capabilityLabel\":"<<quoted(m.capabilityLabel)
      <<",\"audio\":{\"mode\":"<<quoted(chronoMode_?"native-engine":nativeConnected_?instrument_.mode():"no-audio-engine")
      <<",\"name\":"<<quoted(chronoMode_?"Chronomorph":instrument_.name())
      <<",\"sourceRevision\":"<<quoted(chronoMode_?"7fa9a16b55b321a1a8b46e174ed15e927aeaece6":instrument_.sourceRevision())
      <<",\"sampleRate\":"<<(chronoMode_?CM_RATE:instrument_.sampleRate())
      <<",\"generation\":"<<(chronoMode_?chronoGeneration_:instrument_.generation())
      <<",\"clockRunning\":"<<(sampleClock_?"true":"false")
      <<",\"frames\":"<<(chronoMode_?chronoFrames_:instrument_.renderedFrames())
      <<",\"peak\":"<<(chronoMode_?0:instrument_.peak())<<",\"rms\":"<<(chronoMode_?0:instrument_.rms())
      <<",\"rejected\":"<<instrument_.rejected()
      <<",\"queueOverflows\":"<<instrument_.queueOverflows()<<'}'
      <<",\"tx\":"<<m.transmitted<<",\"rx\":"<<m.received<<",\"revision\":"<<m.revision
      <<",\"milliseconds\":"<<now_<<",\"notice\":"<<quoted(m.notice)<<",\"controls\":[";
    for(std::size_t i=0;i<m.controls.size();++i) {
      const auto& value=m.controls[i];if(i){std::cout<<',';}
      std::cout<<"{\"label\":"<<quoted(value.label)<<",\"value\":"<<value.value
        <<",\"display\":"<<quoted(value.display)<<",\"origin\":"<<quoted(origin(value.origin))<<",\"enabled\":"<<(value.enabled?"true":"false")<<'}';
    }
    std::cout<<"],\"telemetryMeters\":[";
    for(std::size_t i=0;i<m.telemetryMeters.size();++i) {
      if(i)std::cout<<',';std::cout<<static_cast<unsigned>(m.telemetryMeters[i]);
    }
    std::cout<<"],\"sequence\":{\"running\":"<<(m.sequence.running?"true":"false")
      <<",\"bpm\":"<<m.sequence.bpm<<",\"length\":"<<static_cast<unsigned>(m.sequence.length)
      <<",\"selected\":"<<static_cast<unsigned>(m.sequence.selected)
      <<",\"playhead\":"<<static_cast<unsigned>(m.sequence.playhead)
      <<",\"skipped\":"<<m.sequence.skipped<<",\"steps\":[";
    for(std::size_t i=0;i<m.sequence.steps.size();++i) {
      if(i)std::cout<<',';const auto& step=m.sequence.steps[i];
      std::cout<<"{\"enabled\":"<<(step.enabled?"true":"false")<<",\"note\":"<<static_cast<unsigned>(step.note)
        <<",\"velocity\":"<<static_cast<unsigned>(step.velocity)<<",\"gate\":"<<static_cast<unsigned>(step.gate)<<'}';
    }
    std::cout<<"]},\"midi\":[";
    for(std::size_t i=0;i<history_.size();++i) {
      if(i){std::cout<<',';}std::cout<<'[';const auto& message=history_[i];
      for(std::size_t j=0;j<message.size;++j) {
        if(j){std::cout<<',';}std::cout<<static_cast<unsigned>(message.data[j]);
      }
      std::cout<<']';
    }
    const auto cv=chrono_.snapshot();
    std::cout<<"],\"chrono\":{\"active\":"<<(chronoMode_?"true":"false")<<",\"revision\":"<<chronoRevision_
      <<",\"origin\":\"native-engine\",\"playing\":"<<(cv.playing?"true":"false")
      <<",\"bpm\":"<<cv.bpm<<",\"warp\":"<<int(cv.warp)<<",\"variation\":"<<unsigned(cv.variation)
      <<",\"preset\":"<<unsigned(cv.preset)<<",\"lanes\":[";
    for(unsigned i=0;i<CM_LANES;++i){if(i)std::cout<<',';const auto& l=cv.lane[i];
      std::cout<<"{\"length\":"<<unsigned(l.length)<<",\"mask\":"<<l.mask<<",\"step\":"<<unsigned(l.step)
        <<",\"cycle\":"<<unsigned(l.cycle)<<",\"clock\":"<<l.clock<<",\"ticks\":"<<l.ticks<<",\"hits\":"<<l.hits<<'}';
    }
    std::cout<<"]},\"profiles\":[";
    for(std::size_t i=0;i<fm1::profileCount();++i) {
      if(i){std::cout<<',';}const auto& entry=fm1::profileAt(i);
      std::cout<<"{\"id\":"<<quoted(entry.id)<<",\"name\":"<<quoted(entry.name)<<'}';
    }
    std::cout<<"]}"<<std::endl;
  }
};
}
int main(int argc,char** argv) {
  Simulator simulator;
  if(argc==3&&std::string(argv[1])=="--script") {
    std::ifstream script(argv[2]);if(!script){std::cerr<<"Cannot open simulator script\n";return 2;}
    std::string line;while(std::getline(script,line)&&simulator.command(line)){}
    return simulator.failed()?1:0;
  }
  if(argc!=1&&!(argc==2&&std::string(argv[1])=="--stdio")) {
    std::cerr<<"Usage: fm1-simulator [--stdio | --script commands.txt]\n";return 2;
  }
  std::string line;while(std::getline(std::cin,line)&&simulator.command(line)){}
  return simulator.failed()?1:0;
}
