#include "ge/app/app.hpp"
#include "ge/app/render.hpp"
#include "ge/mod/particles2d/particles2d.gen.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

namespace particles2d = ge::mod::particles2d;
namespace sprite2d = ge::mod::sprite2d;
namespace transform2d = ge::mod::transform2d;

ge::app::App game(const std::shared_ptr<ge::app::RecordingRenderer>& renderer) {
    ge::app::App app;
    transform2d::install(app);
    sprite2d::install(app);
    particles2d::install(app);
    CHECK(app.module_problems().empty());
    CHECK(app.schedule().problems().empty());
    app.world().insert_resource(ge::app::RenderBackend{renderer});
    app.set_tick_rate(10); // a tick lasts 0.1 s
    return app;
}

ge::ecs::Entity spawn_emitter(ge::app::App& app, float x, float y,
                              const particles2d::Emitter& emitter) {
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{x, y}));
    CHECK(app.world().insert(entity, emitter));
    return entity;
}

/// The particles drawn in the last frame.
std::vector<ge::app::DrawCommand> drawn(ge::app::App& app,
                                        const std::shared_ptr<ge::app::RecordingRenderer>& r) {
    r->clear();
    app.run_frames(1);
    return r->commands();
}

} // namespace

TEST_CASE("particles2d: an emitter lets out its rate of particles a second") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    particles2d::Emitter steady;
    steady.rate = 20.0F; // 2 a tick
    steady.lifetime = 100.0F;
    steady.lifetime_spread = 0.0F;
    spawn_emitter(app, 0.0F, 0.0F, steady);
    app.run_frames(4);
    CHECK(drawn(app, renderer).size() == 10U);
}

TEST_CASE("particles2d: a burst comes out at once, and particles end with their lives") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    particles2d::Emitter blast;
    blast.rate = 0.0F;
    blast.burst = 12;
    blast.lifetime = 0.45F;
    blast.lifetime_spread = 0.0F;
    spawn_emitter(app, 0.0F, 0.0F, blast);
    CHECK(drawn(app, renderer).size() == 12U);
    app.run_frames(3);
    CHECK(drawn(app, renderer).size() == 12U); // 0.4 s old
    CHECK(drawn(app, renderer).empty());       // 0.5 s: gone, and no second burst
}

TEST_CASE("particles2d: particles fly from the emitter and fall with their gravity") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    particles2d::Emitter fountain;
    fountain.rate = 0.0F;
    fountain.burst = 1;
    fountain.speed = 100.0F;
    fountain.speed_spread = 0.0F;
    fountain.angle = 0.0F; // to the right
    fountain.spread = 0.0F;
    fountain.gravity_y = 200.0F;
    fountain.lifetime_spread = 0.0F;
    spawn_emitter(app, 50.0F, 50.0F, fountain);
    app.run_frames(2); // let out, then one tick of flight
    const std::vector<ge::app::DrawCommand> later = drawn(app, renderer);
    REQUIRE(later.size() == 1U);
    CHECK(later[0].x > 65.0F); // 2 ticks of 10 to the right
    CHECK(later[0].y > 50.0F); // and falling
}

TEST_CASE("particles2d: the same emitter gives the same particles on every run") {
    std::vector<float> first;
    for (int run = 0; run < 2; ++run) {
        auto renderer = std::make_shared<ge::app::RecordingRenderer>();
        ge::app::App app = game(renderer);
        spawn_emitter(app, 0.0F, 0.0F, particles2d::Emitter{});
        app.run_frames(10);
        std::vector<float> xs;
        for (const ge::app::DrawCommand& command : drawn(app, renderer)) {
            xs.push_back(command.x);
        }
        CHECK(!xs.empty());
        if (run == 0) {
            first = xs;
        } else {
            CHECK(xs == first);
        }
    }
}

TEST_CASE("particles2d: particles outlive their emitter, and new ones wait under max") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    particles2d::Emitter crowded;
    crowded.rate = 100.0F;
    crowded.max = 5;
    crowded.lifetime = 0.35F;
    crowded.lifetime_spread = 0.0F;
    const ge::ecs::Entity source = spawn_emitter(app, 0.0F, 0.0F, crowded);
    CHECK(drawn(app, renderer).size() == 5U);
    CHECK(app.world().despawn(source));
    CHECK(drawn(app, renderer).size() == 5U); // still there, none new
    app.run_frames(2);
    CHECK(drawn(app, renderer).empty());
}

namespace {

/// A sprite on `layer`, at the origin.
void spawn_sprite(ge::app::App& app, std::int32_t layer) {
    sprite2d::Sprite sprite;
    sprite.layer = layer;
    const ge::ecs::Entity entity = app.world().spawn();
    CHECK(app.world().insert(entity, transform2d::Position{}));
    CHECK(app.world().insert(entity, sprite));
}

/// An emitter that lets out a single particle when it appears.
particles2d::Emitter single() {
    particles2d::Emitter one;
    one.rate = 0.0F;
    one.burst = 1;
    return one;
}

} // namespace

TEST_CASE("particles2d: particles are drawn on their layer, among the sprites") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    spawn_sprite(app, -5);
    spawn_sprite(app, 5);
    spawn_emitter(app, 0.0F, 0.0F, single()); // layer 0
    const std::vector<ge::app::DrawCommand> commands = drawn(app, renderer);
    REQUIRE(commands.size() == 3U);
    CHECK(commands[0].what == "sprite");
    CHECK(commands[1].what == "glow");
    CHECK(commands[2].what == "sprite");
}

TEST_CASE("particles2d: a particle is added light, lit or not, and can be a light") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    particles2d::Emitter sparks = single();
    sparks.unlit = true;
    sparks.light = true;
    spawn_emitter(app, 0.0F, 0.0F, sparks);
    const std::vector<ge::app::DrawCommand> commands = drawn(app, renderer);
    REQUIRE(commands.size() == 1U);
    CHECK(commands[0].blend == ge::app::Blend::Add);
    CHECK(!commands[0].lit);
    CHECK(commands[0].light);
}

TEST_CASE("particles2d: with an atlas a particle is a picture of its size") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    particles2d::Emitter leaves;
    leaves.rate = 0.0F;
    leaves.burst = 1;
    leaves.atlas = 2;
    leaves.frame = 3;
    leaves.size = 12.0F;
    leaves.end_size = 12.0F;
    leaves.additive = false;
    spawn_emitter(app, 0.0F, 0.0F, leaves);
    const std::vector<ge::app::DrawCommand> commands = drawn(app, renderer);
    REQUIRE(commands.size() == 1U);
    CHECK(commands[0].what == "sprite");
    CHECK(commands[0].atlas == 2);
    CHECK(commands[0].frame == 3U);
    CHECK(commands[0].width == 12.0F);
    CHECK(commands[0].blend == ge::app::Blend::Alpha);
}

TEST_CASE("particles2d: pulled particles go to the emitter and end there; swirled ones turn") {
    auto renderer = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App app = game(renderer);
    particles2d::Emitter drain;
    drain.rate = 0.0F;
    drain.burst = 1;
    drain.speed = 0.0F;
    drain.speed_spread = 0.0F;
    drain.lifetime = 100.0F;
    drain.lifetime_spread = 0.0F;
    drain.area_width = 0.0F;
    drain.pull = 400.0F;
    drain.size = 2.0F;
    const ge::ecs::Entity hole = spawn_emitter(app, 0.0F, 0.0F, drain);
    app.run_frames(1); // let out at the emitter
    // Moved away, the emitter draws its particle after it, until the particle reaches it.
    app.world().get<transform2d::Position>(hole)->x = 100.0F;
    app.run_frames(3);
    std::vector<ge::app::DrawCommand> now = drawn(app, renderer);
    REQUIRE(now.size() == 1U);
    CHECK(now[0].x > 5.0F);
    for (int tick = 0; tick < 40; tick += 1) {
        app.run_frames(1);
    }
    CHECK(drawn(app, renderer).empty());

    auto other = std::make_shared<ge::app::RecordingRenderer>();
    ge::app::App spin = game(other);
    particles2d::Emitter vortex = drain;
    vortex.pull = 0.0F;
    vortex.swirl = 200.0F;
    const ge::ecs::Entity middle = spawn_emitter(spin, 0.0F, 0.0F, vortex);
    spin.run_frames(1);
    spin.world().get<transform2d::Position>(middle)->x = 100.0F; // the particle is to its left
    spin.run_frames(3);
    now = drawn(spin, other);
    REQUIRE(now.size() == 1U);
    CHECK(now[0].y > 1.0F); // turned across, not towards the middle
}
