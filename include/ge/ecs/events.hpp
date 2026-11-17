#pragma once

#include "ge/core/hash.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace ge::ecs {

/// A reader's position in an `Events<T>` queue. Each system that reads events
/// keeps its own, so several systems can read the same events independently.
struct EventCursor {
    std::uint64_t next = 0;
};

/// A queue of events of one type, stored in the world as a resource.
///
/// Events survive two `update` calls: one sender can emit during a tick and
/// every reader sees the event, whether it runs before or after the sender,
/// before it is dropped. `App::add_event<T>()` creates the queue and calls
/// `update` at the right moment; see there for the cadence.
///
///     world.resource<Events<Damage>>()->send(Damage{target, 5});
///
///     // in a reader system, which keeps its own cursor:
///     events->read(cursor, [](const Damage& damage) { ... });
template <typename T>
class Events {
public:
    void send(T event) {
        current_.push_back(std::move(event));
        ++sent_;
    }

    template <typename... Args>
    T& emplace(Args&&... args) {
        ++sent_;
        return current_.emplace_back(std::forward<Args>(args)...);
    }

    /// Calls `visit(const T&)` for each event the cursor has not seen yet, oldest
    /// first, and moves the cursor past them. Events dropped before the reader
    /// got to them are skipped.
    template <typename F>
    void read(EventCursor& cursor, const F& visit) const {
        const std::uint64_t first_stored = sent_ - size();
        for (std::uint64_t sequence = std::max(cursor.next, first_stored); sequence < sent_;
             ++sequence) {
            const auto offset = static_cast<std::size_t>(sequence - first_stored);
            visit(offset < previous_.size() ? previous_[offset]
                                            : current_[offset - previous_.size()]);
        }
        cursor.next = sent_;
    }

    /// Calls `visit(const T&)` for every stored event, oldest first.
    template <typename F>
    void for_each(const F& visit) const {
        for (const T& event : previous_) {
            visit(event);
        }
        for (const T& event : current_) {
            visit(event);
        }
    }

    /// Ends a generation: events older than the previous one are dropped.
    void update() {
        previous_.swap(current_);
        current_.clear();
    }

    /// Drops every stored event. Cursors stay valid.
    void clear() {
        previous_.clear();
        current_.clear();
    }

    /// Number of events currently stored.
    [[nodiscard]] std::size_t size() const noexcept { return previous_.size() + current_.size(); }

    /// Number of events ever sent, dropped ones included.
    [[nodiscard]] std::uint64_t total_sent() const noexcept { return sent_; }

    /// Hashes the stored events and the count sent, when `T` is hashable.
    void hash(Hasher& hasher) const
        requires Hashable<T>
    {
        hasher.add(sent_);
        for_each([&hasher](const T& event) { hasher.add(event); });
    }

private:
    std::vector<T> previous_;
    std::vector<T> current_;
    std::uint64_t sent_ = 0;
};

} // namespace ge::ecs
