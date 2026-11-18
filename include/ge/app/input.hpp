#pragma once

#include "ge/core/hash.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ge::app {

/// What the player is doing right now, as named actions with a value.
///
/// Game logic never reads a keyboard, a mouse or a pad: it reads actions
/// ("move_x", "jump"). A backend (a window, a pad, a touch screen, a script)
/// turns raw input into action values; the same game runs with any of them.
/// A button is 0 or 1, an axis is any value, typically in [-1, 1]. An action
/// nobody has set is 0.
///
/// `App` stores an `Actions` resource and, at the start of every fixed tick,
/// remembers the previous values so `pressed` and `released` know what changed.
class Actions {
public:
    /// Sets the value of an action. A name is lower_snake_case by convention.
    void set(std::string_view action, float value);

    /// The current value of the action, 0 if it was never set.
    [[nodiscard]] float value(std::string_view action) const;

    /// True while the value is not 0.
    [[nodiscard]] bool down(std::string_view action) const { return value(action) != 0.0F; }

    /// True during the tick when the action went from 0 to not 0.
    [[nodiscard]] bool pressed(std::string_view action) const;

    /// True during the tick when the action went from not 0 to 0.
    [[nodiscard]] bool released(std::string_view action) const;

    /// Remembers the current values as the previous ones. `App` calls it once
    /// per fixed tick, before the tick's input is applied.
    void begin_tick();

    [[nodiscard]] const std::map<std::string, float, std::less<>>& values() const noexcept {
        return current_;
    }

    void hash(Hasher& hasher) const;

private:
    std::map<std::string, float, std::less<>> current_;
    std::map<std::string, float, std::less<>> previous_;
};

/// One change of an action, at a fixed tick.
struct InputEvent {
    std::uint64_t tick = 0;
    std::string action;
    float value = 0.0F;
};

/// A recorded or hand-written input: action changes at given ticks. It is the
/// input backend of tests and scenarios, so a "player" can be replayed exactly.
///
/// `App` applies the events of tick N at the start of tick N (the first tick
/// is tick 0), after `begin_tick`.
class ScriptedInput {
public:
    /// Adds an event. Events may be added in any order; ties keep insertion order.
    void add(std::uint64_t tick, std::string action, float value);

    /// Sets, in `actions`, every event of `tick` that was not applied yet.
    void apply(std::uint64_t tick, Actions& actions);

    [[nodiscard]] std::size_t size() const noexcept { return events_.size(); }

    void hash(Hasher& hasher) const;

private:
    std::vector<InputEvent> events_;
    std::size_t next_ = 0;
};

} // namespace ge::app
