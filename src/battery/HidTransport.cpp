#include "battery/HidTransport.h"

#include <algorithm>
#include <cstddef>
#include <memory>

#include "core/Logger.h"

#include <hidsdi.h>
#include <setupapi.h>

namespace peek::hid {
namespace {

using Clock = std::chrono::steady_clock;

struct DeviceInfoSetDeleter {
    void operator()(void* set) const noexcept { SetupDiDestroyDeviceInfoList(set); }
};
using DeviceInfoSet = std::unique_ptr<void, DeviceInfoSetDeleter>;

struct PreparsedDataDeleter {
    void operator()(_HIDP_PREPARSED_DATA* data) const noexcept { HidD_FreePreparsedData(data); }
};
using PreparsedData = std::unique_ptr<_HIDP_PREPARSED_DATA, PreparsedDataDeleter>;

enum class Direction { Out, In };

// One overlapped report, given until `deadline`. The OVERLAPPED lives in this frame, so a
// transfer that runs out of time is cancelled and then waited for before returning: left in
// flight, it would complete into stack memory that belongs to somebody else by then.
bool transfer(HANDLE device, HANDLE event, Direction direction, std::span<std::uint8_t> frame,
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

}  // namespace

std::vector<std::wstring> interfacePaths() {
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

std::optional<Collection> describe(std::wstring const& path) {
    winrt::file_handle const device{CreateFileW(path.c_str(), 0,
                                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                                OPEN_EXISTING, 0, nullptr)};
    if (!device) {
        return std::nullopt;
    }

    HIDD_ATTRIBUTES attributes{};
    attributes.Size = sizeof(attributes);
    if (!HidD_GetAttributes(device.get(), &attributes)) {
        return std::nullopt;
    }

    PHIDP_PREPARSED_DATA raw = nullptr;
    if (!HidD_GetPreparsedData(device.get(), &raw)) {
        return std::nullopt;
    }
    PreparsedData const preparsed{raw};
    HIDP_CAPS caps{};
    if (HidP_GetCaps(raw, &caps) != HIDP_STATUS_SUCCESS) {
        return std::nullopt;
    }

    Collection found;
    found.vendorId = attributes.VendorID;
    found.productId = attributes.ProductID;
    found.usagePage = caps.UsagePage;
    found.usage = caps.Usage;
    found.inputLength = caps.InputReportByteLength;
    found.outputLength = caps.OutputReportByteLength;
    // USB caps a string descriptor at 126 characters. The length is in bytes, and one character
    // is held back so the result is terminated even when the device fills the rest.
    wchar_t product[128]{};
    if (HidD_GetProductString(device.get(), product, sizeof(product) - sizeof(wchar_t))) {
        found.product = product;
    }
    return found;
}

Exchange exchange(std::wstring const& path,
                  std::span<std::uint8_t const> request,
                  std::size_t replyLength,
                  ReplyFilter isReply,
                  ExchangePolicy const& policy) {
    Exchange result;
    // Read and write access is what a vendor collection needs, and what it grants without
    // elevation. A device that was pulled out costs nothing to forget.
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

    std::vector<std::uint8_t> frame(request.begin(), request.end());
    std::vector<std::uint8_t> reply(replyLength);
    for (int attempt = 0; attempt < policy.attempts; ++attempt) {
        auto const deadline = Clock::now() + policy.attemptBudget;
        std::copy(request.begin(), request.end(), frame.begin());
        if (!transfer(device.get(), event.get(), Direction::Out, frame, deadline)) {
            // The write itself failing is the device going away, which asking again will not
            // change.
            return result;
        }
        for (int report = 0; report < policy.reportsToScan; ++report) {
            std::fill(reply.begin(), reply.end(), std::uint8_t{0});
            if (!transfer(device.get(), event.get(), Direction::In, reply, deadline)) {
                break;
            }
            if (isReply(reply)) {
                result.reply = std::move(reply);
                return result;
            }
        }
    }
    return result;
}

}  // namespace peek::hid
