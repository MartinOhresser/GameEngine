#include "ge/platform/backend.hpp"

#include "ge/app/host.hpp"
#include "ge/app/viewport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ge::platform {

ScriptedBackend::ScriptedBackend(std::uint32_t tick_rate, std::uint64_t frames)
    : step_ns_(1'000'000'000 / static_cast<std::int64_t>(std::max<std::uint32_t>(tick_rate, 1U))),
      frames_(frames), renderer_(std::make_shared<app::RecordingRenderer>()),
      audio_(std::make_shared<app::RecordingAudio>()) {}

void ScriptedBackend::at(std::uint64_t frame, std::string source, float value) {
    script_.emplace(frame, RawEvent{std::move(source), value});
}

bool ScriptedBackend::poll(std::vector<RawEvent>& events) {
    if (frame_ >= frames_) {
        return false;
    }
    const auto [first, last] = script_.equal_range(frame_);
    for (auto it = first; it != last; ++it) {
        events.push_back(it->second);
    }
    ++frame_;
    return true;
}

std::chrono::nanoseconds ScriptedBackend::now() {
    return std::chrono::nanoseconds(step_ns_ * static_cast<std::int64_t>(frame_));
}

bool ScriptedBackend::resize(int width, int height) {
    width_ = width;
    height_ = height;
    adapt_ = false;
    return true;
}

void ScriptedBackend::adapt_view(float min_aspect, float max_aspect) {
    adapt_ = true;
    min_aspect_ = min_aspect;
    max_aspect_ = max_aspect;
}

std::pair<int, int> ScriptedBackend::view_size() const {
    if (!adapt_) {
        return {0, 0};
    }
    return adapted_view(width_, height_, min_aspect_, max_aspect_, screen_width_, screen_height_);
}

void ScriptedBackend::reset_assets() {
    ++resets_;
    renderer_ = std::make_shared<app::RecordingRenderer>();
    audio_ = std::make_shared<app::RecordingAudio>();
    sounds_.clear();
}

bool ScriptedBackend::load_sound(const std::string& name, const std::string& /*path*/,
                                 std::string& /*error*/) {
    sounds_.push_back(name);
    return true;
}

std::pair<int, int> adapted_view(int width, int height, float min_aspect, float max_aspect,
                                 int screen_width, int screen_height) {
    if (width <= 0 || height <= 0 || screen_width <= 0 || screen_height <= 0) {
        return {width, height};
    }
    const float aspect =
        std::clamp(static_cast<float>(screen_width) / static_cast<float>(screen_height), min_aspect,
                   max_aspect);
    if (width <= height) {
        return {width,
                std::max(1, static_cast<int>(std::lround(static_cast<float>(width) / aspect)))};
    }
    return {std::max(1, static_cast<int>(std::lround(static_cast<float>(height) * aspect))),
            height};
}

namespace {

/// Makes the viewport the size of the drawing area, when the backend lets it follow the screen:
/// the interface anchors itself to its edges, so it moves with them.
void follow_view(app::App& app, const Backend& backend) {
    const auto [width, height] = backend.view_size();
    auto* view = app.world().resource<app::Viewport>();
    if (width > 0 && height > 0 && view != nullptr &&
        (view->width != static_cast<std::uint32_t>(width) ||
         view->height != static_cast<std::uint32_t>(height))) {
        view->width = static_cast<std::uint32_t>(width);
        view->height = static_cast<std::uint32_t>(height);
    }
}

/// True when one of the back sources just went from released to pressed.
bool back_pressed(const std::vector<std::string>& sources, const std::vector<RawEvent>& events,
                  const RawState& before) {
    return std::any_of(events.begin(), events.end(), [&](const RawEvent& event) {
        return event.value > 0.5F && before.value(event.source) <= 0.5F &&
               std::find(sources.begin(), sources.end(), event.source) != sources.end();
    });
}

/// A pointer coordinate, not a button.
bool is_position(const std::string& source) {
    return source.ends_with(":x") || source.ends_with(":y");
}

/// Puts the events of a frame into `raw`. A button that is pressed and released in the same
/// frame (a quick tap on a slow device) would never be seen down: its release waits for the next
/// frame, so that the game sees the press for one tick.
void apply_events(RawState& raw, const std::vector<RawEvent>& events, std::vector<RawEvent>& held) {
    for (const RawEvent& release : held) {
        raw.set(release.source, release.value);
    }
    held.clear();
    std::vector<std::string> pressed_now; // buttons that went down in this frame
    for (const RawEvent& event : events) {
        const bool down = event.value > 0.5F;
        const bool button = !is_position(event.source);
        if (button && down && raw.value(event.source) <= 0.5F) {
            pressed_now.push_back(event.source);
        }
        const bool tapped =
            button && !down &&
            std::find(pressed_now.begin(), pressed_now.end(), event.source) != pressed_now.end();
        if (tapped) {
            held.push_back(event);
        } else {
            raw.set(event.source, event.value);
        }
    }
}

} // namespace

LoopResult run_loop(app::App& app, Backend& backend, const InputMap& map,
                    const LoopOptions& options) {
    app.world().insert_resource(app::RenderBackend{backend.renderer()});
    app.world().insert_resource(app::AudioBackend{backend.audio()});
    if (auto* time = app.world().resource<app::Time>(); time != nullptr) {
        time->interpolate = true; // real time: draw between ticks
    }

    LoopResult result;
    RawState raw;
    std::vector<RawEvent> events;
    std::vector<RawEvent> held; // releases that wait for the next frame
    std::chrono::nanoseconds last = backend.now();
    while (options.max_frames == 0 || result.frames < options.max_frames) {
        events.clear();
        if (!backend.poll(events)) {
            result.backend_stopped = true;
            break;
        }
        if (back_pressed(options.back_sources, events, raw)) {
            result.back_requested = true;
            break;
        }
        apply_events(raw, events, held);
        follow_view(app, backend);
        if (auto* live = app.world().resource<app::LiveInput>(); live != nullptr) {
            map.apply(raw, *live);
        }
        const std::chrono::nanoseconds now = backend.now();
        app.advance(now - last);
        app.world().resource<app::RenderBackend>()->renderer().end_frame();
        last = now;
        ++result.frames;
        if (options.after_frame) {
            options.after_frame(app);
        }
        if (const auto* quit = app.world().resource<app::QuitRequest>();
            quit != nullptr && quit->requested) {
            result.quit_requested = true;
            break;
        }
    }
    return result;
}

} // namespace ge::platform
