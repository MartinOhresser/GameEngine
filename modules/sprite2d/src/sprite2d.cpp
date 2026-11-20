#include "ge/app/fields.hpp"
#include "ge/app/render.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <bit>
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

void bind_frame(ecs::World& world) {
    const auto* table = world.resource<app::FieldTable>();
    if (table == nullptr) {
        return;
    }
    world.each<const FrameBind, Sprite>(
        [&](ecs::Entity entity, const FrameBind& bind, Sprite& sprite) {
            std::int64_t number = 0;
            if (bind.field < table->entries.size()) {
                const app::FieldEntry& field = table->entries[bind.field];
                if (field.component != nullptr && field.component->read != nullptr) {
                    if (const auto read = field.component->read(world, entity, field.field)) {
                        number = std::max<std::int64_t>(app::scalar_as<std::int64_t>(*read), 0);
                    }
                }
            }
            auto step = static_cast<std::uint32_t>(std::min<std::int64_t>(number, 0x7FFFFFFF));
            if (bind.log2) {
                step = step == 0U ? 0U : static_cast<std::uint32_t>(std::bit_width(step)) - 1U;
            }
            if (bind.frame_count > 0U) {
                step = std::min(step, bind.frame_count - 1U);
            }
            sprite.frame = bind.first_frame + step;
        });
}

void draw(ecs::World& world) {
    auto* backend = world.resource<app::RenderBackend>();
    if (backend == nullptr) {
        return;
    }
    std::vector<Queued> queue;
    world.each<const Sprite, const transform2d::Position>(
        [&](ecs::Entity entity, const Sprite& sprite, const transform2d::Position& position) {
            if (world.has<transform2d::Hidden>(entity)) {
                return;
            }
            // The pivot says which point of the picture sits on the Position (0.5:
            // the middle). It needs a size in world units to move anything.
            const float x = position.x + (0.5F - sprite.pivot_x) * sprite.width;
            const float y = position.y + (0.5F - sprite.pivot_y) * sprite.height;
            app::DrawCommand command{"sprite", x, y, 0.0F, pack(sprite)};
            command.atlas = static_cast<int>(std::min<std::uint32_t>(sprite.atlas, 0x7FFFFFFFU));
            command.frame = sprite.frame;
            command.width = sprite.width;
            command.height = sprite.height;
            command.flip_x = sprite.flip_x;
            command.screen = sprite.screen;
            if (const auto* scale = world.get<transform2d::Scale>(entity); scale != nullptr) {
                command.scale = scale->factor;
            }
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
