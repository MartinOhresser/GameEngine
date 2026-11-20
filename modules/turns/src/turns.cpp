#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/turns/turns.gen.hpp"

#include <cstdint>

namespace ge::mod::turns {

void advance(ecs::World& world) {
    const auto* names = world.resource<app::ActionNames>();
    auto* actions = world.resource<app::Actions>();
    if (names == nullptr || actions == nullptr) {
        return;
    }
    bool done = false;
    world.each<Turn>([&](ecs::Entity, Turn& turn) {
        if (done) {
            return;
        }
        done = true;
        if (turn.end_action >= names->names.size() ||
            !actions->pressed(names->names[turn.end_action])) {
            return;
        }
        turn.current = turn.players == 0U ? 0U : (turn.current + 1U) % turn.players;
        ++turn.number;
        if (turn.changed_action < names->names.size()) {
            actions->raise(names->names[turn.changed_action]);
        }
    });
}

} // namespace ge::mod::turns
