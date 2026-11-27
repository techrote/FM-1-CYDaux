#include "transport.hpp"
#include "fm1/midi.hpp"
#include "fm1/companion.hpp"
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <cctype>
#include <cstring>
#include <string>
#include <strings.h>

namespace fm1::device {
namespace {
constexpr char kMidiService[] = "03b80e5a-ede8-4b33-a751-6ce34ec4c700";
constexpr char kMidiCharacteristic[] = "7772e5db-3868-4112-a1a9-f2669d106bf3";
bool melodeeName(const std::string& name) {
    if (name.size()!=12 || name.compare(0,8,"Melodee ")!=0) return false;
    for (std::size_t i=8;i<12;++i)
        if (!std::isxdigit(static_cast<unsigned char>(name[i]))) return false;
    return true;
}
}
bool TransportService::begin() {
    commands_=xQueueCreate(16,sizeof(Command));
    events_=xQueueCreate(16,sizeof(LinkEvent)); // Sent and lifecycle events cannot be displaced by MIDI input.
    received_=xQueueCreate(32,sizeof(LinkEvent));
    blePackets_=xQueueCreate(8,sizeof(BlePacket));
    if (commands_ && events_ && received_ && blePackets_ &&
        xTaskCreatePinnedToCore(task,"fm1-radio",16384,this,1,&workerTask_,0)==pdPASS) return true;
    for (auto queue:{commands_,events_,received_,blePackets_}) if (queue) vQueueDelete(queue);
    commands_=events_=received_=blePackets_=nullptr;
    workerTask_=nullptr;
    return false;
}
uint32_t TransportService::connect(TransportKind kind,const LinkConfig& config) {
    if (kind!=TransportKind::Ble && kind!=TransportKind::Wifi) return disconnect();
    acceptMidi_.store(false); ++midiEpoch_;
    const uint32_t generation=++requested_;
    xQueueReset(commands_);
    Command command; command.generation=generation; command.config=config;
    command.kind=kind==TransportKind::Ble?CommandKind::Ble:CommandKind::Wifi;
    xQueueSend(commands_,&command,0);
    return generation;
}
uint32_t TransportService::disconnect() {
    acceptMidi_.store(false); ++midiEpoch_;
    const uint32_t generation=++requested_;
    xQueueReset(commands_);
    Command command; command.generation=generation; command.kind=CommandKind::Disconnect;
    xQueueSend(commands_,&command,0);
    return generation;
}
void TransportService::panic() {
    acceptMidi_.store(false);
    const uint32_t epoch=++midiEpoch_;
    xQueueReset(commands_);
    Command command; command.kind=CommandKind::Panic;
    command.generation=requested_.load(); command.midiEpoch=epoch;
    xQueueSend(commands_,&command,0);
}
bool TransportService::send(const MidiMessage& message) {
    if (!validMidi(message.data.data(),message.size)) return false;
    const uint32_t epoch=midiEpoch_.load();
    if (!acceptMidi_.load()) return false;
    Command command; command.kind=CommandKind::Midi;
    command.generation=requested_.load(); command.midiEpoch=epoch; command.midi=message;
    return xQueueSend(commands_,&command,0)==pdTRUE;
}
bool TransportService::poll(LinkEvent& event) {
    return xQueueReceive(events_,&event,0)==pdTRUE || xQueueReceive(received_,&event,0)==pdTRUE;
}
uint32_t TransportService::stackHeadroom() const {
    return workerTask_?static_cast<uint32_t>(uxTaskGetStackHighWaterMark(workerTask_)):0;
}
void TransportService::post(LinkEvent event) {
    const bool received=event.kind==LinkEventKind::Received;
    if (xQueueSend(received?received_:events_,&event,0)!=pdTRUE && !received)
        eventOverflow_.store(true); // Worker tears down explicitly; never silently lose a send acknowledgement.
}
void TransportService::task(void* context) { static_cast<TransportService*>(context)->run(); }
void TransportService::run() {
    WiFiClient socket;
    NimBLEClient* ble=nullptr;
    NimBLERemoteCharacteristic* characteristic=nullptr;
    bool bleStarted=false, ready=false, mock=false, helloSeen=false;
    TransportKind mode=TransportKind::Demo;
    uint32_t generation=0, lastRx=0, lastPing=0, ping=0;
    uint16_t noteChannels=0;
    CompanionReleaseTracker companionRelease;
    char line[2049]; std::size_t lineSize=0;
    StaticJsonDocument<4608> json;
    BleMidiDecoder decoder;
    auto status=[&](LinkEventKind kind,const char* detail) {
        LinkEvent event; event.kind=kind; event.generation=generation; event.mock=mock;
        std::snprintf(event.detail,sizeof(event.detail),"%s",detail); post(event);
    };
    auto transmit=[&](const MidiMessage& message)->bool {
        if (mode==TransportKind::Ble) {
            if (!ble || !ble->isConnected() || !characteristic) return false;
            uint8_t bytes[8]; const auto size=encodeBleMidi(message,millis()&8191,bytes,sizeof(bytes));
            return size && characteristic->writeValue(bytes,size,!characteristic->canWriteNoResponse());
        }
        if (mode==TransportKind::Wifi && socket.connected()) {
            json.clear(); json["type"]="midi";
            auto data=json.createNestedArray("data");
            for (std::size_t i=0;i<message.size;++i) data.add(message.data[i]);
            char output[1400]; const auto size=serializeJson(json,output,sizeof(output)-1);
            if (!size || size>=sizeof(output)-1) return false;
            output[size]='\n'; return socket.write(reinterpret_cast<uint8_t*>(output),size+1)==size+1;
        }
        return false;
    };
    auto releaseNotes=[&]() {
        bool success=true;
        const auto companionPanic=companionRelease.panicMessage();
        if (companionPanic.size) {
            if (transmit(companionPanic)) companionRelease.sent(companionPanic);
            else success=false;
        }
        for (uint8_t ch=0;ch<16;++ch) if (noteChannels&(1u<<ch)) {
            MidiMessage off; off.size=3; off.data[0]=0xb0|ch; off.data[1]=64;
            if (!transmit(off)) success=false;
            off.data[1]=123;
            if (!transmit(off)) success=false;
        }
        noteChannels=0;
        return success;
    };
    auto closeLink=[&]() {
        acceptMidi_.store(false); ++midiEpoch_;
        bleGeneration_.store(0);
        // Independent release fallback covers queued/cancelled controller events.
        if (ready) releaseNotes();
        ready=false; helloSeen=false; noteChannels=0; companionRelease.reset();
        if (ble && ble->isConnected()) ble->disconnect();
        characteristic=nullptr;
        socket.stop(); lineSize=0;
        xQueueReset(received_); xQueueReset(blePackets_); decoder.reset();
        bleRxOverflow_.store(false);
        WiFi.disconnect(false,false);
        mode=TransportKind::Demo;
    };
    auto fail=[&](const char* detail) { closeLink(); status(LinkEventKind::Failed,detail); };

    for (;;) {
        vTaskDelay(1); // Keep the radio core's idle task runnable under a saturated TX queue.
        if (eventOverflow_.exchange(false)) {
            closeLink();
            xQueueReset(events_);
            status(LinkEventKind::Failed,"Link event queue overflow; reconnect");
        }
        Command command;
        if (xQueueReceive(commands_,&command,pdMS_TO_TICKS(5))==pdTRUE) {
            if (command.kind==CommandKind::Midi) {
                if (command.generation==generation && command.midiEpoch==midiEpoch_.load() && ready) {
                    if (transmit(command.midi)) {
                        companionRelease.sent(command.midi);
                        if (command.midi.size==3 && (command.midi.data[0]&0xf0)==0x90 && command.midi.data[2])
                            noteChannels|=1u<<(command.midi.data[0]&15);
                        LinkEvent sent; sent.kind=LinkEventKind::Sent; sent.generation=generation;
                        sent.mock=mock; sent.midi=command.midi; post(sent);
                    } else fail("Send failed; press Connect to reconnect");
                }
            } else if (command.kind==CommandKind::Panic) {
                if (command.generation==generation && command.midiEpoch==midiEpoch_.load() && ready) {
                    // An in-flight note completes before this worker sees Panic and
                    // has already joined noteChannels, so it is released too.
                    if (!releaseNotes()) fail("Note release failed; reconnect");
                    else acceptMidi_.store(true);
                }
            } else {
                closeLink(); generation=command.generation; mock=false;
                if (command.kind==CommandKind::Disconnect) {
                    WiFi.mode(WIFI_OFF); status(LinkEventKind::Offline,"Disconnected"); continue;
                }
                if (command.generation!=requested_.load()) continue;
                if (command.kind==CommandKind::Ble) {
                    mode=TransportKind::Ble;
                    WiFi.mode(WIFI_OFF);
                    status(LinkEventKind::Waiting,"Scanning for Melodee BLE MIDI...");
                    if (!bleStarted) {
                        NimBLEDevice::init("FM-1 CYDaux");
                        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
                        // Melodee's current stack uses LE Legacy Just Works pairing.
                        NimBLEDevice::setSecurityAuth(true,false,false);
                        bleStarted=true;
                    }
                    auto* scan=NimBLEDevice::getScan();
                    scan->setActiveScan(true); scan->setInterval(60); scan->setWindow(30); scan->setMaxResults(24);
                    auto results=scan->start(3,false);
                    if (generation!=requested_.load()) { scan->clearResults(); continue; }
                    NimBLEAdvertisedDevice candidate; int matches=0;
                    for (int i=0;i<results.getCount();++i) {
                        auto device=results.getDevice(i);
                        const bool address=command.config.bleAddress[0] &&
                            strcasecmp(device.getAddress().toString().c_str(),command.config.bleAddress)==0;
                        const bool named=!command.config.bleAddress[0] && device.haveName() &&
                            melodeeName(device.getName());
                        if ((address || named) && device.isAdvertisingService(NimBLEUUID(kMidiService))) {
                            candidate=device; ++matches;
                        }
                    }
                    if (matches!=1) {
                        scan->clearResults(); fail(matches?"Several devices: set BLE address in Setup":"No Melodee found; enable VISIBLE on FM-1"); continue;
                    }
                    if (!ble) { ble=NimBLEDevice::createClient(); ble->setConnectTimeout(5); }
                    ble->setConnectionParams(12,24,0,200);
                    status(LinkEventKind::Waiting,"Connecting and subscribing to MIDI...");
                    const bool connected=ble->connect(&candidate,true);
                    scan->clearResults();
                    if (generation!=requested_.load()) { closeLink(); continue; }
                    if (!connected) { fail("Bluetooth connection failed; try Connect"); continue; }
                    auto* service=ble->getService(kMidiService);
                    characteristic=service?service->getCharacteristic(kMidiCharacteristic):nullptr;
                    if (!characteristic || !characteristic->canNotify() ||
                        (!characteristic->canWrite() && !characteristic->canWriteNoResponse())) {
                        fail("Peer has no usable BLE MIDI characteristic"); continue;
                    }
                    // NimBLE 1.4.3 subscribe() can return true when CCCD is absent.
                    // Require the actual subscription descriptor as Melodee's success gate.
                    if (!characteristic->getDescriptor(NimBLEUUID(uint16_t(0x2902)))) {
                        fail("BLE MIDI notification descriptor missing"); continue;
                    }
                    if (characteristic->canRead()) characteristic->readValue();
                    decoder.reset();
                    const uint32_t subscribedGeneration=generation;
                    bleGeneration_.store(generation);
                    const bool subscribed=characteristic->subscribe(true,
                        [this,subscribedGeneration](NimBLERemoteCharacteristic*,uint8_t* data,std::size_t size,bool) {
                            if (subscribedGeneration!=requested_.load() ||
                                subscribedGeneration!=bleGeneration_.load() || !data || !size) return;
                            BlePacket packet; packet.generation=subscribedGeneration;
                            if (size>packet.data.size()) { bleRxOverflow_.store(true); return; }
                            packet.size=static_cast<uint16_t>(size);
                            std::memcpy(packet.data.data(),data,size);
                            if (xQueueSend(blePackets_,&packet,0)!=pdTRUE) bleRxOverflow_.store(true);
                        },true);
                    if (!subscribed) { fail("MIDI subscription failed; check BLE pairing"); continue; }
                    if (generation!=requested_.load()) { closeLink(); continue; }
                    ready=true; ++midiEpoch_; acceptMidi_.store(true);
                    status(LinkEventKind::Connected,"BLE ready: values await instrument feedback");
                } else {
                    mode=TransportKind::Wifi;
                    if (!command.config.ssid[0] || !command.config.host[0] || !command.config.token[0]) {
                        fail("Use Setup to enter Wi-Fi and bridge details"); continue;
                    }
                    status(LinkEventKind::Waiting,"Joining Wi-Fi...");
                    WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(false);
                    WiFi.begin(command.config.ssid,command.config.password);
                    const uint32_t started=millis();
                    while (WiFi.status()!=WL_CONNECTED && millis()-started<12000 && generation==requested_.load())
                        vTaskDelay(pdMS_TO_TICKS(25));
                    if (generation!=requested_.load()) { closeLink(); continue; }
                    if (WiFi.status()!=WL_CONNECTED) { fail("Wi-Fi did not connect; check Setup"); continue; }
                    status(LinkEventKind::Waiting,"Connecting to MIDI bridge...");
                    socket.setTimeout(1000);
                    if (!socket.connect(command.config.host,command.config.port,3000)) {
                        fail("Bridge unreachable; check address and listener"); continue;
                    }
                    socket.setNoDelay(true);
                    json.clear(); json["type"]="hello"; json["protocol"]=1; json["token"]=command.config.token;
                    serializeJson(json,socket); socket.print('\n');
                    lastRx=lastPing=millis(); lineSize=0;
                    json.clear(); json["type"]="profile"; json["id"]=command.config.profile;
                    serializeJson(json,socket); socket.print('\n');
                }
            }
        }
        // Maintenance runs even during continuous outbound controller movement.
        if (bleRxOverflow_.exchange(false)) {
            // A missing packet may have changed running status. Discard the
            // queued prefix too; it must not restore status from before the gap.
            xQueueReset(blePackets_); decoder.reset();
        }
        BlePacket packet;
        for (unsigned budget=8;budget && xQueueReceive(blePackets_,&packet,0)==pdTRUE;--budget) {
            if (mode!=TransportKind::Ble || !ready || packet.generation!=generation ||
                generation!=requested_.load()) continue;
            struct Context { TransportService* service; uint32_t generation; } context{this,generation};
            decoder.feed(packet.data.data(),packet.size,[](const uint8_t* bytes,std::size_t size,void* opaque) {
                auto* ctx=static_cast<Context*>(opaque);
                LinkEvent event; event.kind=LinkEventKind::Received; event.generation=ctx->generation;
                event.midi.size=static_cast<uint16_t>(size);
                std::memcpy(event.midi.data.data(),bytes,size); ctx->service->post(event);
            },&context);
        }
        if (mode==TransportKind::Ble && ready && (!ble || !ble->isConnected())) {
            fail("Bluetooth disconnected; press Connect");
        }
        if (mode!=TransportKind::Wifi) continue;
        if (!socket.connected()) { fail("Bridge disconnected; press Connect"); continue; }
        unsigned budget=2048;
        while (socket.available() && budget--) {
            const int byte=socket.read();
            if (byte<0) break;
            if (byte=='\r') continue;
            if (byte!='\n') {
                if (byte==0 || lineSize>=sizeof(line)-1) { fail("Bridge sent an oversized or invalid frame"); break; }
                line[lineSize++]=static_cast<char>(byte); continue;
            }
            if (deserializeJson(json,line,lineSize)) { fail("Bridge sent malformed JSON"); break; }
            lineSize=0; lastRx=millis();
            const char* type=json["type"]|"";
            if (std::strcmp(type,"hello")==0) {
                const char* peerMode=json["mode"]|"";
                if (helloSeen || !json["protocol"].is<int>() || json["protocol"].as<int>()!=1 || !json["ready"].is<bool>() ||
                    (std::strcmp(peerMode,"mock") && std::strcmp(peerMode,"midi"))) {
                    fail("Invalid or repeated bridge hello"); break;
                }
                helloSeen=true;
                mock=std::strcmp(peerMode,"mock")==0; ready=json["ready"].as<bool>();
                ++midiEpoch_; acceptMidi_.store(ready);
                status(ready?LinkEventKind::Connected:LinkEventKind::Waiting,
                    ready?(mock?"Mock bridge: simulated FM-1":"USB bridge ready; values await feedback"):"Bridge ready; waiting for USB MIDI device");
            } else if (std::strcmp(type,"status")==0) {
                if (!helloSeen || !json["ready"].is<bool>()) { fail("Invalid bridge status"); break; }
                const bool nextReady=json["ready"].as<bool>();
                if (nextReady!=ready) {
                    acceptMidi_.store(false); ++midiEpoch_;
                    ready=nextReady; acceptMidi_.store(ready);
                    if (!ready) xQueueReset(received_);
                    status(ready?LinkEventKind::Connected:LinkEventKind::Waiting,
                        ready?(mock?"Mock bridge: simulated FM-1":"USB MIDI reconnected"):"USB MIDI unavailable; waiting for device");
                }
            } else if (std::strcmp(type,"midi")==0) {
                if (!helloSeen) { fail("Bridge MIDI arrived before hello"); break; }
                if (!ready) continue;
                auto data=json["data"].as<JsonArray>();
                if (data.isNull() || data.size()==0 || data.size()>kMaxMidiBytes) { fail("Invalid bridge MIDI frame"); break; }
                LinkEvent received; received.kind=LinkEventKind::Received; received.generation=generation; received.mock=mock;
                bool valid=true;
                for (auto value:data) {
                    if (!value.is<int>() || value.as<int>()<0 || value.as<int>()>255) { valid=false; break; }
                    received.midi.data[received.midi.size++]=value.as<uint8_t>();
                }
                if (!valid || !validMidi(received.midi.data.data(),received.midi.size)) { fail("Malformed MIDI from bridge"); break; }
                post(received);
            } else if (std::strcmp(type,"error")==0) {
                fail("Bridge rejected the session or command; check token"); break;
            } else if (std::strcmp(type,"pong")==0) {
                if (!helloSeen || !json["id"].is<uint32_t>()) { fail("Invalid bridge pong"); break; }
            } else {
                fail("Unknown bridge message"); break;
            }
        }
        if (mode!=TransportKind::Wifi) continue;
        if (millis()-lastRx>6000) { fail("Bridge timed out; press Connect"); continue; }
        if (millis()-lastPing>=2000) {
            lastPing=millis(); json.clear(); json["type"]="ping"; json["id"]=++ping;
            serializeJson(json,socket); socket.print('\n');
        }
    }
}
} // namespace fm1::device
