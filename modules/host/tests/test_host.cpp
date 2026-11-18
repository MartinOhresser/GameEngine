#include "ge/app/app.hpp"
#include "ge/app/host.hpp"
#include "ge/app/input.hpp"
#include "ge/app/scene.hpp"
#include "ge/mod/host/host.gen.hpp"

#include "mini_test.hpp"

#include <cstdint>

namespace {

namespace host = ge::mod::host;

constexpr std::uint32_t quit_action = 0U;
constexpr std::uint32_t play_action = 1U;

ge::app::App game() {
    ge::app::App app;
    host::install(app);
    CHECK(app.module_problems().empty());
    app.world().insert_resource(ge::app::ActionNames{{"quit_game", "play_game"}});
    return app;
}

void press(ge::app::App& app, const char* action) {
    app.world().resource<ge::app::Actions>()->raise(action);
    app.run_frames(2); // pressed during the tick after the request
}

bool quit_requested(ge::app::App& app) {
    return app.world().resource<ge::app::QuitRequest>()->requested;
}

} // namespace

TEST_CASE("host: a pressed action asks the host to stop") {
    ge::app::App app = game();
    const ge::ecs::Entity button = app.world().spawn();
    CHECK(app.world().insert(button, host::Quit{quit_action, false}));
    app.run_frames(3);
    CHECK(!quit_requested(app)); // nothing pressed
    press(app, "play_game");
    CHECK(!quit_requested(app)); // another action
    press(app, "quit_game");
    CHECK(quit_requested(app));
}

TEST_CASE("host: a Quit without an action does nothing, and a mobile host ignores it") {
    ge::app::App none = game();
    CHECK(none.world().insert(none.world().spawn(), host::Quit{}));
    press(none, "quit_game");
    CHECK(!quit_requested(none));

    ge::app::App mobile = game();
    mobile.world().resource<ge::app::HostInfo>()->mobile = true;
    CHECK(mobile.world().insert(mobile.world().spawn(), host::Quit{quit_action, false}));
    press(mobile, "quit_game");
    CHECK(!quit_requested(mobile)); // closing is the system's job there

    ge::app::App allowed = game();
    allowed.world().resource<ge::app::HostInfo>()->mobile = true;
    CHECK(allowed.world().insert(allowed.world().spawn(), host::Quit{quit_action, true}));
    press(allowed, "quit_game");
    CHECK(quit_requested(allowed));
}

TEST_CASE("host: a Call leaves its command and number for the host, once per press") {
    ge::app::App app = game();
    CHECK(app.world().insert(app.world().spawn(), host::Call{3U, play_action, 7U}));
    CHECK(app.world().insert(app.world().spawn(), host::Call{4294967295U, play_action, 9U}));
    app.run_frames(3);
    CHECK(app.world().resource<ge::app::HostRequests>()->pending.empty());
    press(app, "play_game");
    const auto& pending = app.world().resource<ge::app::HostRequests>()->pending;
    REQUIRE(pending.size() == 1U); // the Call without a command is ignored
    CHECK(pending[0].command == 3U);
    CHECK(pending[0].argument == 7U);
    app.run_frames(5);
    CHECK(app.world().resource<ge::app::HostRequests>()->pending.size() == 1U); // not repeated
}

TEST_CASE("host: without the resources of an app the system does nothing") {
    ge::app::App app = game();
    CHECK(app.world().insert(app.world().spawn(), host::Quit{quit_action, false}));
    app.world().remove_resource<ge::app::ActionNames>();
    app.run_frames(3);
    CHECK(!quit_requested(app));
}
