#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <XPT2046_Touchscreen.h>
#include <esp_system.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include "fm1/controller.hpp"
#include "fm1/touch.hpp"
#include "fm1/ui.hpp"
#include "transport.hpp"

// ESP32-2432S028R: display and resistive touch use separate SPI buses.
namespace {
using namespace fm1;
using namespace fm1::device;
TFT_eSPI display;
SPIClass touchBus(VSPI);
XPT2046_Touchscreen touch(FM1_TOUCH_CS,FM1_TOUCH_IRQ);
Preferences preferences;
WebServer portal(80);
Controller controller;
TransportService radioLink;
LinkConfig config;
TouchCalibration calibration;
bool linkAvailable=false, portalActive=false, pendingPortal=false;
bool pendingCalibration=false, pendingSend=false, fingerDown=false;
bool closePortalSoon=false;
bool bootArmed=true;
uint32_t generation=0, lastDraw=0, drawnRevision=UINT32_MAX;
uint32_t lastTouch=0, lastContact=0, closePortalAt=0, bootHeldAt=0;
uint32_t lastDiagnostics=0;
int lastX=0, lastY=0;
MidiMessage deferredMessage;
char setupSsid[24]="", setupPassword[16]="", formNonce[17]="";

void onAction(void*,UiAction action) {
    if (action.type==ActionType::Panic) {
        pendingSend=false;
        if (linkAvailable && controller.model().connection==ConnectionState::Connected)
            radioLink.panic();
    }
    const auto oldProfile=controller.model().profileIndex;
    controller.dispatch(action);
    if (oldProfile!=controller.model().profileIndex)
        preferences.putUChar("profile",controller.model().profileIndex);
}
Ui ui(onAction);

// A 25 KiB (25,600-byte) strip avoids a 150 KiB frame buffer on the original ESP32.
// All six strips use exactly the same renderer as the native simulator.
class DisplayCanvas final : public Canvas {
public:
    static constexpr int rows=40;
    void setStrip(int y) { top_=y; }
    void fillRect(int x,int y,int width,int height,uint16_t color) override {
        const int left=std::max(0,x), right=std::min(kScreenWidth,x+width);
        const int top=std::max(top_,y), bottom=std::min(top_+rows,y+height);
        if (left>=right || top>=bottom) return;
        for (int row=top;row<bottom;++row)
            std::fill(pixels_+(row-top_)*kScreenWidth+left,
                      pixels_+(row-top_)*kScreenWidth+right,color);
    }
    uint16_t* pixels() { return pixels_; }
private:
    int top_=0;
    uint16_t pixels_[kScreenWidth*rows]{};
} canvas;

void render() {
    const auto& model=controller.model();
    if (model.revision==drawnRevision || millis()-lastDraw<33) return;
    for (int y=0;y<kScreenHeight;y+=DisplayCanvas::rows) {
        canvas.setStrip(y); ui.render(canvas,model);
        display.pushImage(0,y,kScreenWidth,DisplayCanvas::rows,canvas.pixels());
    }
    drawnRevision=model.revision; lastDraw=millis();
}

struct CalibrationRecord {
    uint32_t magic=0x43594131; // CYA1: this structure's storage schema.
    TouchCalibration value{};
};
bool loadCalibration() {
    CalibrationRecord record;
    if (preferences.getBytesLength("touch")!=sizeof(record) ||
        preferences.getBytes("touch",&record,sizeof(record))!=sizeof(record) ||
        record.magic!=0x43594131 || !record.value.valid) return false;
    const auto center=record.value.map(2048,2048);
    if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
        center.x< -640 || center.x>960 || center.y< -480 || center.y>720) return false;
    calibration=record.value; return true;
}
void calibrationScreen(TouchPoint target,unsigned point,const char* message) {
    display.fillScreen(theme::background);
    display.setTextColor(theme::text,theme::background);
    display.setTextSize(2); display.setCursor(64,76); display.print("TOUCH SETUP");
    display.setTextSize(1); display.setCursor(45,108); display.print(message);
    display.setCursor(65,128); display.print("Press and hold each cross");
    display.setCursor(96,146); display.printf("POINT %u OF 4",point);
    const int x=static_cast<int>(target.x),y=static_cast<int>(target.y);
    display.drawCircle(x,y,12,theme::accent);
    display.drawFastHLine(x-18,y,37,theme::accent);
    display.drawFastVLine(x,y-18,37,theme::accent);
}
TouchPoint captureCalibrationPoint() {
    for (;;) {
        while (touch.touched()) { touch.getPoint(); delay(10); }
        delay(100);
        while (!touch.touched()) delay(10);
        delay(40);
        long x=0,y=0; unsigned samples=0;
        for (unsigned i=0;i<12;++i) {
            if (!touch.touched()) break;
            const auto p=touch.getPoint();
            if (p.z>=300) { x+=p.x; y+=p.y; ++samples; }
            delay(8);
        }
        if (samples>=7) {
            while (touch.touched()) { touch.getPoint(); delay(10); }
            return {float(x)/samples,float(y)/samples};
        }
    }
}
void calibrateTouch() {
    ui.cancelTouch(); fingerDown=false;
    constexpr std::array<TouchPoint,3> targets{{{26,26},{293,26},{26,213}}};
    constexpr TouchPoint check{293,213};
    const char* message="Three points, then a separate check";
    for (;;) {
        std::array<TouchPoint,3> raw{};
        for (unsigned i=0;i<3;++i) {
            calibrationScreen(targets[i],i+1,message);
            raw[i]=captureCalibrationPoint();
        }
        TouchCalibration next;
        if (!next.fit(raw,targets)) { message="Please retry with a steady touch"; continue; }
        calibrationScreen(check,4,"Check the fourth corner");
        if (!next.verify(captureCalibrationPoint(),check)) {
            message="Check missed. Let's try again"; continue;
        }
        calibration=next;
        CalibrationRecord record; record.value=next;
        preferences.putBytes("touch",&record,sizeof(record));
        break;
    }
    drawnRevision=UINT32_MAX; bootHeldAt=0; bootArmed=false;
}

template<std::size_t N> void copy(char (&target)[N],const String& value) {
    value.toCharArray(target,N);
}
String htmlEscape(const char* value) {
    String result;
    for (const char* p=value;*p;++p) {
        switch (*p) {
            case '&': result+="&amp;"; break;
            case '<': result+="&lt;"; break;
            case '>': result+="&gt;"; break;
            case '\"': result+="&quot;"; break;
            case '\'': result+="&#39;"; break;
            default: result+=*p;
        }
    }
    return result;
}
String field(const char* label,const char* name,const char* value,unsigned max,bool secret=false) {
    String result="<label>"; result+=label;
    result+="<input name='"; result+=name; result+="' maxlength='"; result+=max;
    result+="' type='"; result+=secret?"password":"text";
    result+="' value='"; result+=secret?"":htmlEscape(value); result+="'></label>";
    return result;
}
void portalPage() {
    String body;
    body.reserve(3500);
    body="<!doctype html><html lang='en'><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>FM-1 CYDaux setup</title><style>"
        "body{font:17px system-ui;background:#0d131c;color:#e8eff7;max-width:34rem;margin:2rem auto;padding:0 1rem}"
        "label{display:block;margin:1rem 0}input,button{font:inherit;padding:.7rem;border:1px solid #45566a;border-radius:.4rem;box-sizing:border-box}"
        "input{display:block;width:100%;background:#182330;color:#e8eff7}input[type=checkbox]{display:inline;width:auto}"
        "button{background:#42deb8;color:#0d131c;font-weight:bold}small{color:#a5b7ca}</style>"
        "<h1>FM-1 CYDaux</h1><p>Connection setup for your CYD touchscreen.</p>"
        "<form method='post' action='/save'><input type='hidden' name='nonce' value='";
    body+=formNonce; body+="'><h2>Wi-Fi / USB bridge</h2>";
    body+=field("Home Wi-Fi name (2.4 GHz)","ssid",config.ssid,32);
    body+=field("Wi-Fi password","password","",64,true);
    body+="<small>Leave a password blank to keep its saved value.</small>";
    body+=field("Computer address (IP or hostname)","host",config.host,63);
    char portText[8]; std::snprintf(portText,sizeof(portText),"%u",config.port);
    body+=field("Bridge TCP port","port",portText,5);
    body+=field("Bridge token","token","",64,true);
    body+="<label><input type='checkbox' name='openwifi' value='1'> My Wi-Fi has no password</label>"
        "<h2>Melodee Bluetooth</h2>";
    body+=field("Optional exact BLE address (AA:BB:CC:DD:EE:FF)","ble",config.bleAddress,17);
    body+="<small>Leave empty to find one Melodee advertising the MIDI service. Select VISIBLE on the FM-1.</small>"
        "<label><input type='checkbox' name='calibrate' value='1'> Calibrate touch again after saving</label>"
        "<button>Save and return to CYD</button></form>"
        "<p>After saving, choose BLE or Wi-Fi on the CYD Link page and press Connect.</p>"
        "<small>The setup network closes when you leave setup. This page does not change the FM-1 firmware.</small></html>";
    portal.sendHeader("Cache-Control","no-store");
    portal.send(200,"text/html",body);
}
bool validBleAddress(const String& value) {
    if (value.isEmpty()) return true;
    if (value.length()!=17) return false;
    for (unsigned i=0;i<17;++i) {
        if (i%3==2) { if (value[i]!=':') return false; }
        else if (!isxdigit(static_cast<unsigned char>(value[i]))) return false;
    }
    return true;
}
void savePortal() {
    if (portal.arg("nonce")!=formNonce) { portal.send(403,"text/plain","Reopen the setup page and try again."); return; }
    const String ssid=portal.arg("ssid"), password=portal.arg("password"), host=portal.arg("host");
    const String portString=portal.arg("port"), token=portal.arg("token"), address=portal.arg("ble");
    bool digits=portString.length()>0 && portString.length()<=5;
    for (unsigned i=0;i<portString.length();++i) digits=digits && isdigit(static_cast<unsigned char>(portString[i]));
    const long port=portString.toInt();
    if (ssid.length()>32 || password.length()>64 || host.length()>63 || token.length()>64 ||
        !digits || port<1 || port>65535 || !validBleAddress(address)) {
        portal.send(400,"text/plain","Check field lengths, port (1..65535) and the optional Bluetooth address."); return;
    }
    copy(config.ssid,ssid); copy(config.host,host); copy(config.bleAddress,address);
    if (password.length()) copy(config.password,password);
    if (portal.hasArg("openwifi")) config.password[0]=0;
    if (token.length()) copy(config.token,token);
    config.port=static_cast<uint16_t>(port);
    preferences.putString("ssid",config.ssid); preferences.putString("wifi_pass",config.password);
    preferences.putString("host",config.host); preferences.putString("token",config.token);
    preferences.putString("ble",config.bleAddress); preferences.putUShort("port",config.port);
    pendingCalibration=portal.hasArg("calibrate");
    portal.sendHeader("Cache-Control","no-store");
    portal.send(200,"text/html","<meta name='viewport' content='width=device-width,initial-scale=1'><h1>Saved</h1><p>Return to your CYD. This setup network will now close.</p>");
    closePortalSoon=true; closePortalAt=millis();
}
void stopPortal() {
    if (portalActive) {
        portal.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF);
    }
    portalActive=false; pendingPortal=false; closePortalSoon=false;
    controller.setSetup(false);
}
void startPortal() {
    pendingPortal=false;
    std::snprintf(formNonce,sizeof(formNonce),"%08lx%08lx",
                  static_cast<unsigned long>(esp_random()),static_cast<unsigned long>(esp_random()));
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(setupSsid,setupPassword,1,false,1)) {
        WiFi.mode(WIFI_OFF); controller.setNotice("Setup network failed; try Setup again"); return;
    }
    portal.begin(); portalActive=true;
    controller.setSetup(true,setupSsid,setupPassword);
    controller.setNotice("Join setup Wi-Fi and open 192.168.4.1");
}
void loadConfig() {
    copy(config.ssid,preferences.getString("ssid",""));
    copy(config.password,preferences.getString("wifi_pass",""));
    copy(config.host,preferences.getString("host",""));
    copy(config.token,preferences.getString("token",""));
    copy(config.bleAddress,preferences.getString("ble",""));
    config.port=preferences.getUShort("port",45821);
    if (!config.port) config.port=45821;
    std::snprintf(setupSsid,sizeof(setupSsid),"CYDaux-%04X",static_cast<unsigned>(ESP.getEfuseMac()&0xffff));
    copy(setupPassword,preferences.getString("ap_pass",""));
    if (std::strlen(setupPassword)<8) {
        std::snprintf(setupPassword,sizeof(setupPassword),"%08lx%04lx",
                      static_cast<unsigned long>(esp_random()),static_cast<unsigned long>(esp_random()&0xffff));
        preferences.putString("ap_pass",setupPassword);
    }
    controller.selectProfile(preferences.getUChar("profile",0)%profileCount());
}

void handlePlatformAction(UiAction action) {
    if (!linkAvailable) { controller.setNotice("Radio task unavailable; restart the CYD"); return; }
    pendingSend=false;
    if (action.type==ActionType::Configure) {
        if (portalActive) { stopPortal(); controller.setNotice("Setup closed"); return; }
        pendingPortal=true;
        generation=radioLink.disconnect();
        controller.setConnection(ConnectionState::Offline,false,"Opening setup network...");
    } else if (action.type==ActionType::Connect) {
        stopPortal(); pendingCalibration=false;
        std::snprintf(config.profile,sizeof(config.profile),"%s",profileAt(controller.model().profileIndex).id);
        generation=radioLink.connect(controller.model().transport,config);
        controller.setConnection(ConnectionState::Connecting,false,"Starting connection...");
    } else if (action.type==ActionType::Disconnect || action.type==ActionType::SelectTransport) {
        stopPortal(); pendingCalibration=false;
        generation=radioLink.disconnect();
        controller.setConnection(ConnectionState::Offline,false,
            controller.model().transport==TransportKind::Demo?"Demo: no instrument needed":"Disconnected");
    }
}
void pollLinks() {
    LinkEvent event;
    while (radioLink.poll(event)) {
        if (event.generation!=generation) continue;
        switch (event.kind) {
            case LinkEventKind::Connected:
                pendingSend=false;
                controller.setConnection(ConnectionState::Connected,event.mock,event.detail); break;
            case LinkEventKind::Waiting:
                pendingSend=false;
                controller.setConnection(ConnectionState::Connecting,event.mock,event.detail); break;
            case LinkEventKind::Offline:
                controller.setConnection(ConnectionState::Offline,false,event.detail);
                if (pendingPortal) startPortal();
                else if (pendingCalibration) { pendingCalibration=false; calibrateTouch(); }
                break;
            case LinkEventKind::Failed:
                pendingSend=false;
                controller.setConnection(ConnectionState::Failed,false,event.detail); break;
            case LinkEventKind::Received:
                controller.receive(event.midi.data.data(),event.midi.size); break;
            case LinkEventKind::Sent:
                controller.didSend(event.midi); break;
        }
    }
}
void pollTouch() {
    if (millis()-lastTouch<10) return;
    lastTouch=millis();
    bool pressed=false;
    if (touch.touched()) {
        const auto raw=touch.getPoint();
        if (raw.z>=300) {
            const auto point=calibration.map(raw.x,raw.y);
            if (std::isfinite(point.x) && std::isfinite(point.y)) {
                lastX=std::max(0,std::min(319,static_cast<int>(std::lround(point.x))));
                lastY=std::max(0,std::min(239,static_cast<int>(std::lround(point.y))));
                pressed=true; lastContact=millis();
            }
        }
    }
    if (pressed) {
        ui.touch(fingerDown?TouchPhase::Move:TouchPhase::Down,lastX,lastY,controller.model());
        fingerDown=true;
    } else if (fingerDown && millis()-lastContact>=40) {
        ui.touch(TouchPhase::Up,lastX,lastY,controller.model()); fingerDown=false;
    }
}
} // namespace

void setup() {
    Serial.begin(115200);
    pinMode(0,INPUT_PULLUP);
    for (const int pin:{FM1_RGB_RED,FM1_RGB_GREEN,FM1_RGB_BLUE}) { pinMode(pin,OUTPUT); digitalWrite(pin,HIGH); }
    pinMode(TFT_BL,OUTPUT); digitalWrite(TFT_BL,LOW);
    display.init(); display.setRotation(1); display.setSwapBytes(true);
    display.fillScreen(theme::background); digitalWrite(TFT_BL,HIGH);
    touchBus.begin(FM1_TOUCH_SCK,FM1_TOUCH_MISO,FM1_TOUCH_MOSI,FM1_TOUCH_CS);
    touch.begin(touchBus); touch.setRotation(0);
    preferences.begin("cydaux",false); loadConfig();
    if (!loadCalibration()) calibrateTouch();
    portal.on("/",HTTP_GET,portalPage); portal.on("/save",HTTP_POST,savePortal);
    portal.onNotFound([] { portal.send(404,"text/plain","Open http://192.168.4.1/ for setup."); });
    linkAvailable=radioLink.begin();
    if (!linkAvailable) controller.setNotice("Radio task unavailable; demo is available");
    Serial.printf("FM-1 CYDaux ready. Free heap: %u bytes.\n",ESP.getFreeHeap());
}

void loop() {
    if (linkAvailable) pollLinks();
    if (portalActive) portal.handleClient();
    if (closePortalSoon && millis()-closePortalAt>=700) {
        stopPortal(); controller.setNotice("Saved. Choose BLE or Wi-Fi, then Connect");
        if (pendingCalibration) { pendingCalibration=false; calibrateTouch(); }
    }
    pollTouch();
    UiAction action{ActionType::Disconnect};
    if (controller.takePlatformAction(action)) handlePlatformAction(action);
    controller.tick(millis());
    if (linkAvailable && controller.model().connection==ConnectionState::Connected) {
        // Keep one unsent message locally when the worker queue is full.
        // Transport delivery, not queue admission, is what changes PENDING to SENT.
        for (unsigned i=0;i<4;++i) {
            if (!pendingSend && !controller.popMidi(deferredMessage)) break;
            pendingSend=true;
            if (!radioLink.send(deferredMessage)) break;
            pendingSend=false;
        }
    }
    if (digitalRead(0)==LOW && bootArmed && !pendingCalibration) {
        if (!bootHeldAt) bootHeldAt=millis();
        if (millis()-bootHeldAt>=1800) {
            stopPortal(); pendingCalibration=true; pendingSend=false;
            controller.setConnection(ConnectionState::Offline,false,"Releasing link for touch calibration...");
            bootHeldAt=0; bootArmed=false;
            if (linkAvailable) generation=radioLink.disconnect();
            else { pendingCalibration=false; calibrateTouch(); }
        }
    } else if (digitalRead(0)!=LOW) { bootHeldAt=0; bootArmed=true; }
    if (millis()-lastDiagnostics>=30000) {
        lastDiagnostics=millis();
        // ESP-IDF reports stack high-water marks in bytes. No credentials are logged.
        Serial.printf("memory heap=%u min=%u largest=%u loop_stack=%u radio_stack=%u\n",
            ESP.getFreeHeap(),ESP.getMinFreeHeap(),ESP.getMaxAllocHeap(),
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),radioLink.stackHeadroom());
    }
    render(); delay(2);
}
