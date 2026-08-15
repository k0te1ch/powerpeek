#include "battery/BatteryHistory.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

#include "core/Json.h"
#include "core/Logger.h"
#include "core/Win.h"

namespace peek {
namespace {

using Clock = std::chrono::system_clock;

// A sample is a step edge, not a measurement, so the drain estimate uses the slope
// between the oldest and the newest edge of a short window rather than a least-squares
// fit: with quantised levels a fit is dominated by how long the pad sat on each step,
// which says more about when the user stopped playing than about the drain rate. Four
// edges is roughly one full discharge run for a controller that reports four levels.
constexpr std::size_t kDrainWindowSamples = 4;

// Two edges minutes apart would divide a whole step by almost nothing and claim hundreds
// of percent per hour.
constexpr std::chrono::minutes kMinimumDrainSpan{20};

// Beyond this the estimate is noise dressed up as a number.
constexpr std::chrono::minutes kMaximumEstimate{72 * 60};

std::string_view chargeKey(ChargeState state) {
    switch (state) {
        case ChargeState::Discharging:
            return "discharging";
        case ChargeState::Charging:
            return "charging";
        case ChargeState::Full:
            return "full";
        case ChargeState::Unknown:
            break;
    }
    return "unknown";
}

ChargeState chargeFromKey(std::string_view key) {
    if (key == "discharging") {
        return ChargeState::Discharging;
    }
    if (key == "charging") {
        return ChargeState::Charging;
    }
    if (key == "full") {
        return ChargeState::Full;
    }
    return ChargeState::Unknown;
}

std::string encode(HistorySample const& sample) {
    json::Value line;
    line.set("t", json::Value{static_cast<double>(
                      std::chrono::duration_cast<std::chrono::seconds>(
                          sample.when.time_since_epoch())
                          .count())});
    line.set("id", json::Value{narrow(sample.controllerId)});
    line.set("p", json::Value{static_cast<int>(sample.percent)});
    line.set("c", json::Value{chargeKey(sample.charge)});
    return json::dump(line, 0);
}

std::optional<HistorySample> decode(std::string_view text) {
    std::string error;
    json::Value const line = json::parse(text, &error);
    if (!error.empty() || !line.contains("id")) {
        return std::nullopt;
    }

    // The instant and the level are the two fields a sample cannot be reconstructed without,
    // and the typed reads below would quietly substitute a zero for either. A hand-edited
    // line carrying a quoted level -- "p": "78" -- would load as a perfectly valid-looking
    // reading of 0%, which draws a dip to empty on the chart and drags the discharge estimate
    // down with it. Every other malformed line here is dropped, and these belong with them:
    // inventing a reading is worse than losing one.
    json::Value const& when = line["t"];
    json::Value const& level = line["p"];
    if (when.kind() != json::Value::Kind::Number || level.kind() != json::Value::Kind::Number) {
        return std::nullopt;
    }

    HistorySample sample;
    sample.when = Clock::time_point{std::chrono::duration_cast<Clock::duration>(
        std::chrono::seconds{static_cast<std::int64_t>(when.asNumber())})};
    sample.controllerId = line["id"].asWide();
    sample.percent = static_cast<std::uint8_t>(std::clamp(level.asInt(0), 0, 100));
    // The charge word is different: a state a later version invented degrades to Unknown,
    // which ends a discharge run and costs an estimate rather than fabricating a reading.
    sample.charge = chargeFromKey(line["c"].asString());
    if (sample.controllerId.empty()) {
        return std::nullopt;
    }
    return sample;
}

}  // namespace

struct BatteryHistory::Impl {
    std::filesystem::path file;
    bool enabled = true;
    std::chrono::days retention{30};

    std::mutex mutex;
    std::vector<HistorySample> samples;
    std::map<std::wstring, HistorySample> lastPerController;

    void load();
    void rewrite();
    void append(HistorySample const& sample);
    std::vector<HistorySample> within(std::wstring const& id) const;
    std::optional<double> drainRate(std::wstring const& id) const;
};

void BatteryHistory::Impl::load() {
    std::ifstream stream{file, std::ios::binary};
    if (!stream) {
        return;
    }

    std::string text;
    while (std::getline(stream, text)) {
        if (!text.empty() && text.back() == '\r') {
            text.pop_back();
        }
        if (text.empty()) {
            continue;
        }
        if (auto sample = decode(text)) {
            samples.push_back(std::move(*sample));
        }
    }

    // A hand-edited or interrupted file can be out of order, and every reader below
    // assumes oldest first.
    std::stable_sort(samples.begin(), samples.end(),
                     [](HistorySample const& a, HistorySample const& b) { return a.when < b.when; });

    // Seeded from the sorted samples rather than while reading them. The baseline this map
    // holds is what record() compares against to decide a reading is unchanged, so it has to
    // be the newest reading per controller -- and in the out-of-order file the sort above
    // exists for, the last line is not the newest sample. Seeded from file order, the first
    // record() of the session can match a stale reading, be dropped as a duplicate, and that
    // transition is then missing from the log for good.
    for (HistorySample const& sample : samples) {
        lastPerController[sample.controllerId] = sample;
    }
}

void BatteryHistory::Impl::rewrite() {
    std::filesystem::path temporary = file;
    temporary += L".tmp";
    {
        std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
        if (!stream) {
            log::warning(L"Could not rewrite the battery history at {}", file.wstring());
            return;
        }
        for (HistorySample const& sample : samples) {
            stream << encode(sample) << '\n';
        }
    }

    std::error_code code;
    std::filesystem::rename(temporary, file, code);
    if (code) {
        log::warning(L"Could not replace the battery history file: {}", widen(code.message()));
        std::filesystem::remove(temporary, code);
    }
}

void BatteryHistory::Impl::append(HistorySample const& sample) {
    std::ofstream stream{file, std::ios::binary | std::ios::app};
    if (!stream) {
        log::warning(L"Could not append to the battery history at {}", file.wstring());
        return;
    }
    stream << encode(sample) << '\n';
}

std::vector<HistorySample> BatteryHistory::Impl::within(std::wstring const& id) const {
    auto const cutoff = Clock::now() - retention;

    std::vector<HistorySample> result;
    for (HistorySample const& sample : samples) {
        if (sample.controllerId == id && sample.when >= cutoff) {
            result.push_back(sample);
        }
    }
    return result;
}

std::optional<double> BatteryHistory::Impl::drainRate(std::wstring const& id) const {
    std::vector<HistorySample> const all = within(id);

    std::vector<HistorySample> run;
    for (auto it = all.rbegin(); it != all.rend() && run.size() < kDrainWindowSamples; ++it) {
        if (it->charge != ChargeState::Discharging) {
            break;
        }
        // Walking backwards, the level may only rise; anything else is a battery swap or
        // a charge that ended the run.
        if (!run.empty() && it->percent < run.back().percent) {
            break;
        }
        run.push_back(*it);
    }

    if (run.size() < 2) {
        return std::nullopt;
    }

    HistorySample const& newest = run.front();
    HistorySample const& oldest = run.back();
    auto const span = newest.when - oldest.when;
    if (oldest.percent <= newest.percent || span < kMinimumDrainSpan) {
        return std::nullopt;
    }

    double const hours = std::chrono::duration<double, std::ratio<3600>>{span}.count();
    return (oldest.percent - newest.percent) / hours;
}

BatteryHistory::BatteryHistory(std::filesystem::path file) : m_impl(std::make_unique<Impl>()) {
    m_impl->file = std::move(file);

    std::error_code code;
    std::filesystem::create_directories(m_impl->file.parent_path(), code);
    if (code) {
        log::warning(L"Could not create the history folder: {}", widen(code.message()));
    }
    m_impl->load();
}

BatteryHistory::~BatteryHistory() = default;

void BatteryHistory::setEnabled(bool enabled) {
    std::scoped_lock lock{m_impl->mutex};
    m_impl->enabled = enabled;
}

void BatteryHistory::setRetention(std::chrono::days retention) {
    std::scoped_lock lock{m_impl->mutex};
    m_impl->retention = std::max(retention, std::chrono::days{1});
}

void BatteryHistory::record(ControllerInfo const& controller) {
    std::scoped_lock lock{m_impl->mutex};
    if (!m_impl->enabled || controller.id.empty() || !controller.hasBattery()) {
        return;
    }

    HistorySample sample;
    sample.when = controller.lastUpdate.time_since_epoch().count() == 0 ? Clock::now()
                                                                       : controller.lastUpdate;
    sample.controllerId = controller.id;
    sample.percent = static_cast<std::uint8_t>(std::clamp(controller.percent, 0, 100));
    sample.charge = controller.charge;

    auto const previous = m_impl->lastPerController.find(sample.controllerId);
    if (previous != m_impl->lastPerController.end() &&
        previous->second.percent == sample.percent && previous->second.charge == sample.charge) {
        return;
    }

    m_impl->lastPerController[sample.controllerId] = sample;

    // Placed rather than appended. samplesFor promises oldest first, and the drain estimate
    // reads consecutive samples as a line through time, so one sample out of order does not
    // just look untidy -- it inverts a segment of that line. A reading can genuinely belong
    // before one already held: the clock moves backwards on an NTP correction, and the pad's
    // lastUpdate is whatever the monitor stamped it with rather than the moment this runs.
    auto const at = std::upper_bound(
        m_impl->samples.begin(), m_impl->samples.end(), sample.when,
        [](Clock::time_point when, HistorySample const& held) { return when < held.when; });
    m_impl->samples.insert(at, sample);
    m_impl->append(sample);
}

std::vector<HistorySample> BatteryHistory::samplesFor(std::wstring const& controllerId) const {
    std::scoped_lock lock{m_impl->mutex};
    return m_impl->within(controllerId);
}

std::optional<double> BatteryHistory::drainPercentPerHour(std::wstring const& controllerId) const {
    std::scoped_lock lock{m_impl->mutex};
    return m_impl->drainRate(controllerId);
}

std::optional<std::chrono::minutes> BatteryHistory::estimatedRemaining(
    ControllerInfo const& controller) const {
    if (!controller.hasBattery() || controller.charge != ChargeState::Discharging) {
        return std::nullopt;
    }

    std::scoped_lock lock{m_impl->mutex};
    auto const rate = m_impl->drainRate(controller.id);
    if (!rate || *rate <= 0.0) {
        return std::nullopt;
    }

    auto const minutes = std::chrono::minutes{
        static_cast<std::int64_t>(controller.percent / *rate * 60.0)};
    if (minutes <= std::chrono::minutes::zero() || minutes > kMaximumEstimate) {
        return std::nullopt;
    }
    return minutes;
}

void BatteryHistory::prune() {
    std::scoped_lock lock{m_impl->mutex};
    auto const cutoff = Clock::now() - m_impl->retention;

    auto const removed = std::erase_if(m_impl->samples, [cutoff](HistorySample const& sample) {
        return sample.when < cutoff;
    });
    if (removed == 0) {
        return;
    }

    log::info(L"Dropped {} battery history samples older than {} days", removed,
              m_impl->retention.count());
    m_impl->rewrite();
}

}  // namespace peek
