#include "ge/app/input.hpp"

#include <algorithm>

namespace ge::app {

namespace {

float find_or_zero(const std::map<std::string, float, std::less<>>& values,
                   std::string_view action) {
    const auto found = values.find(action);
    return found == values.end() ? 0.0F : found->second;
}

} // namespace

void Actions::set(std::string_view action, float value) {
    const auto found = current_.find(action);
    if (found == current_.end()) {
        current_.emplace(std::string(action), value);
    } else {
        found->second = value;
    }
}

float Actions::value(std::string_view action) const {
    return find_or_zero(current_, action);
}

bool Actions::pressed(std::string_view action) const {
    return value(action) != 0.0F && find_or_zero(previous_, action) == 0.0F;
}

bool Actions::released(std::string_view action) const {
    return value(action) == 0.0F && find_or_zero(previous_, action) != 0.0F;
}

void Actions::begin_tick() {
    previous_ = current_;
}

void Actions::hash(Hasher& hasher) const {
    hasher.add(static_cast<std::uint64_t>(current_.size()));
    for (const auto& [action, value] : current_) {
        hasher.add(std::string_view(action));
        hasher.add(value);
    }
}

void ScriptedInput::add(std::uint64_t tick, std::string action, float value) {
    // Keep the events sorted by tick, and in insertion order within a tick. An
    // event added for a tick already passed is applied at the next call.
    const auto position =
        std::upper_bound(events_.begin() + static_cast<std::ptrdiff_t>(next_), events_.end(), tick,
                         [](std::uint64_t at, const InputEvent& event) { return at < event.tick; });
    events_.insert(position, InputEvent{tick, std::move(action), value});
}

void ScriptedInput::apply(std::uint64_t tick, Actions& actions) {
    while (next_ < events_.size() && events_[next_].tick <= tick) {
        actions.set(events_[next_].action, events_[next_].value);
        ++next_;
    }
}

void ScriptedInput::hash(Hasher& hasher) const {
    hasher.add(static_cast<std::uint64_t>(events_.size()));
    hasher.add(static_cast<std::uint64_t>(next_));
}

void LiveInput::set(std::string_view action, float value) {
    const auto found = values_.find(action);
    if (found == values_.end()) {
        values_.emplace(std::string(action), value);
    } else {
        found->second = value;
    }
}

float LiveInput::value(std::string_view action) const {
    const auto found = values_.find(action);
    return found == values_.end() ? 0.0F : found->second;
}

} // namespace ge::app
