#include "chrono_adapter.hpp"
namespace fm1 {
bool ChronoAdapter::reset(uint32_t seed,unsigned preset) {
    if(preset>=3) return false;
    cm_reset(&state_,seed,preset);
    return true;
}
bool ChronoAdapter::controls(unsigned bpm,int warp,unsigned variation) {
    return cm_controls(&state_,bpm,warp,variation)!=0;
}
bool ChronoAdapter::edit(unsigned lane,unsigned length,unsigned mask) {
    return cm_pattern(&state_,lane,length,mask)!=0;
}
bool ChronoAdapter::scene(unsigned bpm,int warp,unsigned variation,
                         const std::array<ChronoPattern,CM_LANES>& patterns) {
    cm_state next=state_;
    if(!cm_controls(&next,bpm,warp,variation)) return false;
    for(unsigned i=0;i<CM_LANES;++i)
        if(!cm_pattern(&next,i,patterns[i].length,patterns[i].mask)) return false;
    state_=next;
    return true;
}
void ChronoAdapter::start(){cm_start(&state_);}
void ChronoAdapter::stop(){cm_stop(&state_);}
bool ChronoAdapter::render(int16_t* pcm,std::size_t frames){
    if(!pcm || frames>kMaximumFrames) return false;
    cm_render(&state_,pcm,static_cast<unsigned>(frames));
    return true;
}
ChronoView ChronoAdapter::snapshot() const {
    ChronoView view{};
    view.bpm=state_.bpm;view.warp=state_.warp;
    view.variation=state_.variation;view.preset=state_.preset;
    view.playing=state_.playing!=0;
    for(unsigned i=0;i<CM_LANES;++i){
        const auto& l=state_.lane[i];
        view.lane[i]={l.length,l.step,l.cycle,l.mask,l.clock,l.ticks,l.hits};
    }
    return view;
}
}
