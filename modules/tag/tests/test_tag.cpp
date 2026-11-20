#include "ge/app/app.hpp"
#include "ge/mod/tag/find.hpp"
#include "ge/mod/tag/tag.gen.hpp"

#include "mini_test.hpp"

#include <cstdint>

namespace {

namespace tag = ge::mod::tag;

using ge::ecs::Entity;

constexpr std::uint32_t player = 0U;
constexpr std::uint32_t board = 1U;
constexpr std::uint32_t nobody = 7U;

Entity tagged_entity(ge::app::App& app, std::uint32_t name) {
    const Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, tag::Tag{name}));
    return entity;
}

} // namespace

TEST_CASE("tag: an entity is found by its tag, the oldest one when several share it") {
    ge::app::App app;
    tag::install(app);
    CHECK(app.schedule().problems().empty());
    const Entity first = tagged_entity(app, player);
    const Entity the_board = tagged_entity(app, board);
    static_cast<void>(tagged_entity(app, player));

    CHECK(tag::find(app.world(), player) == first);
    CHECK(tag::find(app.world(), board) == the_board);
    CHECK(tag::find(app.world(), nobody).is_null());

    const tag::Tagged all = tag::tagged(app.world());
    CHECK(all.find(player) == first);
    CHECK(all.find(board) == the_board);
    CHECK(all.find(nobody).is_null());
}

TEST_CASE("tag: when the oldest entity of a tag is gone, the next one answers") {
    ge::app::App app;
    tag::install(app);
    const Entity first = tagged_entity(app, player);
    const Entity second = tagged_entity(app, player);
    CHECK(app.world().despawn(first));
    CHECK(tag::find(app.world(), player) == second);
    CHECK(tag::tagged(app.world()).find(player) == second);
}
