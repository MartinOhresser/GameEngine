#pragma once

#include "ge/ecs/entity.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ge::app {

/// A request to change scene, left in the world by a system (a door, a menu
/// button). The app applies it between two ticks through its scene loader, so
/// the scene never changes in the middle of a tick.
struct SceneRequest {
    /// The scene to go to: its index in the game's `scene` list.
    std::optional<std::uint32_t> scene;
};

/// The scene the game is in: its index in the game's `scene` list.
struct CurrentScene {
    std::uint32_t index = 0;
};

/// The entities that scenes gave a name, so that rules and scenarios can find
/// them again; kept up to date when the scene changes.
class EntityNames {
public:
    void set(std::string name, ecs::Entity entity) {
        for (auto& item : named_) {
            if (item.first == name) {
                item.second = entity;
                return;
            }
        }
        named_.emplace_back(std::move(name), entity);
    }

    /// The entity with this name, or a null entity.
    [[nodiscard]] ecs::Entity find(std::string_view name) const {
        for (const auto& item : named_) {
            if (item.first == name) {
                return item.second;
            }
        }
        return ecs::Entity{};
    }

    /// Forgets the names of entities `keep` rejects.
    template <typename Keep>
    void retain(Keep keep) {
        std::erase_if(named_, [&keep](const auto& item) { return !keep(item.second); });
    }

    [[nodiscard]] const std::vector<std::pair<std::string, ecs::Entity>>& all() const noexcept {
        return named_;
    }

private:
    std::vector<std::pair<std::string, ecs::Entity>> named_;
};

/// The names of the game's actions, in the order of its `action` list: an
/// action's index is what a field with `"ref": "action"` holds.
struct ActionNames {
    std::vector<std::string> names;
};

} // namespace ge::app
