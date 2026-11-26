#include "ge/app/render.hpp"
#include "ge/app/time.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/mod/barrier2d/barrier2d.gen.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/reserve/stock.hpp"
#include "ge/mod/sprite2d/layered.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace ge::mod::barrier2d {

namespace {

constexpr std::uint32_t none = 0xFFFFFFFFU;

float length(float dx, float dy) { return std::sqrt(dx * dx + dy * dy); }

/// Whether a working caster would raise a wall (else a bubble).
bool is_wall(const Caster& caster, const transform2d::Position& at) {
    return length(caster.end_x - at.x, caster.end_y - at.y) > caster.wall_drag;
}

/// The Barrier of a pool to raise next: one that is down, else the one raised longest ago.
Barrier* free_barrier(ecs::World& world, std::uint32_t pool, ecs::Entity& entity) {
    Barrier* found = nullptr;
    world.each<Barrier>([&](ecs::Entity candidate, Barrier& barrier) {
        if (barrier.pool != pool) {
            return;
        }
        const bool better = found == nullptr || (found->up && !barrier.up) ||
                            (found->up == barrier.up && barrier.order < found->order);
        if (better) {
            found = &barrier;
            entity = candidate;
        }
    });
    return found;
}

std::uint32_t last_order(ecs::World& world) {
    std::uint32_t last = 0U;
    world.each<const Barrier>(
        [&last](ecs::Entity, const Barrier& barrier) { last = std::max(last, barrier.order); });
    return last;
}

void raise(ecs::World& world, Caster& caster, const transform2d::Position& at) {
    const bool wall = is_wall(caster, at);
    const float size = wall ? length(caster.end_x - at.x, caster.end_y - at.y) : caster.radius;
    if (!reserve::spend(world, caster.kind, size * caster.per_unit)) {
        caster.refused += 1U;
        return;
    }
    const std::uint32_t order = last_order(world) + 1U;
    ecs::Entity entity;
    Barrier* barrier = free_barrier(world, caster.pool, entity);
    if (barrier == nullptr) {
        return;
    }
    barrier->up = true;
    barrier->round = !wall;
    barrier->radius = caster.radius;
    barrier->end_x = caster.end_x;
    barrier->end_y = caster.end_y;
    barrier->thickness = caster.thickness;
    barrier->order = order;
    barrier->age = 0.0F;
    if (auto* place = world.get<transform2d::Position>(entity); place != nullptr) {
        *place = at;
    }
    caster.raised += 1U;
}

/// The point of the segment (ax, ay)-(bx, by) closest to (px, py).
void closest_on_segment(float ax, float ay, float bx, float by, float px, float py, float& qx,
                        float& qy) {
    const float dx = bx - ax;
    const float dy = by - ay;
    const float squared = dx * dx + dy * dy;
    const float t = squared > 0.0F ? std::clamp(((px - ax) * dx + (py - ay) * dy) / squared, 0.0F, 1.0F)
                                   : 0.0F;
    qx = ax + dx * t;
    qy = ay + dy * t;
}

struct Up {
    float x = 0.0F;
    float y = 0.0F;
    const Barrier* barrier = nullptr;
};

/// A box: its middle, where its middle was a tick ago, and its half sizes.
struct Box {
    float x = 0.0F;
    float y = 0.0F;
    float before_x = 0.0F;
    float before_y = 0.0F;
    float half_w = 0.0F;
    float half_h = 0.0F;
};

/// The way out of a barrier for a box: the push, or false when it does not touch it, or when it
/// was already inside a bubble (a fast body that crossed the edge in one tick is still outside).
bool push_of(const Up& up, const Box& box, float& push_x, float& push_y) {
    const Barrier& barrier = *up.barrier;
    const float x = box.x;
    const float y = box.y;
    const float half_w = box.half_w;
    const float half_h = box.half_h;
    float qx = up.x; // the point of the barrier nearest to the box
    float qy = up.y;
    float reach = barrier.radius;
    if (barrier.round) {
        if (length(box.before_x - up.x, box.before_y - up.y) < barrier.radius) {
            return false; // inside: the bubble keeps it, it does not push it out
        }
        if (length(x - up.x, y - up.y) < barrier.radius) {
            // Went through the edge this tick: back out along the way it came in.
            const float dx = box.before_x - up.x;
            const float dy = box.before_y - up.y;
            const float from = length(dx, dy);
            const float out = barrier.radius + half_h;
            push_x = up.x + dx / from * out - x;
            push_y = up.y + dy / from * out - y;
            return true;
        }
    } else {
        closest_on_segment(up.x, up.y, barrier.end_x, barrier.end_y, x, y, qx, qy);
        reach = barrier.thickness / 2.0F;
    }
    // The point of the box nearest to that point.
    const float px = std::clamp(qx, x - half_w, x + half_w);
    const float py = std::clamp(qy, y - half_h, y + half_h);
    const float dx = px - qx;
    const float dy = py - qy;
    const float distance = length(dx, dy);
    if (distance >= reach) {
        return false;
    }
    if (distance > 0.0F) {
        push_x = dx / distance * (reach - distance);
        push_y = dy / distance * (reach - distance);
        return true;
    }
    // The barrier's point is inside the box: out by the shortest way, along y first.
    push_x = 0.0F;
    push_y = (y < qy ? -1.0F : 1.0F) * (half_h - std::abs(y - qy) + reach);
    return true;
}

void stop(transform2d::Velocity& velocity, float push_x, float push_y,
          collision2d::Blocked* blocked) {
    const float distance = length(push_x, push_y);
    const float nx = push_x / distance;
    const float ny = push_y / distance;
    const float into = velocity.x * nx + velocity.y * ny;
    if (into < 0.0F) {
        velocity.x -= into * nx;
        velocity.y -= into * ny;
    }
    if (blocked == nullptr) {
        return;
    }
    blocked->below = blocked->below || ny < -0.5F;
    blocked->above = blocked->above || ny > 0.5F;
    blocked->left = blocked->left || nx > 0.5F;
    blocked->right = blocked->right || nx < -0.5F;
}

std::vector<Up> barriers_up(ecs::World& world) {
    std::vector<Up> up;
    world.each<const Barrier, const transform2d::Position>(
        [&up](ecs::Entity, const Barrier& barrier, const transform2d::Position& at) {
            if (barrier.up) {
                up.push_back(Up{at.x, at.y, &barrier});
            }
        });
    return up;
}

void freeze_inside(ecs::World& world, const std::vector<Up>& up) {
    auto* commands = world.resource<ecs::Commands>();
    world.each<const transform2d::Velocity, const transform2d::Position,
               const collision2d::Collider>([&](ecs::Entity entity, const transform2d::Velocity&,
                                                const transform2d::Position& at,
                                                const collision2d::Collider& collider) {
        const bool inside = std::ranges::any_of(up, [&](const Up& bubble) {
            return bubble.barrier->round && (collider.layer & bubble.barrier->freezes) != 0U &&
                   length(at.x - bubble.x, at.y - bubble.y) < bubble.barrier->radius;
        });
        if (inside) {
            // Renewed every tick: it ends one tick after the bubble goes.
            commands->insert(entity, transform2d::SpeedScale{0.0F, 2U});
        }
    });
}

app::DrawCommand sprite(std::uint32_t atlas, std::uint32_t frame, float x, float y) {
    app::DrawCommand command;
    command.what = "sprite";
    command.atlas = static_cast<int>(atlas);
    command.frame = frame;
    command.x = x;
    command.y = y;
    command.lit = false;
    return command;
}

app::DrawCommand bubble_picture(std::uint32_t atlas, std::uint32_t frame, float x, float y,
                                float radius) {
    app::DrawCommand command = sprite(atlas, frame, x, y);
    command.width = radius * 2.0F;
    command.height = radius * 2.0F;
    return command;
}

app::DrawCommand wall_picture(std::uint32_t atlas, std::uint32_t frame, float x, float y,
                              float end_x, float end_y, float thickness) {
    app::DrawCommand command = sprite(atlas, frame, (x + end_x) / 2.0F, (y + end_y) / 2.0F);
    command.width = length(end_x - x, end_y - y) + thickness;
    command.height = thickness;
    command.angle = std::atan2(end_y - y, end_x - x) * 180.0F / std::numbers::pi_v<float>;
    return command;
}

constexpr float turn = 2.0F * std::numbers::pi_v<float>;

/// How much of its size a barrier raised `age` seconds ago is drawn at: from nothing to a little
/// past all of it, then all of it (it springs up), over `appear` seconds.
float springing(float age, float appear) {
    if (appear <= 0.0F || age >= appear) {
        return 1.0F;
    }
    constexpr float back = 1.70158F;
    const float u = (age / appear) - 1.0F;
    return std::max(1.0F + ((back + 1.0F) * u * u * u) + (back * u * u), 0.0F);
}

/// Its size breathing once up: a slow wave of the share wobble.
float breathing(float age, float wobble) {
    return 1.0F + (wobble * std::sin(age * turn * 0.45F)) +
           (wobble * 0.4F * std::sin((age * turn * 1.1F) + 1.3F));
}

/// A number in [0, 1) for a barrier's spark: the same for the same raising and spark.
float chance(std::uint32_t order, std::uint32_t spark, std::uint32_t salt) {
    std::uint64_t value = (static_cast<std::uint64_t>(order) << 32U) ^
                          (static_cast<std::uint64_t>(spark) * 0x9E3779B97F4A7C15ULL) ^
                          (static_cast<std::uint64_t>(salt) * 0xC2B2AE3D27D4EB4FULL);
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    value ^= value >> 31U;
    return static_cast<float>(value >> 40U) / static_cast<float>(1U << 24U);
}

/// The glints a barrier lets fly as it appears, `age` seconds after: from its edge (a bubble) or
/// its length (a wall), outwards, slowing down, shrinking and fading.
void add_sparks(sprite2d::LayeredDraws& draws, const Barrier& barrier,
                const transform2d::Position& at, float age) {
    if (barrier.sparks == 0U || barrier.spark_life <= 0.0F || age >= barrier.spark_life) {
        return;
    }
    const float left = 1.0F - (age / barrier.spark_life);
    for (std::uint32_t spark = 0; spark < barrier.sparks; spark += 1) {
        const float a = chance(barrier.order, spark, 1U);
        const float speed = barrier.spark_speed * (0.5F + chance(barrier.order, spark, 2U));
        const float gone = speed * age * (1.0F - (0.5F * age / barrier.spark_life));
        float x = 0.0F;
        float y = 0.0F;
        float out_x = 0.0F;
        float out_y = 0.0F;
        if (barrier.round) {
            out_x = std::cos(a * turn);
            out_y = std::sin(a * turn);
            x = at.x + (out_x * barrier.radius);
            y = at.y + (out_y * barrier.radius);
        } else {
            const float dx = barrier.end_x - at.x;
            const float dy = barrier.end_y - at.y;
            const float long_way = std::max(length(dx, dy), 0.001F);
            const float side = chance(barrier.order, spark, 3U) < 0.5F ? -1.0F : 1.0F;
            out_x = -dy / long_way * side;
            out_y = dx / long_way * side;
            x = at.x + (dx * a);
            y = at.y + (dy * a);
        }
        app::DrawCommand glint;
        glint.what = "glow";
        glint.x = x + (out_x * gone);
        glint.y = y + (out_y * gone);
        glint.size = barrier.spark_size * left * (0.6F + (0.8F * chance(barrier.order, spark, 4U)));
        const auto alpha = static_cast<std::uint32_t>(
            std::lround(static_cast<float>(barrier.spark_color & 0xFFU) * left));
        glint.color = (barrier.spark_color & 0xFFFFFF00U) | std::min(alpha, 255U);
        glint.blend = app::Blend::Add;
        glint.lit = false;
        draws.add(barrier.draw_layer, glint);
    }
}

/// The picture of a raised barrier as it is drawn now, `age` seconds after it went up.
app::DrawCommand barrier_picture(const Barrier& barrier, const transform2d::Position& at,
                                 float age) {
    const float grown = springing(age, barrier.appear);
    const float alive = breathing(age, barrier.wobble);
    if (barrier.round) {
        return bubble_picture(barrier.atlas, barrier.bubble, at.x, at.y,
                              barrier.radius * grown * alive);
    }
    // A wall opens from its middle to its ends.
    const float mid_x = (at.x + barrier.end_x) / 2.0F;
    const float mid_y = (at.y + barrier.end_y) / 2.0F;
    const float half_x = (barrier.end_x - at.x) / 2.0F * grown;
    const float half_y = (barrier.end_y - at.y) / 2.0F * grown;
    return wall_picture(barrier.atlas, barrier.wall, mid_x - half_x, mid_y - half_y,
                        mid_x + half_x, mid_y + half_y, barrier.thickness * grown * alive);
}

} // namespace

void cast(ecs::World& world) {
    const float tick = world.resource<app::Time>()->fixed_delta *
                       world.resource<transform2d::Params>()->speed_scale;
    world.each<Caster, const transform2d::Position>(
        [&](ecs::Entity entity, Caster& caster, const transform2d::Position& at) {
            const bool working = !world.has<transform2d::Disabled>(entity);
            if (working) {
                caster.radius = caster.working
                                    ? std::min(caster.radius + caster.grow * tick, caster.max_radius)
                                    : caster.start_radius;
                caster.working = true;
                return;
            }
            if (caster.working) {
                raise(world, caster, at);
            }
            caster.working = false;
            caster.radius = 0.0F;
        });
}

void block(ecs::World& world) {
    const float tick = world.resource<app::Time>()->fixed_delta *
                       world.resource<transform2d::Params>()->speed_scale;
    world.each<Barrier>([tick](ecs::Entity, Barrier& barrier) {
        barrier.age = barrier.up ? barrier.age + tick : 0.0F;
    });
    const std::vector<Up> up = barriers_up(world);
    if (up.empty()) {
        return;
    }
    world.each<const collision2d::Body, const collision2d::Collider, transform2d::Position,
               transform2d::Velocity>([&](ecs::Entity entity, const collision2d::Body&,
                                          const collision2d::Collider& collider,
                                          transform2d::Position& position,
                                          transform2d::Velocity& velocity) {
        const auto* own = world.get<transform2d::SpeedScale>(entity);
        const float step = tick * (own == nullptr ? 1.0F : own->factor);
        for (const Up& barrier : up) {
            float push_x = 0.0F;
            float push_y = 0.0F;
            const Box box{position.x, position.y, position.x - velocity.x * step,
                          position.y - velocity.y * step, collider.width / 2.0F,
                          collider.height / 2.0F};
            if ((collider.mask & barrier.barrier->layer) == 0U ||
                !push_of(barrier, box, push_x, push_y)) {
                continue;
            }
            position.x += push_x;
            position.y += push_y;
            stop(velocity, push_x, push_y, world.get<collision2d::Blocked>(entity));
        }
    });
    freeze_inside(world, up);
}

void draw(ecs::World& world) {
    auto& draws = sprite2d::layered_draws(world);
    // Between two ticks on a screen faster than the game: the age of now, not of the last tick.
    const auto* time = world.resource<app::Time>();
    const float since_tick = time != nullptr && time->interpolate
                                 ? time->alpha * time->fixed_delta *
                                       world.resource<transform2d::Params>()->speed_scale
                                 : 0.0F;
    world.each<const Barrier, const transform2d::Position>(
        [&](ecs::Entity entity, const Barrier& barrier, const transform2d::Position& at) {
            if (!barrier.up || barrier.atlas == none || world.has<transform2d::Hidden>(entity)) {
                return;
            }
            const float age = barrier.age + since_tick;
            draws.add(barrier.draw_layer, barrier_picture(barrier, at, age));
            add_sparks(draws, barrier, at, age);
        });
    world.each<const Caster, const transform2d::Position>(
        [&](ecs::Entity, const Caster& caster, const transform2d::Position& at) {
            if (!caster.working || caster.atlas == none) {
                return;
            }
            draws.add(caster.layer,
                      is_wall(caster, at)
                          ? wall_picture(caster.atlas, caster.preview, at.x, at.y, caster.end_x,
                                         caster.end_y, caster.thickness)
                          : bubble_picture(caster.atlas, caster.preview, at.x, at.y,
                                           caster.radius));
        });
}

} // namespace ge::mod::barrier2d
