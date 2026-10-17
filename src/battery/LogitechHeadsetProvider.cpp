#include "battery/LogitechHeadsetProvider.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "battery/CompxProtocol.h"
#include "battery/HidTransport.h"
#include "battery/LogitechHeadsetProtocol.h"
#include "core/Logger.h"

namespace peek {
namespace {

using namespace std::chrono_literals;

// The headset answers through the dongle over the air, which is slower than a mouse receiver
// answering for itself: measured on a PRO X Wireless, about 20 ms. An off headset is answered
// for by the dongle with an error straight away, so the budget is only spent on a lost request.
constexpr hid::ExchangePolicy kPolicy{250ms, 2, 6};

bool isDongleCollection(hid::Collection const& collection) noexcept {
    return logitech::isSupportedHeadset(collection.vendorId, collection.productId) &&
           collection.usagePage == logitech::kUsagePage && collection.usage == logitech::kUsage &&
           collection.inputLength == logitech::kReportLength &&
           collection.outputLength == logitech::kReportLength;
}

// For the log, which stays in English whatever the interface language is.
wchar_t const* describeCharge(ChargeState charge) noexcept {
    switch (charge) {
        case ChargeState::Discharging:
            return L"on battery";
        case ChargeState::Charging:
            return L"charging";
        case ChargeState::Full:
            return L"full";
        case ChargeState::Unknown:
            break;
    }
    return L"charge state unknown";
}

std::optional<logitech::Report> ask(std::wstring const& path, logitech::Report const& request,
                                    hid::Exchange& exchange) {
    exchange = hid::exchange(path, request, logitech::kReportLength, &logitech::isOwnReply,
                             kPolicy);
    if (!exchange.reply) {
        return std::nullopt;
    }
    logitech::Report report{};
    std::copy(exchange.reply->begin(), exchange.reply->end(), report.begin());
    return report;
}

}  // namespace

void LogitechHeadsetProvider::rescan() {
    std::vector<Dongle> found;
    for (std::wstring& path : hid::interfacePaths()) {
        // Everything else on the machine is passed over by its path alone, without being opened.
        if (compx::vendorIdFromPath(path) != logitech::kVendorLogitech) {
            continue;
        }
        auto const collection = hid::describe(path);
        if (!collection || !isDongleCollection(*collection)) {
            continue;
        }

        auto const known = std::find_if(m_dongles.begin(), m_dongles.end(),
                                        [&path](Dongle const& held) { return held.path == path; });
        if (known != m_dongles.end()) {
            found.push_back(std::move(*known));
            continue;
        }

        Dongle entry;
        entry.id = logitech::deviceIdFromPath(path);
        entry.path = std::move(path);
        entry.productId = collection->productId;
        entry.name = logitech::headsetName(collection->productId);
        log::info(L"Logitech headset dongle {:04X}:{:04X} \"{}\" ({})", logitech::kVendorLogitech,
                  entry.productId, collection->product, entry.id);
        found.push_back(std::move(entry));
    }

    if (found.size() != m_dongles.size()) {
        log::info(L"Found {} Logitech headset dongle(s)", found.size());
    }
    m_dongles = std::move(found);
}

LogitechHeadsetProvider::Status LogitechHeadsetProvider::locateFeature(Dongle& dongle) {
    hid::Exchange answer;
    auto const reply =
        ask(dongle.path, logitech::featureIndexRequest(logitech::kAdcMeasurementFeature), answer);
    if (!answer.opened) {
        return Status::Unreachable;
    }
    if (!reply || !logitech::isReplyTo(0, *reply)) {
        return Status::Off;
    }
    dongle.adcIndex = logitech::parseFeatureIndexReply(*reply);
    if (!dongle.adcIndex) {
        // An error here is the headset being off: the dongle cannot ask it. A plain zero is
        // the headset saying it has no such feature, and that will not change.
        return reply->at(2) == 0xff ? Status::Off : Status::NoFeature;
    }
    return Status::Answering;
}

std::vector<DeviceInfo> LogitechHeadsetProvider::poll() {
    std::vector<DeviceInfo> result;
    auto const now = std::chrono::system_clock::now();

    for (Dongle& dongle : m_dongles) {
        if (dongle.status == Status::NoFeature) {
            continue;
        }

        Status status = Status::Answering;
        std::optional<logitech::Reading> reading;
        if (!dongle.adcIndex) {
            status = locateFeature(dongle);
        }
        if (dongle.adcIndex) {
            hid::Exchange answer;
            auto const reply = ask(dongle.path, logitech::adcRequest(*dongle.adcIndex), answer);
            reading = reply ? logitech::parseAdcReply(*dongle.adcIndex, *reply) : std::nullopt;
            status = !answer.opened ? Status::Unreachable
                     : reading      ? Status::Answering
                                    : Status::Off;
        }

        int const percent = reading ? logitech::percentFromMillivolts(reading->millivolts) : -1;
        if (status != dongle.status) {
            switch (status) {
                case Status::Answering:
                    log::info(L"{} ({}) reads {} % at {} mV, {}", dongle.name, dongle.id, percent,
                              reading->millivolts, describeCharge(reading->charge));
                    break;
                case Status::Off:
                    log::info(L"{} ({}) gave no battery reading: the headset is off or out of "
                              L"range",
                              dongle.name, dongle.id);
                    break;
                case Status::NoFeature:
                    log::info(L"{} ({}) has no ADC measurement feature; not asked again",
                              dongle.name, dongle.id);
                    break;
                case Status::Unreachable:
                    log::warning(L"{} ({}) could not be opened for the battery query",
                                 dongle.name, dongle.id);
                    break;
                case Status::Unknown:
                    break;
            }
            dongle.status = status;
        }
        if (!reading) {
            continue;
        }

        log::debug(L"{} ({}): {} % at {} mV", dongle.name, dongle.id, percent,
                   reading->millivolts);
        DeviceInfo info;
        info.id = dongle.id;
        info.name = dongle.name;
        info.vendorId = logitech::kVendorLogitech;
        info.productId = dongle.productId;
        info.kind = DeviceKind::Headset;
        info.percent = percent;
        info.fidelity = Fidelity::Exact;
        info.source = PowerSource::Battery;
        info.charge = reading->charge == ChargeState::Charging && percent >= 100
                          ? ChargeState::Full
                          : reading->charge;
        info.viaReceiver = true;
        info.lastUpdate = now;
        result.push_back(std::move(info));
    }

    return result;
}

}  // namespace peek
