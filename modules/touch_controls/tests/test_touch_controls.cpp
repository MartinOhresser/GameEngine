#include "ge/app/app.hpp"
#include "ge/app/host.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/sprite2d/sprite2d.gen.hpp"
#include "ge/mod/touch_controls/touch_controls.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include "mini_test.hpp"

#include <string>
#include <utility>

namespace {

using ge::mod::touch_controls::TouchButton;
using ge::mod::touch_controls::TouchKnob;
using ge::mod::touch_controls::TouchStick;
using ge::mod::transform2d::Position;

// The game's actions, in the order of its list: fire is 0, move_x 1, move_y 2, jump 3.
constexpr std::uint32_t fire = 0;
constexpr std::uint32_t move_x = 1;
constexpr std::uint32_t move_y = 2;
constexpr std::uint32_t jump = 3;

template <typename T>
void add(ge::ecs::World& world, ge::ecs::Entity entity, T&& component) {
    CHECK(world.insert(entity, std::forward<T>(component)));
}

struct Rig {
    ge::app::App app;
    ge::ecs::Entity fire_button;
    ge::ecs::Entity jump_button;
    ge::ecs::Entity stick;
    ge::ecs::Entity knob;

    Rig() {
        ge::mod::transform2d::install(app);
        ge::mod::sprite2d::install(app);
        ge::mod::touch_controls::install(app);
        app.world().insert_resource(ge::app::ActionNames{{"fire", "move_x", "move_y", "jump"}});
        ge::ecs::World& world = app.world();
        fire_button = world.spawn();
        add(world, fire_button, Position{600.0F, 500.0F});
        add(world, fire_button, TouchButton{fire, 80.0F, 80.0F, 1, 2, 0, false, 255});
        add(world, fire_button, ge::mod::sprite2d::Sprite{});
        jump_button = world.spawn();
        add(world, jump_button, Position{700.0F, 400.0F});
        add(world, jump_button, TouchButton{jump, 80.0F, 80.0F, 0, 0, 0, false, 255});
        stick = world.spawn();
        add(world, stick, Position{100.0F, 500.0F});
        TouchStick pad;
        pad.action_x = move_x;
        pad.action_y = move_y;
        add(world, stick, pad);
        knob = world.spawn();
        add(world, knob, Position{0.0F, 0.0F});
        add(world, knob, TouchKnob{});
    }

    /// Puts a finger (1 is the first) at a place, or lifts it, then lets a frame go by.
    void finger(int number, bool down, float x = 0.0F, float y = 0.0F) {
        const std::string suffix = number == 1 ? "" : std::to_string(number);
        auto* live = app.world().resource<ge::app::LiveInput>();
        live->set("touch_down" + suffix, down ? 1.0F : 0.0F);
        live->set("touch_x" + suffix, x);
        live->set("touch_y" + suffix, y);
    }

    void frame() { app.run_frames(1); }

    [[nodiscard]] float value(const std::string& action) {
        return app.world().resource<ge::app::Actions>()->value(action);
    }
};

} // namespace

TEST_CASE("touch_controls: a button is pressed while a finger is on it, and shows it") {
    Rig rig;
    rig.frame();
    CHECK(rig.value("fire") == 0.0F);
    CHECK(rig.app.world().get<ge::mod::sprite2d::Sprite>(rig.fire_button)->frame == 1U);

    rig.finger(1, true, 620.0F, 520.0F);
    rig.frame();
    CHECK(rig.value("fire") == 1.0F);
    CHECK(rig.app.world().resource<ge::app::Actions>()->pressed("fire"));
    CHECK(rig.app.world().get<ge::mod::sprite2d::Sprite>(rig.fire_button)->frame == 2U);
    CHECK(rig.value("jump") == 0.0F);

    rig.frame();
    CHECK(rig.value("fire") == 1.0F);
    CHECK(!rig.app.world().resource<ge::app::Actions>()->pressed("fire"));

    rig.finger(1, false);
    rig.frame();
    CHECK(rig.value("fire") == 0.0F);
    CHECK(rig.app.world().resource<ge::app::Actions>()->released("fire"));
    CHECK(rig.app.world().get<ge::mod::sprite2d::Sprite>(rig.fire_button)->frame == 1U);
}

TEST_CASE("touch_controls: a finger beside the button does nothing") {
    Rig rig;
    rig.finger(1, true, 700.0F, 100.0F);
    rig.frame();
    CHECK(rig.value("fire") == 0.0F);
    CHECK(rig.value("jump") == 0.0F);
    CHECK(rig.app.world().get<TouchButton>(rig.fire_button)->finger == 0U);
}

TEST_CASE("touch_controls: the stick gives two axes, nothing inside its dead zone") {
    Rig rig;
    rig.finger(1, true, 130.0F, 500.0F); // 30 px right of the centre, radius 60
    rig.frame();
    CHECK(rig.value("move_x") == 0.5F);
    CHECK(rig.value("move_y") == 0.0F);
    const Position* knob = rig.app.world().get<Position>(rig.knob);
    CHECK(knob->x == 130.0F);
    CHECK(knob->y == 500.0F);

    rig.finger(1, true, 100.0F, 560.0F); // straight down by the radius
    rig.frame();
    CHECK(rig.value("move_x") == 0.0F);
    CHECK(rig.value("move_y") == 1.0F);

    rig.finger(1, true, 240.0F, 500.0F); // far away: the knob stays on the circle
    rig.frame();
    CHECK(rig.value("move_x") == 1.0F);
    CHECK(rig.app.world().get<Position>(rig.knob)->x == 160.0F);

    rig.finger(1, true, 104.0F, 502.0F); // inside the dead zone
    rig.frame();
    CHECK(rig.value("move_x") == 0.0F);
    CHECK(rig.value("move_y") == 0.0F);

    rig.finger(1, false);
    rig.frame();
    CHECK(rig.value("move_x") == 0.0F);
    CHECK(rig.app.world().get<Position>(rig.knob)->x == 100.0F);
}

TEST_CASE("touch_controls: a stick and a button work at the same time, with two fingers") {
    Rig rig;
    rig.finger(1, true, 160.0F, 500.0F);
    rig.finger(2, true, 600.0F, 500.0F);
    rig.frame();
    CHECK(rig.value("move_x") == 1.0F);
    CHECK(rig.value("fire") == 1.0F);

    // The first finger lifts: the stick lets go, the button is still held by the second.
    rig.finger(1, false);
    rig.frame();
    CHECK(rig.value("move_x") == 0.0F);
    CHECK(rig.value("fire") == 1.0F);
}

TEST_CASE("touch_controls: a finger keeps its control, and a held one is not taken by another") {
    Rig rig;
    rig.finger(1, true, 600.0F, 500.0F);
    rig.frame();
    // It slides onto the jump button: fire stays held, jump is not pressed.
    rig.finger(1, true, 700.0F, 400.0F);
    rig.frame();
    CHECK(rig.value("fire") == 1.0F);
    CHECK(rig.value("jump") == 0.0F);

    // A finger that slides onto a free control takes it.
    rig.finger(2, true, 10.0F, 10.0F);
    rig.frame();
    rig.finger(2, true, 700.0F, 400.0F);
    rig.frame();
    CHECK(rig.value("jump") == 1.0F);
}

TEST_CASE("touch_controls: a key bound to the same action is not cancelled by idle controls") {
    Rig rig;
    rig.app.world().resource<ge::app::LiveInput>()->set("fire", 1.0F); // the keyboard
    rig.frame();
    rig.frame();
    CHECK(rig.value("fire") == 1.0F);
}

TEST_CASE("touch_controls: scenarios can script the fingers as actions") {
    Rig rig;
    auto* script = rig.app.world().resource<ge::app::ScriptedInput>();
    REQUIRE(script != nullptr);
    script->add(1, "touch_down", 1.0F);
    script->add(1, "touch_x", 620.0F);
    script->add(1, "touch_y", 500.0F);
    rig.app.run_frames(4);
    CHECK(rig.value("fire") == 1.0F);
}

TEST_CASE("touch_controls: they appear with the first finger on a computer, at once on a phone") {
    Rig computer;
    auto& sprite_world = computer.app.world();
    add(sprite_world, computer.stick, ge::mod::sprite2d::Sprite{});
    computer.frame();
    CHECK(sprite_world.get<ge::mod::sprite2d::Sprite>(computer.fire_button)->alpha == 0U);
    CHECK(sprite_world.get<ge::mod::sprite2d::Sprite>(computer.stick)->alpha == 0U);
    computer.finger(1, true, 10.0F, 10.0F);
    computer.frame();
    CHECK(sprite_world.get<ge::mod::sprite2d::Sprite>(computer.fire_button)->alpha == 255U);
    CHECK(sprite_world.get<ge::mod::sprite2d::Sprite>(computer.stick)->alpha == 255U);

    Rig phone;
    phone.app.world().resource<ge::app::HostInfo>()->mobile = true;
    phone.frame();
    CHECK(phone.app.world().get<ge::mod::sprite2d::Sprite>(phone.fire_button)->alpha == 255U);
}
