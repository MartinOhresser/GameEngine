// Tests of the window backend. They use SDL's "dummy" drivers (no window, no
// sound card), so they run anywhere, CI included; keys are pushed into SDL's
// real event queue, so the path from a key to an action is the real one.
#include "ge/game/runtime.hpp"
#include "ge/platform/sdl/sdl_backend.hpp"

#include "mini_test.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

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
    CHECK(sdl::key_source("") == "");
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

TEST_CASE("sdl: closing the window stops the loop") {
    auto backend = open();
    REQUIRE(backend != nullptr);
    backend->inject_quit();
    ge::app::App app;
    const auto result = ge::platform::run_loop(app, *backend, ge::platform::InputMap{});
    CHECK(result.backend_stopped);
    CHECK(result.frames == 0U);
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
    ge::platform::run_loop(app, *backend, map, options);
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
    CHECK(fs::file_size(path) > 800U * 600U * 3U);
    std::error_code ignored;
    fs::remove(path, ignored);
    CHECK(!backend->save_frame_bmp("/nonexistent-dir/frame.bmp", error));
    CHECK(error.find("cannot write") != std::string::npos);
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
    ge::game::LoadedGame loaded = ge::game::load_game(
        path,
        [](const std::string& file) -> std::optional<std::string> {
            std::ifstream stream(file, std::ios::binary);
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
    options.max_frames = 30;
    ge::platform::run_loop(app, *backend, loaded.game.input, options);

    CHECK(app.tick_count() >= 10U);
    // The player started at 400, 300 and moved right, 3 units per tick.
    const sdl::Frame& frame = backend->last_frame();
    CHECK(frame.at(400, 300) == 0x121420FFU); // where it was: the background now
    const double x = 400.0 + 3.0 * static_cast<double>(app.tick_count());
    CHECK(frame.at(static_cast<int>(std::lround(x)), 300) == 0x6EE7A0FFU); // the player's colour
    CHECK(frame.at(100, 100) == 0x475569FFU);                              // a landmark
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

#endif
