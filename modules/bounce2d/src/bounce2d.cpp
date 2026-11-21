#include "ge/app/direction.hpp"
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

/// A Bouncy surface as a box, with what it is made of.
struct Surface {
    ecs::Entity entity;
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    Bouncy bouncy;
};

/// A surface as one Bouncer sees it, in the frame where that Bouncer falls towards bigger numbers:
/// `top` is the edge it comes down on, `low` and `high` the extent across its fall.
struct Seen {
    const Surface* surface = nullptr;
    float top = 0.0F;
    float low = 0.0F;
    float high = 0.0F;
};

/// The size of a box along the fall and across it.
float extent_along(const app::Direction& fall, float width, float height) {
    return fall.vertical ? height : width;
}

float extent_across(const app::Direction& fall, float width, float height) {
    return fall.vertical ? width : height;
}

std::vector<Surface> surfaces_of(ecs::World& world) {
    std::vector<Surface> surfaces;
    world.each<const Bouncy, const collision2d::Collider, const transform2d::Position>(
        [&surfaces](ecs::Entity entity, const Bouncy& bouncy, const collision2d::Collider& box,
                    const transform2d::Position& position) {
            surfaces.push_back(
                Surface{entity, position.x, position.y, box.width, box.height, bouncy});
        });
    return surfaces;
}

std::vector<Seen> seen_by(const std::vector<Surface>& surfaces, const app::Direction& fall) {
    std::vector<Seen> seen;
    seen.reserve(surfaces.size());
    for (const Surface& surface : surfaces) {
        const float half = extent_along(fall, surface.width, surface.height) / 2.0F;
        const float half_across = extent_across(fall, surface.width, surface.height) / 2.0F;
        const float centre_across = fall.across(surface.x, surface.y);
        seen.push_back(Seen{&surface, fall.along(surface.x, surface.y) - half,
                            centre_across - half_across, centre_across + half_across});
    }
    return seen;
}

/// What a body that falls from `last_bottom` to `bottom` between `low` and `high` comes down on.
struct Landing {
    /// The first surface met that throws the body back, if any.
    const Seen* bounce = nullptr;
    /// The surfaces that give way under it: the ones that break, from the first met to where it
    /// bounces (a brittle plank before a solid one breaks, and the body still bounces on the
    /// solid one).
    std::vector<const Seen*> broken;
};

Landing landing_on(const std::vector<Seen>& seen, float low, float high, float last_bottom,
                   float bottom) {
    std::vector<const Seen*> crossed;
    Landing landing;
    for (const Seen& candidate : seen) {
        const bool beside = candidate.high <= low || candidate.low >= high;
        if (!beside && last_bottom <= candidate.top + landing_slack && bottom >= candidate.top) {
            crossed.push_back(&candidate);
            const bool throws = !candidate.surface->bouncy.breaks;
            if (throws && (landing.bounce == nullptr || candidate.top < landing.bounce->top)) {
                landing.bounce = &candidate;
            }
        }
    }
    for (const Seen* candidate : crossed) {
        if (candidate->surface->bouncy.breaks &&
            (landing.bounce == nullptr || candidate->top <= landing.bounce->top)) {
            landing.broken.push_back(candidate);
        }
    }
    return landing;
}

/// A surface a body landed on, and which way that body fell.
struct Landed {
    const Surface* surface = nullptr;
    app::Direction fall;
};

/// What the landing does to the surface: it may give way, vanish or die.
void affect(ecs::World& world, const Landed& landed) {
    auto& commands = *world.resource<ecs::Commands>();
    const Bouncy& bouncy = landed.surface->bouncy;
    if (bouncy.breaks) {
        commands.remove<Bouncy>(landed.surface->entity);
        commands.insert(landed.surface->entity,
                        transform2d::Velocity{landed.fall.vx(bouncy.fall_speed),
                                              landed.fall.vy(bouncy.fall_speed)});
    } else if (bouncy.vanish) {
        commands.despawn(landed.surface->entity);
    }
}

} // namespace

void bounce(ecs::World& world) {
    const std::vector<Surface> surfaces = surfaces_of(world);
    const float step = world.resource<app::Time>()->fixed_delta *
                       world.resource<transform2d::Params>()->speed_scale;
    std::vector<Landed> landed;
    world.each<Bouncer, transform2d::Position, transform2d::Velocity, const collision2d::Collider>(
        [&](ecs::Entity, Bouncer& bouncer, transform2d::Position& position,
            transform2d::Velocity& velocity, const collision2d::Collider& box) {
            const app::Direction fall = app::Direction::of(bouncer.fall_x, bouncer.fall_y);
            const float speed = fall.along(velocity.x, velocity.y);
            if (speed <= 0.0F) {
                return; // not falling
            }
            const float half = extent_along(fall, box.width, box.height) / 2.0F;
            const float half_across = extent_across(fall, box.width, box.height) / 2.0F;
            const float centre_across = fall.across(position.x, position.y);
            // Where the leading edge was a tick ago: the movement ran just before.
            const float bottom = fall.along(position.x, position.y) + half;
            const float last_bottom = bottom - speed * step;
            const std::vector<Seen> seen = seen_by(surfaces, fall);
            const Landing landing = landing_on(seen, centre_across - half_across,
                                               centre_across + half_across, last_bottom, bottom);
            for (const Seen* broken : landing.broken) {
                landed.push_back(Landed{broken->surface, fall});
            }
            if (landing.bounce == nullptr) {
                return;
            }
            landed.push_back(Landed{landing.bounce->surface, fall});
            const float thrown = -landing.bounce->surface->bouncy.strength;
            velocity.x = fall.vertical ? velocity.x : fall.sign * thrown;
            velocity.y = fall.vertical ? fall.sign * thrown : velocity.y;
            const float rest = fall.sign * (landing.bounce->top - half);
            position.x = fall.vertical ? position.x : rest;
            position.y = fall.vertical ? rest : position.y;
            ++bouncer.bounces;
        });
    for (const Landed& one : landed) {
        affect(world, one);
    }
}

} // namespace ge::mod::bounce2d
