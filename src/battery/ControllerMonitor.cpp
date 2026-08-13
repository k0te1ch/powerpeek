#include "battery/ControllerMonitor.h"

#include <algorithm>
#include <condition_variable>
#include <iterator>
#include <map>
#include <mutex>
#include <thread>

#include "battery/WinRtBatteryProvider.h"
#include "battery/XInputBatteryProvider.h"
#include "core/Logger.h"

namespace peek {
namespace {

using namespace std::chrono_literals;

constexpr std::chrono::seconds kMinimumInterval = 5s;

// A pad that has just arrived usually has no battery report for a moment -- seconds, over
// Bluetooth -- so the first readings after a change are taken on a short ramp instead of
// waiting out a full interval or spinning.
constexpr std::chrono::seconds kArrivalRamp[] = {1s, 3s, 8s};

// Windows.Gaming.Input populates its collection asynchronously, so "no controllers" is
// only believable once this much time has passed since the watcher was started.
constexpr std::chrono::seconds kWinRtGrace = 5s;

// Nothing is connected: the WGI events do the waking, and this only paces the XInput
// fallback probe, whose own per-slot cache keeps it to a handful of calls.
constexpr std::chrono::seconds kIdleInterval = 30s;

bool sameReading(ControllerInfo const& a, ControllerInfo const& b) noexcept {
    return a.id == b.id && a.name == b.name && a.percent == b.percent &&
           a.fidelity == b.fidelity && a.source == b.source && a.charge == b.charge;
}

// lastUpdate moves on every poll and would make every poll look like a change, so the
// comparison covers only what the UI actually renders.
bool sameList(std::vector<ControllerInfo> const& a, std::vector<ControllerInfo> const& b) noexcept {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), sameReading);
}

}  // namespace

struct ControllerMonitor::Impl {
    HWND notifyWindow = nullptr;
    UINT changedMessage = 0;

    std::thread thread;
    std::mutex mutex;
    std::condition_variable wake;
    bool running = false;
    bool stopping = false;
    bool refreshRequested = false;
    bool devicesChanged = false;
    std::chrono::seconds interval = 30s;
    bool includeNonXbox = false;

    std::mutex snapshotMutex;
    std::vector<ControllerInfo> latest;

    void run();
    void publish(std::vector<ControllerInfo> list);

    void signal() {
        wake.notify_all();
    }
};

void ControllerMonitor::Impl::publish(std::vector<ControllerInfo> list) {
    bool changed = false;
    {
        std::scoped_lock lock{snapshotMutex};
        changed = !sameList(latest, list);
        if (changed) {
            latest = std::move(list);
        }
    }

    if (!changed || notifyWindow == nullptr) {
        return;
    }
    if (!PostMessageW(notifyWindow, changedMessage, 0, 0)) {
        log::warning(L"Could not notify the UI thread of a controller change (error {})",
                     GetLastError());
    }
}

void ControllerMonitor::Impl::run() {
    try {
        // MTA: the WGI add/remove handlers then run on the Windows.Gaming.Input worker
        // thread directly, which keeps readings flowing while the UI thread sits in a
        // modal loop such as an open tray menu.
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (winrt::hresult_error const& error) {
        log::error(L"The controller thread could not enter the MTA: {}",
                   describeHresult(error.code()));
        return;
    }

    {
        WinRtBatteryProvider winrtProvider;
        winrtProvider.setDeviceChangedHandler([this] {
            {
                std::scoped_lock lock{mutex};
                devicesChanged = true;
            }
            signal();
        });
        bool const winrtUp = winrtProvider.start();

        XInputBatteryProvider xinputProvider;
        std::map<std::wstring, std::chrono::system_clock::time_point> firstSeen;
        auto const startedAt = std::chrono::steady_clock::now();
        std::size_t rampStep = 0;

        for (;;) {
            std::chrono::seconds pollInterval{};
            bool includeAll = false;
            {
                std::scoped_lock lock{mutex};
                pollInterval = interval;
                includeAll = includeNonXbox;
                if (devicesChanged) {
                    devicesChanged = false;
                    rampStep = 0;
                }
            }

            std::vector<ControllerInfo> list;
            if (winrtUp) {
                list = winrtProvider.poll();
            }
            // The two sources cannot be correlated -- an XInput slot carries no device
            // identity -- so they are never merged; XInput only speaks when WinRT is silent.
            if (list.empty() && std::chrono::steady_clock::now() - startedAt >= kWinRtGrace) {
                list = xinputProvider.poll();
            }
            if (!includeAll) {
                std::erase_if(list, [](ControllerInfo const& info) {
                    return !info.isXboxController;
                });
            }

            auto const now = std::chrono::system_clock::now();
            for (ControllerInfo& info : list) {
                info.firstSeen = firstSeen.try_emplace(info.id, now).first->second;
            }
            std::erase_if(firstSeen, [&list](auto const& entry) {
                return std::none_of(list.begin(), list.end(), [&entry](ControllerInfo const& info) {
                    return info.id == entry.first;
                });
            });

            bool const empty = list.empty();
            publish(std::move(list));

            std::chrono::seconds delay = empty ? std::max(pollInterval, kIdleInterval)
                                               : pollInterval;
            if (rampStep < std::size(kArrivalRamp)) {
                delay = kArrivalRamp[rampStep];
                ++rampStep;
            }

            std::unique_lock lock{mutex};
            wake.wait_for(lock, delay, [this] {
                return stopping || refreshRequested || devicesChanged;
            });
            if (stopping) {
                break;
            }
            if (refreshRequested) {
                refreshRequested = false;
                lock.unlock();
                xinputProvider.invalidatePresence();
            }
        }

        // Revoking the handlers and dropping every controller reference has to happen
        // inside this scope: releasing a WinRT proxy after uninit_apartment faults.
        winrtProvider.stop();
    }

    winrt::uninit_apartment();
}

ControllerMonitor::ControllerMonitor(HWND notifyWindow, UINT changedMessage)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->notifyWindow = notifyWindow;
    m_impl->changedMessage = changedMessage;
}

ControllerMonitor::~ControllerMonitor() {
    stop();
}

void ControllerMonitor::start(std::chrono::seconds interval) {
    bool alreadyRunning = false;
    {
        std::scoped_lock lock{m_impl->mutex};
        m_impl->interval = std::max(interval, kMinimumInterval);
        alreadyRunning = m_impl->running;
        m_impl->running = true;
        m_impl->stopping = false;
    }

    if (alreadyRunning) {
        m_impl->signal();
        return;
    }
    m_impl->thread = std::thread([impl = m_impl.get()] { impl->run(); });
}

void ControllerMonitor::stop() {
    {
        std::scoped_lock lock{m_impl->mutex};
        if (!m_impl->running) {
            return;
        }
        m_impl->stopping = true;
    }
    m_impl->signal();

    if (m_impl->thread.joinable()) {
        m_impl->thread.join();
    }

    std::scoped_lock lock{m_impl->mutex};
    m_impl->running = false;
    m_impl->stopping = false;
}

void ControllerMonitor::setInterval(std::chrono::seconds interval) {
    std::scoped_lock lock{m_impl->mutex};
    m_impl->interval = std::max(interval, kMinimumInterval);
}

void ControllerMonitor::setIncludeNonXbox(bool include) {
    {
        std::scoped_lock lock{m_impl->mutex};
        if (m_impl->includeNonXbox == include) {
            return;
        }
        m_impl->includeNonXbox = include;
        m_impl->refreshRequested = true;
    }
    m_impl->signal();
}

void ControllerMonitor::refreshNow() {
    {
        std::scoped_lock lock{m_impl->mutex};
        m_impl->refreshRequested = true;
    }
    m_impl->signal();
}

std::vector<ControllerInfo> ControllerMonitor::snapshot() const {
    std::scoped_lock lock{m_impl->snapshotMutex};
    return m_impl->latest;
}

}  // namespace peek
