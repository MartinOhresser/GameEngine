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

/// 0xRRGGBBAA.
std::uint32_t pack(std::uint32_t red, std::uint32_t green, std::uint32_t blue,
                   std::uint32_t alpha) {
    return (channel(red) << 24U) | (channel(green) << 16U) | (channel(blue) << 8U) | channel(alpha);
}

std::uint32_t pack(const Square& square) {
    return pack(square.red, square.green, square.blue, 255U);
}

} // namespace

float scale_of(const ecs::World& world, ecs::Entity entity) {
    const auto* scale = world.get<transform2d::Scale>(entity);
    return scale == nullptr ? 1.0F : scale->factor;
}

void draw(ecs::World& world) {
    auto* backend = world.resource<app::RenderBackend>();
    if (backend == nullptr) {
        return;
    }
    app::Renderer& renderer = backend->renderer();
    world.each<const Square, const transform2d::Position>(
        [&](ecs::Entity entity, const Square& square, const transform2d::Position& position) {
            if (!world.has<transform2d::Hidden>(entity)) {
                renderer.draw(app::DrawCommand{"square", position.x, position.y,
                                               square.size * scale_of(world, entity),
                                               pack(square)});
            }
        });
    world.each<const Rect, const transform2d::Position>(
        [&](ecs::Entity entity, const Rect& rect, const transform2d::Position& position) {
            if (world.has<transform2d::Hidden>(entity)) {
                return;
            }
            app::DrawCommand command{"rect", position.x, position.y, 0.0F,
                                     pack(rect.red, rect.green, rect.blue, rect.alpha)};
            command.width = rect.width * scale_of(world, entity);
            command.height = rect.height * scale_of(world, entity);
            command.thickness = rect.thickness;
            command.screen = rect.screen;
            renderer.draw(command);
        });
    world.each<const Circle, const transform2d::Position>(
        [&](ecs::Entity entity, const Circle& circle, const transform2d::Position& position) {
            if (world.has<transform2d::Hidden>(entity)) {
                return;
            }
            app::DrawCommand command{"circle", position.x, position.y,
                                     circle.radius * scale_of(world, entity),
                                     pack(circle.red, circle.green, circle.blue, circle.alpha)};
            command.thickness = circle.thickness;
            command.screen = circle.screen;
            renderer.draw(command);
        });
    world.each<const Line, const transform2d::Position>(
        [&](ecs::Entity entity, const Line& line, const transform2d::Position& position) {
            if (world.has<transform2d::Hidden>(entity)) {
                return;
            }
            app::DrawCommand command{"line", position.x, position.y, 0.0F,
                                     pack(line.red, line.green, line.blue, line.alpha)};
            command.x2 = position.x + line.dx;
            command.y2 = position.y + line.dy;
            command.thickness = line.thickness;
            command.screen = line.screen;
            renderer.draw(command);
        });
}

} // namespace ge::mod::debug_draw
