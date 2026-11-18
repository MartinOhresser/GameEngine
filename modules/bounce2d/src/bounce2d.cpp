#include "ge/app/time.hpp"
#include "ge/mod/bounce2d/bounce2d.gen.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <vector>

namespace ge::mod::bounce2d {

namespace {

/// How far a bottom edge may be under a top edge and still count as having been above it:
/// positions are floats, and a Bouncer that was put on a surface sits exactly on it.
constexpr float landing_slack = 0.01F;

/// A Bouncy surface seen from above: where its top edge is and how wide it is.
struct Surface {
    ecs::Entity entity;
    float left = 0.0F;
    float right = 0.0F;
    float top = 0.0F;
    Bouncy bouncy;
};

std::vector<Surface> surfaces_of(ecs::World& world) {
    std::vector<Surface> surfaces;
    world.each<const Bouncy, const collision2d::Collider, const transform2d::Position>(
        [&surfaces](ecs::Entity entity, const Bouncy& bouncy, const collision2d::Collider& box,
                    const transform2d::Position& position) {
            Surface surface;
            surface.entity = entity;
            surface.left = position.x - box.width / 2.0F;
            surface.right = position.x + box.width / 2.0F;
            surface.top = position.y - box.height / 2.0F;
            surface.bouncy = bouncy;
            surfaces.push_back(surface);
        });
    return surfaces;
}

/// The highest surface that a body with these sides and these bottom edges lands on, if any.
const Surface* landing_on(const std::vector<Surface>& surfaces, float left, float right,
                          float last_bottom, float bottom) {
    const Surface* best = nullptr;
    for (const Surface& surface : surfaces) {
        const bool beside = surface.right <= left || surface.left >= right;
        const bool crossed = last_bottom <= surface.top + landing_slack && bottom >= surface.top;
        if (!beside && crossed && (best == nullptr || surface.top < best->top)) {
            best = &surface;
        }
    }
    return best;
}

/// What the landing does to the surface: it may give way, vanish or die.
void affect(ecs::World& world, const Surface& surface) {
    auto& commands = *world.resource<ecs::Commands>();
    if (surface.bouncy.breaks) {
        commands.remove<Bouncy>(surface.entity);
        commands.insert(surface.entity, transform2d::Velocity{0.0F, surface.bouncy.fall_speed});
    } else if (surface.bouncy.vanish || surface.bouncy.kills) {
        commands.despawn(surface.entity);
    }
}

} // namespace

void bounce(ecs::World& world) {
    const std::vector<Surface> surfaces = surfaces_of(world);
    const float step = world.resource<app::Time>()->fixed_delta *
                       world.resource<transform2d::Params>()->speed_scale;
    std::vector<const Surface*> landed;
    world.each<Bouncer, transform2d::Position, transform2d::Velocity, const collision2d::Collider>(
        [&](ecs::Entity, Bouncer& bouncer, transform2d::Position& position,
            transform2d::Velocity& velocity, const collision2d::Collider& box) {
            const float half_height = box.height / 2.0F;
            if (velocity.y > 0.0F) {
                // Where the bottom edge was a tick ago: the movement ran just before.
                const float bottom = position.y + half_height;
                const float last_bottom = bottom - velocity.y * step;
                const float half_width = box.width / 2.0F;
                const Surface* surface = landing_on(surfaces, position.x - half_width,
                                                    position.x + half_width, last_bottom, bottom);
                if (surface != nullptr) {
                    landed.push_back(surface);
                    if (!surface->bouncy.breaks) {
                        velocity.y = -surface->bouncy.strength;
                        position.y = surface->top - half_height;
                        ++bouncer.bounces;
                    }
                }
            }
        });
    for (const Surface* surface : landed) {
        affect(world, *surface);
    }
}

} // namespace ge::mod::bounce2d
