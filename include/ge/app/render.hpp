#pragma once

#include "ge/core/hash.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ge::app {

/// What a game asks the screen to show. Deliberately tiny: the 2D renderer
/// (milestone M7) replaces it with sprites, text and cameras. What matters now
/// is the direction of the dependency: game logic decides what exists, a
/// separate system turns that into draw commands, and a `Renderer` is the only
/// thing that knows what a screen is.
struct DrawCommand {
    std::string what; ///< Name of the thing drawn, e.g. an image or a shape.
    float x = 0.0F;
    float y = 0.0F;

    friend bool operator==(const DrawCommand&, const DrawCommand&) = default;
};

/// Where draw commands go. A window backend draws them; the two below let a
/// game run with no screen at all.
class Renderer {
public:
    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;
    virtual ~Renderer() = default;

    virtual void draw(const DrawCommand& command) = 0;
    /// Called once per frame, after the frame's commands.
    virtual void end_frame() = 0;
};

/// Draws nothing. The default: a game with no window still runs.
class NullRenderer final : public Renderer {
public:
    void draw(const DrawCommand& /*command*/) override {}
    void end_frame() override {}
};

/// Draws nothing but remembers every command, so a test can check what the
/// game asked for ("a ship was drawn at 100, 300") without a screen.
class RecordingRenderer final : public Renderer {
public:
    void draw(const DrawCommand& command) override { commands_.push_back(command); }
    void end_frame() override { ++frames_; }

    [[nodiscard]] const std::vector<DrawCommand>& commands() const noexcept { return commands_; }
    [[nodiscard]] std::size_t frames() const noexcept { return frames_; }
    void clear() { commands_.clear(); }

private:
    std::vector<DrawCommand> commands_;
    std::size_t frames_ = 0;
};

/// The renderer of an app, stored in the world as a resource. `App` installs a
/// `NullRenderer`; a window or a test replaces it.
///
/// Rendering is presentation, never simulation state, so it takes no part in
/// the world fingerprint: the same game must end in the same state whatever
/// renderer is attached.
class RenderBackend {
public:
    RenderBackend() : renderer_(std::make_shared<NullRenderer>()) {}
    explicit RenderBackend(std::shared_ptr<Renderer> renderer) : renderer_(std::move(renderer)) {}

    [[nodiscard]] Renderer& renderer() const noexcept { return *renderer_; }

    void hash(Hasher& /*hasher*/) const noexcept {}

private:
    std::shared_ptr<Renderer> renderer_;
};

} // namespace ge::app
