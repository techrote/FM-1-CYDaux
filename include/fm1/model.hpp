#pragma once
#include "fm1/profile.hpp"
#include <array>
#include <cstdint>
namespace fm1 {
enum class Page : uint8_t { Play, Mix, XY, Link };
enum class ValueOrigin : uint8_t { Unknown, Pending, Sent, Device, Demo };
enum class TransportKind : uint8_t { Demo, Ble, Wifi };
enum class ConnectionState : uint8_t { Offline, Connecting, Connected, Failed };
struct ControlView {
    const char* label = "";
    int16_t value = -1;
    ValueOrigin origin = ValueOrigin::Unknown;
    bool enabled = true;
};
struct UiModel {
    const char* firmware = "FM-1 CYDaux";
    const char* subtitle = "Touch companion";
    const char* setupHint = "";
    Page page = Page::Play;
    TransportKind transport = TransportKind::Demo;
    ConnectionState connection = ConnectionState::Offline;
    uint8_t profileIndex = 0, profiles = 1;
    uint8_t trackIndex = 0, tracks = 1, midiChannel = 1;
    std::array<const char*, kMaxTracks> trackNames{};
    std::array<ControlView, 4> controls{};
    std::array<ControlView, kMaxTracks> mix{};
    uint8_t controlBank = 0;
    uint8_t program = 0, note = 60, velocity = 100;
    uint8_t xyX = 64, xyY = 64;
    ValueOrigin xyOriginX = ValueOrigin::Unknown, xyOriginY = ValueOrigin::Unknown;
    const char* xyLabelX = "X";
    const char* xyLabelY = "Y";
    bool xyEnabled = true;
    bool noteHeld = false, playing = false, playingKnown = false;
    bool mockPeer = false, bleSupported = false, syncSupported = false;
    bool transportSupported = false, programSupported = false;
    bool setupActive = false;
    char setupSsid[24] = "", setupPassword[16] = "";
    uint16_t bpm = 120;
    uint32_t transmitted = 0, received = 0, revision = 0;
    char notice[72] = "Demo: no instrument needed";
};
enum class ActionType : uint8_t {
    SetPage, SelectTrack, NextProfile, SetControl, SetLevel, SetXY,
    NoteOn, NoteOff, Panic, TogglePlay, ProgramDelta, SelectTransport,
    Connect, Disconnect, Configure, ChannelDelta, Refresh, NextControlBank,
    TempoDelta
};
struct UiAction { ActionType type; int16_t a = 0; int16_t b = 0; };
} // namespace fm1
