// gebench: how much the window's renderer draws in one frame at 60 frames per second.
//
//   gebench [--drivers a,b,c] [--atlas FILE.png --cell N] [--size WxH]
//
// For each SDL render driver ("default": the one SDL chooses by itself) it opens a window without
// vsync and measures two loads, each doubled
// until a frame takes longer than 1/60 s:
//   - sprites: small sprites (32 x 32) from one atlas, as in a busy 2D scene;
//   - layers: sprites that cover the whole window, as the planes of a parallax background
//     (what limits them is how many pixels the card can fill, not how many calls are made).
// Everything goes through the engine's Renderer, as a game's drawing does. The result is a table
// (Markdown) on the standard output.

#include "ge/app/render.hpp"
#include "ge/platform/sdl/sdl_backend.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ge::platform::sdl::SdlBackend;

constexpr double frame_budget_ms = 1000.0 / 60.0;
constexpr int warmup_frames = 20;
constexpr int measured_frames = 90;
constexpr std::uint32_t max_load = 1U << 20U;

struct Options {
    std::vector<std::string> drivers{"default", "direct3d11", "direct3d12", "vulkan",
                                     "opengl",  "gpu",        "software"};
    std::string atlas = "games/treasure_run/art/tiles.png";
    int cell = 16;
    int width = 1280;
    int height = 720;
};

std::vector<std::string> split(std::string_view text) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream stream{std::string(text)};
    while (std::getline(stream, part, ',')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

bool parse(int argc, char** argv, Options& options) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); i += 2) {
        if (i + 1 >= args.size()) {
            return false;
        }
        const std::string_view value = args[i + 1];
        if (args[i] == "--drivers") {
            options.drivers = split(value);
        } else if (args[i] == "--atlas") {
            options.atlas = std::string(value);
        } else if (args[i] == "--cell") {
            options.cell = std::stoi(std::string(value));
        } else if (args[i] == "--size") {
            const std::size_t x = value.find('x');
            if (x == std::string_view::npos) {
                return false;
            }
            options.width = std::stoi(std::string(value.substr(0, x)));
            options.height = std::stoi(std::string(value.substr(x + 1)));
        } else {
            return false;
        }
    }
    return true;
}

/// A small deterministic generator, so every driver draws the same scene.
class Lcg {
public:
    std::uint32_t next() {
        state_ = state_ * 1664525U + 1013904223U;
        return state_ >> 8U;
    }

private:
    std::uint32_t state_ = 12345U;
};

/// The commands of one frame of `load` things of a kind.
std::vector<ge::app::DrawCommand> scene(bool layers, std::uint32_t load, const Options& options) {
    std::vector<ge::app::DrawCommand> commands;
    commands.reserve(load);
    Lcg random;
    for (std::uint32_t i = 0; i < load; i += 1U) {
        ge::app::DrawCommand command;
        command.what = "sprite";
        command.atlas = 0;
        command.frame = random.next() % 16U;
        if (layers) {
            command.x = static_cast<float>(options.width) / 2.0F;
            command.y = static_cast<float>(options.height) / 2.0F;
            command.width = static_cast<float>(options.width);
            command.height = static_cast<float>(options.height);
            command.color = 0xFFFFFF40U; // see-through, as a plane of mist
        } else {
            command.x =
                static_cast<float>(random.next() % static_cast<std::uint32_t>(options.width));
            command.y =
                static_cast<float>(random.next() % static_cast<std::uint32_t>(options.height));
            command.width = 32.0F;
            command.height = 32.0F;
        }
        commands.push_back(command);
    }
    return commands;
}

/// Milliseconds per frame for this scene, or a negative number if the window was closed.
double time_frames(SdlBackend& backend, const std::vector<ge::app::DrawCommand>& commands) {
    ge::app::Renderer& renderer = *backend.renderer();
    std::vector<ge::platform::RawEvent> events;
    std::chrono::nanoseconds start{};
    for (int frame = 0; frame < warmup_frames + measured_frames; frame += 1) {
        if (frame == warmup_frames) {
            start = backend.now();
        }
        events.clear();
        if (!backend.poll(events)) {
            return -1.0;
        }
        for (const ge::app::DrawCommand& command : commands) {
            renderer.draw(command);
        }
        renderer.end_frame();
    }
    const std::chrono::duration<double, std::milli> spent = backend.now() - start;
    return spent.count() / measured_frames;
}

/// The biggest load (doubled from `first`) drawn within 1/60 s, and its time per frame.
std::pair<std::uint32_t, double> largest_load(SdlBackend& backend, bool layers, std::uint32_t first,
                                              const Options& options) {
    std::uint32_t best = 0;
    double best_ms = 0.0;
    for (std::uint32_t load = first; load <= max_load; load *= 2U) {
        const double ms = time_frames(backend, scene(layers, load, options));
        if (ms < 0.0 || ms > frame_budget_ms) {
            break;
        }
        best = load;
        best_ms = ms;
    }
    return {best, best_ms};
}

void bench_driver(const std::string& driver, const Options& options) {
    ge::platform::sdl::WindowOptions window;
    window.title = "gebench - " + driver;
    window.width = options.width;
    window.height = options.height;
    const bool sdl_choice = driver == "default";
    window.render_driver = sdl_choice ? std::string() : driver;
    window.vsync = false;
    std::string error;
    const std::unique_ptr<SdlBackend> backend = SdlBackend::create(window, error);
    if (backend == nullptr) {
        std::cout << "| " << driver << " | - | - | " << error << " |\n";
        return;
    }
    const std::string used = backend->render_driver();
    const std::string note = sdl_choice ? "SDL chose " + used : std::string();
    if (!sdl_choice && used != driver) {
        std::cout << "| " << driver << " | - | - | not available (SDL chose " << used << ") |\n";
        return;
    }
    const ge::app::AtlasInfo atlas{options.atlas, options.cell, options.cell};
    if (!backend->renderer()->load_atlas(0, atlas, error)) {
        std::cout << "| " << driver << " | - | - | " << error << " |\n";
        return;
    }
    const auto [sprites, sprites_ms] = largest_load(*backend, false, 1000U, options);
    const auto [layers, layers_ms] = largest_load(*backend, true, 1U, options);
    std::cout << "| " << driver << " | " << sprites << " (" << sprites_ms << " ms) | " << layers
              << " (" << layers_ms << " ms) | " << note << " |\n"
              << std::flush;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse(argc, argv, options)) {
        std::cerr << "usage: gebench [--drivers a,b,c] [--atlas FILE.png --cell N] [--size WxH]\n";
        return 2;
    }
    std::cout.precision(3);
    std::cout << "Window " << options.width << " x " << options.height
              << ", no vsync; each load is doubled while a frame takes less than 1/60 s.\n\n"
              << "| Driver | 32 x 32 sprites | Full-window layers | Note |\n"
              << "| --- | --- | --- | --- |\n";
    for (const std::string& driver : options.drivers) {
        bench_driver(driver, options);
    }
    return 0;
}
