#include "ge/app/fields.hpp"
#include "ge/mod/bind/bind.gen.hpp"
#include "ge/mod/tag/find.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace ge::mod::bind {

namespace {

constexpr std::uint32_t none = 0xFFFFFFFFU;

const app::FieldEntry* field_of(const app::FieldTable* table, std::uint32_t index) {
    if (table == nullptr || index >= table->entries.size()) {
        return nullptr;
    }
    const app::FieldEntry& entry = table->entries[index];
    return entry.component == nullptr ? nullptr : &entry;
}

/// This entity, or the one that answers to the tag.
ecs::Entity holder(const tag::Tagged& tags, ecs::Entity self, std::uint32_t name) {
    return name == none ? self : tags.find(name);
}

/// The share from 0 to 1 that value is of the way from low to high, raised to power.
float share(const Bind& bind, float value) {
    const float span = bind.high - bind.low;
    const float t = span == 0.0F ? (value >= bind.high ? 1.0F : 0.0F)
                                 : std::clamp((value - bind.low) / span, 0.0F, 1.0F);
    return bind.power == 1.0F ? t : std::pow(t, std::max(bind.power, 0.0F));
}

} // namespace

void follow(ecs::World& world) {
    const auto* table = world.resource<app::FieldTable>();
    const tag::Tagged tags(world);
    world.each<const Bind>([&](ecs::Entity entity, const Bind& bind) {
        const app::FieldEntry* source = field_of(table, bind.source);
        const app::FieldEntry* target = field_of(table, bind.target);
        const ecs::Entity from = holder(tags, entity, bind.from);
        const ecs::Entity onto = holder(tags, entity, bind.onto);
        if (source == nullptr || target == nullptr || source->component->read == nullptr ||
            target->component->write == nullptr || !world.is_alive(from) ||
            !world.is_alive(onto)) {
            return;
        }
        const auto value = source->component->read(world, from, source->field);
        if (!value) {
            return;
        }
        const float t = share(bind, app::scalar_as<float>(*value));
        const float out = bind.out_low + (bind.out_high - bind.out_low) * t;
        static_cast<void>(target->component->write(world, onto, target->field,
                                                   app::Scalar{static_cast<double>(out)}));
    });
}

} // namespace ge::mod::bind
