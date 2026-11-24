#include "ge/app/fields.hpp"
#include "ge/app/render.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/transform2d/drawn.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
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

/// Where the centre of a picture goes, from the pivot's offset: the pivot says which point of the
/// picture sits on the Position (0.5: the middle), and needs a size in world units to move
/// anything. The renderer turns a picture around its centre, so the centre of a picture turned by
/// `angle` degrees turns around the pivot.
void turn_offset(float angle, float& offset_x, float& offset_y) {
    const float radians = angle * std::numbers::pi_v<float> / 180.0F;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const float turned_x = offset_x * cosine - offset_y * sine;
    offset_y = offset_x * sine + offset_y * cosine;
    offset_x = turned_x;
}

app::DrawCommand sprite_command(const ecs::World& world, ecs::Entity entity, const Sprite& sprite,
                                const transform2d::Position& position) {
    float offset_x = (0.5F - sprite.pivot_x) * sprite.width;
    float offset_y = (0.5F - sprite.pivot_y) * sprite.height;
    float angle = 0.0F;
    if (const auto* rotation = world.get<transform2d::Rotation>(entity);
        rotation != nullptr && rotation->angle != 0.0F) {
        angle = rotation->angle;
        turn_offset(angle, offset_x, offset_y);
    }
    app::DrawCommand command{"sprite", position.x + offset_x, position.y + offset_y, 0.0F,
                             pack(sprite)};
    command.angle = angle;
    if (sprite.additive) {
        command.blend = app::Blend::Add;
    } else if (sprite.multiply) {
        command.blend = app::Blend::Multiply;
    }
    if (const auto* parallax = world.get<Parallax>(entity); parallax != nullptr) {
        command.parallax_x = parallax->x;
        command.parallax_y = parallax->y;
        command.repeat_x = parallax->repeat_x;
    }
    command.atlas = static_cast<int>(std::min<std::uint32_t>(sprite.atlas, 0x7FFFFFFFU));
    command.frame = sprite.frame;
    command.width = sprite.width;
    command.height = sprite.height;
    command.flip_x = sprite.flip_x;
    command.screen = sprite.screen;
    command.lit = !sprite.unlit;
    command.light = sprite.light;
    if (const auto* scale = world.get<transform2d::Scale>(entity); scale != nullptr) {
        command.scale = scale->factor;
    }
    return command;
}

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
            if (!world.has<transform2d::Hidden>(entity)) {
                queue.push_back({sprite.layer, sprite_command(world, entity, sprite,
                                                              transform2d::drawn_position(
                                                                  world, entity, position))});
            }
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
