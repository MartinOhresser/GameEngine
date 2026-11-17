#pragma once

#include <atomic>
#include <cstddef>

namespace ge::ecs {
namespace detail {

inline std::size_t next_type_id() noexcept {
    static std::atomic<std::size_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

} // namespace detail

/// Small, dense, process-wide integer identifying a type. Works without RTTI.
///
/// Ids are assigned on first use, so they are not stable between runs and must
/// not be persisted or sent over the network.
template <typename T>
[[nodiscard]] std::size_t type_id() noexcept {
    static const std::size_t id = detail::next_type_id();
    return id;
}

} // namespace ge::ecs
