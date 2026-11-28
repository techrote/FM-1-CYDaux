#include "eco_panel.hpp"
#include <algorithm>
#include <cstdio>

namespace fm1 {
namespace {
constexpr Rect xy{8,100,146,59}, sharing{164,112,148,20}, freeze{164,138,148,21};
constexpr Rect presets[4]={{8,182,74,22},{85,182,74,22},{162,182,74,22},{239,182,73,22}};
constexpr Rect play{8,209,95,24},stop{111,209,95,24},reset{214,209,98,24};
void button(Canvas& c,Rect r,const char* text,bool active=false){
    drawPanel(c,r,active?theme::panelActive:theme::panel,active?theme::accent:theme::line);
    drawText(c,r.x+4,r.y+5,text,active?theme::accent:theme::text,1,12);
}
unsigned axis(int p,int origin,int extent){
    return static_cast<unsigned>(std::clamp(p-origin,0,extent-1)*255/(extent-1));
}
int scale(unsigned n,unsigned cap,int w){
    return static_cast<int>(std::min(n,cap)*static_cast<unsigned>(w)/cap);
}
}
int EcoPanel::hit(int x,int y){
    if(xy.contains(x,y))return 0;
    if(sharing.contains(x,y))return 1;
    if(freeze.contains(x,y))return 2;
    for(int i=0;i<4;++i)if(presets[i].contains(x,y))return 3+i;
    if(play.contains(x,y))return 7;
    if(stop.contains(x,y))return 8;
    if(reset.contains(x,y))return 9;
    return -1;
}
EcoPanel::Change EcoPanel::touch(TouchPhase phase,int x,int y,EcoAdapter& a,bool lease){
    if(phase==TouchPhase::Cancel){cancel();return Change::None;}
    if(phase==TouchPhase::Down){capture_=hit(x,y);return Change::None;}
    const int target=capture_;
    if(phase==TouchPhase::Up)cancel();
    if(target<0)return Change::None;
    const auto v=a.snapshot();
    if(target<2 && (phase==TouchPhase::Move||phase==TouchPhase::Up)){
        // Clamped drag stays in its original semantic target, never a button.
        if(target==0)a.xy(axis(x,xy.x,xy.w),axis(y,xy.y,xy.h));
        else a.controls(v.fertility,axis(x,sharing.x,sharing.w),v.pressure,v.frozen);
        return Change::Edit;
    }
    if(phase!=TouchPhase::Up||target!=hit(x,y))return Change::None;
    if(target==2){a.controls(v.fertility,v.sharing,v.pressure,!v.frozen);return Change::Edit;}
    if(target<7){a.reset(1,static_cast<unsigned>(target-3));return Change::Flush;}
    if(target==7){if(!lease)return Change::None;a.start();return Change::Flush;}
    if(target==8){a.stop();return Change::Flush;}
    a.reset(1,v.preset);return Change::Flush;
}
void EcoPanel::render(Canvas& c,const EcoView& v)const{
    c.fillRect(0,0,kScreenWidth,kScreenHeight,theme::background);
    drawText(c,8,5,"ECOPHON / LOCAL NATIVE",theme::accent);
    drawText(c,245,5,v.running?"RUN":"STOP",v.running?theme::accent:theme::amber);
    char label[64];
    unsigned alive=0;
    for(unsigned p=0;p<ECO_PATCHES;++p){
        const Rect r{8+77*static_cast<int>(p),19,73,65};
        drawPanel(c,r,theme::panel);
        std::snprintf(label,sizeof label,"P%u %u",p+1,unsigned(v.resource[p]));
        drawText(c,r.x+3,r.y+4,label,theme::text,1,11);
        c.fillRect(r.x+3,r.y+15,67,4,theme::line);
        c.fillRect(r.x+3,r.y+15,scale(v.resource[p],ECO_CAP,67),4,theme::accent);
        // Slot position is stable within each patch, not an individual identity.
        for(unsigned i=0;i<ECO_AGENTS;++i){
            const auto& a=v.agents[i];
            if(!a.alive||a.patch!=p)continue;
            ++alive;
            const int x=r.x+4+16*static_cast<int>(i%4),y=r.y+25+17*static_cast<int>(i/4);
            const auto col=a.species?theme::amber:theme::blue;
            c.fillRect(x,y,13,10,theme::line);
            c.fillRect(x,y+10-scale(a.energy,ECO_CAP,10),13,scale(a.energy,ECO_CAP,10),col);
            c.fillRect(x,y+11,scale(a.envelope,ECO_CAP,13),2,col);
        }
    }
    drawText(c,8,89,"FERTILITY X / PRESSURE Y",theme::muted);
    drawPanel(c,xy,theme::panel);
    for(int i=1;i<4;++i){
        c.fillRect(xy.x+i*xy.w/4,xy.y+1,1,xy.h-2,theme::line);
        c.fillRect(xy.x+1,xy.y+i*xy.h/4,xy.w-2,1,theme::line);
    }
    const int px=xy.x+scale(v.fertility,255,xy.w-1),py=xy.y+scale(v.pressure,255,xy.h-1);
    c.fillRect(std::clamp(px-3,xy.x,xy.x+xy.w-7),py,7,1,theme::accent);
    c.fillRect(px,std::clamp(py-3,xy.y,xy.y+xy.h-7),1,7,theme::accent);
    std::snprintf(label,sizeof label,"F %u / P %u",unsigned(v.fertility),unsigned(v.pressure));
    drawText(c,8,163,label,theme::text);
    std::snprintf(label,sizeof label,"SHARING %u",unsigned(v.sharing));
    drawText(c,164,100,label,theme::muted);
    drawPanel(c,sharing,theme::panel);
    c.fillRect(sharing.x+2,sharing.y+4,scale(v.sharing,255,sharing.w-4),sharing.h-8,theme::blue);
    button(c,freeze,v.frozen?"ECO FROZEN":"ECO ACTIVE",v.frozen);
    std::snprintf(label,sizeof label,"ALIVE %u  T %u",alive,unsigned(v.ticks));
    drawText(c,164,163,label,theme::text,1,24);
    const char* labels[]={"MUTUAL","SCARCE","BLOOM","FROZEN"};
    for(int i=0;i<4;++i)button(c,presets[i],labels[i],v.preset==i);
    button(c,play,"START",v.running);button(c,stop,"STOP",!v.running);button(c,reset,"RESET");
}
}
