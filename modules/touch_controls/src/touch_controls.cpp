#include "ge/app/host.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/touch_controls/touch_controls.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ge::mod::touch_controls {

namespace {

/// Whether a finger was ever seen: the controls of a game that runs on a computer appear with it.
struct TouchSeen {
    bool seen = false;
    // Only what is drawn depends on it, not the simulation: nothing to hash.
    void hash(::ge::Hasher& /*hasher*/) const noexcept {}
};

/// How many fingers are followed: the actions touch_down to touch_down4.
constexpr std::size_t finger_count = 4;

struct Finger {
    bool down = false;
    float x = 0.0F;
    float y = 0.0F;
};

/// The value of an action as the player gives it right now (the live input), else as the
/// scenario scripts it.
float given(const app::LiveInput* live, const app::Actions& actions, const std::string& name) {
    if (live != nullptr) {
        const auto found = live->values().find(name);
        if (found != live->values().end()) {
            return found->second;
        }
    }
    return actions.value(name);
}

std::vector<Finger> read_fingers(const app::LiveInput* live, const app::Actions& actions) {
    std::vector<Finger> fingers(finger_count);
    for (std::size_t i = 0; i < finger_count; ++i) {
        const std::string suffix = i == 0 ? std::string() : std::to_string(i + 1);
        fingers[i].down = given(live, actions, "touch_down" + suffix) != 0.0F;
        fingers[i].x = given(live, actions, "touch_x" + suffix);
        fingers[i].y = given(live, actions, "touch_y" + suffix);
    }
    return fingers;
}

const std::string* name_of(const app::ActionNames* names, std::uint32_t action) {
    return names == nullptr || action >= names->names.size() ? nullptr : &names->names[action];
}

void give(app::LiveInput& live, const app::ActionNames* names, std::uint32_t action, float value) {
    if (const std::string* name = name_of(names, action); name != nullptr) {
        live.set(*name, value);
    }
}

bool inside(const Finger& finger, const transform2d::Position& at, float half_width,
            float half_height) {
    return std::abs(finger.x - at.x) <= half_width && std::abs(finger.y - at.y) <= half_height;
}

/// Calls `visit(finger, position, half_width, half_height)` on the buttons, then the sticks.
template <typename Visit>
void each_control(ecs::World& world, Visit visit) {
    world.each<TouchButton, const transform2d::Position>(
        [&](ecs::Entity, TouchButton& button, const transform2d::Position& at) {
            visit(button.finger, at, button.width / 2.0F, button.height / 2.0F);
        });
    world.each<TouchStick, const transform2d::Position>(
        [&](ecs::Entity, TouchStick& stick, const transform2d::Position& at) {
            visit(stick.finger, at, stick.zone, stick.zone);
        });
}

} // namespace

void follow_fingers(ecs::World& world) {
    auto* live = world.resource<app::LiveInput>();
    const auto* actions = world.resource<app::Actions>();
    if (live == nullptr || actions == nullptr) {
        return;
    }
    const auto* names = world.resource<app::ActionNames>();
    const std::vector<Finger> fingers = read_fingers(live, *actions);
    if (world.resource<TouchSeen>() == nullptr) {
        world.insert_resource(TouchSeen{});
    }
    auto* seen = world.resource<TouchSeen>();
    const auto* host = world.resource<app::HostInfo>();
    if (std::ranges::any_of(fingers, [](const Finger& f) { return f.down; })) {
        seen->seen = true;
    }
    const bool shown = seen->seen || (host != nullptr && host->mobile);
    const auto alpha = [shown](std::uint32_t opacity) { return shown ? opacity : 0U; };

    // What each control held before, to tell a release from a control that was never touched.
    std::vector<bool> taken(finger_count, false);
    each_control(world, [&](std::uint32_t& finger, const transform2d::Position&, float, float) {
        if (finger != 0 && (finger > finger_count || !fingers[finger - 1].down)) {
            finger = 0;
        } else if (finger != 0) {
            taken[finger - 1] = true;
        }
    });
    // A free finger on a free control takes it.
    each_control(world, [&](std::uint32_t& finger, const transform2d::Position& at,
                            float half_width, float half_height) {
        if (finger != 0) {
            return;
        }
        for (std::size_t i = 0; i < finger_count; ++i) {
            if (!taken[i] && fingers[i].down && inside(fingers[i], at, half_width, half_height)) {
                finger = static_cast<std::uint32_t>(i + 1);
                taken[i] = true;
                return;
            }
        }
    });

    world.each<TouchButton, sprite2d::Sprite>(
        [&](ecs::Entity, TouchButton& button, sprite2d::Sprite& sprite) {
            sprite.frame = button.finger != 0 ? button.pressed_frame : button.normal_frame;
            sprite.alpha = alpha(button.opacity);
        });
    world.each<const TouchStick, sprite2d::Sprite>(
        [&](ecs::Entity, const TouchStick& stick, sprite2d::Sprite& sprite) {
            sprite.alpha = alpha(stick.opacity);
        });
    world.each<const TouchKnob, sprite2d::Sprite>(
        [&](ecs::Entity, const TouchKnob& knob, sprite2d::Sprite& sprite) {
            sprite.alpha = alpha(knob.opacity);
        });
    world.each<TouchButton>([&](ecs::Entity, TouchButton& button) {
        const bool held = button.finger != 0;
        // Give the action while held, and 0 once when it lets go: a key bound to the same
        // action is left alone the rest of the time.
        if (held || button.was_held) {
            give(*live, names, button.action, held ? 1.0F : 0.0F);
        }
        button.was_held = held;
    });

    // The sticks, and the knobs that follow them.
    struct Pose {
        std::uint32_t group;
        float x;
        float y;
    };
    std::vector<Pose> poses;
    world.each<TouchStick, const transform2d::Position>(
        [&](ecs::Entity, TouchStick& stick, const transform2d::Position& at) {
            float dx = 0.0F;
            float dy = 0.0F;
            const bool held = stick.finger != 0;
            if (held && stick.radius > 0.0F) {
                const Finger& f = fingers[stick.finger - 1];
                dx = (f.x - at.x) / stick.radius;
                dy = (f.y - at.y) / stick.radius;
                const float length = std::hypot(dx, dy);
                if (length > 1.0F) {
                    dx /= length;
                    dy /= length;
                } else if (length < stick.dead_zone) {
                    dx = 0.0F;
                    dy = 0.0F;
                }
            }
            if (held || stick.was_held) {
                give(*live, names, stick.action_x, dx);
                give(*live, names, stick.action_y, dy);
            }
            stick.was_held = held;
            poses.push_back(Pose{stick.group, at.x + dx * stick.radius, at.y + dy * stick.radius});
        });
    world.each<const TouchKnob, transform2d::Position>(
        [&](ecs::Entity, const TouchKnob& knob, transform2d::Position& at) {
            for (const Pose& pose : poses) {
                if (pose.group == knob.group) {
                    at.x = pose.x;
                    at.y = pose.y;
                    return;
                }
            }
        });
}

} // namespace ge::mod::touch_controls
