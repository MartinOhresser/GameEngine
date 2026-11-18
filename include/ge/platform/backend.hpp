#pragma once

#include "ge/app/app.hpp"
#include "ge/app/audio.hpp"
#include "ge/app/render.hpp"
#include "ge/platform/input_map.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
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
};

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

private:
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
};

struct LoopResult {
    std::uint64_t frames = 0;
    /// True when the backend stopped the loop (as opposed to `max_frames`).
    bool backend_stopped = false;
};

/// Plays `app` on `backend` in real time: each frame reads the input, turns it
/// into actions with `map`, then advances the game by the time that really
/// passed. The backend's renderer and audio replace the app's. Game logic is
/// unchanged by this: ticks stay fixed (see `App::advance`).
LoopResult run_loop(app::App& app, Backend& backend, const InputMap& map, LoopOptions options = {});

} // namespace ge::platform
