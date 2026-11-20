#include "ge/app/app.hpp"
#include "ge/app/fields.hpp"
#include "ge/app/lists.hpp"
#include "ge/app/save.hpp"
#include "ge/mod/records/records.gen.hpp"
#include "ge/mod/tag/tag.gen.hpp"
#include "ge/mod/ui/ui.gen.hpp"

#include "mini_test.hpp"

#include <algorithm>
#include <cstdint>

namespace {

namespace records = ge::mod::records;
namespace tag = ge::mod::tag;
namespace ui = ge::mod::ui;

using ge::ecs::Entity;

constexpr std::uint32_t player_tag = 2U;

/// A game whose "score" is the `value` of a ui.Bar (any number of the game would do).
ge::app::App game() {
    ge::app::App app;
    // ui's own dependencies (drawing) are not needed to follow a number: the app lists them
    // among its module problems, which this test does not care about.
    tag::install(app);
    ui::install(app);
    records::install(app);
    CHECK(app.schedule().problems().empty());
    app.world().insert_resource(ge::app::ListNames{{{"record", {"best_height", "best_time"}}}});
    return app;
}

std::uint32_t field_index(ge::app::App& app, const char* name) {
    const auto* table = app.world().resource<ge::app::FieldTable>();
    for (std::size_t i = 0; i < table->entries.size(); ++i) {
        if (table->entries[i].name == name) {
            return static_cast<std::uint32_t>(i);
        }
    }
    CHECK(false);
    return 0;
}

Entity spawn_player(ge::app::App& app, float score) {
    const Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, tag::Tag{player_tag}));
    CHECK(app.world().insert(entity, ui::Bar{score, 1000.0F, 100.0F, 10.0F, false}));
    return entity;
}

Entity spawn_record(ge::app::App& app, std::uint32_t key) {
    const Entity entity = app.world().spawn();
    records::Record record;
    record.key = key;
    record.source = player_tag;
    record.field = field_index(app, "ui.Bar.value");
    CHECK(app.world().insert(entity, record));
    return entity;
}

const records::Record& record_of(ge::app::App& app, Entity entity) {
    return *app.world().get<records::Record>(entity);
}

void set_score(ge::app::App& app, Entity player, float score) {
    app.world().get<ui::Bar>(player)->value = score;
}

} // namespace

TEST_CASE("records: best and last follow the source, and the best is saved by name") {
    auto app = game();
    const Entity player = spawn_player(app, 10.0F);
    const Entity record = spawn_record(app, 0);
    app.run_frames(1);
    CHECK(record_of(app, record).last == 10);
    CHECK(record_of(app, record).best == 10);
    set_score(app, player, 40.0F);
    app.run_frames(1);
    set_score(app, player, 25.0F); // a drop: the best stays
    app.run_frames(1);
    CHECK(record_of(app, record).last == 25);
    CHECK(record_of(app, record).best == 40);
    const auto* save = app.world().resource<ge::app::SaveData>();
    CHECK(save->get("best_height") == 40);
    CHECK(!save->has("best_time"));
}

TEST_CASE("records: the best starts from what was saved, and is only raised") {
    auto app = game();
    app.world().resource<ge::app::SaveData>()->set("best_height", 500);
    const Entity player = spawn_player(app, 30.0F);
    const Entity record = spawn_record(app, 0);
    app.run_frames(2);
    CHECK(record_of(app, record).best == 500);
    CHECK(record_of(app, record).last == 30);
    CHECK(app.world().resource<ge::app::SaveData>()->get("best_height") == 500);
    set_score(app, player, 700.0F);
    app.run_frames(1);
    CHECK(record_of(app, record).best == 700);
    CHECK(app.world().resource<ge::app::SaveData>()->get("best_height") == 700);
}

TEST_CASE("records: the last value stays when the source is gone") {
    auto app = game();
    const Entity player = spawn_player(app, 80.0F);
    const Entity record = spawn_record(app, 0);
    app.run_frames(1);
    CHECK(app.world().despawn(player));
    app.run_frames(3);
    CHECK(record_of(app, record).last == 80);
    CHECK(record_of(app, record).best == 80);
    static_cast<void>(spawn_player(app, 5.0F)); // the next game: last follows it again
    app.run_frames(1);
    CHECK(record_of(app, record).last == 5);
    CHECK(record_of(app, record).best == 80);
}

TEST_CASE("records: two records keep their own names") {
    auto app = game();
    static_cast<void>(spawn_player(app, 12.0F));
    const Entity first = spawn_record(app, 0);
    const Entity second = spawn_record(app, 1);
    app.run_frames(1);
    CHECK(record_of(app, first).best == 12);
    CHECK(record_of(app, second).best == 12);
    const auto* save = app.world().resource<ge::app::SaveData>();
    CHECK(save->has("best_height"));
    CHECK(save->has("best_time"));
}

TEST_CASE("records: without a save or a name the record still follows the game") {
    ge::app::App app;
    tag::install(app);
    ui::install(app);
    records::install(app);
    static_cast<void>(spawn_player(app, 9.0F));
    const Entity record = spawn_record(app, 3); // no ListNames: the key means nothing
    app.run_frames(1);
    CHECK(record_of(app, record).best == 9);
    CHECK(!app.world().resource<ge::app::SaveData>()->has("best_height"));
}
