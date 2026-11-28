#include "eco_adapter.hpp"
#include <algorithm>

namespace fm1 {
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
    eco_controls(&state_,f,s,p,frozen);
    return true;
}
bool EcoAdapter::xy(unsigned f,unsigned p){
    return controls(f,state_.sharing,p,state_.ablation);
}
void EcoAdapter::start(){eco_mute(&state_,0);running_=true;}
void EcoAdapter::stop(){running_=false;eco_mute(&state_,1);}
bool EcoAdapter::render(int16_t* pcm,std::size_t frames){
    if(!pcm||frames>kMaximumFrames) return false;
    // Stopped model time is paused, not merely inaudible. No stale advancement
    // from an abandoned browser/lease; resume continues from this state.
    if(!running_) std::fill_n(pcm,frames,int16_t{0});
    else eco_render(&state_,pcm,frames);
    return true;
}
EcoView EcoAdapter::snapshot() const {
    EcoView v{};
    v.ticks=state_.ticks;v.births=state_.births;v.deaths=state_.deaths;
    v.gifts=state_.gifts;v.moves=state_.moves;v.consumed=state_.consumed;
    v.fertility=state_.fertility;v.sharing=state_.sharing;v.pressure=state_.pressure;
    v.regime=state_.regime;v.preset=preset_;v.frozen=state_.ablation!=0;v.running=running_;
    for(unsigned p=0;p<ECO_PATCHES;++p)v.resource[p]=state_.resource[p];
    for(unsigned i=0;i<ECO_AGENTS;++i){
        const auto& a=state_.a[i];
        v.agents[i]={a.alive,a.patch,a.species,a.trait,a.energy,a.envelope};
    }
    return v;
}
}
