#include "ge/app/fields.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/rules/rules.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"
#include "ge/mod/ui/ui.gen.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace ge::mod::rules {

namespace {

using Tagged = std::vector<std::pair<std::uint32_t, ecs::Entity>>;

/// The oldest entity of each tag.
Tagged tagged_entities(ecs::World& world) {
    Tagged tagged;
    world.each<const ui::Tag>([&tagged](ecs::Entity entity, const ui::Tag& tag) {
        const bool known =
            std::ranges::any_of(tagged, [&tag](const auto& item) { return item.first == tag.tag; });
        if (!known) {
            tagged.emplace_back(tag.tag, entity);
        }
    });
    return tagged;
}

ecs::Entity find(const Tagged& tagged, std::uint32_t tag) {
    for (const auto& item : tagged) {
        if (item.first == tag) {
            return item.second;
        }
    }
    return ecs::Entity{};
}

const app::FieldEntry* field_of(const app::FieldTable* table, std::uint32_t index) {
    if (table == nullptr || index >= table->entries.size()) {
        return nullptr;
    }
    const app::FieldEntry& entry = table->entries[index];
    return entry.component == nullptr ? nullptr : &entry;
}

float number(const ecs::World& world, ecs::Entity entity, const app::FieldEntry* field) {
    if (field == nullptr || field->component->read == nullptr || !world.is_alive(entity)) {
        return 0.0F;
    }
    const auto value = field->component->read(world, entity, field->field);
    return value ? app::scalar_as<float>(*value) : 0.0F;
}

void raise(const app::ActionNames* names, app::Actions* actions, std::uint32_t action) {
    if (names != nullptr && actions != nullptr && action < names->names.size()) {
        actions->raise(names->names[action]);
    }
}

} // namespace

void watch(ecs::World& world) {
    const auto* table = world.resource<app::FieldTable>();
    const auto* names = world.resource<app::ActionNames>();
    auto* actions = world.resource<app::Actions>();
    const Tagged tagged = tagged_entities(world);
    world.each<Watch>([&](ecs::Entity entity, Watch& rule) {
        if (world.has<transform2d::Disabled>(entity) || (rule.once && rule.fired)) {
            return;
        }
        const ecs::Entity watched = rule.own ? entity : find(tagged, rule.source);
        const float now = number(world, watched, field_of(table, rule.field));
        const bool inside = now >= rule.min && now <= rule.max;
        if (inside && !rule.inside) {
            rule.fired = true;
            raise(names, actions, rule.action);
        }
        rule.inside = inside;
    });
}

void on_action(ecs::World& world) {
    const auto* table = world.resource<app::FieldTable>();
    const auto* names = world.resource<app::ActionNames>();
    const auto* actions = world.resource<app::Actions>();
    if (names == nullptr || actions == nullptr) {
        return;
    }
    const Tagged tagged = tagged_entities(world);
    world.each<const OnAction>([&](ecs::Entity entity, const OnAction& rule) {
        if (rule.action >= names->names.size() || !actions->pressed(names->names[rule.action]) ||
            world.has<transform2d::Disabled>(entity)) {
            return;
        }
        const app::FieldEntry* field = field_of(table, rule.field);
        const ecs::Entity target = rule.own ? entity : find(tagged, rule.source);
        if (field == nullptr || field->component->write == nullptr || !world.is_alive(target)) {
            return;
        }
        const float next = rule.add ? number(world, target, field) + rule.value : rule.value;
        static_cast<void>(field->component->write(world, target, field->field,
                                                  app::Scalar{static_cast<double>(next)}));
    });
}

} // namespace ge::mod::rules
