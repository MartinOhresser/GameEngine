#pragma once

#include "ge/app/app.hpp"
#include "ge/app/audio.hpp"
#include "ge/app/render.hpp"
#include "ge/platform/input_map.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ge::platform {

/// What the game needs from a machine: input, time, a screen, a speaker.
///
/// A window (SDL), a script and, later, a pad or a touch screen are all
/// backends. The game only sees actions, so it behaves the same with any of
/// them; that is what the tests of this layer check.
class Backend {
public:
    Backend() = default;
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;
    Backend(Backend&&) = delete;
    Backend& operator=(Backend&&) = delete;
    virtual ~Backend() = default;

    /// Appends the raw input changes since the last call. Returns false when
    /// the player asked to stop (closed the window).
    virtual bool poll(std::vector<RawEvent>& events) = 0;

    /// Monotonic time since the backend started.
    [[nodiscard]] virtual std::chrono::nanoseconds now() = 0;

    /// Where the game draws.
    [[nodiscard]] virtual std::shared_ptr<app::Renderer> renderer() = 0;

    /// Where the game plays sounds.
    [[nodiscard]] virtual std::shared_ptr<app::AudioDevice> audio() = 0;

    /// Gives the window a new size (and the drawing the same view), as when a
    /// game asks for a different window than the launcher's. Backends without
    /// a window ignore it and return false.
    virtual bool resize(int width, int height) {
        (void)width;
        (void)height;
        return false;
    }

    /// Makes the drawing area follow the shape of the screen, from now on (until the next
    /// `resize`, which goes back to a fixed area): the short side keeps the size `resize` gave,
    /// the long one stretches (`adapted_view`) with aspects (width / height) from `min_aspect` to
    /// `max_aspect`. Backends without a window ignore it.
    virtual void adapt_view(float min_aspect, float max_aspect) {
        (void)min_aspect;
        (void)max_aspect;
    }

    /// Gives the window itself this size in pixels, whatever the game's drawing area is: a game
    /// that follows the screen (`adapt_view`) then takes its shape, which is how a phone of any
    /// shape is tried on a computer. Backends without a window return false.
    virtual bool set_window_size(int width, int height) {
        (void)width;
        (void)height;
        return false;
    }

    /// The size of the drawing area right now, in the game's pixels, while it follows the screen
    /// (`adapt_view`); 0 x 0 when it does not.
    [[nodiscard]] virtual std::pair<int, int> view_size() const { return {0, 0}; }

    /// Starts again with an empty renderer and an empty sound bank, on the same
    /// window: what belongs to the window (and stays) is separated from what
    /// belongs to a game (atlases, sounds). A launcher calls it before each
    /// game and when it comes back. The previous renderer and audio keep
    /// working for whoever still holds them.
    virtual void reset_assets() {}

    /// Loads a .wav file so that `AudioDevice::play(name)` plays it. False, with
    /// `error` filled, when the backend has no sound or the file cannot be read.
    virtual bool load_sound(const std::string& name, const std::string& path, std::string& error) {
        (void)name;
        (void)path;
        error = "this backend plays no sound";
        return false;
    }
};

/// The drawing area of a game of `width` x `height` on a screen of `screen_width` x
/// `screen_height` pixels, if it follows the screen: a game taller than wide keeps its width and
/// takes the height that fills the screen, a game wider than tall keeps its height; the shape is
/// kept between `min_aspect` and `max_aspect` (width / height), the screen has bars beyond. The
/// game's own size when the screen is not known.
[[nodiscard]] std::pair<int, int> adapted_view(int width, int height, float min_aspect,
                                               float max_aspect, int screen_width,
                                               int screen_height);

/// A backend driven by a script, with a clock that advances by exactly one
/// tick per frame: the "player" of tests, with the same interface as a window.
/// It records what was drawn and played.
class ScriptedBackend final : public Backend {
public:
    /// Stops after `frames` frames; time moves `1 / tick_rate` s per frame.
    ScriptedBackend(std::uint32_t tick_rate, std::uint64_t frames);

    /// At frame `frame` (the first is 0), `source` takes `value`.
    void at(std::uint64_t frame, std::string source, float value);

    bool poll(std::vector<RawEvent>& events) override;
    [[nodiscard]] std::chrono::nanoseconds now() override;
    [[nodiscard]] std::shared_ptr<app::Renderer> renderer() override { return renderer_; }
    [[nodiscard]] std::shared_ptr<app::AudioDevice> audio() override { return audio_; }

    [[nodiscard]] const app::RecordingRenderer& drawn() const noexcept { return *renderer_; }
    [[nodiscard]] const app::RecordingAudio& heard() const noexcept { return *audio_; }

    /// Remembers the size asked for (0 x 0 until a call), so tests can see it.
    bool resize(int width, int height) override;
    void adapt_view(float min_aspect, float max_aspect) override;
    [[nodiscard]] std::pair<int, int> view_size() const override;
    bool set_window_size(int width, int height) override {
        set_screen(width, height);
        return true;
    }
    /// Pretends the screen has this size (in pixels), so a test sees `view_size` follow it.
    void set_screen(int width, int height) noexcept {
        screen_width_ = width;
        screen_height_ = height;
    }
    void reset_assets() override;
    /// Remembers the names it was asked to load (it plays nothing, but accepts them).
    bool load_sound(const std::string& name, const std::string& path, std::string& error) override;
    [[nodiscard]] const std::vector<std::string>& sounds() const noexcept { return sounds_; }
    /// How many times `reset_assets` was called.
    [[nodiscard]] int resets() const noexcept { return resets_; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

private:
    int width_ = 0;
    int height_ = 0;
    bool adapt_ = false;
    float min_aspect_ = 0.0F;
    float max_aspect_ = 0.0F;
    int screen_width_ = 0;
    int screen_height_ = 0;
    int resets_ = 0;
    std::vector<std::string> sounds_;
    std::int64_t step_ns_;
    std::uint64_t frames_;
    std::uint64_t frame_ = 0;
    std::multimap<std::uint64_t, RawEvent> script_;
    std::shared_ptr<app::RecordingRenderer> renderer_;
    std::shared_ptr<app::RecordingAudio> audio_;
};

struct LoopOptions {
    /// Stop after this many frames; 0 means until the backend says to stop.
    std::uint64_t max_frames = 0;
    /// Called after every frame (drawing included): a player of the game uses it
    /// to write the save data when it changed. Nothing when empty.
    std::function<void(app::App&)> after_frame;
    /// Raw sources (for example "key:escape") that belong to the host, not to
    /// the game: pressing one ends the loop with `back_requested`, whatever the
    /// game does with its menus. The way back to a launcher. None by default. A source that the
    /// game's input map binds to one of its actions is left to the game (a game whose Escape opens
    /// its pause menu): leave such a game by its own menus.
    std::vector<std::string> back_sources;
    /// Every `stats_every` frames, writes to `stats` how long a frame took on average: the
    /// simulation, then the drawing (which waits for the screen when vsync is on). Nothing when
    /// it is null: what a developer looks at to see what a game costs.
    std::ostream* stats = nullptr;
    std::uint32_t stats_every = 120;
};

struct LoopResult {
    std::uint64_t frames = 0;
    /// True when the backend stopped the loop (as opposed to `max_frames`).
    bool backend_stopped = false;
    /// True when the game asked to leave (`app::QuitRequest`, raised by a
    /// "Quit" button for instance).
    bool quit_requested = false;
    /// True when the player pressed one of `LoopOptions::back_sources`.
    bool back_requested = false;
};

/// Plays `app` on `backend` in real time: each frame reads the input, turns it
/// into actions with `map`, then advances the game by the time that really
/// passed. The backend's renderer and audio replace the app's. Game logic is
/// unchanged by this: ticks stay fixed (see `App::advance`).
LoopResult run_loop(app::App& app, Backend& backend, const InputMap& map,
                    const LoopOptions& options = {});

} // namespace ge::platform
