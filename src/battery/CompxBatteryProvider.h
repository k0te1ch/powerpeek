#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "battery/CompxProtocol.h"
#include "battery/DeviceInfo.h"

namespace peek {

// Mice on 2.4 GHz receivers running Compx firmware -- VGN, VXE, ATK, Pulsar and the other
// brands that ship the same dongle. They publish no standard HID battery usage and nothing in
// the device tree; the level is only available by asking the receiver in its own protocol,
// which CompxProtocol describes.
//
// The receiver is found by the shape of its vendor collection on a vendor id known to carry
// this firmware, and it is sent two requests, both reads: the battery query on every poll, and
// once per arrival the query that names the paired mouse's model. Nothing else in the protocol
// is ever written.
//
// A receiver that answers with nothing -- or not at all -- has a mouse that is off, asleep or
// out of range. It stays in the poll with no level rather than leaving it: these mice sleep
// after a minute or so of stillness, and a device that left and came back every time would
// chime a disconnection at every pause. What it never becomes is a reading of 0 %.
class CompxBatteryProvider {
public:
    // Looks for receivers again. It walks every HID interface on the machine, so the monitor
    // runs it on the device-tree schedule rather than on every poll.
    void rescan();

    // Asks every receiver the last rescan found. A receiver whose mouse is silent costs its full
    // reply budget, a fraction of a second, which is paid on the monitor thread.
    std::vector<DeviceInfo> poll();

private:
    enum class Status {
        Unknown,
        Answering,
        Silent,
        Unreachable,
    };

    struct Receiver {
        std::wstring path;
        std::wstring id;
        std::wstring name;
        std::wstring product;
        std::uint16_t vendorId = 0;
        std::uint16_t productId = 0;
        bool viaReceiver = true;
        // What the mouse said it is, once it has; and how often it has been asked.
        std::optional<compx::ModelId> model;
        int modelAttempts = 0;
        // What the last poll got. Kept so that the log records a change, not every poll.
        Status status = Status::Unknown;
        // When the mouse last gave a reading, so a silent one says how long it has been.
        std::chrono::system_clock::time_point lastAnswered{};
    };

    // Names the receiver from what is known of it so far; true when the model is on record.
    static bool identify(Receiver& receiver);
    static void askModel(Receiver& receiver);

    std::vector<Receiver> m_receivers;
};

}  // namespace peek
