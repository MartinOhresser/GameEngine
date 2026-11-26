#include "ge/app/app.hpp"
#include "ge/mod/collision2d/collision2d.gen.hpp"
#include "ge/app/render.hpp"
#include "ge/mod/gravity2d/gravity2d.gen.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/terrain2d/cells.hpp"
#include "ge/mod/terrain2d/terrain2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <cmath>
#include <cstdint>
#include <memory>

namespace {

namespace collision2d = ge::mod::collision2d;
namespace gravity2d = ge::mod::gravity2d;
namespace sprite2d = ge::mod::sprite2d;
namespace terrain2d = ge::mod::terrain2d;
namespace transform2d = ge::mod::transform2d;

ge::app::App game() {
    ge::app::App app;
    transform2d::install(app);
    collision2d::install(app);
    gravity2d::install(app);
    sprite2d::install(app);
    terrain2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    return app;
}

/// A flat ground of 8-unit cells, 64 by 32, its top-left at (0, 0), solid from row 16 (y 128).
ge::ecs::Entity ground(ge::app::App& app, terrain2d::Terrain terrain = {}) {
    terrain.columns = 64U;
    terrain.rows = 32U;
    terrain.cell = 8.0F;
    terrain.surface = 16.0F;
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{0.0F, 0.0F}));
    CHECK(app.world().insert(entity, terrain));
    return entity;
}

/// A 10 by 20 body at (x, y), pulled down.
ge::ecs::Entity body(ge::app::App& app, float x, float y) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, transform2d::Velocity{}));
    CHECK(app.world().insert(entity, collision2d::Collider{10.0F, 20.0F, 2U, 0xFFFFFFFFU}));
    CHECK(app.world().insert(entity, collision2d::Body{}));
    CHECK(app.world().insert(entity, collision2d::Blocked{}));
    CHECK(app.world().insert(entity, gravity2d::Gravity{0.0F, 1000.0F, 0.0F}));
    return entity;
}

ge::ecs::Entity shape(ge::app::App& app, float x, float y, const terrain2d::Shape& made) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, made));
    return entity;
}

bool near(float a, float b) { return std::abs(a - b) < 0.05F; }

} // namespace

TEST_CASE("terrain2d: a flat ground is solid below its surface, and a body lands on it") {
    ge::app::App app = game();
    const ge::ecs::Entity land = ground(app);
    const ge::ecs::Entity hero = body(app, 100.0F, 40.0F);
    app.run_frames(1);
    // 64 columns by the 16 rows under the surface.
    CHECK(app.world().get<terrain2d::Terrain>(land)->solid_cells == 64U * 16U);
    CHECK(app.world().get<terrain2d::Terrain>(land)->generated);
    app.run_frames(60);
    // The ground's top is y 128, the body is 20 tall: its middle rests at 118.
    CHECK(near(app.world().get<transform2d::Position>(hero)->y, 118.0F));
    CHECK(app.world().get<collision2d::Blocked>(hero)->below);
    CHECK(app.world().get<transform2d::Velocity>(hero)->y == 0.0F);
}

TEST_CASE("terrain2d: a wall stops a walking body, a low step is climbed") {
    ge::app::App app = game();
    ground(app);
    // A wall 40 high at x 200..216, a step 8 high at x 300..400.
    shape(app, 208.0F, 108.0F, terrain2d::Shape{16.0F, 40.0F, 0.0F, true});
    const ge::ecs::Entity hero = body(app, 150.0F, 118.0F);
    for (int tick = 0; tick < 60; tick += 1) {
        app.world().get<transform2d::Velocity>(hero)->x = 120.0F;
        app.run_frames(1);
    }
    // Stopped against the wall's left side (x 200), its own half width (5) away.
    CHECK(near(app.world().get<transform2d::Position>(hero)->x, 195.0F));
    CHECK(app.world().get<collision2d::Blocked>(hero)->right);

    ge::app::App flat = game();
    ground(flat);
    shape(flat, 350.0F, 124.0F, terrain2d::Shape{100.0F, 8.0F, 0.0F, true});
    const ge::ecs::Entity walker = body(flat, 280.0F, 118.0F);
    for (int tick = 0; tick < 60; tick += 1) {
        flat.world().get<transform2d::Velocity>(walker)->x = 120.0F;
        flat.run_frames(1);
    }
    CHECK(flat.world().get<transform2d::Position>(walker)->x > 330.0F);
    CHECK(near(flat.world().get<transform2d::Position>(walker)->y, 110.0F));
}

TEST_CASE("terrain2d: a digger carves a hole and the body falls into it") {
    ge::app::App app = game();
    const ge::ecs::Entity land = ground(app);
    const ge::ecs::Entity hero = body(app, 100.0F, 118.0F);
    app.run_frames(5);
    const ge::ecs::Entity drill = app.world().spawn();
    CHECK(app.world().insert(drill, transform2d::Position{100.0F, 140.0F}));
    CHECK(app.world().insert(drill, terrain2d::Digger{24.0F}));
    CHECK(app.world().insert(drill, transform2d::Disabled{}));
    app.run_frames(1);
    CHECK(app.world().get<terrain2d::Digger>(drill)->removed == 0U);
    CHECK(app.world().remove<transform2d::Disabled>(drill));
    app.run_frames(1);
    const std::uint32_t carved = app.world().get<terrain2d::Digger>(drill)->removed;
    CHECK(carved > 10U);
    CHECK(app.world().get<terrain2d::Digger>(drill)->last == carved);
    CHECK(app.world().get<terrain2d::Terrain>(land)->solid_cells == 64U * 16U - carved);
    app.run_frames(30);
    CHECK(app.world().get<terrain2d::Digger>(drill)->last == 0U);
    CHECK(app.world().get<transform2d::Position>(hero)->y > 130.0F);
}

TEST_CASE("terrain2d: a round terrain is a disc, and a body stands on its top") {
    ge::app::App app = game();
    terrain2d::Terrain rock;
    rock.round = true;
    const ge::ecs::Entity land = app.world().spawn();
    rock.columns = 40U;
    rock.rows = 40U;
    rock.cell = 8.0F;
    rock.surface = 4.0F; // a disc of radius 16 cells: its top at row 4, y 32
    CHECK(app.world().insert(land, transform2d::Position{0.0F, 0.0F}));
    CHECK(app.world().insert(land, rock));
    const ge::ecs::Entity hero = body(app, 160.0F, 0.0F);
    app.run_frames(60);
    const std::uint32_t solid = app.world().get<terrain2d::Terrain>(land)->solid_cells;
    // About pi × 16², give or take the cells along the edge.
    CHECK(solid > 760U);
    CHECK(solid < 850U);
    CHECK(near(app.world().get<transform2d::Position>(hero)->y, 22.0F));
}

TEST_CASE("terrain2d: a carved shape opens a gap; a removed terrain is forgotten") {
    ge::app::App app = game();
    const ge::ecs::Entity land = ground(app);
    const ge::ecs::Entity gap = shape(app, 100.0F, 160.0F, terrain2d::Shape{32.0F, 64.0F, 0.0F, false});
    const ge::ecs::Entity hero = body(app, 100.0F, 100.0F);
    app.run_frames(40);
    CHECK(app.world().get<terrain2d::Shape>(gap)->applied);
    CHECK(app.world().get<transform2d::Position>(hero)->y > 150.0F);
    CHECK(app.world().resource<terrain2d::Cells>()->grids.size() == 1U);
    app.world().despawn(land);
    app.run_frames(1);
    CHECK(app.world().resource<terrain2d::Cells>()->grids.empty());
}

TEST_CASE("terrain2d: a Look terrain is drawn as pictures, remade only where it is dug") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game();
    app.world().insert_resource(ge::app::RenderBackend{renderer});
    terrain2d::Terrain terrain;
    terrain.columns = 128U; // two parts across, one down
    terrain.rows = 40U;
    terrain.surface = 20.0F;
    const ge::ecs::Entity land = app.world().spawn();
    CHECK(app.world().insert(land, transform2d::Position{0.0F, 0.0F}));
    CHECK(app.world().insert(land, terrain));
    CHECK(app.world().insert(land, terrain2d::Look{}));
    app.run_frames(1);
    CHECK(renderer->images().size() == 2U);
    const std::size_t made = renderer->image_updates();
    const auto& picture = renderer->images().begin()->second;
    // 64 cells of 4 pixels, and a pixel more on each side.
    CHECK(picture.width == 258);
    // Above the surface the picture is clear; deep under it, it is the ground's own colour.
    CHECK((picture.pixels[static_cast<std::size_t>(10 * 258 + 10)] & 0xFFU) == 0U);
    CHECK(picture.pixels[static_cast<std::size_t>(150 * 258 + 10)] == 0x0E0E10FFU ||
          (picture.pixels[static_cast<std::size_t>(150 * 258 + 10)] & 0xFFU) == 0xFFU);
    std::size_t sprites = 0;
    for (const auto& command : renderer->commands()) {
        sprites += command.atlas >= ge::app::Renderer::first_made_image ? 1U : 0U;
    }
    CHECK(sprites == 2U);
    app.run_frames(1);
    CHECK(renderer->image_updates() == made); // nothing changed: nothing remade
    const ge::ecs::Entity drill = app.world().spawn();
    CHECK(app.world().insert(drill, transform2d::Position{40.0F, 170.0F}));
    CHECK(app.world().insert(drill, terrain2d::Digger{12.0F}));
    app.run_frames(1);
    CHECK(renderer->image_updates() == made + 1U); // only the part that was dug
    app.world().despawn(land);
    app.run_frames(2);
    CHECK(renderer->images().empty());
}

TEST_CASE("terrain2d: a smoothed Look draws a step of one cell as a slope") {
    // A ground solid from row 10 on its left half, from row 11 on its right half: a step of one
    // cell at column 32. Drawn as it is, the cell just after the step, in row 10, is open; drawn
    // smoothed over 3 cells, the step becomes a slope that reaches into it.
    const auto open_after_step = [](std::uint32_t smooth) {
        auto renderer = std::make_shared<ge::app::RecordingRenderer>();
        ge::app::App app = game();
        app.world().insert_resource(ge::app::RenderBackend{renderer});
        terrain2d::Terrain terrain;
        terrain.columns = 64U;
        terrain.rows = 32U;
        terrain.surface = 10.0F;
        const ge::ecs::Entity land = app.world().spawn();
        CHECK(app.world().insert(land, transform2d::Position{0.0F, 0.0F}));
        CHECK(app.world().insert(land, terrain));
        terrain2d::Look look;
        look.smooth = smooth;
        CHECK(app.world().insert(land, look));
        terrain2d::Shape step;
        step.width = 32.0F * 8.0F;
        step.height = 8.0F;
        step.fill = false;
        shape(app, 48.0F * 8.0F, 10.5F * 8.0F, step);
        app.run_frames(1);
        CHECK(renderer->images().size() == 1U);
        const auto& picture = renderer->images().begin()->second;
        // The middle of cell (33, 10): 4 pixels a cell, a pixel more on the left and the top.
        const std::size_t at = static_cast<std::size_t>((10 * 4 + 2 + 1) * picture.width +
                                                        33 * 4 + 2 + 1);
        return picture.pixels[at] & 0xFFU;
    };
    CHECK(open_after_step(0U) == 0U);
    CHECK(open_after_step(3U) > 0U);
}

TEST_CASE("terrain2d: the cells of a hard shape cannot be dug") {
    ge::app::App app = game();
    const ge::ecs::Entity land = ground(app);
    terrain2d::Shape bedrock{64.0F, 64.0F, 0.0F, true};
    bedrock.hard = true;
    shape(app, 100.0F, 160.0F, bedrock);
    app.run_frames(1);
    const ge::ecs::Entity drill = app.world().spawn();
    CHECK(app.world().insert(drill, transform2d::Position{100.0F, 140.0F}));
    CHECK(app.world().insert(drill, terrain2d::Digger{24.0F}));
    app.run_frames(1);
    CHECK(app.world().get<terrain2d::Digger>(drill)->removed == 0U);
    // Beside it, the ground is dug as before.
    app.world().get<transform2d::Position>(drill)->x = 300.0F;
    app.run_frames(1);
    CHECK(app.world().get<terrain2d::Digger>(drill)->removed > 10U);
    const terrain2d::Grid& grid = app.world().resource<terrain2d::Cells>()->grids.begin()->second;
    CHECK(grid.at(grid.column_of(100.0F), grid.row_of(140.0F)));
    static_cast<void>(land);
}
