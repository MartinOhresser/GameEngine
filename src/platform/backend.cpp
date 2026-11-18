#include "ge/platform/backend.hpp"

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
    }
    return result;
}

} // namespace ge::platform
