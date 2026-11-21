#include "ge/app/fields.hpp"
#include "ge/app/lists.hpp"
#include "ge/app/save.hpp"
#include "ge/mod/records/records.gen.hpp"
#include "ge/mod/tag/find.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace ge::mod::records {

namespace {

/// The number the record follows, if its source and field exist right now.
std::optional<std::int32_t> read_source(ecs::World& world, const Record& record) {
    const auto* table = world.resource<app::FieldTable>();
    if (table == nullptr || record.field >= table->entries.size()) {
        return std::nullopt;
    }
    const ecs::Entity source = tag::find(world, record.source);
    const app::FieldEntry& field = table->entries[record.field];
    if (source.is_null() || field.component == nullptr || field.component->read == nullptr) {
        return std::nullopt;
    }
    const std::optional<app::Scalar> value = field.component->read(world, source, field.field);
    if (!value) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(std::llround(app::scalar_as<double>(*value)));
}

} // namespace

void track(ecs::World& world) {
    auto* save = world.resource<app::SaveData>();
    const auto* lists = world.resource<app::ListNames>();
    world.each<Record>([&](ecs::Entity, Record& record) {
        const std::string_view name =
            lists != nullptr ? lists->name("record", record.key) : std::string_view();
        const bool saved = save != nullptr && !name.empty();
        if (!record.loaded) {
            record.loaded = true;
            if (saved) {
                record.best = static_cast<std::int32_t>(save->get(name, record.best));
            }
        }
        const std::optional<std::int32_t> value = read_source(world, record);
        if (!value) {
            return;
        }
        record.last = *value;
        if (*value > record.best) {
            record.best = *value;
            if (saved) {
                save->set(name, record.best);
            }
        }
    });
}

} // namespace ge::mod::records
