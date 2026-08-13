#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace peek {

// A minimal observer list.
//
// Every signal in this application is raised on the UI thread -- background work
// marshals through PostMessage before touching anything observable -- so there is no
// locking here. Emission iterates a copy so that a slot may connect or disconnect
// during the callback without invalidating the iteration.
template <class... Args>
class Signal {
public:
    using Slot = std::function<void(Args...)>;
    using Token = std::uint64_t;

    Token connect(Slot slot) {
        m_slots.push_back(Entry{++m_nextToken, std::move(slot)});
        return m_nextToken;
    }

    void disconnect(Token token) {
        std::erase_if(m_slots, [token](Entry const& entry) { return entry.token == token; });
    }

    void operator()(Args... args) const {
        auto const snapshot = m_slots;
        for (auto const& entry : snapshot) {
            entry.slot(args...);
        }
    }

    bool empty() const noexcept { return m_slots.empty(); }

private:
    struct Entry {
        Token token;
        Slot slot;
    };

    std::vector<Entry> m_slots;
    Token m_nextToken = 0;
};

}  // namespace peek
