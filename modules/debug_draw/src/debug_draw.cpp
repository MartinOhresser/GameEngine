#include "ge/app/render.hpp"
#include "ge/mod/debug_draw/debug_draw.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cstdint>

namespace ge::mod::debug_draw {

namespace {

std::uint32_t channel(std::uint32_t value) {
    return std::min<std::uint32_t>(value, 255U);
}

/// 0xRRGGBBAA, opaque.
std::uint32_t pack(const Square& square) {
    return (channel(square.red) << 24U) | (channel(square.green) << 16U) |
           (channel(square.blue) << 8U) | 0xFFU;
}

} // namespace

void draw(ecs::World& world) {
    auto* backend = world.resource<app::RenderBackend>();
    if (backend == nullptr) {
        return;
    }
    app::Renderer& renderer = backend->renderer();
    world.each<const Square, const transform2d::Position>(
        [&renderer](ecs::Entity, const Square& square, const transform2d::Position& position) {
            renderer.draw(
                app::DrawCommand{"square", position.x, position.y, square.size, pack(square)});
        });
    renderer.end_frame();
}

} // namespace ge::mod::debug_draw
