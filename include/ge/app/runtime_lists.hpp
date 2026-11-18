#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ge::app {

/// Lists of texts that are only known while the game runs: the games a launcher
/// found, the players of a lobby, the names of the objects of a bag.
///
/// Texts of a game normally come from its strings files (see `TextTable`), and
/// components only hold numbers. A list of the game's `runtime_list` list gets
/// its items here, from the host or from a system; a `text2d.Dynamic` names a
/// list and an item (numbers again) to show one of them. The lists are not
/// part of the world's fingerprint: the simulation reads the indices.
///
/// Each list also remembers which of its items is selected (a paginated list
/// writes it, a button that "acts on the selection" reads it).
class RuntimeLists {
public:
    /// The value of `selected` for a list that has no selection.
    static constexpr std::uint32_t none = 4294967295U;

    /// Replaces the items of the list, creating it if needed.
    void set(std::string_view name, std::vector<std::string> items) {
        entry(name).items = std::move(items);
    }

    /// The items of the list, or null when it was never set.
    [[nodiscard]] const std::vector<std::string>* find(std::string_view name) const {
        const Entry* found = lookup(name);
        return found == nullptr ? nullptr : &found->items;
    }

    [[nodiscard]] std::size_t size(std::string_view name) const {
        const auto* items = find(name);
        return items == nullptr ? 0U : items->size();
    }

    /// The item at `index`, or an empty text when there is none.
    [[nodiscard]] std::string_view item(std::string_view name, std::size_t index) const {
        const auto* items = find(name);
        return items == nullptr || index >= items->size() ? std::string_view{} : (*items)[index];
    }

    /// Chooses an item (`none` for no selection).
    void select(std::string_view name, std::uint32_t index) { entry(name).selected = index; }

    /// The selected item of the list, `none` when no item was selected.
    [[nodiscard]] std::uint32_t selected(std::string_view name) const {
        const Entry* found = lookup(name);
        return found == nullptr ? none : found->selected;
    }

private:
    struct Entry {
        std::string name;
        std::vector<std::string> items;
        std::uint32_t selected = none;
    };

    [[nodiscard]] const Entry* lookup(std::string_view name) const {
        const auto found =
            std::ranges::find_if(entries_, [name](const Entry& e) { return e.name == name; });
        return found == entries_.end() ? nullptr : &*found;
    }

    Entry& entry(std::string_view name) {
        const auto found =
            std::ranges::find_if(entries_, [name](const Entry& e) { return e.name == name; });
        if (found != entries_.end()) {
            return *found;
        }
        entries_.push_back(Entry{std::string(name), {}, none});
        return entries_.back();
    }

    std::vector<Entry> entries_;
};

} // namespace ge::app
