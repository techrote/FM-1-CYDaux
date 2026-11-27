#include "fm1/controller.hpp"
#include "fm1/ui.hpp"
#include "virtual_instrument.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <locale>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
double micros(Clock::time_point start) {
    return std::chrono::duration<double,std::micro>(Clock::now()-start).count();
}
struct Series {
    std::vector<double> samples;
    void add(double value) { samples.push_back(value); }
    void json() {
        std::sort(samples.begin(),samples.end());
        const auto count=samples.size();
        std::cout<<"{\"count\":"<<count<<",\"p50\":"<<(count?samples[count/2]:0)
            <<",\"p95\":"<<(count?samples[std::min(count-1,count*95/100)]:0)
            <<",\"max\":"<<(count?samples.back():0)<<'}';
    }
};
class StripCanvas final:public fm1::Canvas {
public:
    static constexpr int rows=40;
    std::array<uint16_t,320*rows> pixels{};
    int top=0;
    void fillRect(int x,int y,int w,int h,uint16_t color)override {
        const int left=std::max(0,x),right=std::min(320,x+w);
        const int first=std::max(top,y),last=std::min(top+rows,y+h);
        if(left>=right||first>=last)return;
        for(int row=first;row<last;++row)
            std::fill(pixels.begin()+(row-top)*320+left,pixels.begin()+(row-top)*320+right,color);
    }
};
struct Probe {
    fm1::Controller controller;
    fm1::VirtualInstrument instrument;
    fm1::Ui ui;
    StripCanvas strip;
    uint64_t transmitted=0,replies=0,sampleRemainder=0,totalSamples=0;
    uint32_t now=0,maximumDrain=0;
    unsigned peak=0,zeroBlocks=0,coalescedBurstMessages=0;
    double energy=0;
    uint64_t frameChecksum=1469598103934665603ULL;
    Series audio,control,render,integrated;
    Probe():ui(dispatch,this){}
    static void dispatch(void* context,fm1::UiAction action) {
        static_cast<Probe*>(context)->controller.dispatch(action);
    }
    unsigned drain() {
        fm1::MidiMessage message;
        unsigned count=0,budget=256;
        while(controller.popMidi(message)) {
            if(!budget--)throw std::runtime_error("controller did not settle within bounded budget");
            if(!instrument.receive(message))throw std::runtime_error("native instrument rejected admitted controller event");
            controller.didSend(message);++transmitted;++count;
            fm1::MidiMessage reply;
            while(instrument.popReply(reply)) {controller.receive(reply.data.data(),reply.size);++replies;}
        }
        maximumDrain=std::max(maximumDrain,count);
        return count;
    }
    void connect(const char* profile) {
        controller.setNativeAvailable(true);
        const int index=fm1::profileIndex(profile);
        if(index<0||!instrument.select(profile))throw std::runtime_error("unknown actual instrument profile");
        controller.selectProfile(static_cast<std::size_t>(index));
        controller.dispatch({fm1::ActionType::SelectTransport,static_cast<int16_t>(fm1::TransportKind::Native)});
        fm1::UiAction action{fm1::ActionType::Refresh};
        while(controller.takePlatformAction(action)){}
        controller.setConnection(fm1::ConnectionState::Connected);
        drain();
        if(!controller.model().companionReady)throw std::runtime_error("instrument capability negotiation failed");
    }
    void frame() {
        const auto started=Clock::now();
        for(int y=0;y<240;y+=StripCanvas::rows){
            strip.top=y;ui.render(strip,controller.model());
            // Consume pixels so the compiler cannot erase the measured raster.
            frameChecksum=(frameChecksum^strip.pixels[1111])*1099511628211ULL;
        }
        render.add(micros(started));
    }
    void run(const char* profile,unsigned blocks) {
        connect(profile);
        controller.dispatch({fm1::ActionType::SetPage,static_cast<int16_t>(fm1::Page::XY)});
        ui.touch(fm1::TouchPhase::Down,40,120,controller.model());
        for(unsigned i=0;i<1000;++i)
            ui.touch(fm1::TouchPhase::Move,14+static_cast<int>(i*37%202),66+static_cast<int>(i*19%122),controller.model());
        coalescedBurstMessages=drain();
        // The first XY pair precedes its attack; the subsequent move pair
        // cannot coalesce across that musical ordering barrier. Five messages
        // are therefore the bounded maximum: XY, Note, latest XY.
        if(coalescedBurstMessages>5)throw std::runtime_error("1000 XY moves were not bounded/coalesced");
        ui.cancelTouch();drain();
        bool finger=false;
        uint32_t nextFrame=0;
        std::array<int16_t,128> samples{};
        for(unsigned block=0;block<blocks;++block){
            const auto allStarted=Clock::now();
            const bool xyMode=(block/512)%2==0;
            if(block%512==0){
                if(finger){ui.cancelTouch();finger=false;}
                controller.dispatch({fm1::ActionType::Panic});drain();
                if(xyMode){
                    controller.dispatch({fm1::ActionType::SetPage,static_cast<int16_t>(fm1::Page::XY)});
                    ui.touch(fm1::TouchPhase::Down,40,120,controller.model());finger=true;
                }else{
                    controller.dispatch({fm1::ActionType::SetPage,static_cast<int16_t>(fm1::Page::Sequence)});
                    controller.dispatch({fm1::ActionType::ToggleSequence});
                }
            }
            const auto controlStarted=Clock::now();
            if(xyMode){
                ui.touch(fm1::TouchPhase::Move,14+static_cast<int>(block*17%202),
                         66+static_cast<int>(block*11%122),controller.model());
                // Percussive engines need repeated strikes, not one sustained
                // gate. Exercise the actual same captured-touch path.
                if(block%64==63){ui.cancelTouch();ui.touch(fm1::TouchPhase::Down,80,110,controller.model());}
            }else if(block%16==0){
                controller.dispatch({fm1::ActionType::SetStepVelocity,static_cast<int16_t>((block/16)%16),
                                     static_cast<int16_t>(48+(block*13)%80)});
            }
            drain();control.add(micros(controlStarted));
            const auto audioStarted=Clock::now();
            if(!instrument.render(samples.data(),samples.size()))throw std::runtime_error("actual core rendering failed");
            audio.add(micros(audioStarted));
            bool nonzero=false;
            for(const auto sample:samples){
                const int value=sample;const auto magnitude=static_cast<unsigned>(value<0?-value:value);
                peak=std::max(peak,magnitude);energy+=double(value)*value;nonzero|=sample!=0;
            }
            if(!nonzero)++zeroBlocks;
            totalSamples+=samples.size();
            sampleRemainder+=samples.size()*1000u;
            now+=static_cast<uint32_t>(sampleRemainder/instrument.sampleRate());
            sampleRemainder%=instrument.sampleRate();
            controller.tick(now);drain();
            if(now>=nextFrame){frame();nextFrame=now+33;}
            integrated.add(micros(allStarted));
        }
        if(finger)ui.cancelTouch();
        controller.dispatch({fm1::ActionType::Panic});drain();
        if(instrument.snapshot().gate||controller.model().noteHeld||controller.model().sequence.running)
            throw std::runtime_error("final panic did not retire performance ownership");
        if(peak==0||energy==0)throw std::runtime_error("actual engine produced no audio");
        std::cout<<"{\"profile\":\""<<profile<<"\",\"source_revision\":\""<<instrument.sourceRevision()
            <<"\",\"sample_rate\":"<<instrument.sampleRate()<<",\"samples\":"<<totalSamples
            <<",\"rendered_seconds\":"<<double(totalSamples)/instrument.sampleRate()
            <<",\"peak_pcm16\":"<<peak<<",\"rms_normalized\":"<<std::sqrt(energy/totalSamples)/32768.0
            <<",\"silent_128_sample_blocks\":"<<zeroBlocks
            <<",\"outgoing_messages\":"<<transmitted<<",\"reply_messages\":"<<replies
            <<",\"max_messages_per_drain\":"<<maximumDrain
            <<",\"messages_after_1000_coalesced_xy_moves\":"<<coalescedBurstMessages
            <<",\"endpoint_rejected\":"<<instrument.rejected()<<",\"endpoint_queue_overflows\":"<<instrument.queueOverflows()
            <<",\"final_gate\":false,\"final_sequence_running\":false,\"frame_checksum\":"<<frameChecksum
            <<",\"host_cpu_microseconds\":{\"audio_128_samples\":";audio.json();
        std::cout<<",\"control_dispatch_and_local_protocol\":";control.json();
        std::cout<<",\"six_strip_frame\":";render.json();
        std::cout<<",\"integrated_iteration\":";integrated.json();
        std::cout<<"}}";
    }
};
}
int main(int argc,char** argv) {
    std::cout.imbue(std::locale::classic());std::cout<<std::fixed<<std::setprecision(4);
    unsigned blocks=8192;
    if(argc>2){std::cerr<<"Usage: fm1-resource-probe [1024..65536 audio blocks]\n";return 2;}
    if(argc==2){
        try{
            std::size_t end=0;const auto parsed=std::stoul(argv[1],&end);
            if(end!=std::string(argv[1]).size()||parsed<1024||parsed>65536)throw std::runtime_error("range");
            blocks=static_cast<unsigned>(parsed);
        }catch(...){std::cerr<<"Block count must be 1024..65536\n";return 2;}
    }
    try{
        std::cout<<"{\"schema\":1,\"evidence\":\"host CPU / real native cores / shared renderer / no hardware\","
            "\"does_not_measure\":[\"physical ESP32 render/SPI time\",\"FM-1 CPU\",\"radio latency\",\"OS audio deadlines\"],"
            "\"object_bytes_host_abi\":{\"controller\":"<<sizeof(fm1::Controller)<<",\"ui\":"<<sizeof(fm1::Ui)
            <<",\"ui_model\":"<<sizeof(fm1::UiModel)<<",\"companion_session\":"<<sizeof(fm1::CompanionSession)
            <<",\"sequencer\":"<<sizeof(fm1::PerformanceSequencer)<<",\"native_instrument\":"<<sizeof(fm1::VirtualInstrument)
            <<",\"strip_pixels\":"<<sizeof(StripCanvas::pixels)<<"},\"instruments\":[";
        {Probe probe;probe.run("zaaggenz-native",blocks);}
        std::cout<<',';
        {Probe probe;probe.run("xenoresonator-native",blocks);}
        std::cout<<"]}\n";
    }catch(const std::exception& error){std::cerr<<"resource probe failed: "<<error.what()<<'\n';return 1;}
}
