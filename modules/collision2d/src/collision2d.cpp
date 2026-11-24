#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
};

/// Every collider as a box, sorted from left to right (ties: oldest first), so
/// that overlaps can be found by sweeping along x.
std::vector<Box> boxes_of(ecs::World& world) {
    std::vector<Box> boxes;
    world.each<const Collider, const transform2d::Position>(
        [&boxes, &world](ecs::Entity entity, const Collider& collider,
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

} // namespace

void block(ecs::World& world) {
    world.each<Blocked>([](ecs::Entity, Blocked& blocked) { blocked = Blocked{}; });
    std::vector<Box> boxes = boxes_of(world);
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
            const Box& wall = *first;
            if (wall.min_x >= body.max_x) {
                break;
            }
            if (wall.entity == body.entity || (body.mask & wall.layer) == 0U) {
                continue;
            }
            if (overlap(body, wall)) {
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
