#include "ge/app/fields.hpp"
#include "ge/app/render.hpp"
#include "ge/mod/sprite2d/layered.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/transform2d/drawn.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

namespace ge::mod::sprite2d {

namespace {

/// A field reference that names no field.
constexpr std::uint32_t no_field = 0xFFFFFFFFU;

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

/// The number `field` (an index into the game's field table) of `entity`, when the game knows the
/// field and the entity has its component.
std::optional<double> number_of(const ecs::World& world, const app::FieldTable* table,
                                ecs::Entity entity, std::uint32_t field) {
    if (table == nullptr || field >= table->entries.size()) {
        return std::nullopt;
    }
    const app::FieldEntry& entry = table->entries[field];
    if (entry.component == nullptr || entry.component->read == nullptr) {
        return std::nullopt;
    }
    const auto value = entry.component->read(world, entity, entry.field);
    return value ? std::optional<double>(app::scalar_as<double>(*value)) : std::nullopt;
}

bool holds(const ecs::World& world, const app::FieldTable* table, ecs::Entity entity,
           const AnimationClip& clip) {
    if (clip.field == no_field) {
        return true;
    }
    const std::optional<double> number = number_of(world, table, entity, clip.field);
    if (!number) {
        return false;
    }
    const double value = clip.absolute ? std::abs(*number) : *number;
    return value >= static_cast<double>(clip.min) && value <= static_cast<double>(clip.max);
}

/// The clip of `set` that `entity` should play: the highest priority among those that hold, the
/// oldest at equal priority (`clips` are in creation order).
const AnimationClip* chosen(const ecs::World& world, const app::FieldTable* table,
                            ecs::Entity entity, std::uint32_t set,
                            const std::vector<AnimationClip>& clips) {
    const AnimationClip* best = nullptr;
    for (const AnimationClip& clip : clips) {
        if (clip.set == set && (best == nullptr || clip.priority > best->priority) &&
            holds(world, table, entity, clip)) {
            best = &clip;
        }
    }
    return best;
}

bool same_animation(const SpriteAnimation& animation, const AnimationClip& clip) {
    return animation.first_frame == clip.first_frame && animation.frame_count == clip.frame_count &&
           animation.ticks_per_frame == clip.ticks_per_frame && animation.looping == clip.looping;
}

} // namespace

void choose_clip(ecs::World& world) {
    std::vector<AnimationClip> clips;
    world.each<const AnimationClip>(
        [&clips](ecs::Entity, const AnimationClip& clip) { clips.push_back(clip); });
    if (clips.empty()) {
        return;
    }
    const auto* table = world.resource<app::FieldTable>();
    std::vector<std::pair<ecs::Entity, SpriteAnimation>> changes;
    world.each<const Animator>([&](ecs::Entity entity, const Animator& animator) {
        const AnimationClip* clip = chosen(world, table, entity, animator.set, clips);
        if (clip == nullptr) {
            return;
        }
        const auto* playing = world.get<SpriteAnimation>(entity);
        if (playing == nullptr || !same_animation(*playing, *clip)) {
            changes.emplace_back(entity, SpriteAnimation{clip->first_frame, clip->frame_count,
                                                         clip->ticks_per_frame, clip->looping, 0U});
        }
    });
    // Outside the walk over the world: a new SpriteAnimation changes the entity's components.
    for (const auto& [entity, animation] : changes) {
        static_cast<void>(world.insert(entity, animation));
    }
}

void face(ecs::World& world) {
    const auto* table = world.resource<app::FieldTable>();
    world.each<const Facing, Sprite>([&](ecs::Entity entity, const Facing& facing, Sprite& sprite) {
        std::optional<double> number;
        if (facing.field == no_field) {
            const auto* velocity = world.get<transform2d::Velocity>(entity);
            number = velocity == nullptr ? std::nullopt
                                         : std::optional<double>(static_cast<double>(velocity->x));
        } else {
            number = number_of(world, table, entity, facing.field);
        }
        const auto threshold = static_cast<double>(facing.threshold);
        if (number && *number > threshold) {
            sprite.flip_x = facing.drawn_left;
        } else if (number && *number < -threshold) {
            sprite.flip_x = !facing.drawn_left;
        }
    });
}

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
    // What other modules asked to draw among the sprites, after the sprites of its layer.
    if (auto* layered = world.resource<LayeredDraws>(); layered != nullptr) {
        for (LayeredDraws::Item& item : layered->items) {
            queue.push_back({item.layer, std::move(item.command)});
        }
        layered->items.clear();
    }
    // Stable: sprites of one layer keep the order the world visits them in.
    std::ranges::stable_sort(queue,
                             [](const Queued& a, const Queued& b) { return a.layer < b.layer; });
    app::Renderer& renderer = backend->renderer();
    for (const Queued& queued : queue) {
        renderer.draw(queued.command);
    }
}

} // namespace ge::mod::sprite2d
