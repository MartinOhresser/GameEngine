#pragma once

#include "ge/core/hash.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ge::app {

/// How a drawing is mixed with what is already on the screen.
enum class Blend : std::uint8_t {
    Alpha,    ///< Covers it, as much as its transparency says (the usual).
    Add,      ///< Adds its light: glows, sparks, rays; black adds nothing.
    Multiply, ///< Darkens it by its colour: shadows, tints; white changes nothing.
};

/// What a game asks the screen to show. The direction of the dependency is the
/// point: game logic decides what exists, a separate system turns that into
/// draw commands, and a `Renderer` is the only thing that knows what a screen
/// is. Positions and sizes are in world units, seen through the camera.
struct DrawCommand {
    /// What is drawn: "square" (a flat square), "rect" (a flat rectangle, `width` by `height`),
    /// "circle" (`size` is its radius), "line" (from x, y to `x2`, `y2`), "glow" (a soft disc of
    /// radius `size`, bright in the middle and fading to nothing at its edge: a halo, a light) or
    /// "sprite" (a cell of an atlas). Flat shapes are filled when `thickness` is 0 and drawn as an
    /// outline of that thickness otherwise (a line is always `thickness` thick, at least one
    /// pixel).
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
    /// Sprites: turned by this many degrees around their centre, clockwise on the screen.
    float angle = 0.0F;
    /// Sprites: how they mix with what is under them.
    Blend blend = Blend::Alpha;
    /// How much of the camera's movement applies, per axis: 1 moves with the world, 0.5 moves
    /// half as much (a far plane of a parallax background), 0 stays put on the screen. The zoom
    /// applies the same to all. No effect without a camera or on `screen` drawings.
    float parallax_x = 1.0F;
    float parallax_y = 1.0F;
    /// Sprites: drawn again side by side, every `width`, across the whole width of the view, so
    /// one picture makes an endless band (a sky, a row of hills, a ground).
    bool repeat_x = false;
    /// With lighting on (`Renderer::set_lighting`): darkened by the light map, so that it shows
    /// only as much as the lights around it allow. Off, it keeps its own colours (a sky, a far
    /// plane painted with its own light, a glow). `screen` drawings are never lit.
    bool lit = true;
    /// Not drawn in the world but added to the light map: it lights the `lit` drawings under it
    /// (a lamp, a shaft of sunlight). Ignored without lighting.
    bool light = false;

    friend bool operator==(const DrawCommand&, const DrawCommand&) = default;
};

/// The light of a frame. Off (the default), every drawing keeps its own colours. On, the frame
/// has a light map: the `ambient` colour everywhere (what the dark looks like), plus every
/// `light` drawing added on top; the `lit` drawings are multiplied by it.
struct Lighting {
    bool on = false;
    std::uint32_t ambient = 0xFFFFFFFFU; ///< 0xRRGGBBAA; the alpha is not used.

    friend bool operator==(const Lighting&, const Lighting&) = default;
};

/// Effects on the whole picture of the world, applied after it is drawn and before the
/// interface (`screen` drawings), which they never touch. The defaults change nothing.
struct ScreenEffects {
    /// Bright parts glow softly over what is around them (0: none, 1: strong).
    float bloom = 0.0F;
    /// The edges and corners of the picture darken (0: none, 1: black corners).
    float vignette = 0.0F;
    /// How far from the middle the darkening starts, as a share of the half diagonal.
    /// Below 0 it reaches the middle too (-4 covers almost the whole picture).
    float vignette_size = 0.6F;
    /// The colour of the vignette (0xRRGGBBAA): black darkens the edges, white whitens them.
    std::uint32_t vignette_color = 0x000000FFU;
    /// A second vignette laid over the first, the same way (0: none, 1: strong): edges that
    /// light up while the first darkens them, or the other way round.
    float edge_glow = 0.0F;
    float edge_glow_size = 0.6F;
    std::uint32_t edge_glow_color = 0xFFFFFFFFU;
    /// A fine moving grain, as on film (0: none, 1: strong).
    float grain = 0.0F;
    /// Every colour is multiplied by this one (0xRRGGBBAA; white changes nothing): a cold night,
    /// a warm evening.
    std::uint32_t tint = 0xFFFFFFFFU;
    /// This colour is added everywhere (0xRRGGBBAA; black changes nothing): shadows lifted
    /// towards a colour.
    std::uint32_t lift = 0x000000FFU;
    /// Black bars at the top and bottom, as in a film: the share of the height they cover
    /// together (0 to 1).
    float letterbox = 0.0F;
    /// Shafts of light out of the bright parts, away from a point of the screen (0: none, 1:
    /// strong). The point is a share of the width and height; the length a share of the screen.
    float rays = 0.0F;
    float rays_x = 0.5F;
    float rays_y = 0.35F;
    float rays_length = 0.3F;

    /// Whether any effect is asked for.
    [[nodiscard]] bool any() const noexcept {
        return bloom > 0.0F || vignette > 0.0F || edge_glow > 0.0F || grain > 0.0F || (tint | 0xFFU) != 0xFFFFFFFFU ||
               (lift & 0xFFFFFF00U) != 0U || letterbox > 0.0F || rays > 0.0F;
    }

    friend bool operator==(const ScreenEffects&, const ScreenEffects&) = default;
};

/// A picture cut into equal cells, as a renderer was told about it.
struct AtlasInfo {
    std::string image; ///< Path of the image file.
    int cell_width = 0;
    int cell_height = 0;
    /// Smoothed when enlarged, shrunk or turned (painted pictures). Without it each pixel stays a
    /// sharp square (pixel art).
    bool smooth = false;

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

    /// The atlas index of the first picture made while the game runs (`set_image`): far above
    /// the game's own atlases, which are numbered from 0.
    static constexpr int first_made_image = 1 << 24;

    /// Whether this renderer shows pictures made while the game runs. A module that makes them
    /// (a terrain that is dug, a map that is explored) skips the work when it does not.
    [[nodiscard]] virtual bool makes_images() const noexcept { return false; }

    /// Makes, or replaces, the picture of atlas number `index` (from `first_made_image`) out of
    /// `pixels`: width by height colours 0xRRGGBBAA, row after row from the top left. One cell
    /// covers the whole picture; `smooth` as for an atlas. Returns false if it cannot.
    virtual bool set_image(int /*index*/, int /*width*/, int /*height*/,
                           const std::vector<std::uint32_t>& /*pixels*/, bool /*smooth*/) {
        return true;
    }

    /// Forgets a picture made by `set_image`.
    virtual void drop_image(int /*index*/) {}

    /// Sets the camera for the commands of this frame. A frame with no call has
    /// no camera.
    virtual void set_camera(const CameraView& /*camera*/) {}

    /// Sets the light of this frame. A frame with no call has no lighting.
    virtual void set_lighting(const Lighting& /*lighting*/) {}

    /// Sets the screen effects of this frame. A frame with no call has none.
    virtual void set_effects(const ScreenEffects& /*effects*/) {}
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
    void set_lighting(const Lighting& lighting) override { lighting_ = lighting; }
    void set_effects(const ScreenEffects& effects) override { effects_ = effects; }
    [[nodiscard]] bool makes_images() const noexcept override { return true; }
    bool set_image(int index, int width, int height, const std::vector<std::uint32_t>& pixels,
                   bool /*smooth*/) override {
        images_[index] = MadeImage{width, height, pixels};
        image_updates_ += 1U;
        return true;
    }
    void drop_image(int index) override { images_.erase(index); }

    /// A picture made by `set_image`.
    struct MadeImage {
        int width = 0;
        int height = 0;
        std::vector<std::uint32_t> pixels;
    };
    /// The pictures made while the game runs, by index, and how many times one was set.
    [[nodiscard]] const std::map<int, MadeImage>& images() const noexcept { return images_; }
    [[nodiscard]] std::size_t image_updates() const noexcept { return image_updates_; }

    [[nodiscard]] const std::vector<DrawCommand>& commands() const noexcept { return commands_; }
    [[nodiscard]] std::size_t frames() const noexcept { return frames_; }
    [[nodiscard]] const std::vector<AtlasInfo>& atlases() const noexcept { return atlases_; }
    /// The last camera set, or nothing if none was.
    [[nodiscard]] const std::optional<CameraView>& camera() const noexcept { return camera_; }
    /// The last lighting set, or nothing if none was.
    [[nodiscard]] const std::optional<Lighting>& lighting() const noexcept { return lighting_; }
    /// The last screen effects set, or nothing if none were.
    [[nodiscard]] const std::optional<ScreenEffects>& effects() const noexcept { return effects_; }
    void clear() { commands_.clear(); }

private:
    std::vector<DrawCommand> commands_;
    std::size_t frames_ = 0;
    std::vector<AtlasInfo> atlases_;
    std::map<int, MadeImage> images_;
    std::size_t image_updates_ = 0;
    std::optional<CameraView> camera_;
    std::optional<Lighting> lighting_;
    std::optional<ScreenEffects> effects_;
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
