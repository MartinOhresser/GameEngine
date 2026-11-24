#include "ge/app/render.hpp"
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
    bool found = false;
    world.each<const Effects>([&](ecs::Entity, const Effects& effects) {
        if (found) {
            return;
        }
        found = true;
        app::ScreenEffects screen;
        screen.bloom = std::clamp(effects.bloom, 0.0F, 1.0F);
        screen.vignette = std::clamp(effects.vignette, 0.0F, 1.0F);
        screen.vignette_size = std::clamp(effects.vignette_size, 0.0F, 1.0F);
        screen.grain = std::clamp(effects.grain, 0.0F, 1.0F);
        screen.tint = rgba(effects.tint_red, effects.tint_green, effects.tint_blue);
        screen.lift = rgba(effects.lift_red, effects.lift_green, effects.lift_blue);
        screen.letterbox = std::clamp(effects.letterbox, 0.0F, 1.0F);
        backend->renderer().set_effects(screen);
    });
}

} // namespace ge::mod::postfx
