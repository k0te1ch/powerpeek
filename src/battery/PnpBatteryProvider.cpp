#include "battery/PnpBatteryProvider.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "battery/PnpMapping.h"
#include "core/Logger.h"
#include "core/Win.h"

#include <cfgmgr32.h>

namespace peek {
namespace {

// The keys are written out rather than taken from <devpkey.h> because one of them has no
// header to take it from, and mixing the two styles would mean defining INITGUID in this
// translation unit -- which turns every GUID in every header it pulls in into a definition.
// A DEVPROPKEY is a GUID and an integer; spelling four of them out costs less than that.
constexpr DEVPROPKEY key(GUID const& guid, ULONG pid) noexcept {
    return DEVPROPKEY{guid, pid};
}

// Undocumented, and in no SDK header. Written by the Bluetooth audio gateway service for
// hands-free headsets and by the LE enumerator for GATT Battery Service peripherals; read by
// the Settings device page. DEVPROP_TYPE_BYTE, a percentage.
constexpr GUID kBluetoothBatteryGuid{
    0x104ea319, 0x6ee2, 0x4701, {0xbd, 0x47, 0x8d, 0xdb, 0xf4, 0x25, 0xbb, 0xe5}};

// Also undocumented, and commonly called DEVPKEY_Device_IsConnected. DEVPROP_TYPE_BOOLEAN,
// where true is -1 rather than 1.
constexpr GUID kDeviceConnectedGuid{
    0x83da6326, 0x97a6, 0x4088, {0x94, 0x53, 0xa1, 0x92, 0x3f, 0x57, 0x3b, 0x29}};

// DEVPKEY_NAME and DEVPKEY_Device_ContainerId, copied from shared/devpkey.h.
constexpr GUID kNameGuid{
    0xb725f130, 0x47ef, 0x101a, {0xa5, 0xf1, 0x02, 0x60, 0x8c, 0x9e, 0xeb, 0xac}};
constexpr GUID kContainerIdGuid{
    0x8c7ed206, 0x3f8a, 0x4827, {0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c}};

// DEVPKEY_Bluetooth_ClassOfDevice, copied from km/bthguid.h -- a kernel-mode header this
// application has no business including. Property 4 is the same field under a name the header
// marks deprecated, and is read only when 10 is absent.
constexpr GUID kBluetoothGuid{
    0x2bd67d8b, 0x8beb, 0x48d5, {0x87, 0xe0, 0x6c, 0xda, 0x34, 0x28, 0x04, 0x0a}};

constexpr DEVPROPKEY kBatteryLevel = key(kBluetoothBatteryGuid, 2);
constexpr DEVPROPKEY kIsConnected = key(kDeviceConnectedGuid, 15);
constexpr DEVPROPKEY kName = key(kNameGuid, 10);
constexpr DEVPROPKEY kContainerId = key(kContainerIdGuid, 2);
constexpr DEVPROPKEY kClassOfDevice = key(kBluetoothGuid, 10);
constexpr DEVPROPKEY kClassOfDeviceDeprecated = key(kBluetoothGuid, 4);

// A property that is simply not on this devnode is the ordinary case, not a failure: most
// devnodes carry none of these. The size query reports that by answering anything other than
// CR_BUFFER_SMALL, which is the one return code here that means success.
bool readProperty(DEVINST node, DEVPROPKEY const& property, DEVPROPTYPE& type,
                  std::vector<BYTE>& data) {
    ULONG bytes = 0;
    type = 0;
    if (CM_Get_DevNode_PropertyW(node, &property, &type, nullptr, &bytes, 0) != CR_BUFFER_SMALL ||
        bytes == 0) {
        return false;
    }
    data.assign(bytes, 0);
    return CM_Get_DevNode_PropertyW(node, &property, &type, data.data(), &bytes, 0) == CR_SUCCESS;
}

std::optional<std::uint32_t> readByte(DEVINST node, DEVPROPKEY const& property) {
    DEVPROPTYPE type{};
    std::vector<BYTE> data;
    if (!readProperty(node, property, type, data) || type != DEVPROP_TYPE_BYTE ||
        data.size() != 1) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(data[0]);
}

std::optional<std::uint32_t> readUint32(DEVINST node, DEVPROPKEY const& property) {
    DEVPROPTYPE type{};
    std::vector<BYTE> data;
    if (!readProperty(node, property, type, data) || type != DEVPROP_TYPE_UINT32 ||
        data.size() != sizeof(std::uint32_t)) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    std::memcpy(&value, data.data(), sizeof(value));
    return value;
}

std::optional<std::wstring> readString(DEVINST node, DEVPROPKEY const& property) {
    DEVPROPTYPE type{};
    std::vector<BYTE> data;
    if (!readProperty(node, property, type, data) || type != DEVPROP_TYPE_STRING ||
        data.size() < sizeof(wchar_t)) {
        return std::nullopt;
    }
    return std::wstring{reinterpret_cast<wchar_t const*>(data.data())};
}

std::optional<GUID> readGuid(DEVINST node, DEVPROPKEY const& property) {
    DEVPROPTYPE type{};
    std::vector<BYTE> data;
    if (!readProperty(node, property, type, data) || type != DEVPROP_TYPE_GUID ||
        data.size() != sizeof(GUID)) {
        return std::nullopt;
    }
    GUID value{};
    std::memcpy(&value, data.data(), sizeof(value));
    return value;
}

std::optional<bool> readBool(DEVINST node, DEVPROPKEY const& property) {
    DEVPROPTYPE type{};
    std::vector<BYTE> data;
    if (!readProperty(node, property, type, data) || type != DEVPROP_TYPE_BOOLEAN ||
        data.size() != 1) {
        return std::nullopt;
    }
    return data[0] != 0;
}

}  // namespace

std::vector<DeviceInfo> PnpBatteryProvider::poll() {
    std::vector<DeviceInfo> result;

    ULONG length = 0;
    if (CM_Get_Device_ID_List_SizeW(&length, nullptr, CM_GETIDLIST_FILTER_PRESENT) != CR_SUCCESS ||
        length == 0) {
        return result;
    }
    std::vector<wchar_t> ids(length);
    if (CM_Get_Device_ID_ListW(nullptr, ids.data(), length, CM_GETIDLIST_FILTER_PRESENT) !=
        CR_SUCCESS) {
        return result;
    }

    auto const now = std::chrono::system_clock::now();

    for (wchar_t const* id = ids.data(); *id != L'\0'; id += std::wcslen(id) + 1) {
        DEVINST node = 0;
        // PHANTOM rather than NORMAL: a headset that has just gone idle is still present but
        // no longer started, and NORMAL would skip exactly the device whose level matters.
        if (CM_Locate_DevNodeW(&node, const_cast<DEVINSTID_W>(id), CM_LOCATE_DEVNODE_PHANTOM) !=
            CR_SUCCESS) {
            continue;
        }

        auto const raw = readByte(node, kBatteryLevel);
        if (!raw) {
            continue;
        }
        int const percent = pnp::levelFromProperty(*raw);
        if (percent < 0) {
            continue;
        }
        // Absent means connected: the property is undocumented, and the devnodes measured to
        // carry it at all carried it as true. A device that is genuinely gone is filtered out
        // by the present-only list above rather than by this.
        if (!readBool(node, kIsConnected).value_or(true)) {
            continue;
        }

        DeviceInfo info;
        auto const container = readGuid(node, kContainerId);
        // The instance id would do as a key, but for Bluetooth it contains the device address
        // -- the hardware's MAC -- and this id is written to the battery log on disk. The
        // container id is both the better key and the one that is nobody's serial number.
        info.id = container ? pnp::deviceIdFromContainer(*container)
                            : pnp::deviceIdFromInstance(id);
        info.name = readString(node, kName).value_or(std::wstring{});
        info.kind = pnp::kindFromClassOfDevice(
            readUint32(node, kClassOfDevice)
                .value_or(readUint32(node, kClassOfDeviceDeprecated).value_or(0)));
        info.percent = percent;
        // A percentage the device itself reported, not a bucket -- but with no direction to
        // it: nothing in this property says whether the level is rising or falling.
        info.fidelity = Fidelity::Exact;
        info.source = PowerSource::Battery;
        info.charge = ChargeState::Unknown;
        info.lastUpdate = now;
        result.push_back(std::move(info));
    }

    log::debug(L"Device tree sweep found {} device(s) reporting a battery level", result.size());
    return result;
}

}  // namespace peek
