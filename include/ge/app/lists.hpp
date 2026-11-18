#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ge::app {

/// The names of the game's lists (its own `lists`, and the built-in ones such
/// as `prefab`), as a resource: a field with `"ref": "list"` holds an index
/// into one of them, and a system that needs the name (to save under it, say)
/// looks it up here.
struct ListNames {
    std::vector<std::pair<std::string, std::vector<std::string>>> lists;

    /// The name at `index` of the list, or an empty text when there is none.
    [[nodiscard]] std::string_view name(std::string_view list, std::size_t index) const {
        const auto found = std::ranges::find_if(
            lists, [list](const auto& item) { return item.first == list; });
        if (found == lists.end() || index >= found->second.size()) {
            return {};
        }
        return found->second[index];
    }
};

} // namespace ge::app
