#pragma once
#include "fm1/model.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace fm1 {
class Controller {
public:
    Controller();
    const UiModel& model() const { return model_; }
    void dispatch(UiAction);
    void tick(uint32_t milliseconds);
    void receive(const uint8_t*, std::size_t);
    bool popMidi(MidiMessage&);
    void didSend(const MidiMessage&);
    void setConnection(ConnectionState, bool mockPeer = false, const char* detail = nullptr);
    void setNotice(const char*);
    void setSetup(bool active, const char* ssid = nullptr, const char* password = nullptr);
    void selectProfile(std::size_t);
    bool takePlatformAction(UiAction&);
private:
    struct Value { int16_t value = -1; ValueOrigin origin = ValueOrigin::Unknown; };
    static constexpr std::size_t kQueueSize = 40;
    UiModel model_{};
    std::array<std::array<Value, kProfileControls>, kMaxTracks> values_{};
    std::array<Value, kMaxTracks> levels_{};
    std::array<uint8_t, kMaxTracks> channels_{};
    std::array<MidiMessage, kQueueSize> outgoing_{};
    std::size_t read_ = 0, write_ = 0, count_ = 0;
    bool platformPending_ = false;
    UiAction platformAction_{ActionType::Disconnect};
    uint32_t now_ = 0, lastPoll_ = 0;
    uint8_t editorProtocol_ = 0;
    bool editorConfirmed_ = false;
    uint8_t heldChannel_ = 1, heldNote_ = 60;
    bool heldSent_ = false;
    uint16_t touchedChannels_ = 0;
    int pendingCommand_ = -1, pendingOperation_ = -1;
    bool pendingSent_ = false;
    uint32_t requestSentAt_ = 0;
    bool active() const;
    void rebuild();
    void resetValues();
    bool send(uint8_t, uint8_t = 0, uint8_t = 0, uint8_t = 3);
    bool enqueue(const MidiMessage&);
    void setParameter(std::size_t track, std::size_t parameter, int value);
    void releaseNote();
    void panic();
    void discardPending();
    void releaseChannels(uint16_t mask);
    bool requestEditor(const MidiMessage&);
};
} // namespace fm1
