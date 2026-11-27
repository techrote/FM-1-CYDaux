#include "fm1/controller.hpp"
#include "fm1/midi.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace fm1 {
namespace { int clamp7(int v) { return std::max(0,std::min(127,v)); } }
Controller::Controller() { selectProfile(0); }
const CompanionProfile* Controller::companionProfile() const {
    return companionProfileFor(profileAt(model_.profileIndex).id);
}
bool Controller::companionAllows(uint16_t capability) const {
    const auto* profile=companionProfile();
    if (!profile) return false;
    const auto available=model_.transport==TransportKind::Demo?profile->capabilities:
        (companion_.ready()?companion_.identity().capabilities:0);
    return (available & capability)==capability;
}
bool Controller::active() const {
    return model_.transport==TransportKind::Demo ||
        (model_.connection==ConnectionState::Connected && (!companionProfile() || companion_.ready()));
}
void Controller::setNativeAvailable(bool available) {
    if (!available && model_.transport==TransportKind::Native)
        dispatch({ActionType::SelectTransport,static_cast<int16_t>(TransportKind::Demo)});
    model_.nativeSupported=available;
    rebuild();
}
void Controller::setSessionSeed(uint32_t seed) {
    // Configure once before opening a transport. A live epoch is never reused.
    if (model_.connection==ConnectionState::Offline) sessionCounter_=seed & kCompanionSessionMask;
}
void Controller::setControl(std::size_t parameter, int normalized) {
    setParameter(model_.trackIndex,parameter,normalized);
}
void Controller::setNotice(const char* s) {
    std::snprintf(model_.notice,sizeof(model_.notice),"%s",s?s:"");
    ++model_.revision;
}
void Controller::setSetup(bool active,const char* ssid,const char* password) {
    if (model_.setupActive!=active) ++model_.interactionEpoch;
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
    const auto* companion=companionProfile();
    model_.companionEnabled=companion!=nullptr;
    model_.companionReady=companion && companion_.ready() && model_.connection==ConnectionState::Connected;
    model_.noteMinimum=companion?companion->noteMinimum:0;
    model_.noteMaximum=companion?companion->noteMaximum:127;
    model_.notesSupported=active() && (!companion || companionAllows(CompanionNotes));
    model_.sequenceSupported=companion && model_.notesSupported;
    model_.sequence=sequencer_.view();
    model_.presetCount=companion?companion->presetCount:0;
    model_.presetIndex=std::min<uint8_t>(model_.program,model_.presetCount?model_.presetCount-1:0);
    model_.presetNames.fill("");
    if (companion) for (std::size_t i=0;i<model_.presetNames.size() && i<companion->presetCount;++i)
        model_.presetNames[i]=companion->presetNames[i];
    model_.presetName=model_.presetCount?model_.presetNames[model_.presetIndex]:"";
    model_.telemetryAvailable=model_.companionReady && companionAllows(CompanionTelemetry) &&
                              companion_.stateFresh(now_);
    if (!model_.telemetryAvailable) {
        model_.outputPeak=0; model_.meterCount=0; model_.activeVoices=0;
        model_.telemetryMeters.fill(0); model_.telemetryOrigin=ValueOrigin::Unknown;
    }
    if (model_.transport==TransportKind::Demo) model_.sourceLabel="Demo values / no audio";
    else if (model_.companionReady) {
        switch (companion_.identity().source) {
            case CompanionSource::NativeEngine: model_.sourceLabel="Native engine"; break;
            case CompanionSource::EmulatedFirmware: model_.sourceLabel="Emulated firmware"; break;
            case CompanionSource::PhysicalDevice: model_.sourceLabel="Device-reported state"; break;
            default: model_.sourceLabel="Unknown endpoint"; break;
        }
    } else model_.sourceLabel=companion?"Endpoint not confirmed":
        (model_.mockPeer?"Simulated MIDI peer":"MIDI / partial readback");
    model_.capabilityLabel=companion?(model_.companionReady?"Extended / negotiated":"Needs CYDaux endpoint"):"Basic MIDI";
    model_.firmware=p.name; model_.subtitle=p.description; model_.setupHint=p.setupHint;
    model_.profiles=static_cast<uint8_t>(profileCount()); model_.tracks=p.tracks;
    model_.trackNames=p.trackNames; model_.midiChannel=channels_[model_.trackIndex];
    model_.bleSupported=p.directBle;
    model_.programSupported=companion?(active() && companionAllows(CompanionPresets)):p.programChange;
    model_.transportSupported=companion?model_.sequenceSupported:(model_.transport==TransportKind::Demo ||
                              (active() && (p.transport || (model_.syncSupported && model_.playingKnown))));
    model_.xyLabelX=p.parameters[0].name; model_.xyLabelY=p.parameters[1].name;
    model_.xyEnabled=active() && (p.parameterMask[model_.trackIndex]&3)==3 &&
                     (!companion || companionAllows(CompanionParameters|CompanionXY));
    const auto& x=values_[model_.trackIndex][0];
    const auto& y=values_[model_.trackIndex][1];
    model_.xyX=static_cast<uint8_t>(x.value<0?64:clamp7(x.value));
    model_.xyY=static_cast<uint8_t>(y.value<0?64:clamp7(y.value));
    model_.xyOriginX=x.origin; model_.xyOriginY=y.origin;
    for (std::size_t i=0;i<4;++i) {
        const auto parameter=i+model_.controlBank*4;
        const auto& v=values_[model_.trackIndex][parameter];
        model_.controls[i]={p.parameters[parameter].name,v.value,v.origin,
                            active() && ((p.parameterMask[model_.trackIndex]>>parameter)&1) &&
                            (!companion || (companionAllows(CompanionParameters) && companion->parameters[parameter].writable))};
        if (companion && v.value>=0)
            formatCompanionParameter(companion->parameters[parameter],v.value,
                model_.controls[i].display,sizeof(model_.controls[i].display));
    }
    for (std::size_t t=0;t<kMaxTracks;++t)
        model_.mix[t]={p.trackNames[t],levels_[t].value,levels_[t].origin,active()&&t<p.tracks&&!companion};
    ++model_.revision;
}
void Controller::selectProfile(std::size_t index) {
    if (index>=profileCount()) return;
    ++model_.interactionEpoch;
    const bool wasLinked=model_.connection==ConnectionState::Connected ||
                         model_.connection==ConnectionState::Connecting;
    stopSequence();
    discardPending();
    releaseNote();
    companion_.reset();
    model_.profileIndex=static_cast<uint8_t>(index); model_.trackIndex=0;
    model_.controlBank=0; model_.program=0; model_.playing=false; model_.playingKnown=false;
    model_.syncSupported=false; editorProtocol_=0; editorConfirmed_=false;
    pendingCommand_=-1; pendingOperation_=-1; pendingSent_=false;
    channels_=profileAt(index).channels;
    model_.xyTouchGate=companionProfile()!=nullptr; model_.fullscreenXY=false;
    model_.themeIndex=companionProfile()?static_cast<uint8_t>(companionProfile()->instrument):0;
    if (const auto* companion=companionProfile()) {
        model_.note=static_cast<uint8_t>(std::max(int(companion->noteMinimum),
            std::min(int(companion->noteMaximum),int(model_.note))));
        for (std::size_t i=0;i<kPerformanceSteps;++i)
            sequencer_.setNote(static_cast<int>(i),std::max(int(companion->noteMinimum),
                std::min(int(companion->noteMaximum),int(sequencer_.view().steps[i].note))));
    }
    if (!companionProfile() && (model_.page==Page::Sequence || model_.page==Page::Presets)) model_.page=Page::Play;
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
    CompanionPacket incoming;
    const bool extended=decodeCompanion(m.data.data(),m.size,incoming);
    if (m.size==3 && (m.data[0]&0xf0)==0xb0 && m.data[1]<120) {
        for (std::size_t n=0;n<count_;++n) {
            auto& q=outgoing_[(read_+n)%kQueueSize];
            if (q.size==3 && q.data[0]==m.data[0] && q.data[1]==m.data[1]) { q=m; return true; }
        }
    }
    if (count_==kQueueSize) {
        if (extended && (incoming.op==CompanionOp::Panic ||
            (incoming.op==CompanionOp::Note && incoming.size==2 && incoming.payload[1]==0))) {
            discardPending();
            sequencer_.abandonAfterPanic(); heldSent_=false; model_.noteHeld=false;
            const auto emergency=companion_.panic();
            if (!emergency.size) return false;
            outgoing_[0]=emergency; read_=0; write_=1; count_=1;
            return true;
        }
        if ((m.data[0]&0xf0)==0x80 || ((m.data[0]&0xf0)==0xb0 && m.data[1]>=120)) {
            uint16_t mask=touchedChannels_ | (1u<<(m.data[0]&15));
            for (std::size_t n=0;n<count_;++n) {
                const auto& q=outgoing_[(read_+n)%kQueueSize];
                if ((q.data[0]&0xf0)==0x80 || (q.data[0]&0xf0)==0x90)
                    mask|=1u<<(q.data[0]&15);
            }
            releaseChannels(mask);
            return true;
        }
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
    companion_.sent(m,now_);
    CompanionPacket extended;
    if (decodeCompanion(m.data.data(),m.size,extended) && extended.session==companion_.session() &&
        extended.op==CompanionOp::SetParameter && extended.size==2 && extended.payload[0]<kProfileControls) {
        auto& value=values_[0][extended.payload[0]];
        if (value.origin==ValueOrigin::Pending && value.value==extended.payload[1]) value.origin=ValueOrigin::Sent;
    }
    if (m.size==3 && (m.data[0]&0xf0)==0x90 && m.data[2])
        touchedChannels_|=1u<<(m.data[0]&15);
    if (pendingCommand_>=0 && m.size>=6 && m.data[0]==0xf0 && m.data[4]==pendingCommand_ &&
        (pendingCommand_!=77 || m.data[5]==pendingOperation_)) {
        pendingSent_=true; requestSentAt_=now_;
    }
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
    if (!active() || track>=p.tracks || parameter>=kProfileControls ||
        !((p.parameterMask[track]>>parameter)&1)) return;
    value=clamp7(value);
    const auto& spec=p.parameters[parameter];
    const auto* companion=companionProfile();
    if (companion) {
        if (!companionAllows(CompanionParameters) || !companion->parameters[parameter].writable) return;
        value=companionParameterNormalized(companion->parameters[parameter],
              companionParameterValue(companion->parameters[parameter],value));
        if (model_.transport!=TransportKind::Demo) {
            bool coalesced=false;
            for (std::size_t n=count_;n>0;--n) {
                auto& queued=outgoing_[(read_+n-1)%kQueueSize];
                CompanionPacket packet;
                // Notes, presets and reads are ordering barriers. Coalescing
                // across one would move a later edit before an earlier event.
                if (!decodeCompanion(queued.data.data(),queued.size,packet) ||
                    packet.session!=companion_.session() || packet.op!=CompanionOp::SetParameter) break;
                if (packet.payload[0]==parameter) {
                    // Keep the admitted command's sequence. Unsent touch
                    // samples must not burn through the 14-bit replay window.
                    packet.payload[1]=static_cast<uint8_t>(value);
                    queued=encodeCompanion(packet); coalesced=true; break;
                }
            }
            if (!coalesced && (count_==kQueueSize ||
                !enqueue(companion_.setParameter(static_cast<uint8_t>(parameter),static_cast<uint8_t>(value))))) return;
        }
    } else if (model_.transport!=TransportKind::Demo && !send(0xb0|(channels_[track]-1),spec.cc,value)) return;
    values_[track][parameter]={static_cast<int16_t>(value),
                              model_.transport==TransportKind::Demo?ValueOrigin::Demo:ValueOrigin::Pending};
    if (spec.cc==7) levels_[track]=values_[track][parameter];
    rebuild();
}
void Controller::releaseNote() {
    if (heldSent_) emitNote(heldChannel_,heldNote_,0);
    heldSent_=false; model_.noteHeld=false;
}
bool Controller::emitNote(uint8_t channel,uint8_t note,uint8_t velocity) {
    if (model_.transport==TransportKind::Demo) return true;
    if (companionProfile()) {
        if (!companionAllows(CompanionNotes)) return false;
        if (count_==kQueueSize && velocity) return false;
        return enqueue(companion_.note(note,velocity));
    }
    return send(static_cast<uint8_t>((velocity?0x90:0x80)|(channel-1)),note,velocity);
}
bool Controller::sequenceOutput(void* context,uint8_t status,uint8_t note,uint8_t velocity) {
    auto& controller=*static_cast<Controller*>(context);
    if (!controller.active()) return false;
    return controller.emitNote(static_cast<uint8_t>((status&15)+1),note,
                               (status&0xf0)==0x90?velocity:0);
}
void Controller::stopSequence() { sequencer_.stop(sequenceOutput,this); }
void Controller::panic() {
    ++model_.interactionEpoch;
    stopSequence();
    if (companionProfile()) {
        discardPending();
        if (model_.transport!=TransportKind::Demo && companion_.ready()) enqueue(companion_.panic());
        sequencer_.abandonAfterPanic(); heldSent_=false; model_.noteHeld=false;
        setNotice(model_.transport==TransportKind::Demo?"Demo notes released":"Endpoint panic requested");
        return;
    }
    uint16_t mask=touchedChannels_;
    const auto& p=profileAt(model_.profileIndex);
    for (std::size_t t=0;t<p.tracks;++t) mask|=1u<<(channels_[t]-1);
    releaseChannels(mask);
    setNotice(model_.transport==TransportKind::Demo?"Demo notes released":"Release notes requested");
}
void Controller::discardPending() {
    read_=write_=count_=0;
    companion_.cancelPending();
    for (auto& track:values_) for (auto& value:track)
        if (value.origin==ValueOrigin::Pending) value={-1,ValueOrigin::Unknown};
    for (auto& value:levels_)
        if (value.origin==ValueOrigin::Pending) value={-1,ValueOrigin::Unknown};
    if (!pendingSent_) { pendingCommand_=-1; pendingOperation_=-1; }
}
void Controller::releaseChannels(uint16_t mask) {
    discardPending();
    heldSent_=false; model_.noteHeld=false;
    // At most32 messages fit in the40-entry queue, including every old release.
    for (unsigned channel=0;channel<16;++channel) if (mask&(1u<<channel)) {
        send(0xb0|channel,64,0); send(0xb0|channel,123,0);
    }
}
bool Controller::requestEditor(const MidiMessage& message) {
    if (pendingCommand_>=0 || message.size<6 || !enqueue(message)) return false;
    pendingCommand_=message.data[4];
    pendingOperation_=pendingCommand_==77?message.data[5]:-1;
    pendingSent_=false;
    return true;
}
void Controller::selectPreset(int preset) {
    const auto* profile=companionProfile();
    if (!profile || !active() || !companionAllows(CompanionPresets) ||
        preset<0 || preset>=profile->presetCount) return;
    ++model_.interactionEpoch;
    stopSequence(); releaseNote();
    if (model_.transport==TransportKind::Demo) {
        model_.program=static_cast<uint8_t>(preset);
        setNotice("Preview preset name only; connect for actual sound");
    } else if (count_<kQueueSize && enqueue(companion_.setPreset(static_cast<uint8_t>(preset))))
        setNotice("Preset requested; waiting for endpoint state");
}
void Controller::dispatch(UiAction a) {
    const auto& p=profileAt(model_.profileIndex);
    switch (a.type) {
        case ActionType::SetPage:
            if (a.a>=0 && a.a<=5 && (a.a<=3 || companionProfile())) {
                ++model_.interactionEpoch;
                releaseNote(); model_.page=static_cast<Page>(a.a); model_.fullscreenXY=false;
            }
            break;
        case ActionType::SelectTrack:
            if (a.a>=0 && a.a<p.tracks) { ++model_.interactionEpoch; releaseNote(); model_.trackIndex=a.a; }
            break;
        case ActionType::NextProfile: selectProfile((model_.profileIndex+1)%profileCount()); break;
        case ActionType::NextControlBank: ++model_.interactionEpoch; model_.controlBank^=1; break;
        case ActionType::SetControl:
            if (a.a>=0 && a.a<4) setParameter(model_.trackIndex,a.a+model_.controlBank*4,a.b);
            break;
        case ActionType::SetLevel:
            if (!companionProfile() && active() && a.a>=0 && a.a<p.tracks) {
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
            if (model_.xyEnabled) {
                const int x=clamp7(a.a),y=clamp7(a.b);
                setParameter(model_.trackIndex,0,x);
                setParameter(model_.trackIndex,1,y);
            }
            break;
        case ActionType::NoteOn:
            if (model_.notesSupported) {
                stopSequence();
                releaseNote();
                if (a.b>0) {
                    model_.note=static_cast<uint8_t>(std::max(int(model_.noteMinimum),std::min(int(model_.noteMaximum),int(a.a))));
                    model_.velocity=static_cast<uint8_t>(std::min(127,int(a.b)));
                }
                heldChannel_=channels_[model_.trackIndex]; heldNote_=model_.note;
                heldSent_=emitNote(heldChannel_,heldNote_,model_.velocity);
                model_.noteHeld=model_.transport==TransportKind::Demo || heldSent_;
            }
            break;
        case ActionType::NoteOff: releaseNote(); break;
        case ActionType::Panic: panic(); break;
        case ActionType::TogglePlay:
            if (companionProfile()) {
                if (model_.sequenceSupported) {
                    releaseNote();
                    if (sequencer_.view().running) stopSequence();
                    else sequencer_.start(now_,channels_[model_.trackIndex]);
                }
            } else if (model_.transport==TransportKind::Demo) { model_.playing=!model_.playing; model_.playingKnown=true; }
            else if (active() && model_.syncSupported && model_.playingKnown)
                requestEditor(sloopLiveRequest(1,model_.playing?0:1));
            else if (active() && p.transport) {
                if (send(model_.playing?0xfc:0xfa,0,0,1)) {
                    model_.playing=!model_.playing; model_.playingKnown=false;
                    setNotice("Transport sent; state unconfirmed");
                }
            } else setNotice("Use FM-1 PLAY; this profile does not send clock");
            break;
        case ActionType::ProgramDelta:
            if (companionProfile()) selectPreset(model_.presetIndex+a.a);
            else if (active() && p.programChange) {
                const int next=std::max(0,std::min(127,model_.program+a.a));
                if (model_.transport==TransportKind::Demo || send(0xc0|(model_.midiChannel-1),next,0,2))
                    model_.program=next;
            }
            break;
        case ActionType::SelectTransport:
            if (a.a<0 || a.a>3 || (a.a==3 && (!model_.nativeSupported || !companionProfile()))) break;
            ++model_.interactionEpoch;
            stopSequence();
            discardPending(); releaseNote();
            companion_.reset(); sequencer_.abandonAfterPanic();
            model_.transport=static_cast<TransportKind>(a.a);
            model_.connection=ConnectionState::Offline; model_.mockPeer=false;
            model_.playing=false; model_.playingKnown=false; model_.syncSupported=false;
            editorConfirmed_=false; editorProtocol_=0;
            pendingCommand_=-1; pendingOperation_=-1; pendingSent_=false;
            platformAction_=a; platformPending_=true; resetValues();
            setNotice(model_.transport==TransportKind::Demo?"Demo: no instrument needed":"Connect to control the instrument");
            break;
        case ActionType::ChannelDelta:
            if (companionProfile()) { setNotice("Endpoint controls use the negotiated instrument identity"); break; }
            ++model_.interactionEpoch;
            releaseNote();
            channels_[model_.trackIndex]=static_cast<uint8_t>(1+(channels_[model_.trackIndex]-1+a.a%16+16)%16);
            resetValues(); setNotice("Channel changed; match the FM-1 settings");
            break;
        case ActionType::Connect:
            if (model_.transport==TransportKind::Demo) { setNotice("Demo: no instrument needed"); break; }
            if (model_.transport==TransportKind::Ble && !p.directBle) {
                setNotice("This firmware needs the Wi-Fi USB bridge"); break;
            }
            if (model_.transport==TransportKind::Native && (!model_.nativeSupported || !companionProfile())) break;
            platformAction_=a; platformPending_=true; break;
        case ActionType::Disconnect: stopSequence(); releaseNote(); platformAction_=a; platformPending_=true; break;
        case ActionType::Configure: stopSequence(); releaseNote(); platformAction_=a; platformPending_=true; break;
        case ActionType::Refresh:
            if (companionProfile() && model_.connection==ConnectionState::Connected) {
                if (companionRetired_) {
                    // Release tracking belongs to the transport epoch. A new
                    // wire session on the same link would leave the independent
                    // worker/bridge panic guard attached to the retired session.
                    // Reuse the normal close/release/reset/connect path instead.
                    platformAction_={ActionType::Connect}; platformPending_=true;
                    setNotice("Reconnecting endpoint and release tracking...");
                }
                else if (count_<kQueueSize) {
                    const auto message=companion_.ready()?companion_.requestState(now_):companion_.discover(now_);
                    if (message.size && !enqueue(message)) companion_.cancelPending();
                }
            } else if (active() && p.sloopLive && model_.transport==TransportKind::Wifi) requestEditor(editorInfoRequest());
            else setNotice("Values update only when the FM-1 reports them");
            break;
        case ActionType::TempoDelta:
            if (companionProfile()) sequencer_.setTempo(sequencer_.view().bpm+a.a);
            else if (model_.transport==TransportKind::Demo) model_.bpm=std::max(30,std::min(300,model_.bpm+a.a));
            else setNotice("Set tempo on the instrument");
            break;
        case ActionType::ToggleSequence:
            if (model_.sequenceSupported) {
                releaseNote();
                if (sequencer_.view().running) stopSequence();
                else sequencer_.start(now_,channels_[model_.trackIndex]);
            }
            break;
        case ActionType::SelectStep: if (companionProfile()) sequencer_.select(a.a); break;
        case ActionType::ToggleStep: if (companionProfile()) sequencer_.toggle(a.a); break;
        case ActionType::StepNoteDelta:
            if (companionProfile()) sequencer_.setNote(sequencer_.view().selected,
                std::max(int(model_.noteMinimum),std::min(int(model_.noteMaximum),
                    sequencer_.view().steps[sequencer_.view().selected].note+a.a)));
            break;
        case ActionType::StepVelocityDelta:
            if (companionProfile()) sequencer_.setVelocity(sequencer_.view().selected,
                sequencer_.view().steps[sequencer_.view().selected].velocity+a.a);
            break;
        case ActionType::StepGateDelta:
            if (companionProfile()) sequencer_.setGate(sequencer_.view().selected,
                sequencer_.view().steps[sequencer_.view().selected].gate+a.a);
            break;
        case ActionType::SetStepNote:
            if (companionProfile()) sequencer_.setNote(a.a,
                std::max(int(model_.noteMinimum),std::min(int(model_.noteMaximum),int(a.b))));
            break;
        case ActionType::SetStepVelocity: if (companionProfile()) sequencer_.setVelocity(a.a,a.b); break;
        case ActionType::SetStepGate: if (companionProfile()) sequencer_.setGate(a.a,a.b); break;
        case ActionType::SequenceLengthDelta:
            if (companionProfile()) sequencer_.setLength(sequencer_.view().length+a.a);
            break;
        case ActionType::SequenceTempoDelta:
            if (companionProfile()) sequencer_.setTempo(sequencer_.view().bpm+a.a);
            break;
        case ActionType::ToggleFullscreen:
            if (model_.page==Page::XY && companionProfile()) { ++model_.interactionEpoch; releaseNote(); model_.fullscreenXY=!model_.fullscreenXY; }
            break;
        case ActionType::NextTheme: model_.themeIndex=(model_.themeIndex+1)%3; break;
        case ActionType::SelectPreset: selectPreset(a.a); break;
        case ActionType::PresetDelta: selectPreset(model_.presetIndex+a.a); break;
        case ActionType::NoteDelta:
            releaseNote(); model_.note=static_cast<uint8_t>(std::max(int(model_.noteMinimum),
                std::min(int(model_.noteMaximum),model_.note+a.a))); break;
        case ActionType::VelocityDelta: model_.velocity=std::max(1,clamp7(model_.velocity+a.a)); break;
        case ActionType::ToggleXYGate: releaseNote(); model_.xyTouchGate=!model_.xyTouchGate; break;
    }
    rebuild();
}
bool Controller::takePlatformAction(UiAction& a) {
    if (!platformPending_) return false;
    a=platformAction_; platformPending_=false; return true;
}
void Controller::setConnection(ConnectionState state,bool mockPeer,const char* detail) {
    ++model_.interactionEpoch;
    // The transport adapter owns release of its old physical connection. No
    // old sequencer command or pending mutation may cross the new epoch.
    sequencer_.abandonAfterPanic(); companion_.reset();
    model_.connection=state; model_.mockPeer=mockPeer;
    read_=write_=count_=0; heldSent_=false; model_.noteHeld=false;
    model_.playingKnown=false; model_.playing=false; model_.syncSupported=false;
    editorConfirmed_=false; editorProtocol_=0; touchedChannels_=0;
    pendingCommand_=-1; pendingOperation_=-1; pendingSent_=false; resetValues();
    if (detail) setNotice(detail);
    else setNotice(state==ConnectionState::Connected?
                   (mockPeer?"Mock bridge: simulated instrument":"Connected: values await feedback"):"Disconnected");
    if (state==ConnectionState::Connected && profileAt(model_.profileIndex).sloopLive && model_.transport==TransportKind::Wifi)
        requestEditor(editorInfoRequest());
    else if (state==ConnectionState::Connected && companionProfile()) beginCompanion();
    rebuild();
}
void Controller::beginCompanion() {
    const auto* profile=companionProfile();
    if (!profile || !validateCompanionProfile(*profile)) return;
    sessionCounter_=(sessionCounter_+1)&kCompanionSessionMask;
    if (!sessionCounter_) sessionCounter_=1;
    companion_.begin(profile->instrument,sessionCounter_,now_);
    companionTimeoutShown_=false; companionRetired_=false; lastCompanionPoll_=now_;
    const auto request=companion_.discover(now_);
    if (request.size && !enqueue(request)) companion_.cancelPending();
    setNotice("Discovering matching companion endpoint...");
}
void Controller::applyCompanionState() {
    const auto* profile=companionProfile();
    if (!profile || !companion_.stateFresh(now_)) return;
    const auto& snapshot=companion_.snapshot();
    const auto origin=companion_.identity().source==CompanionSource::NativeEngine?ValueOrigin::Native:ValueOrigin::Device;
    for (std::size_t i=0;i<kProfileControls;++i) {
        if (snapshot.validMask & (1u<<i)) values_[0][i]={snapshot.values[i],origin};
        else values_[0][i]={-1,ValueOrigin::Unknown};
    }
    if (model_.program!=snapshot.preset) ++model_.interactionEpoch;
    model_.program=snapshot.preset;
    model_.meterCount=snapshot.meterCount;
    model_.telemetryMeters=snapshot.meters;
    model_.outputPeak=snapshot.outputPeak;
    model_.telemetryOrigin=origin;
}
void Controller::tick(uint32_t ms) {
    now_=ms;
    if (companionProfile() && model_.connection==ConnectionState::Connected) {
        companion_.tick(ms);
        if (companion_.timedOut() && !companionTimeoutShown_) {
            companionTimeoutShown_=true;
            setNotice(companion_.ready()?"Endpoint read timed out; use Refresh":"No matching endpoint; extended controls disabled");
        }
        if (companion_.ready() && !companion_.pending() && count_<kQueueSize && uint32_t(ms-lastCompanionPoll_)>=100) {
            const auto request=companion_.requestState(ms);
            if (request.size && enqueue(request)) lastCompanionPoll_=ms;
            else companion_.cancelPending();
        }
    }
    const auto oldHead=sequencer_.view().playhead;
    const auto oldRunning=sequencer_.view().running;
    sequencer_.tick(ms,sequenceOutput,this);
    if (oldHead!=sequencer_.view().playhead || oldRunning!=sequencer_.view().running ||
        (model_.telemetryAvailable && !companion_.stateFresh(ms))) rebuild();
    if (pendingCommand_>=0 && pendingSent_ && uint32_t(ms-requestSentAt_)>=1000) {
        pendingCommand_=-1; pendingOperation_=-1; pendingSent_=false;
        model_.playingKnown=false;
        setNotice("No editor reply; controls remain available");
        rebuild();
    }
    if (active() && model_.transport==TransportKind::Wifi && model_.syncSupported &&
        uint32_t(ms-lastPoll_)>=250) {
        if (requestEditor(sloopLiveRequest(0))) lastPoll_=ms;
    }
}
void Controller::receive(const uint8_t* data,std::size_t size) {
    if (model_.connection!=ConnectionState::Connected || model_.transport==TransportKind::Demo || !validMidi(data,size)) return;
    ++model_.received;
    const auto& p=profileAt(model_.profileIndex);
    if (companionProfile()) {
        const bool wasReady=companion_.ready();
        if (companion_.receive(data,size,now_)) {
            if (!wasReady && companion_.ready()) {
                setNotice("Endpoint matched: controls and state ready");
                const auto request=companion_.requestState(now_);
                if (request.size && !enqueue(request)) companion_.cancelPending();
            } else applyCompanionState();
        }
        if (wasReady && !companion_.ready()) {
            // The peer has retired our session/version. Never retain its
            // queued attack, local loop or apparent value confirmation.
            discardPending(); sequencer_.abandonAfterPanic();
            heldSent_=false; model_.noteHeld=false; ++model_.interactionEpoch;
            companionRetired_=true; resetValues();
            setNotice("Endpoint session lost; Refresh to reconnect");
        }
        rebuild();
        return;
    }
    if (data[0]==0xf0 && model_.transport==TransportKind::Wifi && p.sloopLive) {
        uint8_t version=0; bool isSloop=false;
        if (pendingCommand_==1 && pendingSent_ && parseEditorInfo(data,size,version,isSloop)) {
            pendingCommand_=-1; pendingOperation_=-1; pendingSent_=false;
            editorConfirmed_=isSloop; editorProtocol_=version;
            model_.syncSupported=isSloop&&version>=12;
            setNotice(model_.syncSupported?"SLOOP live sync ready":"Controller only: SLOOP v12 not confirmed");
            if (model_.syncSupported) requestEditor(sloopLiveRequest(0));
        } else if (pendingCommand_==77 && pendingSent_ && editorConfirmed_ && editorProtocol_>=12 &&
                   size>5 && data[5]==pendingOperation_) {
            SloopLiveState live;
            if (parseSloopLive(data,size,live)) {
                pendingCommand_=-1; pendingOperation_=-1; pendingSent_=false;
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
