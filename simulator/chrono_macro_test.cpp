#include "chrono_panel.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>

using namespace fm1;
static void compare(const ChronoView& view,const cm_state& ref){
    assert(view.bpm==ref.bpm&&view.warp==ref.warp&&view.variation==ref.variation);
    assert(view.preset==ref.preset&&view.playing==(ref.playing!=0));
    for(unsigned i=0;i<CM_LANES;++i){
        const auto& a=view.lane[i];const auto& b=ref.lane[i];
        assert(a.length==b.length&&a.mask==b.mask&&a.step==b.step&&a.cycle==b.cycle);
        assert(a.clock==b.clock&&a.ticks==b.ticks&&a.hits==b.hits);
    }
}
int main(){
    constexpr int lo[]={40,-60,0},span[]={180,120,100};
    unsigned blocks=0;
    for(unsigned preset=0;preset<3;++preset)for(unsigned seed:{1u,42u,999u}){
        ChronoAdapter a;cm_state ref{};std::array<int16_t,257> pcm{},oracle{};
        assert(a.reset(seed,preset));cm_reset(&ref,seed,preset);a.start();cm_start(&ref);
        for(int k=0;k<3;++k)for(unsigned code=0;code<128;++code){
            const auto knob=static_cast<cm_knob>(k);
            const int value=lo[k]+(int(code)*span[k]+63)/127;
            const int before=k==0?ref.bpm:k==1?ref.warp:ref.variation;
            const auto result=a.normalized(knob,code);
            assert(result==(value==before?CM_KNOB_UNCHANGED:CM_KNOB_CHANGED));
            assert(cm_controls(&ref,k==0?unsigned(value):ref.bpm,k==1?value:ref.warp,k==2?unsigned(value):ref.variation));
            compare(a.snapshot(),ref);
            cm_knob_readback read{};assert(a.normalizedRead(knob,read));
            assert(read.applied==value&&read.canonical==((value-lo[k])*127+span[k]/2)/span[k]);
            assert(a.normalized(knob,code)==CM_KNOB_UNCHANGED);
            assert(a.normalized(knob,128)==CM_KNOB_REJECT);
            compare(a.snapshot(),ref);
            assert(a.render(pcm.data(),pcm.size()));cm_render(&ref,oracle.data(),oracle.size());
            assert(pcm==oracle);compare(a.snapshot(),ref);++blocks;
        }
    }
    ChronoAdapter a;ChronoPanel panel;std::array<int16_t,4096> pcm{};
    using Phase=TouchPhase;using Change=ChronoPanel::Change;
    auto tap=[&](int x,int y){panel.touch(Phase::Down,x,y,a,true);return panel.touch(Phase::Up,x,y,a,true);};
    assert(a.controls(42,24,12));cm_knob_readback read{};
    assert(a.normalizedRead(CM_KNOB_BPM,read)&&read.applied==42&&read.canonical==1);
    assert(tap(180,10)==Change::Edit); // Macro page does not quantize existing state.
    assert(a.snapshot().bpm==42);
    assert(tap(45,220)==Change::Flush&&a.snapshot().playing);
    assert(a.render(pcm.data(),pcm.size()));
    const auto before=a.snapshot();
    assert(panel.touch(Phase::Down,8,55,a,true)==Change::Edit&&a.snapshot().bpm==40);
    assert(panel.touch(Phase::Move,311,220,a,true)==Change::Edit&&a.snapshot().bpm==220);
    assert(panel.touch(Phase::Up,311,220,a,true)==Change::None&&a.snapshot().playing);
    assert(panel.touch(Phase::Down,8,110,a,true)==Change::Edit&&a.snapshot().warp==-60);
    panel.touch(Phase::Cancel,0,0,a,true);
    assert(panel.touch(Phase::Up,311,110,a,true)==Change::None&&a.snapshot().warp==-60);
    tap(311,110);tap(311,165);
    const auto edited=a.snapshot();
    assert(edited.warp==60&&edited.variation==100&&edited.bpm==220&&edited.playing);
    for(unsigned i=0;i<CM_LANES;++i){
        assert(edited.lane[i].clock==before.lane[i].clock&&edited.lane[i].ticks==before.lane[i].ticks);
        assert(edited.lane[i].mask==before.lane[i].mask&&edited.lane[i].length==before.lane[i].length);
    }
    assert(tap(180,10)==Change::Edit); // Pattern editing remains accessible.
    assert(tap(16,48)==Change::Edit&&a.snapshot().lane[0].mask==(before.lane[0].mask^1u));
    assert(tap(155,220)==Change::Flush&&!a.snapshot().playing);
    assert(a.render(pcm.data(),pcm.size())&&std::all_of(pcm.begin(),pcm.end(),[](auto v){return v==0;}));
    std::cout<<"Chronomorph normalized macro: "<<blocks<<" exact PCM/state blocks; captured touch, exact readback, pattern and release PASS\n";
}
