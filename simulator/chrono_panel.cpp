#include "chrono_panel.hpp"
#include <algorithm>
#include <cstdio>

namespace fm1 {
namespace {
constexpr Rect play{8,209,95,24}, stop{111,209,95,24}, reset{214,209,98,24};
constexpr Rect macroPage{154,2,66,18};
constexpr Rect slider(int knob){return {8,40+54*knob,304,30};}
constexpr const char* knobLabels[]={"TEMPO BPM","WARP %","VARIATION %"};
constexpr Rect preset[3]={{8,182,95,22},{111,182,95,22},{214,182,98,22}};
constexpr Rect adjust[6]={{8,157,35,21},{65,157,35,21},{111,157,35,21},
                          {168,157,35,21},{214,157,35,21},{277,157,35,21}};
constexpr Rect cell(int lane,int step){return {8+19*step,40+38*lane,17,18};}
constexpr Rect minusLength(int lane){return {220,23+38*lane,38,15};}
constexpr Rect plusLength(int lane){return {270,23+38*lane,42,15};}
void button(Canvas& c,Rect r,const char* label,bool active=false){
    drawPanel(c,r,active?theme::panelActive:theme::panel,active?theme::accent:theme::line);
    drawText(c,r.x+3,r.y+4,label,active?theme::accent:theme::text,1,16);
}
}
int ChronoPanel::hit(int x,int y,const ChronoView& v) const {
    if(macroPage.contains(x,y))return 66;
    if(macros_){
        for(int i=0;i<3;++i)if(slider(i).contains(x,y))return 67+i;
        if(play.contains(x,y))return 63;
        if(stop.contains(x,y))return 64;
        if(reset.contains(x,y))return 65;
        return -1;
    }
    for(int lane=0;lane<3;++lane){
        for(int step=0;step<v.lane[lane].length;++step)
            if(cell(lane,step).contains(x,y))return lane*16+step;
        if(minusLength(lane).contains(x,y))return 48+lane*2;
        if(plusLength(lane).contains(x,y))return 49+lane*2;
    }
    for(int i=0;i<6;++i)if(adjust[i].contains(x,y))return 54+i;
    for(int i=0;i<3;++i)if(preset[i].contains(x,y))return 60+i;
    if(play.contains(x,y))return 63;
    if(stop.contains(x,y))return 64;
    if(reset.contains(x,y))return 65;
    return -1;
}
ChronoPanel::Change ChronoPanel::slide(int x,ChronoAdapter& a){
    const auto r=slider(captured_-67);
    const unsigned offset=static_cast<unsigned>(std::clamp(x,r.x,r.x+r.w-1)-r.x);
    const unsigned code=(offset*127u+unsigned(r.w-1)/2u)/unsigned(r.w-1);
    return a.normalized(static_cast<cm_knob>(captured_-67),code)==CM_KNOB_CHANGED?Change::Edit:Change::None;
}
ChronoPanel::Change ChronoPanel::touch(TouchPhase phase,int x,int y,ChronoAdapter& a,bool lease){
    const auto v=a.snapshot();
    if(phase==TouchPhase::Cancel){cancel();return Change::None;}
    if(phase==TouchPhase::Down){captured_=hit(x,y,v);return captured_>=67?slide(x,a):Change::None;}
    if(captured_>=67&&(phase==TouchPhase::Move||phase==TouchPhase::Up)){
        const auto changed=slide(x,a);
        if(phase==TouchPhase::Up)cancel();
        return changed;
    }
    if(phase!=TouchPhase::Up)return Change::None;
    const int target=captured_;cancel();
    if(target<0||target!=hit(x,y,v))return Change::None;
    if(target==66){macros_=!macros_;return Change::Edit;}
    if(target<48){
        const unsigned lane=static_cast<unsigned>(target/16),step=static_cast<unsigned>(target%16);
        return a.edit(lane,v.lane[lane].length,v.lane[lane].mask^(1u<<step))?Change::Edit:Change::None;
    }
    if(target<54){
        const unsigned lane=static_cast<unsigned>((target-48)/2);
        const int length=std::clamp(static_cast<int>(v.lane[lane].length)+(target%2?1:-1),1,16);
        return a.edit(lane,static_cast<unsigned>(length),v.lane[lane].mask&((1u<<length)-1u))?Change::Edit:Change::None;
    }
    if(target<60){
        int bpm=v.bpm,warp=v.warp,variation=v.variation;
        const int delta=target%2?1:-1;
        if(target<56)bpm=std::clamp(bpm+delta*5,40,220);
        else if(target<58)warp=std::clamp(warp+delta*5,-60,60);
        else variation=std::clamp(variation+delta*5,0,100);
        return a.controls(static_cast<unsigned>(bpm),warp,static_cast<unsigned>(variation))?Change::Edit:Change::None;
    }
    if(target<63){a.reset(1,static_cast<unsigned>(target-60));return Change::Flush;}
    if(target==63){if(!lease)return Change::None;a.start();return Change::Flush;}
    if(target==64){a.stop();return Change::Flush;}
    a.reset(1,v.preset);return Change::Flush;
}
void ChronoPanel::render(Canvas& c,const ChronoView& v) const {
    c.fillRect(0,0,kScreenWidth,kScreenHeight,theme::background);
    drawText(c,8,5,"CHRONOMORPH / NATIVE",theme::accent,1);
    drawText(c,224,5,v.playing?"RUN":"STOP",v.playing?theme::accent:theme::amber,1);
    button(c,macroPage,macros_?"PATTERN":"MACROS",macros_);
    char label[64];
    if(macros_){
        const int applied[]={int(v.bpm),int(v.warp),int(v.variation)};
        for(int i=0;i<3;++i){
            uint8_t code=0;
            if(!cm_knob_encode(static_cast<cm_knob>(i),applied[i],&code))continue;
            const auto r=slider(i);
            std::snprintf(label,sizeof label,"%s  %d / CODE %u",knobLabels[i],applied[i],unsigned(code));
            drawText(c,r.x,r.y-13,label,theme::text,1);
            drawPanel(c,r,theme::panel,theme::line);
            c.fillRect(r.x+2,r.y+2,int(unsigned(r.w-4)*code/127u),r.h-4,theme::panelActive);
            const int cursor=r.x+2+int(unsigned(r.w-5)*code/127u);
            c.fillRect(cursor,r.y+2,2,r.h-4,theme::accent);
        }
        drawText(c,8,198,"LOCAL CORE / 7-BIT REQUEST",theme::muted,1);
        button(c,play,"START",v.playing);button(c,stop,"STOP",!v.playing);button(c,reset,"RESET");
        return;
    }
    for(int lane=0;lane<3;++lane){
        const auto& l=v.lane[lane];
        std::snprintf(label,sizeof label,"L%d LEN %u STEP %u C%u",lane+1,unsigned(l.length),unsigned(l.step)+1,unsigned(l.cycle)+1);
        drawText(c,8,24+38*lane,label,theme::text,1);
        button(c,minusLength(lane),"-LEN");button(c,plusLength(lane),"+LEN");
        for(int step=0;step<16;++step){
            const auto r=cell(lane,step);
            const bool valid=step<l.length,hitBit=valid&&(l.mask&(1u<<step)),current=valid&&v.playing&&step==l.step;
            drawPanel(c,r,hitBit?theme::panelActive:theme::panel,current?theme::amber:theme::line);
            if(valid)c.fillRect(r.x+3,r.y+4,r.w-6,r.h-8,hitBit?theme::accent:theme::muted);
            if(current)c.fillRect(r.x+2,r.y+r.h-3,r.w-4,2,theme::amber);
        }
        c.fillRect(8,60+38*lane,304,2,theme::line);
        c.fillRect(8,60+38*lane,static_cast<int>(static_cast<uint64_t>(l.clock)*304u/CM_CYCLE),2,theme::blue);
    }
    // Compact +/- controls; all semantic values are visible, not seven-bit aliases.
    for(int i=0;i<6;++i)button(c,adjust[i],i%2?"+":"-");
    std::snprintf(label,sizeof label,"%u",unsigned(v.bpm));drawText(c,44,164,label,theme::accent,1);
    std::snprintf(label,sizeof label,"%d",int(v.warp));drawText(c,147,164,label,theme::accent,1);
    std::snprintf(label,sizeof label,"%u",unsigned(v.variation));drawText(c,250,164,label,theme::accent,1);
    drawText(c,8,146,"BPM",theme::muted);drawText(c,111,146,"WARP",theme::muted);drawText(c,214,146,"VARIATION",theme::muted);
    for(int i=0;i<3;++i){std::snprintf(label,sizeof label,"SCENE %d",i+1);button(c,preset[i],label,v.preset==i);}
    button(c,play,"START",v.playing);button(c,stop,"STOP",!v.playing);button(c,reset,"RESET");
}
}
