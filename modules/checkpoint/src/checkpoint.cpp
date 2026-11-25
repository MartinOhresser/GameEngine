#include "ge/app/fields.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/core/hash.hpp"
#include "ge/mod/checkpoint/checkpoint.gen.hpp"
#include "ge/mod/tag/find.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace ge::mod::checkpoint {

namespace {

constexpr std::uint32_t none = 0xFFFFFFFFU;

/// What was kept: for each Saved entity, the number of each of its fields (by its index in the
/// field table).
struct Kept {
    std::map<std::uint64_t, std::vector<std::pair<std::uint32_t, double>>> entities;

    void hash(Hasher& hasher) const {
        for (const auto& [link, numbers] : entities) {
            hasher.add(link);
            for (const auto& [field, value] : numbers) {
                hasher.add(field);
                hasher.add(value);
            }
        }
    }
};

Kept& kept_of(ecs::World& world) {
    if (auto* kept = world.resource<Kept>(); kept != nullptr) {
        return *kept;
    }
    return world.emplace_resource<Kept>();
}

bool pressed(const ecs::World& world, std::uint32_t action) {
    const auto* actions = world.resource<app::Actions>();
    const auto* names = world.resource<app::ActionNames>();
    return action != none && actions != nullptr && names != nullptr &&
           action < names->names.size() && actions->pressed(names->names[action]);
}

std::optional<double> number(const ecs::World& world, ecs::Entity entity,
                             const app::FieldEntry& entry) {
    if (entry.component == nullptr || entry.component->read == nullptr) {
        return std::nullopt;
    }
    const auto value = entry.component->read(world, entity, entry.field);
    return value ? std::optional<double>(app::scalar_as<double>(*value)) : std::nullopt;
}

void save(ecs::World& world, const app::FieldTable& table, Kept& kept) {
    kept.entities.clear();
    world.each<const Saved>([&](ecs::Entity entity, const Saved&) {
        auto& numbers = kept.entities[entity.to_link()];
        for (std::uint32_t index = 0; index < table.entries.size(); index += 1) {
            if (const auto value = number(world, entity, table.entries[index])) {
                numbers.emplace_back(index, *value);
            }
        }
    });
}

void load(ecs::World& world, const app::FieldTable& table, const Kept& kept) {
    for (const auto& [link, numbers] : kept.entities) {
        const ecs::Entity entity = ecs::Entity::from_link(link);
        if (!world.is_alive(entity)) {
            continue;
        }
        for (const auto& [index, value] : numbers) {
            const app::FieldEntry& entry = table.entries[index];
            if (entry.component != nullptr && entry.component->write != nullptr) {
                static_cast<void>(
                    entry.component->write(world, entity, entry.field, app::Scalar{value}));
            }
        }
    }
}

/// The watched number now, or nothing.
std::optional<double> watched(ecs::World& world, const Checkpoint& checkpoint,
                              const app::FieldTable& table) {
    if (checkpoint.watch >= table.entries.size()) {
        return std::nullopt;
    }
    const ecs::Entity source = tag::find(world, checkpoint.source);
    if (source.is_null()) {
        return std::nullopt;
    }
    return number(world, source, table.entries[checkpoint.watch]);
}

} // namespace

void keep(ecs::World& world) {
    const auto* table = world.resource<app::FieldTable>();
    if (table == nullptr) {
        return;
    }
    Checkpoint* checkpoint = nullptr;
    world.each<Checkpoint>([&checkpoint](ecs::Entity, Checkpoint& found) {
        if (checkpoint == nullptr) {
            checkpoint = &found;
        }
    });
    if (checkpoint == nullptr) {
        return;
    }
    Kept& kept = kept_of(world);
    const std::optional<double> now = watched(world, *checkpoint, *table);
    const bool first = checkpoint->at_start && checkpoint->saves == 0U;
    const bool moved = now.has_value() && *now != checkpoint->seen;
    if (first || moved || pressed(world, checkpoint->save)) {
        save(world, *table, kept);
        checkpoint->saves += 1U;
        checkpoint->seen = now.value_or(checkpoint->seen);
    }
    if (pressed(world, checkpoint->load)) {
        load(world, *table, kept);
        checkpoint->loads += 1U;
    }
}

} // namespace ge::mod::checkpoint
