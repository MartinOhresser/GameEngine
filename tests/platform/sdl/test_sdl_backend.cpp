// Tests of the window backend. They use SDL's "dummy" drivers (no window, no
// sound card), so they run anywhere, CI included; keys are pushed into SDL's
// real event queue, so the path from a key to an action is the real one.
#include "ge/game/runtime.hpp"
#include "ge/platform/sdl/assets.hpp"
#include "ge/platform/sdl/sdl_backend.hpp"

#include "mini_test.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef GE_HAS_GEPLAY
#include "ge/platform/sdl/geplay.hpp"

#include "registry.hpp"
#endif

namespace sdl = ge::platform::sdl;
namespace fs = std::filesystem;

namespace {

sdl::WindowOptions headless(bool capture = false) {
    sdl::WindowOptions options;
    options.headless = true;
    options.capture = capture;
    return options;
}

std::unique_ptr<sdl::SdlBackend> open(bool capture = false) {
    std::string error;
    auto backend = sdl::SdlBackend::create(headless(capture), error);
    CHECK(error.empty());
    return backend;
}

fs::path temp_file(const std::string& name) {
    return fs::temp_directory_path() / ("ge_sdl_test_" + name);
}

/// A 100-sample, 8000 Hz, 8-bit mono silence: the smallest valid .wav.
std::string tiny_wav() {
    const std::uint32_t data_size = 100;
    std::string wav = "RIFF";
    const auto le32 = [&wav](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            wav += static_cast<char>((v >> (8 * i)) & 0xFFU);
        }
    };
    const auto le16 = [&wav](std::uint16_t v) {
        wav += static_cast<char>(v & 0xFFU);
        wav += static_cast<char>((v >> 8U) & 0xFFU);
    };
    le32(36 + data_size);
    wav += "WAVEfmt ";
    le32(16);
    le16(1);    // PCM
    le16(1);    // mono
    le32(8000); // sample rate
    le32(8000); // byte rate
    le16(1);    // block align
    le16(8);    // bits per sample
    wav += "data";
    le32(data_size);
    wav += std::string(data_size, static_cast<char>(128));
    return wav;
}

} // namespace

TEST_CASE("sdl: key names become sources of the input file") {
    CHECK(sdl::key_source("Left") == "key:left");
    CHECK(sdl::key_source("Space") == "key:space");
    CHECK(sdl::key_source("A") == "key:a");
    CHECK(sdl::key_source("Left Shift") == "key:left_shift");
    CHECK(sdl::key_source("").empty());
    CHECK(ge::platform::is_valid_source(sdl::key_source("Return")));
}

TEST_CASE("sdl: a headless backend starts, tells time and hands out its devices") {
    auto backend = open();
    REQUIRE(backend != nullptr);
    const auto first = backend->now();
    std::vector<ge::platform::RawEvent> events;
    CHECK(backend->poll(events));
    CHECK(events.empty());
    CHECK(backend->now() >= first);
    CHECK(backend->renderer() != nullptr);
    CHECK(backend->audio() != nullptr);
    CHECK(backend->frames_presented() == 0U);
}

TEST_CASE("sdl: keys and mouse buttons arrive as raw events, repeats do not") {
    auto backend = open();
    REQUIRE(backend != nullptr);
    CHECK(backend->inject_key("Right", true));
    CHECK(backend->inject_key("Right", false));
    CHECK(backend->inject_key("Left Shift", true));
    CHECK(backend->inject_mouse_button("left", true));
    CHECK(backend->inject_mouse_button("middle", true));
    CHECK(backend->inject_mouse_button("right", false));
    CHECK(!backend->inject_key("NoSuchKey", true));
    CHECK(!backend->inject_mouse_button("fourth", true));
    std::vector<ge::platform::RawEvent> events;
    CHECK(backend->poll(events));
    REQUIRE(events.size() == 6U);
    CHECK(events[0].source == "key:right");
    CHECK(events[0].value == 1.0F);
    CHECK(events[1].source == "key:right");
    CHECK(events[1].value == 0.0F);
    CHECK(events[2].source == "key:left_shift");
    CHECK(events[3].source == "mouse:left");
    CHECK(events[4].source == "mouse:middle");
    CHECK(events[5].source == "mouse:right");
    CHECK(events[5].value == 0.0F);
}

TEST_CASE("sdl: fingers arrive as touch sources, the first one also as a click") {
    auto backend = open();
    REQUIRE(backend != nullptr);
    CHECK(!backend->inject_finger(1, 0.5F, 0.5F, "tap"));
    // The window is 800x600 by default: the middle is (400, 300), a quarter is (200, 150).
    CHECK(backend->inject_finger(7, 0.5F, 0.5F, "down"));
    CHECK(backend->inject_finger(9, 0.25F, 0.25F, "down"));
    CHECK(backend->inject_finger(9, 0.5F, 0.25F, "motion"));
    CHECK(backend->inject_finger(7, 0.5F, 0.5F, "up"));
    std::vector<ge::platform::RawEvent> events;
    CHECK(backend->poll(events));
    ge::platform::RawState state;
    int clicks = 0;
    for (const ge::platform::RawEvent& event : events) {
        state.set(event.source, event.value);
        clicks += event.source == "mouse:left" ? 1 : 0;
    }
    // The first finger came and went: a press and a release of the left button, once each.
    CHECK(clicks == 2);
    CHECK(state.value("touch:down") == 0.0F);
    CHECK(state.value("touch:x") == 400.0F);
    CHECK(state.value("touch:y") == 300.0F);
    CHECK(state.value("mouse:left") == 0.0F);
    // The second finger is still down and moved.
    CHECK(state.value("touch:down2") == 1.0F);
    CHECK(state.value("touch:x2") == 400.0F);
    CHECK(state.value("touch:y2") == 150.0F);
    CHECK(state.value("touch:down3") == 0.0F);

    // A finger that goes first takes the first slot again.
    CHECK(backend->inject_finger(11, 0.0F, 0.0F, "down"));
    events.clear();
    CHECK(backend->poll(events));
    REQUIRE(!events.empty());
    CHECK(events.front().source == "touch:x");
    CHECK(ge::platform::is_valid_source("touch:down5"));
}

TEST_CASE("sdl: the Back button of Android is a key like another") {
    auto backend = open();
    REQUIRE(backend != nullptr);
    CHECK(backend->inject_key("AC Back", true));
    std::vector<ge::platform::RawEvent> events;
    CHECK(backend->poll(events));
    REQUIRE(events.size() == 1U);
    CHECK(events[0].source == "key:ac_back");
}

TEST_CASE("sdl: closing the window stops the loop") {
    auto backend = open();
    REQUIRE(backend != nullptr);
    backend->inject_quit();
    ge::app::App app;
    const auto result = ge::platform::run_loop(app, *backend, ge::platform::InputMap{});
    CHECK(result.backend_stopped);
    CHECK(result.frames == 0U);
}

TEST_CASE("sdl: the window can take another size, and the frame follows it") {
    auto backend = open(true);
    REQUIRE(backend != nullptr);
    CHECK(!backend->resize(0, 100));
    REQUIRE(backend->resize(480, 800));
    auto renderer = backend->renderer();
    renderer->draw({"square", 240.0F, 400.0F, 20.0F, 0xFF8000FFU});
    renderer->end_frame();
    const sdl::Frame& frame = backend->last_frame();
    REQUIRE(frame.width == 480);
    REQUIRE(frame.height == 800);
    CHECK(frame.at(240, 400) == 0xFF8000FFU);
}

TEST_CASE("sdl: rectangles, circles and lines are drawn, filled or as outlines") {
    auto backend = open(true);
    REQUIRE(backend != nullptr);
    auto renderer = backend->renderer();
    constexpr std::uint32_t orange = 0xFF8000FFU;
    constexpr std::uint32_t background = 0x121420FFU;
    ge::app::DrawCommand filled{"rect", 100.0F, 100.0F, 0.0F, orange};
    filled.width = 40.0F;
    filled.height = 20.0F;
    renderer->draw(filled);
    ge::app::DrawCommand frame = filled;
    frame.x = 300.0F;
    frame.thickness = 4.0F;
    renderer->draw(frame);
    const ge::app::DrawCommand disc{"circle", 100.0F, 300.0F, 30.0F, orange};
    renderer->draw(disc);
    ge::app::DrawCommand ring = disc;
    ring.x = 300.0F;
    ring.thickness = 4.0F;
    renderer->draw(ring);
    ge::app::DrawCommand line{"line", 500.0F, 100.0F, 0.0F, orange};
    line.x2 = 500.0F;
    line.y2 = 200.0F;
    line.thickness = 6.0F;
    renderer->draw(line);
    renderer->end_frame();
    const sdl::Frame& image = backend->last_frame();
    CHECK(image.at(100, 100) == orange); // a filled rectangle
    CHECK(image.at(81, 91) == orange);
    CHECK(image.at(79, 100) == background);
    CHECK(image.at(300, 100) == background); // an outline is hollow...
    CHECK(image.at(281, 100) == orange);     // ...with an edge
    CHECK(image.at(100, 300) == orange);     // a disc
    CHECK(image.at(100, 275) == orange);
    CHECK(image.at(100, 335) == background);
    CHECK(image.at(300, 300) == background); // a ring is hollow too
    CHECK(image.at(300, 273) == orange);
    CHECK(image.at(500, 150) == orange); // a line
    CHECK(image.at(520, 150) == background);
}

TEST_CASE("sdl: a game that follows the screen takes the shape of the window") {
    auto backend = open(true);
    REQUIRE(backend != nullptr);
    REQUIRE(backend->resize(480, 720));
    CHECK(backend->view_size().first == 0); // fixed until a game asks
    backend->adapt_view(0.4F, 0.8F);
    CHECK(backend->view_size().first == 480);
    CHECK(backend->view_size().second == 720);

    // A phone 9:20 high: the width stays, the height grows, and the frame is that big.
    REQUIRE(backend->inject_window_size(270, 600));
    std::vector<ge::platform::RawEvent> events;
    backend->poll(events);
    CHECK(backend->view_size().first == 480);
    CHECK(backend->view_size().second == 1067);
    // A square 1000 pixels down the game's view: it only exists on a view that tall (the window
    // has 270 x 600 pixels, so it lands at 135 x 562).
    backend->renderer()->draw({"square", 240.0F, 1000.0F, 20.0F, 0xFF8000FFU});
    backend->renderer()->end_frame();
    CHECK(backend->last_frame().width == 270);
    CHECK(backend->last_frame().height == 600);
    CHECK(backend->last_frame().at(135, 562) == 0xFF8000FFU);

    // Back to a fixed area with a new size.
    REQUIRE(backend->resize(480, 720));
    CHECK(backend->view_size().first == 0);
}

TEST_CASE("sdl: a mouse click reaches the game as an action") {
    auto backend = open();
    REQUIRE(backend != nullptr);
    ge::platform::InputMap map;
    map.bind("fire", "mouse:left", 1.0F);
    ge::app::App app;
    bool fired = false;
    app.add_system(ge::app::Stage::FixedUpdate, [&fired](ge::ecs::World& world) {
        fired = fired || world.resource<ge::app::Actions>()->pressed("fire");
    });
    CHECK(backend->inject_mouse_button("left", true));
    ge::platform::LoopOptions options;
    options.max_frames = 5;
    // A tick runs only once enough real time has passed, which a fast machine may not have
    // reached after five frames: keep playing until the click was seen (a second at most).
    for (int round = 0; round < 200 && !fired; ++round) {
        ge::platform::run_loop(app, *backend, map, options);
    }
    CHECK(fired);
}

TEST_CASE("sdl: sounds load from .wav files and play without a sound card") {
    auto backend = open();
    REQUIRE(backend != nullptr);
    const fs::path path = temp_file("beep.wav");
    {
        std::ofstream file(path, std::ios::binary);
        file << tiny_wav();
    }
    std::string error;
    CHECK(backend->load_sound("beep", path.string(), error));
    CHECK(error.empty());
    CHECK(backend->load_sound("beep", path.string(), error)); // loading again replaces it
    backend->audio()->play("beep");
    backend->audio()->play("unknown sounds play nothing");

    CHECK(!backend->load_sound("gone", temp_file("missing.wav").string(), error));
    CHECK(error.find("cannot load") != std::string::npos);
    std::error_code ignored;
    fs::remove(path, ignored);
}

TEST_CASE("sdl: the devices a game keeps outlive the backend") {
    // An App holds the renderer and the sound device and may be destroyed after
    // the backend: SDL must stay up until the last of them is gone.
    const fs::path path = temp_file("outlive.wav");
    {
        std::ofstream file(path, std::ios::binary);
        file << tiny_wav();
    }
    std::shared_ptr<ge::app::AudioDevice> audio;
    std::shared_ptr<ge::app::Renderer> renderer;
    {
        auto backend = open();
        REQUIRE(backend != nullptr);
        std::string error;
        CHECK(backend->load_sound("beep", path.string(), error));
        audio = backend->audio();
        renderer = backend->renderer();
    }
    audio->play("beep");
    renderer->draw({"square", 1.0F, 1.0F, 4.0F, 0xFFFFFFFFU});
    renderer->end_frame();
    audio.reset();
    renderer.reset();
    std::error_code ignored;
    fs::remove(path, ignored);
}

TEST_CASE("sdl: the software renderer draws the same frame") {
    sdl::WindowOptions options = headless(true);
    options.software = true;
    std::string error;
    auto backend = sdl::SdlBackend::create(options, error);
    CHECK(error.empty());
    REQUIRE(backend != nullptr);
    auto renderer = backend->renderer();
    renderer->draw({"square", 100.0F, 50.0F, 20.0F, 0xFF8000FFU});
    renderer->end_frame();
    CHECK(backend->last_frame().at(100, 50) == 0xFF8000FFU);
    CHECK(backend->last_frame().at(400, 300) == 0x121420FFU);
}

TEST_CASE("sdl: a frame is drawn, captured and saved") {
    auto backend = open(true);
    REQUIRE(backend != nullptr);
    auto renderer = backend->renderer();
    renderer->draw({"square", 100.0F, 50.0F, 20.0F, 0xFF8000FFU}); // orange
    renderer->end_frame();
    CHECK(backend->frames_presented() == 1U);
    const sdl::Frame& frame = backend->last_frame();
    REQUIRE(frame.width == 800);
    REQUIRE(frame.height == 600);
    CHECK(frame.at(100, 50) == 0xFF8000FFU);  // inside the square
    CHECK(frame.at(91, 41) == 0xFF8000FFU);   // near its corner
    CHECK(frame.at(120, 50) != 0xFF8000FFU);  // outside it
    CHECK(frame.at(400, 300) == 0x121420FFU); // the background
    CHECK(frame.at(-1, 0) == 0U);
    CHECK(frame.at(800, 0) == 0U);

    renderer->end_frame(); // an empty frame clears the screen
    CHECK(backend->last_frame().at(100, 50) == 0x121420FFU);

    const fs::path path = temp_file("frame.bmp");
    std::string error;
    CHECK(backend->save_frame_bmp(path.string(), error));
    CHECK(fs::file_size(path) > std::uintmax_t{800} * 600 * 3);
    std::error_code ignored;
    fs::remove(path, ignored);
    CHECK(!backend->save_frame_bmp("/nonexistent-dir/frame.bmp", error));
    CHECK(error.find("cannot write") != std::string::npos);
}

namespace {
std::string test_atlas() {
    return std::string(GE_SOURCE_DIR) + "/tests/platform/sdl/data/atlas.png";
}
} // namespace

TEST_CASE("sdl: sprites are cut from an atlas, tinted and flipped") {
    auto backend = open(true);
    REQUIRE(backend != nullptr);
    auto renderer = backend->renderer();
    std::string error;
    // 16x16 picture, 8x8 cells: red, green / blue, transparent.
    REQUIRE(renderer->load_atlas(0, {test_atlas(), 8, 8}, error));

    ge::app::DrawCommand red{"sprite", 100.0F, 100.0F, 0.0F, 0xFFFFFFFFU};
    red.atlas = 0;
    red.frame = 0;
    ge::app::DrawCommand green = red;
    green.x = 200.0F;
    green.frame = 1;
    ge::app::DrawCommand blue_flipped = red;
    blue_flipped.x = 300.0F;
    blue_flipped.frame = 2;
    blue_flipped.flip_x = true;
    ge::app::DrawCommand empty = red;
    empty.x = 400.0F;
    empty.frame = 3;
    ge::app::DrawCommand tinted = red;
    tinted.x = 500.0F;
    tinted.color = 0x00FFFFFFU; // keeps only green and blue: red becomes black
    ge::app::DrawCommand big = red;
    big.x = 600.0F;
    big.width = 32.0F;
    big.height = 16.0F;
    for (const auto& command : {red, green, blue_flipped, empty, tinted, big}) {
        renderer->draw(command);
    }
    renderer->end_frame();

    const sdl::Frame& frame = backend->last_frame();
    CHECK(frame.at(100, 100) == 0xFF0000FFU); // red cell, drawn at its natural size
    CHECK(frame.at(96, 100) == 0x7F0000FFU);  // its darker left edge
    CHECK(frame.at(104, 100) == 0x121420FFU); // just outside it
    CHECK(frame.at(200, 100) == 0x00FF00FFU); // cell 1 is the green one
    CHECK(frame.at(300, 100) == 0x0000FFFFU); // cell 2, mirrored
    CHECK(frame.at(303, 100) == 0x00007FFFU); // the dark edge is now on the right
    CHECK(frame.at(296, 100) == 0x0000FFFFU);
    CHECK(frame.at(400, 100) == 0x121420FFU); // a transparent cell draws nothing
    CHECK(frame.at(500, 100) == 0x000000FFU); // the tint removed the red
    CHECK(frame.at(600, 100) == 0xFF0000FFU); // a wider sprite is stretched...
    CHECK(frame.at(615, 107) == 0xFF0000FFU); // ...to 32x16
    CHECK(frame.at(615, 109) == 0x121420FFU);
}

TEST_CASE("sdl: the camera moves and zooms sprites and squares, for one frame") {
    auto backend = open(true);
    REQUIRE(backend != nullptr);
    auto renderer = backend->renderer();
    std::string error;
    REQUIRE(renderer->load_atlas(0, {test_atlas(), 8, 8}, error));

    ge::app::DrawCommand sprite{"sprite", 100.0F, 100.0F, 0.0F, 0xFFFFFFFFU};
    sprite.atlas = 0;
    renderer->set_camera({50.0F, 50.0F, 2.0F}); // looks at (50, 50), twice as big
    renderer->draw(sprite);
    renderer->draw({"square", 50.0F, 50.0F, 10.0F, 0xFF8000FFU}); // at the screen centre
    renderer->end_frame();
    const sdl::Frame& zoomed = backend->last_frame();
    // World (100, 100) is 50 away from the camera: 100 pixels at zoom 2, from the
    // centre of an 800x600 window; the 8-pixel cell becomes 16 pixels wide.
    CHECK(zoomed.at(500, 400) == 0xFF0000FFU);
    CHECK(zoomed.at(507, 407) == 0xFF0000FFU);
    CHECK(zoomed.at(509, 400) == 0x121420FFU);
    CHECK(zoomed.at(400, 300) == 0xFF8000FFU);
    CHECK(zoomed.at(100, 100) == 0x121420FFU); // nothing at the unmoved place

    renderer->draw(sprite); // the next frame has no camera again
    renderer->end_frame();
    CHECK(backend->last_frame().at(100, 100) == 0xFF0000FFU);
}

TEST_CASE("sdl: an atlas that cannot be loaded says why") {
    auto backend = open();
    REQUIRE(backend != nullptr);
    auto renderer = backend->renderer();
    std::string error;
    CHECK(!renderer->load_atlas(0, {"/nonexistent/atlas.png", 8, 8}, error));
    CHECK(error.find("cannot read") != std::string::npos);
    const fs::path not_a_png = temp_file("not_a_png.png");
    std::ofstream(not_a_png) << "this is not a picture";
    CHECK(!renderer->load_atlas(0, {not_a_png.string(), 8, 8}, error));
    CHECK(error.find("PNG") != std::string::npos);
    std::error_code ignored;
    fs::remove(not_a_png, ignored);
    // Drawing from an atlas that never loaded is silent, not a crash.
    ge::app::DrawCommand sprite{"sprite", 10.0F, 10.0F, 0.0F, 0xFFFFFFFFU};
    sprite.atlas = 3;
    renderer->draw(sprite);
    renderer->end_frame();
}

TEST_CASE("sdl: without capture there is no frame to save") {
    auto backend = open(false);
    REQUIRE(backend != nullptr);
    backend->renderer()->end_frame();
    CHECK(backend->last_frame().pixels.empty());
    std::string error;
    CHECK(!backend->save_frame_bmp(temp_file("none.bmp").string(), error));
    CHECK(error.find("no frame was captured") != std::string::npos);
}

#ifdef GE_HAS_GEPLAY

namespace {

std::string game_path(const char* name) {
    return std::string(GE_SOURCE_DIR) + "/games/" + name + "/game.json";
}

int geplay(const std::vector<std::string>& args, std::string& out, std::string& err) {
    std::ostringstream out_stream;
    std::ostringstream err_stream;
    const int code = sdl::run_geplay(args, ge::runner::linked_modules(), out_stream, err_stream);
    out = out_stream.str();
    err = err_stream.str();
    return code;
}

} // namespace

TEST_CASE("geplay: a held key moves the square on screen") {
    // The whole path: key -> SDL event -> raw input -> input.json -> action ->
    // simulation -> draw command -> pixels.
    const std::string path = game_path("move_sprite");
    const ge::game::LoadedGame loaded = ge::game::load_game(
        path,
        [](const std::string& file) -> std::optional<std::string> {
            const std::ifstream stream(file, std::ios::binary);
            if (!stream) {
                return std::nullopt;
            }
            std::ostringstream text;
            text << stream.rdbuf();
            return text.str();
        },
        ge::game::catalog_of(ge::runner::linked_modules()));
    REQUIRE(loaded.ok());

    ge::app::App app;
    REQUIRE(ge::game::setup_game(app, loaded.game, ge::runner::linked_modules()).ok());
    auto backend = open(true);
    REQUIRE(backend != nullptr);
    CHECK(backend->inject_key("Right", true));
    ge::platform::LoopOptions options;
    // Few frames, so that however slow the machine is (the loop catches up at most 8 ticks a
    // frame) the player cannot have left the 800 pixel wide screen.
    options.max_frames = 10;
    ge::platform::run_loop(app, *backend, loaded.game.input, options);

    // The player started at 400, 300 and moved right, 3 units per tick. How many ticks ran
    // depends on the machine's speed, so the test finds the player on the row it moves along
    // instead of at a computed pixel.
    REQUIRE(app.tick_count() >= 1U);
    const sdl::Frame& frame = backend->last_frame();
    int first = -1;
    int last = -1;
    for (int x = 0; x < frame.width; ++x) {
        if (frame.at(x, 300) == 0x6EE7A0FFU) { // the player's colour
            first = first < 0 ? x : first;
            last = x;
        }
    }
    if (first < 0) { // the log of a failing run must say what the loop did
        std::fprintf(stderr, "no player pixel on row 300 after %llu ticks in %llu frames\n",
                     static_cast<unsigned long long>(app.tick_count()),
                     static_cast<unsigned long long>(options.max_frames));
    }
    REQUIRE(first >= 0);
    CHECK(static_cast<double>(first + last) / 2.0 >= 402.0);
    CHECK(static_cast<double>(first + last) / 2.0 <= 400.0 + 3.0 * 8.0 * 10.0 + 2.0);
    CHECK(frame.at(100, 100) == 0x475569FFU); // a landmark
}

TEST_CASE("geplay: plays a game headless and saves a screenshot") {
    std::string out;
    std::string err;
    const fs::path shot = temp_file("geplay.bmp");
    const int code = geplay({game_path("move_sprite"), "--headless", "--frames", "8", "--size",
                             "640x480", "--screenshot", shot.string()},
                            out, err);
    CHECK(err.empty());
    CHECK(code == 0);
    CHECK(out.find("move_sprite: 8 frames") != std::string::npos);
    CHECK(fs::exists(shot));
    std::error_code ignored;
    fs::remove(shot, ignored);
}

TEST_CASE("geplay: wrong usage and invalid games are reported") {
    std::string out;
    std::string err;
    CHECK(geplay({}, out, err) == 2);
    CHECK(err.find("no game file given") != std::string::npos);
    CHECK(geplay({"a.json", "b.json"}, out, err) == 2);
    CHECK(geplay({game_path("move_sprite"), "--bogus"}, out, err) == 2);
    CHECK(err.find("unknown option --bogus") != std::string::npos);
    CHECK(geplay({game_path("move_sprite"), "--frames"}, out, err) == 2);
    CHECK(geplay({game_path("move_sprite"), "--frames", "many"}, out, err) == 2);
    CHECK(geplay({game_path("move_sprite"), "--size"}, out, err) == 2);
    CHECK(geplay({game_path("move_sprite"), "--size", "big"}, out, err) == 2);
    CHECK(geplay({game_path("move_sprite"), "--size", "0x10"}, out, err) == 2);
    CHECK(geplay({game_path("move_sprite"), "--screenshot"}, out, err) == 2);
    CHECK(geplay({"/nonexistent/game.json", "--headless"}, out, err) == 1);
    CHECK(err.find("cannot read the file") != std::string::npos);
}

TEST_CASE("geplay: the sounds of a game's directory are loaded") {
    const fs::path directory = fs::temp_directory_path() / "ge_sdl_test_game";
    std::error_code ignored;
    fs::remove_all(directory, ignored);
    fs::create_directories(directory / "sounds", ignored);
    {
        std::ofstream game(directory / "game.json");
        game << R"({"schema_version": 1, "name": "quiet", "modules": ["transform2d"],
                    "scenes": ["main.json"], "start_scene": "main"})";
        std::ofstream scene(directory / "main.json");
        scene << R"({"schema_version": 1, "name": "main", "entities": []})";
        std::ofstream good(directory / "sounds" / "beep.wav", std::ios::binary);
        good << tiny_wav();
        std::ofstream bad(directory / "sounds" / "broken.wav", std::ios::binary);
        bad << "not a wav";
        std::ofstream other(directory / "sounds" / "notes.txt");
        other << "ignored";
    }
    std::string out;
    std::string err;
    const int code =
        geplay({(directory / "game.json").string(), "--headless", "--frames", "2"}, out, err);
    CHECK(code == 0);
    CHECK(err.find("broken.wav") != std::string::npos); // reported, not fatal
    CHECK(err.find("beep.wav") == std::string::npos);
    fs::remove_all(directory, ignored);
}

namespace {

/// A game of one entity with a score of 42 and a record that follows it, saved as "best".
fs::path write_scoring_game(const char* name) {
    const fs::path directory = temp_file(name);
    std::error_code ignored;
    fs::remove_all(directory, ignored);
    fs::create_directories(directory, ignored);
    std::ofstream game(directory / "game.json");
    game << R"({"schema_version": 1, "name": "scoring",
                "modules": ["transform2d", "sprite2d", "text2d", "ui", "records"],
                "lists": {"tag": ["player", "board"], "record": ["best"]},
                "scenes": ["main.json"], "start_scene": "main"})";
    std::ofstream scene(directory / "main.json");
    scene << R"({"schema_version": 1, "name": "main", "entities": [
        {"components": {"ui.Tag": {"tag": "player"}, "ui.Bar": {"value": 42, "max": 100}}},
        {"components": {"ui.Tag": {"tag": "board"}, "records.Record": {
            "key": "best", "source": "player", "field": "ui.Bar.value"}}}]})";
    return directory;
}

std::string read_text(const fs::path& path) {
    const std::ifstream stream(path);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

} // namespace

TEST_CASE("geplay: a new best score is written to the save file") {
    const fs::path directory = write_scoring_game("scoring_new");
    const fs::path save = directory / "save.txt";
    std::string out;
    std::string err;
    const int code = geplay({(directory / "game.json").string(), "--headless", "--frames", "3",
                             "--save", save.string()},
                            out, err);
    CHECK(err.empty());
    CHECK(code == 0);
    CHECK(read_text(save) == "best 42\n");
    std::error_code ignored;
    fs::remove_all(directory, ignored);
}

TEST_CASE("geplay: the save file is read first, and a lower score does not replace it") {
    const fs::path directory = write_scoring_game("scoring_kept");
    const fs::path save = directory / "save.txt";
    {
        std::ofstream previous(save);
        previous << "best 100\nother 7\n";
    }
    std::string out;
    std::string err;
    const int code = geplay({(directory / "game.json").string(), "--headless", "--frames", "3",
                             "--save", save.string()},
                            out, err);
    CHECK(code == 0);
    CHECK(read_text(save) == "best 100\nother 7\n"); // read, found higher, left alone
    std::error_code ignored;
    fs::remove_all(directory, ignored);
}

TEST_CASE("geplay: a headless run without --save writes nothing") {
    const fs::path directory = write_scoring_game("scoring_none");
    std::string out;
    std::string err;
    CHECK(geplay({(directory / "game.json").string(), "--headless", "--frames", "3"}, out, err) ==
          0);
    CHECK(!fs::exists(directory / "save.txt"));
    std::error_code ignored;
    fs::remove_all(directory, ignored);
}

#endif

TEST_CASE("assets: a file is read whole, and a missing one is nothing") {
    const fs::path path = temp_file("assets_read.txt");
    {
        std::ofstream file(path, std::ios::binary);
        file << "one\r\ntwo";
    }
    const std::optional<std::string> text = sdl::read_file(path.string());
    REQUIRE(text.has_value());
    CHECK(*text == "one\r\ntwo");
    CHECK(
        !sdl::read_file((fs::temp_directory_path() / "ge_no_such_file.txt").string()).has_value());
}

TEST_CASE("assets: the files of a game come from its index, else from its folder") {
    const fs::path folder = temp_file("assets_game");
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    fs::create_directories(folder / "sounds");
    for (const char* name : {"game.json", "sounds/b.wav", "sounds/a.wav"}) {
        std::ofstream(folder / name) << "x";
    }
    const std::string root = folder.string() + "/";
    // No index: the folder is walked, in order, with `/` separators.
    const std::vector<std::string> walked{"game.json", "sounds/a.wav", "sounds/b.wav"};
    CHECK(sdl::list_game_files(root) == walked);
    // An index wins: the files of an APK cannot be walked. A line ending is tolerated.
    {
        std::ofstream index(folder / sdl::file_index_name, std::ios::binary);
        index << "sounds/z.wav\r\ngame.json\n\n";
    }
    const std::vector<std::string> indexed{"game.json", "sounds/z.wav"};
    CHECK(sdl::list_game_files(root) == indexed);
    fs::remove_all(folder, ignored);
}
