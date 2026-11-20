#pragma once

#include "ge/core/hash.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ge::app {

/// What a game asks the screen to show. The direction of the dependency is the
/// point: game logic decides what exists, a separate system turns that into
/// draw commands, and a `Renderer` is the only thing that knows what a screen
/// is. Positions and sizes are in world units, seen through the camera.
struct DrawCommand {
    /// What is drawn: "square" (a flat square), "rect" (a flat rectangle, `width` by `height`),
    /// "circle" (`size` is its radius), "line" (from x, y to `x2`, `y2`) or "sprite" (a cell of
    /// an atlas). Flat shapes are filled when `thickness` is 0 and drawn as an outline of that
    /// thickness otherwise (a line is always `thickness` thick, at least one pixel).
    std::string what;
    float x = 0.0F; ///< Centre, in world units.
    float y = 0.0F;
    float size = 0.0F;                 ///< Side of a square; 0 when the thing has its own size.
    std::uint32_t color = 0xFFFFFFFFU; ///< 0xRRGGBBAA; tints a sprite.
    /// Sprites: the atlas (its index in the game's list) and the cell of it.
    int atlas = -1;
    std::uint32_t frame = 0;
    /// Sprites: size in world units; 0 means the size of one cell.
    float width = 0.0F;
    float height = 0.0F;
    bool flip_x = false;
    /// The position and size are pixels of the window and the camera does not
    /// apply: for the interface, which stays put while the world scrolls.
    bool screen = false;
    /// Lines: the end point, in world units.
    float x2 = 0.0F;
    float y2 = 0.0F;
    /// How much bigger than its size it is drawn, around its centre (1: as it is). A sprite
    /// of size 0 is drawn at its cell's size times this.
    float scale = 1.0F;
    /// Outline or line thickness, in world units; 0 fills a shape.
    float thickness = 0.0F;

    friend bool operator==(const DrawCommand&, const DrawCommand&) = default;
};

/// A picture cut into equal cells, as a renderer was told about it.
struct AtlasInfo {
    std::string image; ///< Path of the image file.
    int cell_width = 0;
    int cell_height = 0;

    friend bool operator==(const AtlasInfo&, const AtlasInfo&) = default;
};

/// Where the view is looking: the world point at the centre of the screen, and
/// how much it is enlarged. Without a camera, one world unit is one pixel and
/// the world's origin is the top left corner of the screen.
struct CameraView {
    float x = 0.0F;
    float y = 0.0F;
    float zoom = 1.0F;

    friend bool operator==(const CameraView&, const CameraView&) = default;
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

    /// Tells the renderer about the picture of atlas number `index` so that
    /// sprites can use it. Returns false, with `error` filled, if it cannot be
    /// loaded. A renderer with no screen has nothing to load and says yes.
    virtual bool load_atlas(int /*index*/, const AtlasInfo& /*atlas*/, std::string& /*error*/) {
        return true;
    }

    /// Sets the camera for the commands of this frame. A frame with no call has
    /// no camera.
    virtual void set_camera(const CameraView& /*camera*/) {}
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
    bool load_atlas(int index, const AtlasInfo& atlas, std::string& /*error*/) override {
        if (static_cast<std::size_t>(index) >= atlases_.size()) {
            atlases_.resize(static_cast<std::size_t>(index) + 1U);
        }
        atlases_[static_cast<std::size_t>(index)] = atlas;
        return true;
    }
    void set_camera(const CameraView& camera) override { camera_ = camera; }

    [[nodiscard]] const std::vector<DrawCommand>& commands() const noexcept { return commands_; }
    [[nodiscard]] std::size_t frames() const noexcept { return frames_; }
    [[nodiscard]] const std::vector<AtlasInfo>& atlases() const noexcept { return atlases_; }
    /// The last camera set, or nothing if none was.
    [[nodiscard]] const std::optional<CameraView>& camera() const noexcept { return camera_; }
    void clear() { commands_.clear(); }

private:
    std::vector<DrawCommand> commands_;
    std::size_t frames_ = 0;
    std::vector<AtlasInfo> atlases_;
    std::optional<CameraView> camera_;
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
