#include "ge/app/render.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ge::mod::sprite2d {

namespace {

std::uint32_t channel(std::uint32_t value) {
    return std::min<std::uint32_t>(value, 255U);
}

/// 0xRRGGBBAA.
std::uint32_t pack(const Sprite& sprite) {
    return (channel(sprite.red) << 24U) | (channel(sprite.green) << 16U) |
           (channel(sprite.blue) << 8U) | channel(sprite.alpha);
}

struct Queued {
    std::int32_t layer = 0;
    app::DrawCommand command;
};

} // namespace

void animate(ecs::World& world) {
    world.each<Sprite, SpriteAnimation>([](ecs::Entity, Sprite& sprite, SpriteAnimation& anim) {
        const std::uint32_t count = std::max<std::uint32_t>(anim.frame_count, 1U);
        const std::uint32_t length = std::max<std::uint32_t>(anim.ticks_per_frame, 1U);
        const std::uint32_t total = count * length;
        if (anim.looping) {
            anim.elapsed = (anim.elapsed + 1U) % total;
        } else if (anim.elapsed < total - 1U) {
            ++anim.elapsed; // stops on the last tick of the last cell
        }
        sprite.frame = anim.first_frame + std::min(anim.elapsed / length, count - 1U);
    });
}

void draw(ecs::World& world) {
    auto* backend = world.resource<app::RenderBackend>();
    if (backend == nullptr) {
        return;
    }
    std::vector<Queued> queue;
    world.each<const Sprite, const transform2d::Position>(
        [&queue](ecs::Entity, const Sprite& sprite, const transform2d::Position& position) {
            app::DrawCommand command{"sprite", position.x, position.y, 0.0F, pack(sprite)};
            command.atlas = static_cast<int>(std::min<std::uint32_t>(sprite.atlas, 0x7FFFFFFFU));
            command.frame = sprite.frame;
            command.width = sprite.width;
            command.height = sprite.height;
            command.flip_x = sprite.flip_x;
            queue.push_back({sprite.layer, command});
        });
    // Stable: sprites of one layer keep the order the world visits them in.
    std::ranges::stable_sort(queue,
                             [](const Queued& a, const Queued& b) { return a.layer < b.layer; });
    app::Renderer& renderer = backend->renderer();
    for (const Queued& queued : queue) {
        renderer.draw(queued.command);
    }
}

} // namespace ge::mod::sprite2d
