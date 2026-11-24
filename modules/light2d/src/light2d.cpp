#include "ge/app/render.hpp"
#include "ge/app/time.hpp"
#include "ge/mod/light2d/light2d.gen.hpp"
#include "ge/mod/transform2d/drawn.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace ge::mod::light2d {

namespace {

/// A colour channel of 0 to 255, as a byte.
std::uint32_t channel(std::uint32_t value) {
    return std::min(value, 255U);
}

std::uint32_t rgba(std::uint32_t red, std::uint32_t green, std::uint32_t blue,
                   std::uint32_t alpha) {
    return (channel(red) << 24U) | (channel(green) << 16U) | (channel(blue) << 8U) | channel(alpha);
}

/// How much of its intensity a flickering light has at `seconds`: between 1 - flicker and 1.
/// A sum of three waves at unrelated rates, shifted by `phase`, so it never looks regular.
float wavering(const Light& light, float seconds, float phase) {
    if (light.flicker <= 0.0F) {
        return 1.0F;
    }
    const float turn = 2.0F * std::numbers::pi_v<float>;
    const float a = (seconds * light.flicker_speed + phase) * turn;
    const float wave = (0.5F * std::sin(a)) + (0.3F * std::sin((1.73F * a) + phase)) +
                       (0.2F * std::sin((2.91F * a) + (2.0F * phase))); // in [-1, 1]
    return 1.0F - (std::clamp(light.flicker, 0.0F, 1.0F) * (1.0F - wave) / 2.0F);
}

} // namespace

void shine(ecs::World& world) {
    auto* backend = world.resource<app::RenderBackend>();
    if (backend == nullptr) {
        return;
    }
    app::Renderer& renderer = backend->renderer();
    bool found = false;
    world.each<const Ambient>([&](ecs::Entity, const Ambient& ambient) {
        if (!found) {
            found = true;
            renderer.set_lighting({true, rgba(ambient.red, ambient.green, ambient.blue, 255U)});
        }
    });

    // The time the frame shows: the last tick, and how far towards the next one when drawing
    // in real time.
    const auto* time = world.resource<app::Time>();
    float seconds = 0.0F;
    if (time != nullptr) {
        const float ticks =
            static_cast<float>(time->tick) + (time->interpolate ? time->alpha : 0.0F);
        seconds = ticks * time->fixed_delta;
    }
    world.each<const Light, const transform2d::Position>([&](ecs::Entity entity, const Light& light,
                                                             const transform2d::Position& now) {
        const transform2d::Position position = transform2d::drawn_position(world, entity, now);
        const float phase = static_cast<float>(entity.index()) * 0.618F;
        const float strength =
            std::clamp(light.intensity * wavering(light, seconds, phase), 0.0F, 1.0F);
        const auto alpha = static_cast<std::uint32_t>(std::lround(strength * 255.0F));
        app::DrawCommand command{"glow", position.x + light.offset_x, position.y + light.offset_y,
                                 light.radius, rgba(light.red, light.green, light.blue, alpha)};
        command.light = true;
        command.blend = app::Blend::Add;
        renderer.draw(command);
    });
}

} // namespace ge::mod::light2d
