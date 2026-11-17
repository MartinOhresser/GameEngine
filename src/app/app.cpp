#include "ge/app/app.hpp"

#include <array>

namespace ge::app {

namespace {

constexpr std::array<Stage, 3> frame_order{Stage::PreUpdate, Stage::Update, Stage::PostUpdate};

constexpr std::size_t index_of(Stage stage) noexcept {
    return static_cast<std::size_t>(stage);
}

} // namespace

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index): `Stage` values are
// always smaller than `stage_count`.
void Schedule::add_system(Stage stage, System system, std::string name) {
    auto& systems = stages_[index_of(stage)];
    if (name.empty()) {
        name = "system_" + std::to_string(systems.size());
    }
    systems.push_back({std::move(name), std::move(system)});
}

void Schedule::run_stage(Stage stage, ecs::World& world) {
    for (auto& entry : stages_[index_of(stage)]) {
        entry.run(world);
    }
}

std::size_t Schedule::system_count(Stage stage) const noexcept {
    return stages_[index_of(stage)].size();
}

std::vector<std::string> Schedule::system_names(Stage stage) const {
    std::vector<std::string> names;
    for (const auto& entry : stages_[index_of(stage)]) {
        names.push_back(entry.name);
    }
    return names;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)

void App::update() {
    if (!started_) {
        schedule_.run_stage(Stage::Startup, world_);
        started_ = true;
    }
    for (const Stage stage : frame_order) {
        schedule_.run_stage(stage, world_);
    }
    ++frames_;
}

void App::run_frames(std::uint32_t frames) {
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        update();
    }
}

} // namespace ge::app
