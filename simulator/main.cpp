#include "fm1/controller.hpp"
#include "fm1/ui.hpp"
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
  }
  return "unknown";
}
class Simulator {
 public:
  Simulator():ui_(dispatch,this){}
  bool failed()const{return failed_;}
  bool command(const std::string& input) {
    std::istringstream stream(input);std::string name;stream>>name;
    if(name.empty()||name[0]=='#')return true;
    try {
      if(name=="quit"){std::cout<<"{\"ok\":true,\"quit\":true}"<<std::endl;return false;}
      if(name=="down"||name=="move"||name=="up"||name=="tap") {
        int x,y;
        if(!(stream>>x>>y)||x< -4096||x>4096||y< -4096||y>4096)
          throw std::runtime_error("touch needs bounded x and y");
        if(name=="tap") {
          ui_.touch(fm1::TouchPhase::Down,x,y,controller_.model());
          ui_.touch(fm1::TouchPhase::Up,x,y,controller_.model());
        }else{
          const auto phase=name=="down"?fm1::TouchPhase::Down:
                            name=="move"?fm1::TouchPhase::Move:fm1::TouchPhase::Up;
          ui_.touch(phase,x,y,controller_.model());
        }
      }else if(name=="cancel") {
        ui_.cancelTouch();
      }else if(name=="tick") {
        uint32_t elapsed=0;
        if(!(stream>>elapsed)||elapsed>60000)throw std::runtime_error("tick needs 0..60000 ms");
        now_+=elapsed;controller_.tick(now_);
      }else if(name=="profile") {
        std::string id;stream>>id;const int index=fm1::profileIndex(id.c_str());
        if(index<0)throw std::runtime_error("unknown profile");
        ui_.cancelTouch();controller_.selectProfile(static_cast<std::size_t>(index));
      }else if(name=="page") {
        std::string page;stream>>page;const std::vector<std::string> pages{"play","mix","xy","link"};
        const auto found=std::find(pages.begin(),pages.end(),page);
        if(found==pages.end())throw std::runtime_error("unknown page");
        ui_.cancelTouch();
        controller_.dispatch({fm1::ActionType::SetPage,static_cast<int16_t>(found-pages.begin())});
      }else if(name=="transport") {
        std::string mode;stream>>mode;const std::vector<std::string> modes{"demo","ble","wifi"};
        const auto found=std::find(modes.begin(),modes.end(),mode);
        if(found==modes.end())throw std::runtime_error("unknown transport");
        ui_.cancelTouch();
        controller_.dispatch({fm1::ActionType::SelectTransport,static_cast<int16_t>(found-modes.begin())});
      }else if(name=="connect") {
        controller_.dispatch({fm1::ActionType::Connect});
      }else if(name=="disconnect") {
        ui_.cancelTouch();controller_.dispatch({fm1::ActionType::Disconnect});
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
        settle();ui_.render(frame_,controller_.model());frame_.writePpm(path);
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
  uint32_t now_=0;
  bool echo_=true,failed_=false;
  std::deque<fm1::MidiMessage> history_;
  static void dispatch(void* context,fm1::UiAction action) {
    static_cast<Simulator*>(context)->controller_.dispatch(action);
  }
  void settle() {
    fm1::UiAction action{fm1::ActionType::Refresh};
    while(controller_.takePlatformAction(action)) {
      if(action.type==fm1::ActionType::Connect&&controller_.model().transport!=fm1::TransportKind::Demo)
        controller_.setConnection(fm1::ConnectionState::Connected,true,
                                  "Simulator mock link: no instrument connected");
      else if(action.type==fm1::ActionType::Disconnect)
        controller_.setConnection(fm1::ConnectionState::Offline,false,"Disconnected in simulator");
      else if(action.type==fm1::ActionType::Configure)
        controller_.setSetup(!controller_.model().setupActive,"CYDaux-DEMO","demo1234");
    }
    fm1::MidiMessage message;
    while(controller_.popMidi(message)) {
      // The mock wire explicitly accepts the message. Hardware adapters must
      // call didSend only after their real transport has accepted it.
      controller_.didSend(message);history_.push_back(message);
      if(history_.size()>24)history_.pop_front();
      if(echo_&&controller_.model().mockPeer&&message.size==3&&
         (message.data[0]&0xf0u)==0xb0u&&message.data[1]<120)
        controller_.receive(message.data.data(),message.size);
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
      <<",\"controlBank\":"<<static_cast<unsigned>(m.controlBank)<<",\"noteHeld\":"<<(m.noteHeld?"true":"false")
      <<",\"xyX\":"<<static_cast<unsigned>(m.xyX)<<",\"xyY\":"<<static_cast<unsigned>(m.xyY)
      <<",\"xyOriginX\":"<<quoted(origin(m.xyOriginX))<<",\"xyOriginY\":"<<quoted(origin(m.xyOriginY))
      <<",\"xyEnabled\":"<<(m.xyEnabled?"true":"false")
      <<",\"playing\":"<<(m.playing?"true":"false")<<",\"playingKnown\":"<<(m.playingKnown?"true":"false")
      <<",\"bleSupported\":"<<(m.bleSupported?"true":"false")
      <<",\"transportSupported\":"<<(m.transportSupported?"true":"false")
      <<",\"programSupported\":"<<(m.programSupported?"true":"false")
      <<",\"tx\":"<<m.transmitted<<",\"rx\":"<<m.received<<",\"revision\":"<<m.revision
      <<",\"milliseconds\":"<<now_<<",\"notice\":"<<quoted(m.notice)<<",\"controls\":[";
    for(std::size_t i=0;i<m.controls.size();++i) {
      const auto& value=m.controls[i];if(i){std::cout<<',';}
      std::cout<<"{\"label\":"<<quoted(value.label)<<",\"value\":"<<value.value
        <<",\"origin\":"<<quoted(origin(value.origin))<<",\"enabled\":"<<(value.enabled?"true":"false")<<'}';
    }
    std::cout<<"],\"midi\":[";
    for(std::size_t i=0;i<history_.size();++i) {
      if(i){std::cout<<',';}std::cout<<'[';const auto& message=history_[i];
      for(std::size_t j=0;j<message.size;++j) {
        if(j){std::cout<<',';}std::cout<<static_cast<unsigned>(message.data[j]);
      }
      std::cout<<']';
    }
    std::cout<<"],\"profiles\":[";
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
