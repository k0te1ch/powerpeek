#include "battery/CompxBatteryProvider.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include "battery/CompxProtocol.h"
#include "core/Logger.h"
#include "core/Win.h"

#include <hidsdi.h>
#include <setupapi.h>

namespace peek {
namespace {

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

// One request and its reply have to fit in this. Measured on a VGN receiver with its mouse
// awake, the reply came back in 1 to 23 ms -- but about one request in thirty got no reply at
// all, however long it was waited for. A lost request is asked again rather than waited on, and
// a mouse that stays silent through every attempt costs the monitor thread their sum.
constexpr std::chrono::milliseconds kAttemptBudget = 100ms;
constexpr int kAttempts = 3;

// Input reports read after the request before concluding the answer is not among them. The
// receiver also sends reports of its own, and replies to whatever a vendor utility asked.
constexpr int kReportsToScan = 4;

struct DeviceInfoSetDeleter {
    void operator()(void* set) const noexcept { SetupDiDestroyDeviceInfoList(set); }
};
using DeviceInfoSet = std::unique_ptr<void, DeviceInfoSetDeleter>;

struct PreparsedDataDeleter {
    void operator()(_HIDP_PREPARSED_DATA* data) const noexcept { HidD_FreePreparsedData(data); }
};
using PreparsedData = std::unique_ptr<_HIDP_PREPARSED_DATA, PreparsedDataDeleter>;

std::vector<std::wstring> hidInterfacePaths() {
    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO const raw =
        SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (raw == INVALID_HANDLE_VALUE) {
        log::warning(L"Could not list the HID interfaces (error {})", GetLastError());
        return {};
    }
    DeviceInfoSet const set{raw};

    std::vector<std::wstring> paths;
    SP_DEVICE_INTERFACE_DATA interfaceData{};
    interfaceData.cbSize = sizeof(interfaceData);
    for (DWORD index = 0;
         SetupDiEnumDeviceInterfaces(raw, nullptr, &hidGuid, index, &interfaceData); ++index) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(raw, &interfaceData, nullptr, 0, &needed, nullptr);
        if (needed < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }
        std::vector<std::byte> buffer(needed);
        auto* const detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        // The size of the fixed part, not of the buffer: anything else is refused with
        // ERROR_INVALID_USER_BUFFER.
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(raw, &interfaceData, detail, needed, nullptr,
                                             nullptr)) {
            paths.emplace_back(detail->DevicePath);
        }
    }
    return paths;
}

struct Found {
    std::wstring product;
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
};

// Whether the interface is a receiver's vendor collection. Opened with no access at all, which
// is enough for everything asked here and is refused by nothing.
std::optional<Found> inspect(std::wstring const& path) {
    winrt::file_handle const device{CreateFileW(path.c_str(), 0,
                                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                                OPEN_EXISTING, 0, nullptr)};
    if (!device) {
        return std::nullopt;
    }

    HIDD_ATTRIBUTES attributes{};
    attributes.Size = sizeof(attributes);
    if (!HidD_GetAttributes(device.get(), &attributes) ||
        !compx::isKnownVendor(attributes.VendorID)) {
        return std::nullopt;
    }

    PHIDP_PREPARSED_DATA raw = nullptr;
    if (!HidD_GetPreparsedData(device.get(), &raw)) {
        return std::nullopt;
    }
    PreparsedData const preparsed{raw};
    HIDP_CAPS caps{};
    if (HidP_GetCaps(raw, &caps) != HIDP_STATUS_SUCCESS || caps.UsagePage != compx::kUsagePage ||
        caps.Usage != compx::kUsage || caps.InputReportByteLength != compx::kReportLength ||
        caps.OutputReportByteLength != compx::kReportLength) {
        return std::nullopt;
    }

    Found found;
    found.vendorId = attributes.VendorID;
    found.productId = attributes.ProductID;
    // USB caps a string descriptor at 126 characters. The length is in bytes, and one character
    // is held back so the result is terminated even when the device fills the rest.
    wchar_t product[128]{};
    if (HidD_GetProductString(device.get(), product, sizeof(product) - sizeof(wchar_t))) {
        found.product = product;
    }
    return found;
}

enum class Direction { Out, In };

// One overlapped report, given until `deadline`. The OVERLAPPED lives in this frame, so a
// transfer that runs out of time is cancelled and then waited for before returning: left in
// flight, it would complete into stack memory that belongs to somebody else by then.
bool transfer(HANDLE device, HANDLE event, Direction direction, compx::Report& frame,
              Clock::time_point deadline) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = event;
    auto const size = static_cast<DWORD>(frame.size());
    BOOL const started = direction == Direction::Out
                             ? WriteFile(device, frame.data(), size, nullptr, &overlapped)
                             : ReadFile(device, frame.data(), size, nullptr, &overlapped);
    if (!started && GetLastError() != ERROR_IO_PENDING) {
        return false;
    }

    auto const left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    DWORD transferred = 0;
    if (WaitForSingleObject(event, static_cast<DWORD>(std::max<long long>(0, left.count()))) !=
        WAIT_OBJECT_0) {
        CancelIoEx(device, &overlapped);
        GetOverlappedResult(device, &overlapped, &transferred, TRUE);
        return false;
    }
    return GetOverlappedResult(device, &overlapped, &transferred, FALSE) && transferred == size;
}

using ReplyFilter = bool (*)(std::span<std::uint8_t const>) noexcept;

struct Exchange {
    bool opened = false;
    DWORD error = 0;
    std::optional<compx::Report> reply;
};

// Sends `request` and waits for the report `isReply` accepts.
Exchange exchange(std::wstring const& path, compx::Report const& request, ReplyFilter isReply) {
    Exchange result;
    // Read and write access is what the vendor collection needs, and what it grants without
    // elevation. The handle is opened for each exchange rather than kept: its report queue then
    // starts empty, so nothing stale from before the request can pass for the answer, and a
    // receiver that was pulled out costs nothing to forget.
    winrt::file_handle const device{CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                                OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)};
    if (!device) {
        result.error = GetLastError();
        return result;
    }
    winrt::handle const event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event) {
        result.error = GetLastError();
        return result;
    }
    result.opened = true;

    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        auto const deadline = Clock::now() + kAttemptBudget;
        compx::Report frame = request;
        if (!transfer(device.get(), event.get(), Direction::Out, frame, deadline)) {
            // The write itself failing is the receiver going away, which asking again will not
            // change.
            return result;
        }
        for (int report = 0; report < kReportsToScan; ++report) {
            compx::Report reply{};
            if (!transfer(device.get(), event.get(), Direction::In, reply, deadline)) {
                break;
            }
            // An answer with nothing in it is the mouse asleep, and asking again would only ask
            // the receiver the same question; any answer is final.
            if (isReply(reply)) {
                result.reply = reply;
                return result;
            }
        }
    }
    return result;
}

// How many times a receiver is asked for the model before its mouse is taken to be one whose
// firmware does not answer that question. Each ask happens only on a poll where the mouse has
// just answered the battery query, so an asleep mouse does not use them up.
constexpr int kModelAttempts = 3;

}  // namespace

void CompxBatteryProvider::rescan() {
    std::vector<Receiver> found;
    for (std::wstring& path : hidInterfacePaths()) {
        // Everything else on the machine is passed over by its path alone, without being opened.
        auto const vendorId = compx::vendorIdFromPath(path);
        if (!vendorId || !compx::isKnownVendor(*vendorId)) {
            continue;
        }
        auto const receiver = inspect(path);
        if (!receiver) {
            continue;
        }

        auto const known =
            std::find_if(m_receivers.begin(), m_receivers.end(),
                         [&path](Receiver const& held) { return held.path == path; });
        if (known != m_receivers.end()) {
            found.push_back(std::move(*known));
            continue;
        }

        Receiver entry;
        entry.id = compx::deviceIdFromPath(path);
        entry.path = std::move(path);
        entry.product = receiver->product;
        entry.vendorId = receiver->vendorId;
        entry.productId = receiver->productId;
        identify(entry);
        // Once per arrival, with everything a user needs to say which mouse this really is when
        // nothing on record does: none of it identifies the person or the machine.
        log::info(L"Compx receiver {:04X}:{:04X} \"{}\" ({}), shown as \"{}\" until the mouse "
                  L"names its model",
                  entry.vendorId, entry.productId, entry.product, entry.id, entry.name);
        found.push_back(std::move(entry));
    }

    if (found.size() != m_receivers.size()) {
        log::info(L"Found {} Compx mouse receiver(s)", found.size());
    }
    m_receivers = std::move(found);
}

bool CompxBatteryProvider::identify(Receiver& receiver) {
    compx::Identity const identity = compx::identify(receiver.vendorId, receiver.productId,
                                                     receiver.product, receiver.model);
    receiver.name = identity.name;
    receiver.viaReceiver = identity.viaReceiver;
    return identity.known;
}

void CompxBatteryProvider::askModel(Receiver& receiver) {
    if (receiver.model || receiver.modelAttempts >= kModelAttempts) {
        return;
    }
    ++receiver.modelAttempts;
    Exchange const answer =
        exchange(receiver.path, compx::modelRequest(), &compx::isModelReply);
    receiver.model = answer.reply ? compx::parseModelReply(*answer.reply) : std::nullopt;
    if (!receiver.model) {
        if (receiver.modelAttempts == kModelAttempts) {
            log::info(L"{} ({}) did not name its model", receiver.name, receiver.id);
        }
        return;
    }

    bool const known = identify(receiver);
    // The pair is logged either way, so that a model not on record can be put on record from
    // a user's log.
    log::info(L"{} ({}) reports model {}:{}{}", receiver.name, receiver.id,
              receiver.model->customer, receiver.model->model,
              known ? L"" : L", which is not on record");
}

std::vector<DeviceInfo> CompxBatteryProvider::poll() {
    std::vector<DeviceInfo> result;
    auto const now = std::chrono::system_clock::now();

    for (Receiver& receiver : m_receivers) {
        Exchange const answer =
            exchange(receiver.path, compx::batteryRequest(), &compx::isBatteryReply);
        std::optional<compx::Reading> const reading =
            answer.reply ? compx::parseBatteryReply(*answer.reply) : std::nullopt;
        Status const status = !answer.opened ? Status::Unreachable
                              : reading      ? Status::Answering
                                             : Status::Silent;
        if (status != receiver.status) {
            switch (status) {
                case Status::Answering:
                    log::info(L"{} ({}) reads {} % at {} mV, {}", receiver.name, receiver.id,
                              reading->percent, reading->millivolts,
                              reading->onCable ? L"on the cable" : L"on battery");
                    break;
                case Status::Silent:
                    log::info(L"{} ({}) gave no battery reading: the mouse is off, asleep or out "
                              L"of range",
                              receiver.name, receiver.id);
                    break;
                case Status::Unreachable:
                    log::warning(L"{} ({}) could not be opened for the battery query (error {})",
                                 receiver.name, receiver.id, answer.error);
                    break;
                case Status::Unknown:
                    break;
            }
            receiver.status = status;
        }
        // A receiver that cannot be opened was pulled out between the rescan and now, or is held
        // by something that will not share it; either way there is no device to show.
        if (status == Status::Unreachable) {
            continue;
        }
        // Asked only now that the mouse is known to be awake: asleep, it could not answer.
        if (reading) {
            askModel(receiver);
        }

        DeviceInfo info;
        info.id = receiver.id;
        info.name = receiver.name;
        info.vendorId = receiver.vendorId;
        info.productId = receiver.productId;
        info.kind = DeviceKind::Mouse;
        info.fidelity = Fidelity::Exact;
        info.viaReceiver = receiver.viaReceiver;

        if (reading) {
            log::debug(L"{} ({}): {} % at {} mV, cable {}", receiver.name, receiver.id,
                       reading->percent, reading->millivolts, reading->onCable);
            info.percent = reading->percent;
            info.source = PowerSource::Battery;
            info.charge = compx::chargeStateOf(*reading);
            receiver.lastAnswered = now;
        }
        // Silent: no level, and nothing about the power either -- percent stays -1, source and
        // charge stay Unknown. The time is when the mouse last spoke, so the card can say how
        // long ago that was instead of claiming a fresh check found nothing.
        info.lastUpdate =
            receiver.lastAnswered.time_since_epoch().count() != 0 ? receiver.lastAnswered : now;
        result.push_back(std::move(info));
    }

    return result;
}

}  // namespace peek
