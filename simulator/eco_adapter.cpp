#include "eco_adapter.hpp"
#include <algorithm>

namespace fm1 {
namespace {
constexpr unsigned arcFrames=ECO_RATE/10;
constexpr std::array<unsigned,3> mutualism{180,200,40}, drought{0,0,255}, recovery{255,220,0};
std::array<unsigned,3> blend(const std::array<unsigned,3>& from,const std::array<unsigned,3>& to,unsigned n){
    std::array<unsigned,3> out{};
    for(unsigned i=0;i<3;++i)out[i]=(from[i]*(40-n)+to[i]*n)/40;
    return out;
}
}
bool EcoAdapter::reset(uint32_t seed,unsigned preset){
    if(preset>3) return false;
    eco_reset(&state_,seed,preset);
    // The upstream live fixture implements preset 3 as regime 0 + ablation.
    if(preset==3) eco_controls(&state_,state_.fertility,state_.sharing,state_.pressure,1);
    preset_=static_cast<uint8_t>(preset);
    stop();
    return true;
}
bool EcoAdapter::controls(unsigned f,unsigned s,unsigned p,unsigned frozen){
    if(f>255||s>255||p>255||frozen>1) return false;
    cancelArc();
    eco_controls(&state_,f,s,p,frozen);
    return true;
}
bool EcoAdapter::xy(unsigned f,unsigned p){
    return controls(f,state_.sharing,p,state_.ablation);
}
void EcoAdapter::start(){eco_mute(&state_,0);running_=true;}
void EcoAdapter::stop(){cancelArc();running_=false;eco_mute(&state_,1);}
bool EcoAdapter::startArc(){
    if(!running_||state_.ablation)return false;
    arcFrom_={state_.fertility,state_.sharing,state_.pressure};
    arcBlock_=arcRemaining_=0;arcActive_=true;
    return true;
}
void EcoAdapter::cancelArc(){arcActive_=false;arcBlock_=arcRemaining_=0;}
void EcoAdapter::beginArcBlock(){
    const unsigned n=arcBlock_+1;
    const auto target=n<=40?blend(arcFrom_,mutualism,n):n<=80?blend(mutualism,drought,n-40):
        n<=120?drought:n<=160?blend(drought,recovery,n-120):recovery;
    eco_controls(&state_,target[0],target[1],target[2],0);
    arcRemaining_=arcFrames;
}
bool EcoAdapter::render(int16_t* pcm,std::size_t frames){
    if(!pcm||frames>kMaximumFrames) return false;
    // Stopped model time is paused, not merely inaudible. No stale advancement
    // from an abandoned browser/lease; resume continues from this state.
    if(!running_) std::fill_n(pcm,frames,int16_t{0});
    else {
        while(frames&&arcActive_){
            if(!arcRemaining_)beginArcBlock();
            const auto n=std::min(frames,static_cast<std::size_t>(arcRemaining_));
            eco_render(&state_,pcm,n);pcm+=n;frames-=n;arcRemaining_-=static_cast<unsigned>(n);
            if(!arcRemaining_&&++arcBlock_==240)arcActive_=false;
        }
        if(frames)eco_render(&state_,pcm,frames);
    }
    return true;
}
EcoView EcoAdapter::snapshot() const {
    EcoView v{};
    v.ticks=state_.ticks;v.births=state_.births;v.deaths=state_.deaths;
    v.gifts=state_.gifts;v.moves=state_.moves;v.consumed=state_.consumed;
    v.fertility=state_.fertility;v.sharing=state_.sharing;v.pressure=state_.pressure;
    v.arcBlock=static_cast<uint16_t>(arcBlock_);v.arcActive=arcActive_;
    v.regime=state_.regime;v.preset=preset_;v.frozen=state_.ablation!=0;v.running=running_;
    for(unsigned p=0;p<ECO_PATCHES;++p)v.resource[p]=state_.resource[p];
    for(unsigned i=0;i<ECO_AGENTS;++i){
        const auto& a=state_.a[i];
        v.agents[i]={a.alive,a.patch,a.species,a.trait,a.energy,a.envelope};
    }
    return v;
}
}
