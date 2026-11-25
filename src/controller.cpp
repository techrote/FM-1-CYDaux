#include "fm1/controller.hpp"
#include "fm1/midi.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace fm1 {
namespace { int clamp7(int v) { return std::max(0,std::min(127,v)); } }
Controller::Controller() { selectProfile(0); }
bool Controller::active() const {
    return model_.transport==TransportKind::Demo || model_.connection==ConnectionState::Connected;
}
void Controller::setNotice(const char* s) {
    std::snprintf(model_.notice,sizeof(model_.notice),"%s",s?s:"");
    ++model_.revision;
}
void Controller::setSetup(bool active,const char* ssid,const char* password) {
    model_.setupActive=active;
    std::snprintf(model_.setupSsid,sizeof(model_.setupSsid),"%s",ssid?ssid:"");
    std::snprintf(model_.setupPassword,sizeof(model_.setupPassword),"%s",password?password:"");
    ++model_.revision;
}
void Controller::resetValues() {
    const auto& p=profileAt(model_.profileIndex);
    const bool demo=model_.transport==TransportKind::Demo;
    for (std::size_t t=0;t<kMaxTracks;++t) {
        levels_[t]={static_cast<int16_t>(demo?100:-1),demo?ValueOrigin::Demo:ValueOrigin::Unknown};
        for (std::size_t v=0;v<kProfileControls;++v)
            values_[t][v]={static_cast<int16_t>(demo?p.parameters[v].initial:-1),
                           demo?ValueOrigin::Demo:ValueOrigin::Unknown};
    }
}
void Controller::rebuild() {
    const auto& p=profileAt(model_.profileIndex);
    model_.firmware=p.name; model_.subtitle=p.description; model_.setupHint=p.setupHint;
    model_.profiles=static_cast<uint8_t>(profileCount()); model_.tracks=p.tracks;
    model_.trackNames=p.trackNames; model_.midiChannel=channels_[model_.trackIndex];
    model_.bleSupported=p.directBle; model_.programSupported=p.programChange;
    model_.transportSupported=model_.transport==TransportKind::Demo ||
                              (active() && (p.transport || model_.syncSupported));
    for (std::size_t i=0;i<4;++i) {
        const auto parameter=i+model_.controlBank*4;
        const auto& v=values_[model_.trackIndex][parameter];
        model_.controls[i]={p.parameters[parameter].name,v.value,v.origin,active()};
    }
    for (std::size_t t=0;t<kMaxTracks;++t)
        model_.mix[t]={p.trackNames[t],levels_[t].value,levels_[t].origin,active()&&t<p.tracks};
    ++model_.revision;
}
void Controller::selectProfile(std::size_t index) {
    if (index>=profileCount()) return;
    const bool wasLinked=model_.connection==ConnectionState::Connected ||
                         model_.connection==ConnectionState::Connecting;
    read_=write_=count_=0;
    releaseNote();
    model_.profileIndex=static_cast<uint8_t>(index); model_.trackIndex=0;
    model_.controlBank=0; model_.program=0; model_.playing=false; model_.playingKnown=false;
    model_.syncSupported=false; editorProtocol_=0; editorConfirmed_=false;
    channels_=profileAt(index).channels;
    if (wasLinked) {
        model_.connection=ConnectionState::Offline;
        platformAction_={ActionType::Disconnect}; platformPending_=true;
    }
    resetValues(); rebuild();
    if (model_.transport==TransportKind::Demo) setNotice("Demo: no instrument needed");
    else setNotice("Profile changed: reconnect to the matching firmware");
}
bool Controller::enqueue(const MidiMessage& m) {
    if (!validMidi(m.data.data(),m.size) || model_.transport==TransportKind::Demo ||
        model_.connection!=ConnectionState::Connected) return false;
    if (m.size==3 && (m.data[0]&0xf0)==0xb0 && m.data[1]<120) {
        for (std::size_t n=0;n<count_;++n) {
            auto& q=outgoing_[(read_+n)%kQueueSize];
            if (q.size==3 && q.data[0]==m.data[0] && q.data[1]==m.data[1]) { q=m; return true; }
        }
    }
    if (count_==kQueueSize) {
        if ((m.data[0]&0xf0)==0x80 || ((m.data[0]&0xf0)==0xb0 && m.data[1]>=120))
            read_=write_=count_=0;
        else { setNotice("MIDI queue busy; slow the controls"); return false; }
    }
    outgoing_[write_]=m; write_=(write_+1)%kQueueSize; ++count_;
    return true;
}
bool Controller::send(uint8_t a,uint8_t b,uint8_t c,uint8_t size) {
    MidiMessage m; m.size=size; m.data[0]=a; m.data[1]=b; m.data[2]=c; return enqueue(m);
}
bool Controller::popMidi(MidiMessage& m) {
    if (!count_) return false;
    m=outgoing_[read_]; read_=(read_+1)%kQueueSize; --count_; return true;
}
void Controller::didSend(const MidiMessage& m) {
    ++model_.transmitted;
    if (m.size==3 && (m.data[0]&0xf0)==0xb0) {
        const auto& p=profileAt(model_.profileIndex);
        for (std::size_t t=0;t<p.tracks;++t) {
            if (channels_[t]!=(m.data[0]&15)+1) continue;
            for (std::size_t i=0;i<kProfileControls;++i) {
                auto& v=values_[t][i];
                if (p.parameters[i].cc==m.data[1] && v.value==m.data[2] && v.origin==ValueOrigin::Pending)
                    v.origin=ValueOrigin::Sent;
            }
            if (m.data[1]==7 && levels_[t].value==m.data[2] && levels_[t].origin==ValueOrigin::Pending)
                levels_[t].origin=ValueOrigin::Sent;
        }
    }
    rebuild();
}
void Controller::setParameter(std::size_t track,std::size_t parameter,int value) {
    const auto& p=profileAt(model_.profileIndex);
    if (!active() || track>=p.tracks || parameter>=kProfileControls) return;
    value=clamp7(value);
    const auto& spec=p.parameters[parameter];
    if (model_.transport!=TransportKind::Demo && !send(0xb0|(channels_[track]-1),spec.cc,value)) return;
    values_[track][parameter]={static_cast<int16_t>(value),
                              model_.transport==TransportKind::Demo?ValueOrigin::Demo:ValueOrigin::Pending};
    if (spec.cc==7) levels_[track]=values_[track][parameter];
    rebuild();
}
void Controller::releaseNote() {
    if (heldSent_) send(0x80|(heldChannel_-1),heldNote_,0);
    heldSent_=false; model_.noteHeld=false;
}
void Controller::panic() {
    read_=write_=count_=0;
    releaseNote();
    const auto& p=profileAt(model_.profileIndex);
    for (std::size_t t=0;t<p.tracks;++t) {
        send(0xb0|(channels_[t]-1),64,0);
        send(0xb0|(channels_[t]-1),123,0);
    }
    setNotice(model_.transport==TransportKind::Demo?"Demo notes released":"Release notes requested");
}
void Controller::dispatch(UiAction a) {
    const auto& p=profileAt(model_.profileIndex);
    switch (a.type) {
        case ActionType::SetPage:
            if (a.a>=0 && a.a<=3) { releaseNote(); model_.page=static_cast<Page>(a.a); }
            break;
        case ActionType::SelectTrack:
            if (a.a>=0 && a.a<p.tracks) { releaseNote(); model_.trackIndex=a.a; }
            break;
        case ActionType::NextProfile: selectProfile((model_.profileIndex+1)%profileCount()); break;
        case ActionType::NextControlBank: model_.controlBank^=1; break;
        case ActionType::SetControl:
            if (a.a>=0 && a.a<4) setParameter(model_.trackIndex,a.a+model_.controlBank*4,a.b);
            break;
        case ActionType::SetLevel:
            if (active() && a.a>=0 && a.a<p.tracks) {
                const int value=clamp7(a.b);
                if (model_.transport==TransportKind::Demo || send(0xb0|(channels_[a.a]-1),7,value)) {
                    levels_[a.a]={static_cast<int16_t>(value),
                                  model_.transport==TransportKind::Demo?ValueOrigin::Demo:ValueOrigin::Pending};
                    for (std::size_t i=0;i<kProfileControls;++i)
                        if (p.parameters[i].cc==7) values_[a.a][i]=levels_[a.a];
                }
            }
            break;
        case ActionType::SetXY:
            if (active()) {
                model_.xyX=clamp7(a.a); model_.xyY=clamp7(a.b);
                setParameter(model_.trackIndex,0,model_.xyX);
                setParameter(model_.trackIndex,1,model_.xyY);
            }
            break;
        case ActionType::NoteOn:
            if (active()) {
                releaseNote(); heldChannel_=channels_[model_.trackIndex]; heldNote_=model_.note;
                heldSent_=send(0x90|(heldChannel_-1),heldNote_,model_.velocity);
                model_.noteHeld=model_.transport==TransportKind::Demo || heldSent_;
            }
            break;
        case ActionType::NoteOff: releaseNote(); break;
        case ActionType::Panic: panic(); break;
        case ActionType::TogglePlay:
            if (model_.transport==TransportKind::Demo) { model_.playing=!model_.playing; model_.playingKnown=true; }
            else if (active() && model_.syncSupported)
                enqueue(sloopLiveRequest(1,model_.playing?0:1));
            else if (active() && p.transport) {
                if (send(model_.playing?0xfc:0xfa,0,0,1)) {
                    model_.playing=!model_.playing; model_.playingKnown=false;
                    setNotice("Transport sent; state unconfirmed");
                }
            } else setNotice("Use FM-1 PLAY; this profile does not send clock");
            break;
        case ActionType::ProgramDelta:
            if (active() && p.programChange) {
                const int next=std::max(0,std::min(127,model_.program+a.a));
                if (model_.transport==TransportKind::Demo || send(0xc0|(model_.midiChannel-1),next,0,2))
                    model_.program=next;
            }
            break;
        case ActionType::SelectTransport:
            if (a.a<0 || a.a>2) break;
            read_=write_=count_=0; releaseNote();
            model_.transport=static_cast<TransportKind>(a.a);
            model_.connection=ConnectionState::Offline; model_.mockPeer=false;
            model_.playing=false; model_.playingKnown=false; model_.syncSupported=false;
            editorConfirmed_=false; editorProtocol_=0;
            platformAction_=a; platformPending_=true; resetValues();
            setNotice(model_.transport==TransportKind::Demo?"Demo: no instrument needed":"Connect to control the instrument");
            break;
        case ActionType::ChannelDelta:
            releaseNote();
            channels_[model_.trackIndex]=static_cast<uint8_t>(1+(channels_[model_.trackIndex]-1+a.a%16+16)%16);
            resetValues(); setNotice("Channel changed; match the FM-1 settings");
            break;
        case ActionType::Connect:
            if (model_.transport==TransportKind::Demo) { setNotice("Demo: no instrument needed"); break; }
            if (model_.transport==TransportKind::Ble && !p.directBle) {
                setNotice("This firmware needs the Wi-Fi USB bridge"); break;
            }
            platformAction_=a; platformPending_=true; break;
        case ActionType::Disconnect: releaseNote(); platformAction_=a; platformPending_=true; break;
        case ActionType::Configure: releaseNote(); platformAction_=a; platformPending_=true; break;
        case ActionType::Refresh:
            if (active() && p.sloopLive && model_.transport==TransportKind::Wifi) enqueue(editorInfoRequest());
            else setNotice("Values update only when the FM-1 reports them");
            break;
        case ActionType::TempoDelta:
            if (model_.transport==TransportKind::Demo) model_.bpm=std::max(30,std::min(300,model_.bpm+a.a));
            else setNotice("Set tempo on the instrument");
            break;
    }
    rebuild();
}
bool Controller::takePlatformAction(UiAction& a) {
    if (!platformPending_) return false;
    a=platformAction_; platformPending_=false; return true;
}
void Controller::setConnection(ConnectionState state,bool mockPeer,const char* detail) {
    model_.connection=state; model_.mockPeer=mockPeer;
    read_=write_=count_=0; heldSent_=false; model_.noteHeld=false;
    model_.playingKnown=false; model_.playing=false; model_.syncSupported=false;
    editorConfirmed_=false; editorProtocol_=0; resetValues();
    if (detail) setNotice(detail);
    else setNotice(state==ConnectionState::Connected?
                   (mockPeer?"Mock bridge: simulated instrument":"Connected: values await feedback"):"Disconnected");
    if (state==ConnectionState::Connected && profileAt(model_.profileIndex).sloopLive && model_.transport==TransportKind::Wifi)
        enqueue(editorInfoRequest());
    rebuild();
}
void Controller::tick(uint32_t ms) {
    now_=ms;
    if (active() && model_.transport==TransportKind::Wifi && model_.syncSupported &&
        uint32_t(ms-lastPoll_)>=250) {
        lastPoll_=ms; enqueue(sloopLiveRequest(0));
    }
}
void Controller::receive(const uint8_t* data,std::size_t size) {
    if (!active() || model_.transport==TransportKind::Demo || !validMidi(data,size)) return;
    ++model_.received;
    const auto& p=profileAt(model_.profileIndex);
    if (data[0]==0xf0 && model_.transport==TransportKind::Wifi && p.sloopLive) {
        uint8_t version=0; bool isSloop=false;
        if (parseEditorInfo(data,size,version,isSloop)) {
            editorConfirmed_=isSloop; editorProtocol_=version;
            model_.syncSupported=isSloop&&version>=12;
            setNotice(model_.syncSupported?"SLOOP live sync ready":"Controller only: SLOOP v12 not confirmed");
        } else if (editorConfirmed_ && editorProtocol_>=12) {
            SloopLiveState live;
            if (parseSloopLive(data,size,live)) {
                model_.playing=live.playing; model_.playingKnown=true; model_.bpm=live.bpm;
            }
        }
    } else if (size==3 && (data[0]&0xf0)==0xb0) {
        for (std::size_t t=0;t<p.tracks;++t) {
            if (channels_[t]!=(data[0]&15)+1) continue;
            for (std::size_t i=0;i<kProfileControls;++i)
                if (p.parameters[i].cc==data[1])
                    values_[t][i]={data[2],model_.mockPeer?ValueOrigin::Demo:ValueOrigin::Device};
            if (data[1]==7) levels_[t]={data[2],model_.mockPeer?ValueOrigin::Demo:ValueOrigin::Device};
        }
    } else if (size==1 && p.transport && (data[0]==0xfa || data[0]==0xfb || data[0]==0xfc)) {
        model_.playing=data[0]!=0xfc; model_.playingKnown=true;
    }
    rebuild();
}
} // namespace fm1
