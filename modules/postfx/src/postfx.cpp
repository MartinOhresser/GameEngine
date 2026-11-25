#include "ge/app/render.hpp"
#include "ge/app/time.hpp"
#include "ge/mod/postfx/postfx.gen.hpp"

#include <algorithm>
#include <cstdint>

namespace ge::mod::postfx {

namespace {

std::uint32_t rgba(std::uint32_t red, std::uint32_t green, std::uint32_t blue) {
    return (std::min(red, 255U) << 24U) | (std::min(green, 255U) << 16U) |
           (std::min(blue, 255U) << 8U) | 0xFFU;
}

} // namespace

void apply(ecs::World& world) {
    auto* backend = world.resource<app::RenderBackend>();
    if (backend == nullptr) {
        return;
    }
    const auto* time = world.resource<app::Time>();
    const bool paused = time != nullptr && time->paused;
    bool found = false;
    world.each<const Effects>([&](ecs::Entity, const Effects& effects) {
        if (found) {
            return;
        }
        found = true;
        app::ScreenEffects screen;
        screen.bloom = std::clamp(effects.bloom, 0.0F, 1.0F);
        screen.vignette = std::clamp(effects.vignette, 0.0F, 1.0F);
        screen.vignette_size = std::clamp(effects.vignette_size, -4.0F, 1.0F);
        screen.vignette_color = rgba(effects.vignette_red, effects.vignette_green, effects.vignette_blue);
        screen.edge_glow = std::clamp(effects.edge_glow, 0.0F, 1.0F);
        screen.edge_glow_size = std::clamp(effects.edge_glow_size, -4.0F, 1.0F);
        screen.edge_glow_color =
            rgba(effects.edge_glow_red, effects.edge_glow_green, effects.edge_glow_blue);
        screen.grain = std::clamp(effects.grain, 0.0F, 1.0F);
        screen.tint = rgba(effects.tint_red, effects.tint_green, effects.tint_blue);
        screen.lift = rgba(effects.lift_red, effects.lift_green, effects.lift_blue);
        screen.letterbox = std::clamp(effects.letterbox, 0.0F, 1.0F);
        screen.rays = std::clamp(effects.rays, 0.0F, 1.0F);
        screen.rays_x = effects.rays_x;
        screen.rays_y = effects.rays_y;
        screen.rays_length = std::clamp(effects.rays_length, 0.0F, 1.0F);
        screen.blur = std::clamp(paused ? effects.paused_blur : effects.blur, 0.0F, 1.0F);
        backend->renderer().set_effects(screen);
    });
}

} // namespace ge::mod::postfx
