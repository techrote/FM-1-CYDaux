#include "chrono_adapter.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>

using fm1::ChronoAdapter;
using fm1::ChronoPattern;
using fm1::ChronoView;
static bool same(const ChronoView& a,const ChronoView& b){
    if(a.bpm!=b.bpm||a.warp!=b.warp||a.variation!=b.variation||
       a.preset!=b.preset||a.playing!=b.playing)return false;
    for(unsigned i=0;i<CM_LANES;++i){
        const auto& x=a.lane[i];const auto& y=b.lane[i];
        if(x.length!=y.length||x.mask!=y.mask||x.step!=y.step||
           x.cycle!=y.cycle||x.clock!=y.clock||x.ticks!=y.ticks||x.hits!=y.hits)return false;
    }
    return true;
}
int main(){
    ChronoAdapter a,b;
    std::array<int16_t,4096> pcm{},reference{};
    assert(a.reset(42,0)&&b.reset(42,0));
    assert(a.render(pcm.data(),pcm.size()));
    assert(std::all_of(pcm.begin(),pcm.end(),[](auto v){return v==0;}));
    a.start();b.start();
    for(unsigned i=0;i<80;++i){
        assert(a.render(pcm.data(),pcm.size()));
        assert(b.render(reference.data(),137));
        assert(b.render(reference.data()+137,pcm.size()-137));
        assert(pcm==reference&&same(a.snapshot(),b.snapshot()));
        for(const auto& lane:a.snapshot().lane){
            assert(lane.step<lane.length&&lane.clock<CM_CYCLE&&lane.cycle<4);
        }
    }
    const auto before=a.snapshot();
    assert(before.lane[0].ticks!=before.lane[1].ticks);
    assert(!a.reset(42,3)&&!a.controls(221,0,0)&&!a.edit(3,1,1));
    assert(!a.edit(0,16,65536)&&!a.edit(0,0,0));
    assert(!a.scene(180,-45,80,{{{9,257},{16,65535},{1,2}}}));
    assert(same(before,a.snapshot()));
    assert(!a.render(nullptr,1)&&!a.render(pcm.data(),4097));
    assert(same(before,a.snapshot()));
    assert(a.scene(180,-45,80,{{{9,257},{16,65535},{1,1}}}));
    const auto edited=a.snapshot();
    assert(edited.playing&&edited.bpm==180&&edited.warp==-45);
    assert(edited.lane[0].mask==257&&edited.lane[1].mask==65535);
    for(unsigned i=0;i<CM_LANES;++i){
        assert(edited.lane[i].clock==before.lane[i].clock);
        assert(edited.lane[i].ticks==before.lane[i].ticks);
    }
    bool different=false, audible=false;
    for(unsigned i=0;i<32;++i){
        assert(a.render(pcm.data(),pcm.size())&&b.render(reference.data(),reference.size()));
        different|=pcm!=reference;
        audible|=std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;});
    }
    assert(different&&audible);
    a.disconnect();
    assert(!a.snapshot().playing&&a.render(pcm.data(),pcm.size()));
    assert(std::all_of(pcm.begin(),pcm.end(),[](auto v){return v==0;}));
    assert(a.reset(42,2)&&!a.snapshot().playing);
    std::cout<<"Chronomorph actual core: block partition, atomic scene, clocks, bounds, PCM and disconnect PASS\n";
}
