#include "ge/app/app.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/app/spawn.hpp"
#include "ge/app/viewport.hpp"
#include "ge/core/random.hpp"
#include "ge/mod/camera2d/camera2d.gen.hpp"
#include "ge/mod/spawner2d/spawner2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace {

namespace camera2d = ge::mod::camera2d;
namespace spawner2d = ge::mod::spawner2d;
namespace transform2d = ge::mod::transform2d;

using ge::app::SpawnRequest;
using ge::ecs::Entity;

/// A game of 480 x 720 pixels whose app only records what systems asked to create.
struct Setup {
    ge::app::App app;
    std::shared_ptr<std::vector<SpawnRequest>> seen = std::make_shared<std::vector<SpawnRequest>>();
    Entity camera;

    explicit Setup(std::uint64_t seed = 1) {
        transform2d::install(app);
        camera2d::install(app);
        spawner2d::install(app);
        CHECK(app.module_problems().empty());
        CHECK(app.schedule().problems().empty());
        app.world().insert_resource(ge::app::Viewport{480, 720});
        app.world().insert_resource(ge::Random{seed});
        app.world().insert_resource(ge::app::ActionNames{{"shoot"}});
        app.set_prefab_spawner([seen = seen](ge::app::App&, const SpawnRequest& request) {
            seen->push_back(request);
        });
        camera = app.world().spawn();
        CHECK(app.world().insert(camera, transform2d::Position{240.0F, 360.0F}));
        CHECK(app.world().insert(camera, camera2d::Camera{}));
    }

    void move_camera_to(float y) { app.world().get<transform2d::Position>(camera)->y = y; }
};

/// A generator of prefab 3 whose rows are `gap` apart, starting at y = 700, 200 above the screen.
spawner2d::Generator rows_of(float gap) {
    spawner2d::Generator generator;
    generator.prefab = 3;
    generator.gap_min = gap;
    generator.gap_max = gap;
    generator.across_min = 100.0F;
    generator.across_max = 300.0F;
    generator.next = 700.0F;
    generator.origin = 700.0F; // the height of a row is how far it is above the first one
    return generator;
}

void add(Setup& setup, const spawner2d::Generator& generator) {
    const Entity entity = setup.app.world().spawn();
    CHECK(setup.app.world().insert(entity, generator));
}

/// A usual platform row: prefab 3, inside the band x in [100, 300], no velocity.
void check_row(const SpawnRequest& request, float y) {
    CHECK(request.prefab == 3U);
    CHECK(request.y == y);
    CHECK(request.x >= 100.0F);
    CHECK(request.x <= 300.0F);
    CHECK(!request.has_velocity);
}

/// The shot of the hero at (100, 300) with the gun below: up at 500, from (104, 280).
void check_shot(const SpawnRequest& shot) {
    CHECK(shot.prefab == 2U);
    CHECK(shot.x == 104.0F);
    CHECK(shot.y == 280.0F);
    CHECK(shot.has_velocity);
    CHECK(shot.vx == 0.0F);
    CHECK(shot.vy == -500.0F);
}

} // namespace

TEST_CASE("spawner2d: a generator fills the screen and 200 units above it") {
    Setup setup;
    add(setup, rows_of(100.0F));
    setup.app.run_frames(1);
    // the screen shows y from 0 to 720: rows from 700 up to just above -200
    REQUIRE(setup.seen->size() == 9);
    for (std::size_t i = 0; i < setup.seen->size(); ++i) {
        check_row((*setup.seen)[i], 700.0F - 100.0F * static_cast<float>(i));
    }
}

TEST_CASE("spawner2d: it builds more as the camera climbs, and never the same row twice") {
    Setup setup;
    add(setup, rows_of(100.0F));
    setup.app.run_frames(2);
    CHECK(setup.seen->size() == 9); // nothing more while the camera stays
    setup.move_camera_to(60.0F);    // the top edge is now at -300
    setup.app.run_frames(1);
    // the top edge is at -300 and rows are built down to 200 above it: -200, -300, -400
    REQUIRE(setup.seen->size() == 12);
    CHECK(setup.seen->back().y == -400.0F);
    setup.move_camera_to(160.0F); // going down builds nothing
    setup.app.run_frames(3);
    CHECK(setup.seen->size() == 12);
}

TEST_CASE("spawner2d: the gaps grow with the height over the ramp") {
    Setup setup;
    spawner2d::Generator generator = rows_of(50.0F);
    generator.gap_max = 50.0F;
    generator.gap_min_end = 150.0F;
    generator.gap_max_end = 150.0F;
    generator.ramp_distance = 400.0F;
    add(setup, generator);
    setup.move_camera_to(-2000.0F); // a lot to build
    setup.app.run_frames(1);
    REQUIRE(setup.seen->size() > 10);
    std::vector<float> gaps;
    for (std::size_t i = 1; i < setup.seen->size(); ++i) {
        gaps.push_back((*setup.seen)[i - 1].y - (*setup.seen)[i].y);
    }
    CHECK(gaps.front() == 50.0F);
    CHECK(std::is_sorted(gaps.begin(), gaps.end()));
    CHECK(gaps.back() == 150.0F);
}

TEST_CASE("spawner2d: alternatives replace the usual prefab by their chance") {
    Setup setup;
    spawner2d::Generator generator = rows_of(10.0F);
    generator.alt_prefab = 4;
    generator.alt_chance = 0.5F;
    generator.alt2_prefab = 5;
    generator.alt2_chance = 0.2F;
    add(setup, generator);
    setup.move_camera_to(-3000.0F);
    setup.app.run_frames(8); // a generator builds at most 64 rows per tick
    const auto count = [&setup](std::uint32_t prefab) {
        return std::count_if(
            setup.seen->begin(), setup.seen->end(),
            [prefab](const SpawnRequest& request) { return request.prefab == prefab; });
    };
    const auto total = static_cast<long>(setup.seen->size());
    REQUIRE(total > 300);
    CHECK(count(5) > total * 15 / 100); // 20% of rows
    CHECK(count(5) < total * 25 / 100);
    CHECK(count(4) > total * 40 / 100); // 50% of the rest... of the rows left by the first draw
    CHECK(count(3) > total * 10 / 100);
    CHECK(count(3) + count(4) + count(5) == total);
}

TEST_CASE("spawner2d: an alternative that is not named is never used") {
    Setup setup;
    spawner2d::Generator generator = rows_of(10.0F);
    generator.alt_chance = 1.0F; // alt_prefab stays "none"
    add(setup, generator);
    setup.app.run_frames(1);
    CHECK(std::all_of(setup.seen->begin(), setup.seen->end(),
                      [](const SpawnRequest& request) { return request.prefab == 3U; }));
}

TEST_CASE(
    "spawner2d: rows below from_distance are skipped, and so are rows that miss their chance") {
    Setup setup;
    spawner2d::Generator generator = rows_of(100.0F);
    generator.from_distance = 250.0F; // rows at 700, 600, 500 are skipped
    add(setup, generator);
    setup.app.run_frames(1);
    REQUIRE(!setup.seen->empty());
    CHECK(setup.seen->front().y == 400.0F);

    Setup never;
    spawner2d::Generator silent = rows_of(100.0F);
    silent.row_chance = 0.0F;
    add(never, silent);
    never.app.run_frames(1);
    CHECK(never.seen->empty());
    never.move_camera_to(60.0F);
    never.app.run_frames(1);
    CHECK(never.seen->empty()); // and it did not pile rows up for later
}

TEST_CASE("spawner2d: a row can bring an extra prefab next to it") {
    Setup setup;
    spawner2d::Generator generator = rows_of(100.0F);
    generator.extra_prefab = 7;
    generator.extra_chance = 1.0F;
    generator.extra_across = 10.0F;
    generator.extra_along = 14.0F; // ahead of a row that goes up: y is smaller
    add(setup, generator);
    setup.app.run_frames(1);
    REQUIRE(setup.seen->size() == 18); // a row and its extra, nine times
    for (std::size_t i = 0; i < setup.seen->size(); i += 2) {
        const SpawnRequest& row = (*setup.seen)[i];
        const SpawnRequest& extra = (*setup.seen)[i + 1];
        CHECK(row.prefab == 3U);
        CHECK(extra.prefab == 7U);
        CHECK(extra.y == row.y - 14.0F);
        CHECK(std::abs(extra.x - row.x) <= 10.0F);
    }
}

TEST_CASE("spawner2d: an extra can be kept off the alternative prefabs") {
    Setup setup;
    spawner2d::Generator generator = rows_of(10.0F);
    generator.alt_prefab = 4;
    generator.alt_chance = 0.5F;
    generator.extra_prefab = 7;
    generator.extra_chance = 1.0F;
    generator.extra_on_alt = false;
    add(setup, generator);
    setup.move_camera_to(-1000.0F);
    setup.app.run_frames(4);
    std::size_t rows_of_4 = 0;
    std::size_t rows_of_3 = 0;
    std::size_t extras = 0;
    for (const SpawnRequest& request : *setup.seen) {
        rows_of_4 += request.prefab == 4U ? 1U : 0U;
        rows_of_3 += request.prefab == 3U ? 1U : 0U;
        extras += request.prefab == 7U ? 1U : 0U;
    }
    CHECK(rows_of_4 > 0);
    CHECK(extras == rows_of_3); // one extra per usual row, none for the others
}

TEST_CASE("spawner2d: the same seed builds the same world, another seed another one") {
    Setup first(5);
    Setup second(5);
    Setup other(6);
    for (Setup* setup : {&first, &second, &other}) {
        add(*setup, rows_of(100.0F));
        setup->app.run_frames(1);
    }
    REQUIRE(first.seen->size() == second.seen->size());
    bool same = true;
    bool different = false;
    for (std::size_t i = 0; i < first.seen->size(); ++i) {
        same = same && (*first.seen)[i].x == (*second.seen)[i].x;
        different = different || (*first.seen)[i].x != (*other.seen)[i].x;
    }
    CHECK(same);
    CHECK(different);
}

TEST_CASE("spawner2d: nothing is built without a camera") {
    Setup setup;
    add(setup, rows_of(100.0F));
    CHECK(setup.app.world().despawn(setup.camera));
    setup.app.run_frames(2);
    CHECK(setup.seen->empty());
}

TEST_CASE("spawner2d: a camera that jumped far does not freeze the game") {
    Setup setup;
    add(setup, rows_of(1.0F)); // a row per unit
    setup.move_camera_to(-1.0e6F);
    setup.app.run_frames(1);
    CHECK(setup.seen->size() <= 64U);
    setup.app.run_frames(1);
    CHECK(setup.seen->size() <= 128U); // it catches up over the next ticks
}

TEST_CASE("spawner2d: what fell below the screen is removed, what is above stays") {
    Setup setup;
    const auto spawn_cull = [&setup](float y, float margin) {
        const Entity entity = setup.app.world().spawn();
        CHECK(setup.app.world().insert(entity, transform2d::Position{0.0F, y}));
        CHECK(setup.app.world().insert(entity, spawner2d::Cull{margin}));
        return entity;
    };
    const Entity inside = spawn_cull(700.0F, 100.0F);
    const Entity in_margin = spawn_cull(800.0F, 100.0F); // 720 + 100 = 820
    const Entity gone = spawn_cull(830.0F, 100.0F);
    const Entity above = spawn_cull(-5000.0F, 100.0F);
    setup.app.run_frames(1);
    CHECK(setup.app.world().is_alive(inside));
    CHECK(setup.app.world().is_alive(in_margin));
    CHECK(!setup.app.world().is_alive(gone));
    CHECK(setup.app.world().is_alive(above));
    setup.move_camera_to(60.0F); // the screen climbs: 700 is now below its bottom edge
    setup.app.run_frames(1);
    CHECK(!setup.app.world().is_alive(inside));
}

TEST_CASE("spawner2d: a generator that goes right lays rows ahead of the right edge") {
    Setup setup; // the screen shows x 0 to 480; rows reach 200 beyond it
    spawner2d::Generator generator = rows_of(100.0F);
    generator.toward_x = 1.0F;
    generator.toward_y = 0.0F;
    generator.across_min = 50.0F; // across a horizontal layout is y
    generator.across_max = 250.0F;
    generator.next = 400.0F;
    generator.origin = 400.0F;
    add(setup, generator);
    setup.app.run_frames(1);
    REQUIRE(setup.seen->size() == 3); // x 400, 500 and 600; 700 is beyond 480 + 200
    float x = 400.0F;
    for (const SpawnRequest& row : *setup.seen) {
        CHECK(row.prefab == 3U);
        CHECK(row.x == x);
        CHECK(row.y >= 50.0F);
        CHECK(row.y <= 250.0F);
        x += 100.0F;
    }
    setup.app.world().get<transform2d::Position>(setup.camera)->x = 440.0F; // the screen runs on
    setup.app.run_frames(1);
    REQUIRE(setup.seen->size() == 5); // x 700 and 800 as well: the limit is now 440 + 240 + 200
    CHECK(setup.seen->back().x == 800.0F);
}

TEST_CASE("spawner2d: a generator that goes left lays its rows towards smaller x") {
    Setup left;
    spawner2d::Generator toward_left = rows_of(100.0F);
    toward_left.toward_x = -1.0F;
    toward_left.toward_y = 0.0F;
    toward_left.next = 100.0F;
    toward_left.origin = 100.0F;
    add(left, toward_left);
    left.app.run_frames(1);
    REQUIRE(left.seen->size() == 3); // x 100, 0 and -100: the left edge is at 0, and 200 ahead
    CHECK(left.seen->front().x == 100.0F);
    CHECK(left.seen->back().x == -100.0F);
}

TEST_CASE("spawner2d: a generator that goes down lays its rows towards bigger y") {
    Setup down;
    spawner2d::Generator toward_down = rows_of(100.0F);
    toward_down.toward_x = 0.0F;
    toward_down.toward_y = 1.0F;
    toward_down.next = 700.0F;
    toward_down.origin = 700.0F;
    add(down, toward_down);
    down.app.run_frames(1);
    REQUIRE(down.seen->size() == 3); // y 700, 800 and 900: the bottom edge is at 720, 200 ahead
    CHECK(down.seen->back().y == 900.0F);
}

TEST_CASE("spawner2d: the difficulty ramp counts the distance along the direction") {
    Setup setup;
    spawner2d::Generator generator = rows_of(100.0F);
    generator.toward_x = 1.0F;
    generator.toward_y = 0.0F;
    generator.next = 400.0F;
    generator.origin = 400.0F;
    generator.from_distance = 250.0F; // rows at 400, 500 and 600 are less than that from the start
    add(setup, generator);
    setup.app.run_frames(1);
    CHECK(setup.seen->empty());
    setup.app.world().get<transform2d::Position>(setup.camera)->x = 640.0F;
    setup.app.run_frames(1);
    REQUIRE(!setup.seen->empty());
    CHECK(setup.seen->front().x >= 650.0F); // the first row built is 250 or more past the origin
}

TEST_CASE("spawner2d: Cull destroys what is past the side of the screen it names") {
    Setup setup; // the screen shows x 0 to 480
    const auto spawn_cull = [&](float x, spawner2d::Cull cull) {
        const Entity entity = setup.app.world().spawn();
        CHECK(setup.app.world().insert(entity, transform2d::Position{x, 300.0F}));
        CHECK(setup.app.world().insert(entity, cull));
        return entity;
    };
    spawner2d::Cull behind; // a runner leaves things on its left
    behind.side_x = -1.0F;
    behind.side_y = 0.0F;
    behind.margin = 100.0F;
    const Entity near_left = spawn_cull(-50.0F, behind);
    const Entity far_left = spawn_cull(-150.0F, behind);
    const Entity right = spawn_cull(900.0F, behind);
    setup.app.run_frames(1);
    CHECK(setup.app.world().is_alive(near_left));
    CHECK(!setup.app.world().is_alive(far_left));
    CHECK(setup.app.world().is_alive(right));
}

TEST_CASE("spawner2d: a shooter fires when its action is pressed, with a cooldown") {
    Setup setup;
    spawner2d::Shooter gun;
    gun.prefab = 2;
    gun.action = 0; // "shoot"
    gun.cooldown = 3;
    gun.speed = 500.0F;
    gun.dir_x = 0.0F;
    gun.dir_y = -1.0F;
    gun.dx = 4.0F;
    gun.dy = -20.0F;
    const Entity hero = setup.app.world().spawn();
    CHECK(setup.app.world().insert(hero, transform2d::Position{100.0F, 300.0F}));
    CHECK(setup.app.world().insert(hero, gun));
    auto* actions = setup.app.world().resource<ge::app::Actions>();

    setup.app.run_frames(2);
    CHECK(setup.seen->empty()); // nothing pressed

    actions->raise("shoot"); // pressed during the next tick
    setup.app.run_frames(1);
    REQUIRE(setup.seen->size() == 1);
    check_shot(setup.seen->front());

    // pressed again at once: the cooldown (3 ticks) refuses it
    actions->raise("shoot");
    setup.app.run_frames(1);
    CHECK(setup.seen->size() == 1);
    setup.app.run_frames(2);
    actions->raise("shoot");
    setup.app.run_frames(1);
    CHECK(setup.seen->size() == 2);
}
