#include "ge/app/time.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace ge::mod::collision2d {

namespace {

/// A collider seen as a box, for the sweeps below.
struct Box {
    ecs::Entity entity;
    float min_x = 0;
    float max_x = 0;
    float min_y = 0;
    float max_y = 0;
    std::uint32_t layer = 0;
    std::uint32_t mask = 0;
    bool solid = false;
    bool body = false;
    bool pushable = false;
    /// The farthest a Pushable can be moved in one tick (pushables only).
    float push_step = 0;
};

/// The duration of a tick, 0 without a clock.
float tick_of(const ecs::World& world) {
    const auto* time = world.resource<app::Time>();
    return time == nullptr ? 0.0F : time->fixed_delta;
}

/// Every collider as a box, sorted from left to right (ties: oldest first), so
/// that overlaps can be found by sweeping along x.
std::vector<Box> boxes_of(ecs::World& world) {
    std::vector<Box> boxes;
    const float tick = tick_of(world);
    world.each<const Collider, const transform2d::Position>(
        [&boxes, &world, tick](ecs::Entity entity, const Collider& collider,
                               const transform2d::Position& position) {
            const float half_width = collider.width / 2.0F;
            const float half_height = collider.height / 2.0F;
            Box box;
            box.entity = entity;
            box.min_x = position.x - half_width;
            box.max_x = position.x + half_width;
            box.min_y = position.y - half_height;
            box.max_y = position.y + half_height;
            box.layer = collider.layer;
            box.mask = collider.mask;
            box.solid = collider.solid;
            box.body = world.get<Body>(entity) != nullptr;
            if (const auto* pushable = world.get<Pushable>(entity); pushable != nullptr) {
                box.pushable = collider.solid;
                box.push_step = pushable->speed > 0.0F ? pushable->speed * tick
                                                       : std::numeric_limits<float>::infinity();
            }
            boxes.push_back(box);
        });
    std::ranges::sort(boxes, [](const Box& a, const Box& b) {
        if (a.min_x != b.min_x) {
            return a.min_x < b.min_x;
        }
        return a.entity.index() < b.entity.index();
    });
    return boxes;
}

bool overlap(const Box& a, const Box& b) {
    return a.min_x < b.max_x && b.min_x < a.max_x && a.min_y < b.max_y && b.min_y < a.max_y;
}

/// Moves `body` (and its entity) out of `wall` along the shortest way.
void push_out(ecs::World& world, Box& body, const Box& wall) {
    const float left = body.max_x - wall.min_x;  // how far to go to the left
    const float right = wall.max_x - body.min_x; // ... to the right
    const float up = body.max_y - wall.min_y;
    const float down = wall.max_y - body.min_y;
    const float along_x = std::min(left, right);
    const float along_y = std::min(up, down);
    auto* position = world.get<transform2d::Position>(body.entity);
    auto* velocity = world.get<transform2d::Velocity>(body.entity);
    auto* blocked = world.get<Blocked>(body.entity);
    if (along_x < along_y) {
        const float shift = left < right ? -left : right;
        if (blocked != nullptr) {
            // Pushed to the left: the wall is on the right.
            (shift < 0.0F ? blocked->right : blocked->left) = true;
        }
        position->x += shift;
        body.min_x += shift;
        body.max_x += shift;
        if (velocity != nullptr && velocity->x * shift < 0.0F) {
            velocity->x = 0.0F;
        }
    } else {
        const float shift = up < down ? -up : down;
        if (blocked != nullptr) {
            // Pushed up (y grows downwards): the floor is below.
            (shift < 0.0F ? blocked->below : blocked->above) = true;
        }
        position->y += shift;
        body.min_y += shift;
        body.max_y += shift;
        if (velocity != nullptr && velocity->y * shift < 0.0F) {
            velocity->y = 0.0F;
        }
    }
}

/// The part of `shift` (along x) that `crate` can make before it meets a solid of its mask that
/// lies across its way (`pusher` and itself aside).
float room(const std::vector<Box>& walls, const Box& crate, const Box& pusher, float shift) {
    float allowed = std::abs(shift);
    for (const Box& other : walls) {
        if (other.entity == crate.entity || other.entity == pusher.entity ||
            (crate.mask & other.layer) == 0U || other.min_y >= crate.max_y ||
            crate.min_y >= other.max_y) {
            continue;
        }
        const float gap = shift > 0.0F ? other.min_x - crate.max_x : crate.min_x - other.max_x;
        if (gap >= 0.0F) {
            allowed = std::min(allowed, gap);
        }
    }
    return shift > 0.0F ? allowed : -allowed;
}

void shift_x(ecs::World& world, Box& box, float shift) {
    world.get<transform2d::Position>(box.entity)->x += shift;
    box.min_x += shift;
    box.max_x += shift;
}

/// After a push: a body that could not move the crate is stopped by it; one that moved it less
/// than it pushed (a heavy crate, a wall ahead) goes along at the crate's pace.
void after_push(ecs::World& world, ecs::Entity body, float push, float moved, float tick) {
    auto* velocity = world.get<transform2d::Velocity>(body);
    if (moved == 0.0F) {
        if (auto* blocked = world.get<Blocked>(body); blocked != nullptr) {
            (push > 0.0F ? blocked->right : blocked->left) = true;
        }
        if (velocity != nullptr && velocity->x * push > 0.0F) {
            velocity->x = 0.0F;
        }
    } else if (velocity != nullptr && tick > 0.0F && moved != push &&
               std::abs(velocity->x) * tick > std::abs(moved)) {
        velocity->x = moved / tick;
    }
}

/// `body` walks into the side of the Pushable `crate`: the crate moves along as far as it can,
/// and the body gives back what the crate could not make. False when the body comes from above
/// or below: the crate is then an ordinary wall.
bool shove(ecs::World& world, std::vector<Box>& boxes, const std::vector<Box>& walls, Box& body,
           Box& crate, float tick) {
    const float left = body.max_x - crate.min_x;
    const float right = crate.max_x - body.min_x;
    const float along_y = std::min(body.max_y - crate.min_y, crate.max_y - body.min_y);
    if (std::min(left, right) >= along_y) {
        return false;
    }
    const float push = left < right ? left : -right; // the crate goes the way the body comes
    const float moved =
        room(walls, crate, body, std::clamp(push, -crate.push_step, crate.push_step));
    shift_x(world, crate, moved);
    for (Box& copy : boxes) { // the crate's own box, for its turn as a Body
        if (copy.entity == crate.entity) {
            copy.min_x = crate.min_x;
            copy.max_x = crate.max_x;
        }
    }
    shift_x(world, body, moved - push);
    after_push(world, body.entity, push, moved, tick);
    return true;
}

} // namespace

void block(ecs::World& world) {
    world.each<Blocked>([](ecs::Entity, Blocked& blocked) { blocked = Blocked{}; });
    std::vector<Box> boxes = boxes_of(world);
    const float tick = tick_of(world);
    std::vector<Box> walls;
    float widest = 0.0F;
    for (const Box& box : boxes) {
        if (box.solid) {
            walls.push_back(box);
            widest = std::max(widest, box.max_x - box.min_x);
        }
    }
    if (walls.empty()) {
        return;
    }
    for (Box& body : boxes) {
        if (!body.body) {
            continue;
        }
        // Walls that start too far left to reach the body are skipped by a
        // binary search; the others are visited until they start past it.
        // The body may be pushed left while it is checked, so allow one more
        // body width of margin.
        const float earliest = body.min_x - widest - (body.max_x - body.min_x);
        auto first = std::ranges::lower_bound(walls, earliest, {},
                                              [](const Box& wall) { return wall.min_x; });
        for (; first != walls.end(); ++first) {
            Box& wall = *first;
            if (wall.min_x >= body.max_x) {
                break;
            }
            if (wall.entity == body.entity || (body.mask & wall.layer) == 0U) {
                continue;
            }
            if (overlap(body, wall) &&
                !(wall.pushable && shove(world, boxes, walls, body, wall, tick))) {
                push_out(world, body, wall);
            }
        }
    }
}

void detect(ecs::World& world) {
    const std::vector<Box> boxes = boxes_of(world);
    std::vector<std::uint32_t> counts(boxes.size(), 0U);
    std::vector<std::uint32_t> layers(boxes.size(), 0U);
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        for (std::size_t j = i + 1; j < boxes.size() && boxes[j].min_x < boxes[i].max_x; ++j) {
            if (!overlap(boxes[i], boxes[j])) {
                continue;
            }
            if ((boxes[i].mask & boxes[j].layer) != 0U) {
                ++counts[i];
                layers[i] |= boxes[j].layer;
            }
            if ((boxes[j].mask & boxes[i].layer) != 0U) {
                ++counts[j];
                layers[j] |= boxes[i].layer;
            }
        }
    }
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        if (auto* contacts = world.get<Contacts>(boxes[i].entity); contacts != nullptr) {
            contacts->count = counts[i];
            contacts->layers = layers[i];
        }
    }
}

} // namespace ge::mod::collision2d
