#include "ge/platform/backend.hpp"

#include "ge/app/host.hpp"

#include <algorithm>

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
    return true;
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

namespace {

/// True when one of the back sources just went from released to pressed.
bool back_pressed(const std::vector<std::string>& sources, const std::vector<RawEvent>& events,
                  const RawState& before) {
    return std::any_of(events.begin(), events.end(), [&](const RawEvent& event) {
        return event.value > 0.5F && before.value(event.source) <= 0.5F &&
               std::find(sources.begin(), sources.end(), event.source) != sources.end();
    });
}

} // namespace

LoopResult run_loop(app::App& app, Backend& backend, const InputMap& map,
                    const LoopOptions& options) {
    app.world().insert_resource(app::RenderBackend{backend.renderer()});
    app.world().insert_resource(app::AudioBackend{backend.audio()});

    LoopResult result;
    RawState raw;
    std::vector<RawEvent> events;
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
        for (const RawEvent& event : events) {
            raw.set(event.source, event.value);
        }
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
