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
#include <utility>
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

struct Pose {
    std::uint32_t group;
    float x;
    float y;
};

/// Whether the controls show: on a phone always, elsewhere from the first finger.
bool controls_shown(ecs::World& world, const std::vector<Finger>& fingers) {
    if (world.resource<TouchSeen>() == nullptr) {
        world.insert_resource(TouchSeen{});
    }
    auto* seen = world.resource<TouchSeen>();
    if (std::ranges::any_of(fingers, [](const Finger& f) { return f.down; })) {
        seen->seen = true;
    }
    const auto* host = world.resource<app::HostInfo>();
    return seen->seen || (host != nullptr && host->mobile);
}

/// Controls whose finger lifted let go; a free finger on a free control takes it.
void assign_fingers(ecs::World& world, const std::vector<Finger>& fingers) {
    std::vector<bool> taken(finger_count, false);
    each_control(world, [&](std::uint32_t& finger, const transform2d::Position&, float, float) {
        if (finger == 0) {
            return;
        }
        if (finger > finger_count || !fingers[finger - 1].down) {
            finger = 0;
        } else {
            taken[finger - 1] = true;
        }
    });
    each_control(world, [&](std::uint32_t& finger, const transform2d::Position& at,
                            float half_width, float half_height) {
        for (std::size_t i = 0; finger == 0 && i < finger_count; ++i) {
            if (!taken[i] && fingers[i].down && inside(fingers[i], at, half_width, half_height)) {
                finger = static_cast<std::uint32_t>(i + 1);
                taken[i] = true;
            }
        }
    });
}

/// What the sprites of the controls show: the frame of a button, and every opacity.
void draw_controls(ecs::World& world, bool shown) {
    const auto alpha = [shown](std::uint32_t opacity) { return shown ? opacity : 0U; };
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
}

/// Gives the action of each button while it is held, and 0 once when it lets go: a key bound to
/// the same action is left alone the rest of the time.
void drive_buttons(ecs::World& world, app::LiveInput& live, const app::ActionNames* names) {
    world.each<TouchButton>([&](ecs::Entity, TouchButton& button) {
        const bool held = button.finger != 0;
        if (held || button.was_held) {
            give(live, names, button.action, held ? 1.0F : 0.0F);
        }
        button.was_held = held;
    });
}

/// The offset of a finger from the centre of a stick, as a fraction of the radius, within the
/// unit circle and 0 inside the dead zone.
std::pair<float, float> stick_offset(const Finger& finger, const transform2d::Position& at,
                                     const TouchStick& stick) {
    const float dx = (finger.x - at.x) / stick.radius;
    const float dy = (finger.y - at.y) / stick.radius;
    const float length = std::hypot(dx, dy);
    if (length > 1.0F) {
        return {dx / length, dy / length};
    }
    if (length < stick.dead_zone) {
        return {0.0F, 0.0F};
    }
    return {dx, dy};
}

/// Gives the axes of each stick and says where its knob goes.
std::vector<Pose> drive_sticks(ecs::World& world, const std::vector<Finger>& fingers,
                               app::LiveInput& live, const app::ActionNames* names) {
    std::vector<Pose> poses;
    world.each<TouchStick, const transform2d::Position>(
        [&](ecs::Entity, TouchStick& stick, const transform2d::Position& at) {
            const bool held = stick.finger != 0;
            std::pair<float, float> offset{0.0F, 0.0F};
            if (held && stick.radius > 0.0F) {
                offset = stick_offset(fingers[stick.finger - 1], at, stick);
            }
            if (held || stick.was_held) {
                give(live, names, stick.action_x, offset.first);
                give(live, names, stick.action_y, offset.second);
            }
            stick.was_held = held;
            poses.push_back(Pose{stick.group, at.x + offset.first * stick.radius,
                                 at.y + offset.second * stick.radius});
        });
    return poses;
}

void move_knobs(ecs::World& world, const std::vector<Pose>& poses) {
    world.each<const TouchKnob, transform2d::Position>([&](ecs::Entity, const TouchKnob& knob,
                                                           transform2d::Position& at) {
        const auto pose =
            std::ranges::find_if(poses, [&knob](const Pose& p) { return p.group == knob.group; });
        if (pose != poses.end()) {
            at.x = pose->x;
            at.y = pose->y;
        }
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
    const bool shown = controls_shown(world, fingers);
    assign_fingers(world, fingers);
    draw_controls(world, shown);
    drive_buttons(world, *live, names);
    move_knobs(world, drive_sticks(world, fingers, *live, names));
}

} // namespace ge::mod::touch_controls
