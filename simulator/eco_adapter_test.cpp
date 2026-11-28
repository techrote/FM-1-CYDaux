#include "eco_adapter.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
using fm1::EcoAdapter;
using fm1::EcoView;
static bool same(const EcoView& a,const EcoView& b){
    if(a.ticks!=b.ticks||a.births!=b.births||a.deaths!=b.deaths||
       a.gifts!=b.gifts||a.moves!=b.moves||a.consumed!=b.consumed||
       a.fertility!=b.fertility||a.sharing!=b.sharing||a.pressure!=b.pressure||
       a.regime!=b.regime||a.preset!=b.preset||a.frozen!=b.frozen||
       a.running!=b.running||a.resource!=b.resource)return false;
    for(unsigned i=0;i<ECO_AGENTS;++i){
        const auto& x=a.agents[i];const auto& y=b.agents[i];
        if(x.alive!=y.alive||x.patch!=y.patch||x.species!=y.species||
           x.trait!=y.trait||x.energy!=y.energy||x.envelope!=y.envelope)return false;
    }
    return true;
}
static void bounds(const EcoView& v){
    unsigned live=0;
    for(auto r:v.resource)assert(r<=ECO_CAP);
    for(const auto& a:v.agents){
        assert(a.alive<=1&&a.patch<ECO_PATCHES&&a.species<2&&a.trait<16);
        assert(a.energy<=ECO_CAP&&a.envelope<=3000);
        live+=a.alive;
    }
    assert(5u+v.births==live+v.deaths);
}
int main(){
    EcoAdapter a,b,frozen;
    std::array<int16_t,4096> x{},y{},z{};
    assert(a.reset(42,0)&&b.reset(42,0)&&frozen.reset(42,3));
    assert(frozen.snapshot().frozen&&frozen.snapshot().regime==0&&frozen.snapshot().preset==3);
    auto initial=a.snapshot();
    x.fill(123);
    assert(a.render(x.data(),x.size())&&same(initial,a.snapshot()));
    assert(std::all_of(x.begin(),x.end(),[](auto n){return n==0;}));
    a.start();b.start();frozen.start();
    bool audible=false,ablationDiff=false;
    for(unsigned i=0;i<128;++i){
        assert(a.render(x.data(),x.size())&&b.render(y.data(),137)&&
               b.render(y.data()+137,y.size()-137)&&frozen.render(z.data(),z.size()));
        assert(x==y&&same(a.snapshot(),b.snapshot()));
        bounds(a.snapshot());bounds(frozen.snapshot());
        audible|=std::any_of(x.begin(),x.end(),[](auto n){return n!=0;});
        ablationDiff|=x!=z;
        assert(std::all_of(x.begin(),x.end(),[](auto n){return n>=-24000&&n<=24000;}));
    }
    assert(audible&&ablationDiff&&a.snapshot().births>0);
    const auto f=frozen.snapshot();
    assert(f.births==0&&f.deaths==0&&f.gifts==0&&f.moves==0&&f.consumed==0);
    const auto before=a.snapshot();
    assert(!a.controls(256,0,255,0)&&!a.controls(0,0,255,2)&&!a.xy(0,256));
    assert(!a.reset(0,4)&&!a.render(nullptr,0)&&!a.render(x.data(),4097));
    assert(same(before,a.snapshot()));
    assert(a.xy(0,255)&&a.snapshot().sharing==before.sharing);
    assert(a.controls(0,0,255,0));
    bool interventionDiff=false;
    for(unsigned i=0;i<128;++i){
        assert(a.render(x.data(),x.size())&&b.render(y.data(),y.size()));
        interventionDiff|=x!=y;bounds(a.snapshot());
    }
    assert(interventionDiff&&a.snapshot().deaths>before.deaths);
    a.disconnect();const auto stopped=a.snapshot();assert(!stopped.running);
    assert(a.render(x.data(),x.size())&&same(stopped,a.snapshot()));
    assert(std::all_of(x.begin(),x.end(),[](auto n){return n==0;}));
    // Start after an idle/panic interval resumes from the same sample phase.
    assert(a.reset(42,2)&&b.reset(42,2));a.start();b.start();
    assert(a.render(x.data(),137)&&b.render(y.data(),137));
    a.stop();assert(a.render(x.data(),4096));a.start();
    assert(a.render(x.data(),4096)&&b.render(y.data(),4096)&&x==y&&same(a.snapshot(),b.snapshot()));
    assert(a.snapshot().regime==2&&!a.snapshot().frozen);
    std::cout<<"Ecophon actual core: partition, bounds, population, frozen contrast, intervention, invalid rollback and pause/resume PASS\n";
}
