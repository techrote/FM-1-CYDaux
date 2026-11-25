#pragma once
#include "fm1/model.hpp"
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace fm1::device {
struct LinkConfig {
    char ssid[33] = "", password[65] = "";
    char host[64] = "", token[65] = "", bleAddress[18] = "";
    char profile[16] = "melodee";
    uint16_t port = 45821;
};
enum class LinkEventKind : uint8_t { Connected, Offline, Failed, Waiting, Received, Sent };
struct LinkEvent {
    LinkEventKind kind = LinkEventKind::Offline;
    uint32_t generation = 0;
    bool mock = false;
    MidiMessage midi{};
    char detail[72] = "";
};
// Blocking radio/network calls live on a worker task, never on the touch loop.
class TransportService {
public:
    bool begin();
    uint32_t connect(TransportKind, const LinkConfig&);
    uint32_t disconnect();
    void panic();
    bool send(const MidiMessage&);
    bool poll(LinkEvent&);
    uint32_t stackHeadroom() const; // Minimum free stack bytes on pinned ESP-IDF; zero if not started.
private:
    enum class CommandKind : uint8_t { Ble, Wifi, Disconnect, Midi, Panic };
    struct Command {
        CommandKind kind = CommandKind::Disconnect;
        uint32_t generation = 0;
        uint32_t midiEpoch = 0;
        LinkConfig config{};
        MidiMessage midi{};
    };
    // BLE callbacks only copy bounded packets. The worker owns the decoder.
    struct BlePacket {
        uint32_t generation = 0;
        uint16_t size = 0;
        std::array<uint8_t, 514> data{}; // Maximum ATT value at MTU 517.
    };
    QueueHandle_t commands_ = nullptr, events_ = nullptr;
    QueueHandle_t received_ = nullptr, blePackets_ = nullptr;
    TaskHandle_t workerTask_ = nullptr;
    std::atomic<uint32_t> requested_{0};
    std::atomic<uint32_t> midiEpoch_{0}, bleGeneration_{0};
    std::atomic<bool> acceptMidi_{false}, eventOverflow_{false}, bleRxOverflow_{false};
    static void task(void*);
    void run();
    void post(LinkEvent);
};
} // namespace fm1::device
