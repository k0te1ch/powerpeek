#include "battery/CompxBatteryProvider.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

#include "battery/CompxProtocol.h"
#include "battery/HidTransport.h"
#include "core/Logger.h"

namespace peek {
namespace {

// Whether the interface is a receiver's vendor collection, from what it says about itself.
bool isReceiverCollection(hid::Collection const& collection) noexcept {
    return compx::isKnownVendor(collection.vendorId) &&
           collection.usagePage == compx::kUsagePage && collection.usage == compx::kUsage &&
           collection.inputLength == compx::kReportLength &&
           collection.outputLength == compx::kReportLength;
}

// One request to the receiver, on the transport's default policy. Measured on a VGN receiver
// with its mouse awake, the reply came back in 1 to 23 ms -- but about one request in thirty got
// no reply at all, however long it was waited for, so a lost request is asked again rather than
// waited on. An answer with nothing in it is the mouse asleep, and asking again would only ask
// the receiver the same question; any answer is final.
std::optional<compx::Report> ask(std::wstring const& path, compx::Report const& request,
                                 hid::ReplyFilter isReply, hid::Exchange& exchange) {
    exchange = hid::exchange(path, request, compx::kReportLength, isReply);
    if (!exchange.reply) {
        return std::nullopt;
    }
    compx::Report report{};
    std::copy(exchange.reply->begin(), exchange.reply->end(), report.begin());
    return report;
}

// How many times a receiver is asked for the model before its mouse is taken to be one whose
// firmware does not answer that question. Each ask happens only on a poll where the mouse has
// just answered the battery query, so an asleep mouse does not use them up.
constexpr int kModelAttempts = 3;

}  // namespace

void CompxBatteryProvider::rescan() {
    std::vector<Receiver> found;
    for (std::wstring& path : hid::interfacePaths()) {
        // Everything else on the machine is passed over by its path alone, without being opened.
        auto const vendorId = compx::vendorIdFromPath(path);
        if (!vendorId || !compx::isKnownVendor(*vendorId)) {
            continue;
        }
        auto const receiver = hid::describe(path);
        if (!receiver || !isReceiverCollection(*receiver)) {
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
    hid::Exchange answer;
    auto const reply = ask(receiver.path, compx::modelRequest(), &compx::isModelReply, answer);
    receiver.model = reply ? compx::parseModelReply(*reply) : std::nullopt;
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
        hid::Exchange answer;
        auto const reply =
            ask(receiver.path, compx::batteryRequest(), &compx::isBatteryReply, answer);
        std::optional<compx::Reading> const fresh =
            reply ? compx::parseBatteryReply(*reply) : std::nullopt;
        std::optional<compx::Reading> const reading = receiver.hold.update(fresh);
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
        if (fresh) {
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
            if (fresh) {
                receiver.lastAnswered = now;
            }
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
