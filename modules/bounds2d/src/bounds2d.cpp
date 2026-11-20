#include "ge/app/direction.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/ecs/commands.hpp"
#include "ge/mod/bounds2d/bounds2d.gen.hpp"
#include "ge/mod/bounds2d/edge.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace ge::mod::bounds2d {

void check(ecs::World& world) {
    auto* commands = world.resource<ecs::Commands>();
    auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    // A game has one or two directions: the edge is looked for once per distinct direction.
    std::vector<std::pair<app::Direction, std::optional<float>>> edges;
    const auto edge_for = [&](const app::Direction& side) {
        for (const auto& known : edges) {
            if (known.first.vertical == side.vertical && known.first.sign == side.sign) {
                return known.second;
            }
        }
        edges.emplace_back(side, screen_edge(world, side));
        return edges.back().second;
    };
    world.each<OutOfView, const transform2d::Position>(
        [&](ecs::Entity entity, OutOfView& bounds, const transform2d::Position& position) {
            const app::Direction side = app::Direction::of(bounds.side_x, bounds.side_y);
            const std::optional<float> edge = edge_for(side);
            if (!edge || side.along(position.x, position.y) <= *edge + bounds.margin) {
                return;
            }
            if (!bounds.fired) {
                bounds.fired = true;
                if (actions != nullptr && names != nullptr &&
                    static_cast<std::size_t>(bounds.action) < names->names.size()) {
                    actions->raise(names->names[bounds.action]);
                }
            }
            if (bounds.despawn) {
                commands->despawn(entity);
            }
        });
}

} // namespace ge::mod::bounds2d
