#include "ge/platform/sdl/sdl_backend.hpp"

#include "ge/platform/sdl/assets.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <map>
#include <optional>
#include <stb_image.h>
#include <utility>
#include <vector>

namespace ge::platform::sdl {

namespace {

/// How many fingers are followed at once (sources `touch:x` to `touch:x5`).
constexpr std::size_t max_fingers = 5;

std::string sdl_error() {
    const char* message = SDL_GetError();
    return message != nullptr && *message != '\0' ? message : "unknown SDL error";
}

std::string lower_name(std::string_view text) {
    std::string name;
    for (const char c : text) {
        if (c == ' ' || c == '-') {
            name += '_';
        } else if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') {
            name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return name;
}

std::string mouse_source(std::uint8_t button) {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return "mouse:left";
    case SDL_BUTTON_MIDDLE:
        return "mouse:middle";
    case SDL_BUTTON_RIGHT:
        return "mouse:right";
    case SDL_BUTTON_X1:
        return "mouse:x1";
    case SDL_BUTTON_X2:
        return "mouse:x2";
    default:
        return {};
    }
}

/// SDL itself, a window and its renderer. Shared by everything that uses them
/// (the backend, the renderer and the sound device the game keeps), so SDL only
/// shuts down when the last of them is gone, whatever the order they die in.
struct Session {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    ~Session() {
        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
    }
};

/// Draws the game's commands into the window, then shows the frame.
class SdlRenderer final : public app::Renderer {
public:
    SdlRenderer(std::shared_ptr<Session> session, bool capture, int view_width, int view_height)
        : session_(std::move(session)), renderer_(session_->renderer), capture_(capture),
          half_width_(static_cast<float>(view_width) / 2.0F),
          half_height_(static_cast<float>(view_height) / 2.0F) {}

    ~SdlRenderer() override {
        for (const Atlas& atlas : atlases_) {
            if (atlas.texture != nullptr) {
                SDL_DestroyTexture(atlas.texture);
            }
        }
        for (const auto& [index, image] : images_) {
            SDL_DestroyTexture(image.texture);
        }
        for (SDL_Texture* texture : {glow_, light_map_, world_, mask_, vignette_, edge_glow_, grain_}) {
            if (texture != nullptr) {
                SDL_DestroyTexture(texture);
            }
        }
        destroy_effect_targets();
    }
    SdlRenderer(const SdlRenderer&) = delete;
    SdlRenderer& operator=(const SdlRenderer&) = delete;
    SdlRenderer(SdlRenderer&&) = delete;
    SdlRenderer& operator=(SdlRenderer&&) = delete;

    /// The view the game is drawn in changed size (a new window size).
    void set_view(int view_width, int view_height) {
        half_width_ = static_cast<float>(view_width) / 2.0F;
        half_height_ = static_cast<float>(view_height) / 2.0F;
    }

    /// Commands are kept until the end of the frame: the light map has to be made from every
    /// light of the frame before the drawings it darkens.
    void draw(const app::DrawCommand& command) override { pending_.push_back(command); }

    bool load_atlas(int index, const app::AtlasInfo& atlas, std::string& error) override {
        return load_atlas_now(index, atlas, error);
    }

    void set_camera(const app::CameraView& camera) override { camera_ = camera; }
    void set_lighting(const app::Lighting& lighting) override { lighting_ = lighting; }
    void set_effects(const app::ScreenEffects& effects) override { effects_ = effects; }

    [[nodiscard]] bool makes_images() const noexcept override { return true; }

    bool set_image(int index, int width, int height, const std::vector<std::uint32_t>& pixels,
                   bool smooth) override {
        if (width <= 0 || height <= 0 ||
            pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
            return false;
        }
        Atlas& slot = images_[index];
        if (slot.texture == nullptr || slot.width != width || slot.height != height) {
            if (slot.texture != nullptr) {
                SDL_DestroyTexture(slot.texture);
            }
            // 0xRRGGBBAA as a 32-bit number is SDL's RGBA8888, whatever the byte order.
            slot.texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_STATIC, width, height);
            if (slot.texture == nullptr) {
                images_.erase(index);
                return false;
            }
            SDL_SetTextureBlendMode(slot.texture, SDL_BLENDMODE_BLEND);
            slot.width = width;
            slot.height = height;
            slot.cell_width = width;
            slot.cell_height = height;
        }
        SDL_SetTextureScaleMode(slot.texture, smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        return SDL_UpdateTexture(slot.texture, nullptr, pixels.data(), width * 4);
    }

    void drop_image(int index) override {
        if (const auto made = images_.find(index); made != images_.end()) {
            SDL_DestroyTexture(made->second.texture);
            images_.erase(made);
        }
    }

    void end_frame() override {
        open_frame(); // a frame with nothing in it is still a frame: the screen is cleared
        if (effects_.any() && make_effect_targets()) {
            // The world goes to a picture of its own, the effects lay it on the screen, then the
            // interface is drawn over them.
            base_ = scene_;
            SDL_SetRenderTarget(renderer_, scene_);
            SDL_SetRenderDrawColor(renderer_, 18, 20, 32, 255);
            SDL_RenderClear(renderer_);
            compose(Part::World);
            base_ = nullptr;
            SDL_SetRenderTarget(renderer_, nullptr);
            apply_effects();
            compose(Part::Screen);
        } else {
            compose(Part::All);
        }
        if (capture_) {
            capture_frame();
        }
        SDL_RenderPresent(renderer_);
        open_ = false;
        camera_.reset();
        lighting_ = {};
        effects_ = {};
        pending_.clear();
        ++presented_;
    }

    [[nodiscard]] const Frame& last_frame() const noexcept { return frame_; }
    [[nodiscard]] std::uint64_t presented() const noexcept { return presented_; }

private:
    /// Which drawings a pass of `compose` draws: all of them, or the world and the interface
    /// apart (with screen effects between them).
    enum class Part : std::uint8_t { All, World, Screen };

    /// Draws the frame's commands in their order. With lighting, the light drawings make the
    /// light map first; then each run of lit drawings is drawn darkened by it, between the unlit
    /// ones.
    void compose(Part part) {
        const bool lighting = part != Part::Screen && lighting_.on && make_targets();
        if (lighting) {
            draw_light_map();
        }
        const auto lit = [lighting](const app::DrawCommand& command) {
            return lighting && command.lit && !command.screen;
        };
        std::size_t index = 0;
        while (index < pending_.size()) {
            const app::DrawCommand& command = pending_[index];
            // Drawn in the other part, or a light: in the light map, or nowhere without lighting.
            const bool after = command.screen || command.over_effects;
            if ((part == Part::World && after) || (part == Part::Screen && !after) ||
                command.light) {
                ++index;
            } else if (!lit(command)) {
                draw_now(command);
                ++index;
            } else {
                std::size_t end = index;
                while (end < pending_.size() && (pending_[end].light || lit(pending_[end]))) {
                    ++end;
                }
                draw_lit(index, end);
                index = end;
            }
        }
    }

    /// The lit drawings from `first` to `last` (light drawings among them are skipped), each
    /// drawn twice on opaque pictures: their colours over black, darkened by the light map, and
    /// how much they cover what is behind them (white where they are not, black where they are
    /// opaque). On the screen, what is there is multiplied by the second and the first is added.
    /// That is the same as drawing them darkened over it, without asking the renderer to keep
    /// transparency in a picture it draws into (SDL's software renderer does not always).
    void draw_lit(std::size_t first, std::size_t last) {
        SDL_SetRenderTarget(renderer_, world_);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        for (std::size_t index = first; index < last; ++index) {
            if (!pending_[index].light) {
                draw_now(pending_[index]);
            }
        }
        SDL_RenderTexture(renderer_, light_map_, nullptr, nullptr);

        SDL_SetRenderTarget(renderer_, mask_);
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
        SDL_RenderClear(renderer_);
        for (std::size_t index = first; index < last; ++index) {
            const app::DrawCommand& command = pending_[index];
            if (command.light || command.blend == app::Blend::Add) {
                continue; // added light hides nothing behind it
            }
            if (command.blend == app::Blend::Multiply) {
                draw_now(command); // it darkens what is behind it as much as the rest
                continue;
            }
            app::DrawCommand shadow = command;
            shadow.color &= 0xFFU; // black, as opaque as the drawing
            draw_now(shadow);
        }

        SDL_SetRenderTarget(renderer_, base_);
        SDL_RenderTexture(renderer_, mask_, nullptr, nullptr);
        SDL_RenderTexture(renderer_, world_, nullptr, nullptr);
    }

    void draw_now(const app::DrawCommand& command) {
        if (command.atlas >= 0) {
            draw_sprite(command);
        } else if (command.what == "glow") {
            draw_glow(command);
        } else if (command.what == "rect") {
            draw_rect(command);
        } else if (command.what == "circle") {
            draw_circle(command);
        } else if (command.what == "line") {
            draw_line(command);
        } else {
            draw_square(command);
        }
    }

    bool load_atlas_now(int index, const app::AtlasInfo& atlas, std::string& error) {
        const std::optional<std::string> bytes = read_file(atlas.image);
        if (!bytes.has_value()) {
            error = "cannot read the image \"" + atlas.image + "\"";
            return false;
        }
        int width = 0;
        int height = 0;
        int channels = 0;
        const std::vector<stbi_uc> encoded(bytes->begin(), bytes->end());
        stbi_uc* pixels = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()),
                                                &width, &height, &channels, 4);
        if (pixels == nullptr) {
            error = "\"" + atlas.image + "\" is not a PNG image this engine can read";
            return false;
        }
        SDL_Surface* surface =
            SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels, width * 4);
        SDL_Texture* texture =
            surface != nullptr ? SDL_CreateTextureFromSurface(renderer_, surface) : nullptr;
        if (surface != nullptr) {
            SDL_DestroySurface(surface);
        }
        stbi_image_free(pixels);
        if (texture == nullptr) {
            error = "cannot use \"" + atlas.image + "\": " + sdl_error();
            return false;
        }
        // Pixel art stays sharp; painted pictures are smoothed.
        SDL_SetTextureScaleMode(texture,
                                atlas.smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        if (static_cast<std::size_t>(index) >= atlases_.size()) {
            atlases_.resize(static_cast<std::size_t>(index) + 1U);
        }
        Atlas& slot = atlases_[static_cast<std::size_t>(index)];
        if (slot.texture != nullptr) {
            SDL_DestroyTexture(slot.texture);
        }
        slot = Atlas{texture, width, height, atlas.cell_width, atlas.cell_height};
        return true;
    }

    static constexpr float default_size = 16.0F;
    /// Side of the picture of a glow, in pixels: smoothed when drawn bigger.
    static constexpr int glow_size = 256;

    struct Atlas {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
        int cell_width = 0;
        int cell_height = 0;
    };

    /// The world point seen through the camera, as pixels of the screen.
    void to_screen(const app::DrawCommand& command, float& screen_x, float& screen_y) const {
        const float world_x = command.x;
        const float world_y = command.y;
        if (!camera_ || command.screen) {
            screen_x = world_x;
            screen_y = world_y;
            return;
        }
        // A far plane (parallax below 1) follows the camera only partly.
        screen_x = (world_x - camera_->x * command.parallax_x) * camera_->zoom + half_width_;
        screen_y = (world_y - camera_->y * command.parallax_y) * camera_->zoom + half_height_;
    }

    static SDL_BlendMode blend_mode(app::Blend blend) {
        switch (blend) {
        case app::Blend::Add:
            return SDL_BLENDMODE_ADD;
        case app::Blend::Multiply:
            return SDL_BLENDMODE_MUL;
        case app::Blend::Alpha:
            break;
        }
        return SDL_BLENDMODE_BLEND;
    }

    [[nodiscard]] float zoom(const app::DrawCommand& command) const {
        return camera_ && !command.screen ? camera_->zoom : 1.0F;
    }

    void set_color(const app::DrawCommand& command) {
        const std::uint32_t c = command.color;
        SDL_SetRenderDrawColor(renderer_, static_cast<std::uint8_t>((c >> 24U) & 0xFFU),
                               static_cast<std::uint8_t>((c >> 16U) & 0xFFU),
                               static_cast<std::uint8_t>((c >> 8U) & 0xFFU),
                               static_cast<std::uint8_t>(c & 0xFFU));
    }

    void draw_square(const app::DrawCommand& command) {
        set_color(command);
        const float size = (command.size > 0.0F ? command.size : default_size) * zoom(command);
        float x = 0.0F;
        float y = 0.0F;
        to_screen(command, x, y);
        const SDL_FRect rect{x - size / 2.0F, y - size / 2.0F, size, size};
        SDL_RenderFillRect(renderer_, &rect);
    }

    /// A rectangle: filled, or four bars of `thickness` along its edges.
    void draw_rect(const app::DrawCommand& command) {
        set_color(command);
        const float width = command.width * zoom(command);
        const float height = command.height * zoom(command);
        float x = 0.0F;
        float y = 0.0F;
        to_screen(command, x, y);
        const float left = x - width / 2.0F;
        const float top = y - height / 2.0F;
        if (command.thickness <= 0.0F) {
            const SDL_FRect rect{left, top, width, height};
            SDL_RenderFillRect(renderer_, &rect);
            return;
        }
        const float edge = std::min(std::max(command.thickness * zoom(command), 1.0F),
                                    std::min(width, height) / 2.0F);
        const std::array<SDL_FRect, 4> bars{
            {{left, top, width, edge},
             {left, top + height - edge, width, edge},
             {left, top + edge, edge, height - 2.0F * edge},
             {left + width - edge, top + edge, edge, height - 2.0F * edge}}};
        SDL_RenderFillRects(renderer_, bars.data(), static_cast<int>(bars.size()));
    }

    /// A disc or a ring, drawn one pixel row at a time.
    void draw_circle(const app::DrawCommand& command) {
        set_color(command);
        const float radius = command.size * zoom(command);
        if (radius <= 0.0F) {
            return;
        }
        const float edge =
            command.thickness <= 0.0F
                ? radius
                : std::min(std::max(command.thickness * zoom(command), 1.0F), radius);
        const float inner = radius - edge;
        float x = 0.0F;
        float y = 0.0F;
        to_screen(command, x, y);
        const int rows = static_cast<int>(std::ceil(radius * 2.0F));
        for (int row = 0; row < rows; ++row) {
            const float dy = -radius + 0.5F + static_cast<float>(row);
            const float outer_half = std::sqrt(std::max(radius * radius - dy * dy, 0.0F));
            const float inner_square = inner * inner - dy * dy;
            if (inner_square <= 0.0F) {
                const SDL_FRect span{x - outer_half, y + dy - 0.5F, 2.0F * outer_half, 1.0F};
                SDL_RenderFillRect(renderer_, &span);
            } else {
                const float inner_half = std::sqrt(inner_square);
                const std::array<SDL_FRect, 2> spans{
                    {{x - outer_half, y + dy - 0.5F, outer_half - inner_half, 1.0F},
                     {x + inner_half, y + dy - 0.5F, outer_half - inner_half, 1.0F}}};
                SDL_RenderFillRects(renderer_, spans.data(), static_cast<int>(spans.size()));
            }
        }
    }

    /// A line of `thickness`, as a thin quad.
    void draw_line(const app::DrawCommand& command) {
        app::DrawCommand end = command;
        end.x = command.x2;
        end.y = command.y2;
        float x1 = 0.0F;
        float y1 = 0.0F;
        float x2 = 0.0F;
        float y2 = 0.0F;
        to_screen(command, x1, y1);
        to_screen(end, x2, y2);
        const float half = std::max(command.thickness * zoom(command), 1.0F) / 2.0F;
        const float length = std::hypot(x2 - x1, y2 - y1);
        if (length <= 0.0F) {
            return;
        }
        const float nx = -(y2 - y1) / length * half;
        const float ny = (x2 - x1) / length * half;
        const std::uint32_t c = command.color;
        const SDL_FColor color{static_cast<float>((c >> 24U) & 0xFFU) / 255.0F,
                               static_cast<float>((c >> 16U) & 0xFFU) / 255.0F,
                               static_cast<float>((c >> 8U) & 0xFFU) / 255.0F,
                               static_cast<float>(c & 0xFFU) / 255.0F};
        const std::array<SDL_Vertex, 4> corners{{{{x1 + nx, y1 + ny}, color, {0.0F, 0.0F}},
                                                 {{x1 - nx, y1 - ny}, color, {0.0F, 0.0F}},
                                                 {{x2 - nx, y2 - ny}, color, {0.0F, 0.0F}},
                                                 {{x2 + nx, y2 + ny}, color, {0.0F, 0.0F}}}};
        const std::array<int, 6> order{0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(renderer_, nullptr, corners.data(), static_cast<int>(corners.size()),
                           order.data(), static_cast<int>(order.size()));
    }

    /// The atlas of a sprite: one of the game's, or a picture made while it runs; null for none.
    [[nodiscard]] const Atlas* atlas_at(int number) const {
        if (number >= app::Renderer::first_made_image) {
            const auto made = images_.find(number);
            return made == images_.end() ? nullptr : &made->second;
        }
        const auto index = static_cast<std::size_t>(number);
        if (number < 0 || index >= atlases_.size() || atlases_[index].texture == nullptr) {
            return nullptr; // an atlas that failed to load draws nothing
        }
        return &atlases_[index];
    }

    void draw_sprite(const app::DrawCommand& command) {
        const Atlas* found = atlas_at(command.atlas);
        if (found == nullptr) {
            return;
        }
        const Atlas& atlas = *found;
        const int columns = std::max(1, atlas.width / std::max(1, atlas.cell_width));
        const int rows = std::max(1, atlas.height / std::max(1, atlas.cell_height));
        const int frame = static_cast<int>(command.frame) % (columns * rows);
        const int column = frame % columns;
        const int row = frame / columns;
        const SDL_FRect source{static_cast<float>(column * atlas.cell_width),
                               static_cast<float>(row * atlas.cell_height),
                               static_cast<float>(atlas.cell_width),
                               static_cast<float>(atlas.cell_height)};
        const float width =
            (command.width > 0.0F ? command.width : static_cast<float>(atlas.cell_width)) *
            zoom(command) * command.scale;
        const float height =
            (command.height > 0.0F ? command.height : static_cast<float>(atlas.cell_height)) *
            zoom(command) * command.scale;
        float x = 0.0F;
        float y = 0.0F;
        to_screen(command, x, y);
        SDL_FRect target{x - width / 2.0F, y - height / 2.0F, width, height};
        const std::uint32_t c = command.color;
        SDL_SetTextureColorMod(atlas.texture, static_cast<std::uint8_t>((c >> 24U) & 0xFFU),
                               static_cast<std::uint8_t>((c >> 16U) & 0xFFU),
                               static_cast<std::uint8_t>((c >> 8U) & 0xFFU));
        SDL_SetTextureAlphaMod(atlas.texture, static_cast<std::uint8_t>(c & 0xFFU));
        SDL_SetTextureBlendMode(atlas.texture, blend_mode(command.blend));
        const SDL_FlipMode flip = command.flip_x ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        const auto angle = static_cast<double>(command.angle);
        if (!command.repeat_x || width < 1.0F) {
            draw_cut(atlas.texture, source, target, angle, flip);
            return;
        }
        // An endless band: copies side by side from the left edge of the view to its right edge.
        const float view_width = half_width_ * 2.0F;
        float left = std::fmod(target.x, width);
        if (left > 0.0F) {
            left -= width;
        }
        for (target.x = left; target.x < view_width; target.x += width) {
            draw_cut(atlas.texture, source, target, angle, flip);
        }
    }

    /// Draws part of a texture, first cut to the view when it is not turned: renderers without
    /// a graphics card scale the whole target, so a sprite much bigger than the view (a backdrop)
    /// would otherwise take gigabytes and minutes.
    void draw_cut(SDL_Texture* texture, SDL_FRect source, SDL_FRect target, double angle,
                  SDL_FlipMode flip) {
        const float view_width = half_width_ * 2.0F;
        const float view_height = half_height_ * 2.0F;
        const float right = target.x + target.w;
        const float bottom = target.y + target.h;
        const bool beyond = target.x < 0.0F || target.y < 0.0F || right > view_width ||
                            bottom > view_height;
        if (angle == 0.0 && beyond && target.w > 0.0F && target.h > 0.0F) {
            const float left = std::max(target.x, 0.0F);
            const float top = std::max(target.y, 0.0F);
            const float kept_right = std::min(right, view_width);
            const float kept_bottom = std::min(bottom, view_height);
            if (kept_right <= left || kept_bottom <= top) {
                return;
            }
            const float per_x = source.w / target.w;
            const float per_y = source.h / target.h;
            float cut_left = left - target.x;
            float cut_right = right - kept_right;
            if (flip == SDL_FLIP_HORIZONTAL) {
                std::swap(cut_left, cut_right);
            }
            source.x += cut_left * per_x;
            source.w -= (cut_left + cut_right) * per_x;
            source.y += (top - target.y) * per_y;
            source.h -= (top - target.y + bottom - kept_bottom) * per_y;
            target = SDL_FRect{left, top, kept_right - left, kept_bottom - top};
        }
        SDL_RenderTextureRotated(renderer_, texture, &source, &target, angle, nullptr, flip);
    }

    /// A soft disc, from the glow picture made on first use.
    void draw_glow(const app::DrawCommand& command) {
        if (glow_ == nullptr) {
            glow_ = make_glow();
            if (glow_ == nullptr) {
                return;
            }
        }
        const float radius = command.size * zoom(command) * command.scale;
        if (radius <= 0.0F) {
            return;
        }
        float x = 0.0F;
        float y = 0.0F;
        to_screen(command, x, y);
        const std::uint32_t c = command.color;
        SDL_SetTextureColorMod(glow_, static_cast<std::uint8_t>((c >> 24U) & 0xFFU),
                               static_cast<std::uint8_t>((c >> 16U) & 0xFFU),
                               static_cast<std::uint8_t>((c >> 8U) & 0xFFU));
        SDL_SetTextureAlphaMod(glow_, static_cast<std::uint8_t>(c & 0xFFU));
        SDL_SetTextureBlendMode(glow_, blend_mode(command.blend));
        const SDL_FRect target{x - radius, y - radius, radius * 2.0F, radius * 2.0F};
        SDL_RenderTexture(renderer_, glow_, nullptr, &target);
    }

    /// White, opaque in the middle and fading smoothly to nothing at the edge of the disc.
    SDL_Texture* make_glow() {
        SDL_Surface* surface = SDL_CreateSurface(glow_size, glow_size, SDL_PIXELFORMAT_RGBA32);
        if (surface == nullptr) {
            return nullptr;
        }
        auto* bytes = static_cast<std::uint8_t*>(surface->pixels);
        const float half = static_cast<float>(glow_size) / 2.0F;
        for (int y = 0; y < glow_size; ++y) {
            std::uint8_t* row = bytes + static_cast<std::ptrdiff_t>(y) * surface->pitch;
            for (int x = 0; x < glow_size; ++x) {
                const float dx = (static_cast<float>(x) + 0.5F - half) / half;
                const float dy = (static_cast<float>(y) + 0.5F - half) / half;
                const float fade = std::max(1.0F - (dx * dx + dy * dy), 0.0F);
                std::uint8_t* pixel = row + static_cast<std::ptrdiff_t>(x) * 4;
                pixel[0] = 255U;
                pixel[1] = 255U;
                pixel[2] = 255U;
                pixel[3] = static_cast<std::uint8_t>(std::lround(fade * fade * 255.0F));
            }
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_DestroySurface(surface);
        if (texture != nullptr) {
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        }
        return texture;
    }

    /// The light map, the world picture and its mask, the size of the view. False if they
    /// cannot be made (a renderer without render targets): the frame is then drawn without
    /// lighting.
    bool make_targets() {
        const int width = static_cast<int>(std::lround(half_width_ * 2.0F));
        const int height = static_cast<int>(std::lround(half_height_ * 2.0F));
        if (light_map_ != nullptr && world_ != nullptr && mask_ != nullptr &&
            width == target_width_ && height == target_height_) {
            return true;
        }
        for (SDL_Texture** texture : {&light_map_, &world_, &mask_}) {
            if (*texture != nullptr) {
                SDL_DestroyTexture(*texture);
            }
            // The light map is soft: half the size is enough, and a quarter of the pixels to fill.
            const int divide = texture == &light_map_ ? light_map_divide : 1;
            *texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_TARGET, std::max(width / divide, 1),
                                         std::max(height / divide, 1));
        }
        if (light_map_ == nullptr || world_ == nullptr || mask_ == nullptr) {
            return false;
        }
        SDL_SetTextureScaleMode(light_map_, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(light_map_, SDL_BLENDMODE_MOD);
        SDL_SetTextureBlendMode(mask_, SDL_BLENDMODE_MOD);
        SDL_SetTextureBlendMode(world_, SDL_BLENDMODE_ADD);
        target_width_ = width;
        target_height_ = height;
        return true;
    }

    /// The ambient colour everywhere, then every light added on top.
    void draw_light_map() {
        SDL_SetRenderTarget(renderer_, light_map_);
        const std::uint32_t c = lighting_.ambient;
        SDL_SetRenderDrawColor(renderer_, static_cast<std::uint8_t>((c >> 24U) & 0xFFU),
                               static_cast<std::uint8_t>((c >> 16U) & 0xFFU),
                               static_cast<std::uint8_t>((c >> 8U) & 0xFFU), 255U);
        SDL_RenderClear(renderer_);
        constexpr float shrink = 1.0F / static_cast<float>(light_map_divide);
        SDL_SetRenderScale(renderer_, shrink, shrink);
        for (const app::DrawCommand& command : pending_) {
            if (command.light) {
                app::DrawCommand light = command;
                light.blend = app::Blend::Add;
                draw_now(light);
            }
        }
        SDL_SetRenderScale(renderer_, 1.0F, 1.0F);
        SDL_SetRenderTarget(renderer_, base_);
    }

    /// The pictures the screen effects need, the size of the view (the blur ones smaller). False
    /// if they cannot be made: the frame is then drawn without effects.
    bool make_effect_targets() {
        const int width = static_cast<int>(std::lround(half_width_ * 2.0F));
        const int height = static_cast<int>(std::lround(half_height_ * 2.0F));
        if (scene_ != nullptr && width == effect_width_ && height == effect_height_) {
            return true;
        }
        destroy_effect_targets();
        const auto make = [this](int w, int h) {
            SDL_Texture* texture =
                SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
                                  std::max(w, 1), std::max(h, 1));
            if (texture != nullptr) {
                SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
                SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
            }
            return texture;
        };
        scene_ = make(width, height);
        quarter_ = make(width / 4, height / 4);
        blur_ = make(width / 16, height / 16);
        bright_ = make(width / 16, height / 16);
        bloom_ = make(width / 4, height / 4);
        bloom_spare_ = make(width / 4, height / 4);
        if (scene_ == nullptr || quarter_ == nullptr || blur_ == nullptr || bright_ == nullptr ||
            bloom_ == nullptr || bloom_spare_ == nullptr) {
            destroy_effect_targets();
            return false;
        }
        effect_width_ = width;
        effect_height_ = height;
        return true;
    }

    void destroy_effect_targets() {
        for (SDL_Texture** texture :
             {&scene_, &quarter_, &blur_, &bright_, &bloom_, &bloom_spare_}) {
            if (*texture != nullptr) {
                SDL_DestroyTexture(*texture);
                *texture = nullptr;
            }
        }
        effect_width_ = 0;
        effect_height_ = 0;
    }

    /// Lays the picture of the world on the screen, with the effects of the frame. Each uses only
    /// the plain blend modes, so it works with every renderer, software included.
    void apply_effects() {
        SDL_RenderTexture(renderer_, scene_, nullptr, nullptr);
        if (effects_.blur > 0.0F) {
            apply_blur(std::min(effects_.blur, 1.0F));
        }
        if (effects_.bloom > 0.0F || effects_.rays > 0.0F) {
            make_bright();
        }
        if (effects_.bloom > 0.0F) {
            add_bright(std::min(effects_.bloom, 1.0F));
        }
        if (effects_.rays > 0.0F) {
            apply_rays(std::min(effects_.rays, 1.0F));
        }
        apply_grade();
        if (effects_.vignette > 0.0F) {
            apply_vignette();
        }
        if (effects_.edge_glow > 0.0F) {
            apply_edges(edge_glow_, edge_glow_made_for_, effects_.edge_glow,
                        effects_.edge_glow_size, effects_.edge_glow_color);
        }
        if (effects_.grain > 0.0F) {
            apply_grain();
        }
        if (effects_.letterbox > 0.0F) {
            const float view_width = half_width_ * 2.0F;
            const float bar = std::min(effects_.letterbox, 1.0F) * half_height_;
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
            const std::array<SDL_FRect, 2> bars{
                {{0.0F, 0.0F, view_width, bar},
                 {0.0F, (half_height_ * 2.0F) - bar, view_width, bar}}};
            SDL_RenderFillRects(renderer_, bars.data(), static_cast<int>(bars.size()));
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        }
    }

    /// `bright_` becomes the picture shrunk twice, its bright parts kept by multiplying it by
    /// itself (a dark grey cubed is nearly black, white stays white), spread out.
    void make_bright() {
        SDL_SetRenderTarget(renderer_, quarter_);
        SDL_RenderTexture(renderer_, scene_, nullptr, nullptr);
        SDL_SetRenderTarget(renderer_, blur_);
        SDL_RenderTexture(renderer_, quarter_, nullptr, nullptr);
        SDL_SetRenderTarget(renderer_, bright_);
        SDL_RenderTexture(renderer_, blur_, nullptr, nullptr);
        SDL_SetTextureBlendMode(blur_, SDL_BLENDMODE_MOD);
        SDL_RenderTexture(renderer_, blur_, nullptr, nullptr);
        SDL_RenderTexture(renderer_, blur_, nullptr, nullptr);
        SDL_SetTextureBlendMode(blur_, SDL_BLENDMODE_NONE);
        spread(bright_, blur_);
        spread(blur_, bright_);
        SDL_SetRenderTarget(renderer_, nullptr);
    }

    /// The picture softened: shrunk to a quarter and enlarged again, smoothly, over the screen
    /// (up to a strength of 0.5), then shrunk to a sixteenth, spread, and enlarged (up to 1).
    void apply_blur(float strength) {
        SDL_SetRenderTarget(renderer_, quarter_);
        SDL_RenderTexture(renderer_, scene_, nullptr, nullptr);
        SDL_SetRenderTarget(renderer_, blur_);
        SDL_RenderTexture(renderer_, quarter_, nullptr, nullptr);
        spread(blur_, bright_);
        spread(bright_, blur_);
        SDL_SetRenderTarget(renderer_, nullptr);
        const auto lay = [this](SDL_Texture* texture, float alpha) {
            if (alpha <= 0.0F) {
                return;
            }
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(texture, static_cast<std::uint8_t>(
                                                std::lround(std::min(alpha, 1.0F) * 255.0F)));
            SDL_RenderTexture(renderer_, texture, nullptr, nullptr);
            SDL_SetTextureAlphaMod(texture, 255U);
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
        };
        lay(quarter_, strength * 2.0F);
        lay(blur_, (strength * 2.0F) - 1.0F);
    }

    /// The bright parts enlarged and added over the screen: a soft glow around what is bright.
    /// Enlarged in two steps, softened between them: a sixteenth of the view laid straight on the
    /// screen shows its pixels, as blocks and creases in the glows.
    void add_bright(float strength) {
        SDL_SetRenderTarget(renderer_, bloom_);
        SDL_RenderTexture(renderer_, bright_, nullptr, nullptr);
        spread(bloom_, bloom_spare_);
        spread(bloom_spare_, bloom_);
        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_SetTextureBlendMode(bloom_, SDL_BLENDMODE_ADD);
        SDL_SetTextureAlphaMod(bloom_, static_cast<std::uint8_t>(std::lround(strength * 255.0F)));
        SDL_RenderTexture(renderer_, bloom_, nullptr, nullptr);
        SDL_SetTextureBlendMode(bloom_, SDL_BLENDMODE_NONE);
        SDL_SetTextureAlphaMod(bloom_, 255U);
    }

    /// Shafts of light: the bright parts added again and again, each copy bigger around the rays'
    /// point and fainter, so that light streams away from it.
    void apply_rays(float strength) {
        // Drawn on the quarter-sized picture (it is soft anyway), then laid once on the screen:
        // a sixteenth of the pixels to fill for each copy.
        constexpr int copies = 14;
        float width = 0.0F;
        float height = 0.0F;
        SDL_GetTextureSize(quarter_, &width, &height);
        const float cx = effects_.rays_x * width;
        const float cy = effects_.rays_y * height;
        SDL_SetRenderTarget(renderer_, quarter_);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        SDL_SetTextureBlendMode(bright_, SDL_BLENDMODE_ADD);
        for (int copy = 1; copy <= copies; copy += 1) {
            const float share = static_cast<float>(copy) / static_cast<float>(copies);
            const float grow = 1.0F + (effects_.rays_length * share);
            const float fade = (1.0F - share) * strength * 2.4F / static_cast<float>(copies);
            SDL_SetTextureAlphaMod(bright_, static_cast<std::uint8_t>(
                                                std::lround(std::min(fade, 1.0F) * 255.0F)));
            const SDL_FRect grown{cx - (cx * grow), cy - (cy * grow), width * grow,
                                  height * grow};
            SDL_RenderTexture(renderer_, bright_, nullptr, &grown);
        }
        SDL_SetTextureBlendMode(bright_, SDL_BLENDMODE_NONE);
        SDL_SetTextureAlphaMod(bright_, 255U);
        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_SetTextureBlendMode(quarter_, SDL_BLENDMODE_ADD);
        SDL_RenderTexture(renderer_, quarter_, nullptr, nullptr);
        SDL_SetTextureBlendMode(quarter_, SDL_BLENDMODE_NONE);
    }

    /// `target` becomes `source` blurred: the average of five copies, one in place and four
    /// moved by two pixels in each direction (each copy is laid over the average of the
    /// previous ones with its share of the weight).
    void spread(SDL_Texture* source, SDL_Texture* target) {
        constexpr float step = 2.0F;
        float width = 0.0F;
        float height = 0.0F;
        SDL_GetTextureSize(source, &width, &height);
        SDL_SetRenderTarget(renderer_, target);
        SDL_RenderTexture(renderer_, source, nullptr, nullptr);
        SDL_SetTextureBlendMode(source, SDL_BLENDMODE_BLEND);
        const std::array<std::array<float, 2>, 4> moves{
            {{-step, 0.0F}, {step, 0.0F}, {0.0F, -step}, {0.0F, step}}};
        float copies = 1.0F;
        for (const std::array<float, 2>& move : moves) {
            copies += 1.0F;
            SDL_SetTextureAlphaMod(source, static_cast<std::uint8_t>(std::lround(255.0F / copies)));
            const SDL_FRect moved{move[0], move[1], width, height};
            SDL_RenderTexture(renderer_, source, nullptr, &moved);
        }
        SDL_SetTextureAlphaMod(source, 255U);
        SDL_SetTextureBlendMode(source, SDL_BLENDMODE_NONE);
    }

    /// The tint multiplies the whole picture, the lift is added to it.
    void apply_grade() {
        const std::uint32_t tint = effects_.tint;
        const std::uint32_t lift = effects_.lift;
        if ((tint | 0xFFU) != 0xFFFFFFFFU) {
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_MOD);
            SDL_SetRenderDrawColor(renderer_, static_cast<std::uint8_t>((tint >> 24U) & 0xFFU),
                                   static_cast<std::uint8_t>((tint >> 16U) & 0xFFU),
                                   static_cast<std::uint8_t>((tint >> 8U) & 0xFFU), 255U);
            SDL_RenderFillRect(renderer_, nullptr);
        }
        if ((lift & 0xFFFFFF00U) != 0U) {
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
            SDL_SetRenderDrawColor(renderer_, static_cast<std::uint8_t>((lift >> 24U) & 0xFFU),
                                   static_cast<std::uint8_t>((lift >> 16U) & 0xFFU),
                                   static_cast<std::uint8_t>((lift >> 8U) & 0xFFU), 255U);
            SDL_RenderFillRect(renderer_, nullptr);
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    }

    /// Black, more opaque towards the corners, over the whole screen.
    /// Lays a vignette over the picture: the edges take `color`, `strength` (0 to 1) of it, from
    /// `size` of the way out. `texture` and `made_for` cache its picture.
    void apply_edges(SDL_Texture*& texture, float& made_for, float strength, float size,
                     std::uint32_t color) {
        if (texture == nullptr || made_for != size) {
            if (texture != nullptr) {
                SDL_DestroyTexture(texture);
            }
            texture = make_vignette(size);
            made_for = size;
        }
        if (texture == nullptr) {
            return;
        }
        SDL_SetTextureColorMod(texture, static_cast<std::uint8_t>((color >> 24U) & 0xFFU),
                               static_cast<std::uint8_t>((color >> 16U) & 0xFFU),
                               static_cast<std::uint8_t>((color >> 8U) & 0xFFU));
        SDL_SetTextureAlphaMod(texture, static_cast<std::uint8_t>(
                                            std::lround(std::min(strength, 1.0F) * 255.0F)));
        SDL_RenderTexture(renderer_, texture, nullptr, nullptr);
    }

    void apply_vignette() {
        apply_edges(vignette_, vignette_made_for_, effects_.vignette, effects_.vignette_size,
                    effects_.vignette_color);
    }

    SDL_Texture* make_vignette(float size) {
        constexpr int side = 256;
        SDL_Surface* surface = SDL_CreateSurface(side, side, SDL_PIXELFORMAT_RGBA32);
        if (surface == nullptr) {
            return nullptr;
        }
        auto* bytes = static_cast<std::uint8_t*>(surface->pixels);
        const float start = std::clamp(size, -4.0F, 0.99F);
        for (int y = 0; y < side; ++y) {
            std::uint8_t* row = bytes + static_cast<std::ptrdiff_t>(y) * surface->pitch;
            for (int x = 0; x < side; ++x) {
                // Distance from the middle, 1 at a corner.
                const float dx =
                    ((static_cast<float>(x) + 0.5F) / static_cast<float>(side) - 0.5F) * 2.0F;
                const float dy =
                    ((static_cast<float>(y) + 0.5F) / static_cast<float>(side) - 0.5F) * 2.0F;
                const float distance = std::sqrt((dx * dx + dy * dy) / 2.0F);
                const float t = std::clamp((distance - start) / (1.0F - start), 0.0F, 1.0F);
                std::uint8_t* pixel = row + static_cast<std::ptrdiff_t>(x) * 4;
                pixel[0] = 255U; // white: the colour comes from the colour mod
                pixel[1] = 255U;
                pixel[2] = 255U;
                pixel[3] =
                    static_cast<std::uint8_t>(std::lround(t * t * (3.0F - 2.0F * t) * 255.0F));
            }
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_DestroySurface(surface);
        if (texture != nullptr) {
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        }
        return texture;
    }

    /// A picture of random greys laid over the screen, moved to another place every frame.
    void apply_grain() {
        constexpr int side = 256;
        if (grain_ == nullptr) {
            SDL_Surface* surface = SDL_CreateSurface(side, side, SDL_PIXELFORMAT_RGBA32);
            if (surface == nullptr) {
                return;
            }
            auto* bytes = static_cast<std::uint8_t*>(surface->pixels);
            std::uint32_t state = 0x9E3779B9U;
            for (int y = 0; y < side; ++y) {
                std::uint8_t* row = bytes + static_cast<std::ptrdiff_t>(y) * surface->pitch;
                for (int x = 0; x < side; ++x) {
                    state ^= state << 13U;
                    state ^= state >> 17U;
                    state ^= state << 5U;
                    const auto grey = static_cast<std::uint8_t>(state >> 24U);
                    std::uint8_t* pixel = row + static_cast<std::ptrdiff_t>(x) * 4;
                    pixel[0] = grey;
                    pixel[1] = grey;
                    pixel[2] = grey;
                    pixel[3] = 255U;
                }
            }
            grain_ = SDL_CreateTextureFromSurface(renderer_, surface);
            SDL_DestroySurface(surface);
            if (grain_ == nullptr) {
                return;
            }
            SDL_SetTextureScaleMode(grain_, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(grain_, SDL_BLENDMODE_BLEND);
        }
        SDL_SetTextureAlphaMod(
            grain_, static_cast<std::uint8_t>(std::lround(std::min(effects_.grain, 1.0F) * 64.0F)));
        // A new place each frame, the same on every run.
        const auto shift_x = static_cast<int>((presented_ * 97U) % side);
        const auto shift_y = static_cast<int>((presented_ * 61U) % side);
        const auto view_width = static_cast<int>(std::lround(half_width_ * 2.0F));
        const auto view_height = static_cast<int>(std::lround(half_height_ * 2.0F));
        for (int y = -shift_y; y < view_height; y += side) {
            for (int x = -shift_x; x < view_width; x += side) {
                const SDL_FRect target{static_cast<float>(x), static_cast<float>(y),
                                       static_cast<float>(side), static_cast<float>(side)};
                SDL_RenderTexture(renderer_, grain_, nullptr, &target);
            }
        }
    }

    void open_frame() {
        if (!open_) {
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_, 18, 20, 32, 255);
            SDL_RenderClear(renderer_);
            open_ = true;
        }
    }

    void capture_frame() {
        SDL_Surface* raw = SDL_RenderReadPixels(renderer_, nullptr);
        if (raw == nullptr) {
            return;
        }
        SDL_Surface* rgba = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(raw);
        if (rgba == nullptr) {
            return;
        }
        frame_.width = rgba->w;
        frame_.height = rgba->h;
        frame_.pixels.assign(static_cast<std::size_t>(rgba->w) * static_cast<std::size_t>(rgba->h),
                             0U);
        const auto* bytes = static_cast<const std::uint8_t*>(rgba->pixels);
        for (int y = 0; y < rgba->h; ++y) {
            const std::uint8_t* row = bytes + static_cast<std::ptrdiff_t>(y) * rgba->pitch;
            for (int x = 0; x < rgba->w; ++x) {
                const std::uint8_t* p = row + static_cast<std::ptrdiff_t>(x) * 4;
                frame_.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba->w) +
                              static_cast<std::size_t>(x)] =
                    (static_cast<std::uint32_t>(p[0]) << 24U) |
                    (static_cast<std::uint32_t>(p[1]) << 16U) |
                    (static_cast<std::uint32_t>(p[2]) << 8U) | static_cast<std::uint32_t>(p[3]);
            }
        }
        SDL_DestroySurface(rgba);
    }

    std::shared_ptr<Session> session_;
    SDL_Renderer* renderer_;
    bool capture_;
    bool open_ = false;
    std::vector<Atlas> atlases_;
    std::map<int, Atlas> images_; ///< The pictures made while the game runs (set_image).
    std::optional<app::CameraView> camera_;
    app::Lighting lighting_;
    std::vector<app::DrawCommand> pending_;
    SDL_Texture* glow_ = nullptr;
    SDL_Texture* light_map_ = nullptr;
    /// The light map is this many times smaller than the view on each side.
    static constexpr int light_map_divide = 2;
    SDL_Texture* world_ = nullptr;
    SDL_Texture* mask_ = nullptr;
    app::ScreenEffects effects_;
    /// Where the world is drawn: the screen, or the picture the effects start from.
    SDL_Texture* base_ = nullptr;
    SDL_Texture* scene_ = nullptr;
    SDL_Texture* quarter_ = nullptr;
    SDL_Texture* blur_ = nullptr;
    SDL_Texture* bright_ = nullptr;
    /// The glow of the bright parts, enlarged from `bright_` to a quarter of the view and softened
    /// there, before it goes on the screen.
    SDL_Texture* bloom_ = nullptr;
    SDL_Texture* bloom_spare_ = nullptr;
    SDL_Texture* vignette_ = nullptr;
    SDL_Texture* edge_glow_ = nullptr;
    SDL_Texture* grain_ = nullptr;
    float vignette_made_for_ = -1.0F;
    float edge_glow_made_for_ = -1.0F;
    int effect_width_ = 0;
    int effect_height_ = 0;
    int target_width_ = 0;
    int target_height_ = 0;
    float half_width_;
    float half_height_;
    std::uint64_t presented_ = 0;
    Frame frame_;
};

/// Plays the .wav files it was given. Each sound has its own stream, so
/// different sounds mix; the same sound played twice queues.
class SdlAudio final : public app::AudioDevice {
public:
    explicit SdlAudio(std::shared_ptr<Session> session) : session_(std::move(session)) {}
    SdlAudio(const SdlAudio&) = delete;
    SdlAudio& operator=(const SdlAudio&) = delete;
    SdlAudio(SdlAudio&&) = delete;
    SdlAudio& operator=(SdlAudio&&) = delete;
    ~SdlAudio() override {
        for (auto& [name, sound] : sounds_) {
            SDL_DestroyAudioStream(sound.stream);
            SDL_free(sound.data);
        }
    }

    bool load(const std::string& name, const std::string& path, std::string& error) {
        Sound sound;
        if (!SDL_LoadWAV(path.c_str(), &sound.spec, &sound.data, &sound.length)) {
            error = "cannot load \"" + path + "\": " + sdl_error();
            return false;
        }
        sound.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &sound.spec,
                                                 nullptr, nullptr);
        if (sound.stream == nullptr) {
            error = "cannot open the sound device: " + sdl_error();
            SDL_free(sound.data);
            return false;
        }
        SDL_ResumeAudioStreamDevice(sound.stream);
        if (const auto old = sounds_.find(name); old != sounds_.end()) {
            SDL_DestroyAudioStream(old->second.stream);
            SDL_free(old->second.data);
            sounds_.erase(old);
        }
        sounds_.emplace(name, sound);
        return true;
    }

    void play(std::string_view sound) override {
        const auto found = sounds_.find(sound);
        if (found != sounds_.end()) {
            SDL_PutAudioStreamData(found->second.stream, found->second.data,
                                   static_cast<int>(found->second.length));
        }
    }

private:
    struct Sound {
        SDL_AudioSpec spec{};
        std::uint8_t* data = nullptr;
        std::uint32_t length = 0;
        SDL_AudioStream* stream = nullptr;
    };
    std::shared_ptr<Session> session_; // declared first: destroyed after the sounds
    std::map<std::string, Sound, std::less<>> sounds_;
};

/// The suffix of the sources of finger `slot`: "" for the first ("touch:x"), "2" for the second.
std::string finger_suffix(std::size_t slot) {
    return slot == 0 ? std::string() : std::to_string(slot + 1);
}

/// Turns a finger event into `touch:` sources, and the first finger into the mouse's (a touch
/// is a click for the games that only know the mouse). SDL's own touch-to-mouse events are off
/// (see `create`), so a touch is counted once.
void push_finger(std::vector<SDL_FingerID>& fingers, const Session& session,
                 const SDL_TouchFingerEvent& finger, std::vector<RawEvent>& events) {
    const bool ending =
        finger.type == SDL_EVENT_FINGER_UP || finger.type == SDL_EVENT_FINGER_CANCELED;
    auto slot = std::ranges::find(fingers, finger.fingerID);
    if (slot == fingers.end()) {
        if (ending) {
            return;
        }
        slot = std::ranges::find(fingers, SDL_FingerID{0});
        if (slot == fingers.end()) {
            return; // more fingers than we follow
        }
        *slot = finger.fingerID;
    }
    const auto index = static_cast<std::size_t>(slot - fingers.begin());
    int window_width = 1;
    int window_height = 1;
    SDL_GetWindowSize(session.window, &window_width, &window_height);
    float x = 0.0F;
    float y = 0.0F;
    SDL_RenderCoordinatesFromWindow(session.renderer, finger.x * static_cast<float>(window_width),
                                    finger.y * static_cast<float>(window_height), &x, &y);
    const std::string suffix = finger_suffix(index);
    events.push_back(RawEvent{"touch:x" + suffix, x});
    events.push_back(RawEvent{"touch:y" + suffix, y});
    events.push_back(RawEvent{"touch:down" + suffix, ending ? 0.0F : 1.0F});
    if (index == 0) {
        events.push_back(RawEvent{"mouse:x", x});
        events.push_back(RawEvent{"mouse:y", y});
        if (finger.type != SDL_EVENT_FINGER_MOTION) {
            events.push_back(RawEvent{"mouse:left", ending ? 0.0F : 1.0F});
        }
    }
    if (ending) {
        *slot = 0;
    }
}

} // namespace

std::uint32_t Frame::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return 0;
    }
    return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(x)];
}

std::string key_source(std::string_view sdl_key_name) {
    const std::string name = lower_name(sdl_key_name);
    return name.empty() ? std::string() : "key:" + name;
}

struct SdlBackend::Impl {
    std::shared_ptr<Session> session;
    std::shared_ptr<SdlRenderer> renderer;
    std::shared_ptr<SdlAudio> audio;
    std::uint64_t start_ns = 0;
    bool headless = false;
    bool capture = false;
    bool quit = false;
    int width = 0; ///< The view of the window (what the renderer draws in).
    int height = 0;
    /// The size the game asked for, and whether the view follows the shape of the window.
    int design_width = 0;
    int design_height = 0;
    bool adapt = false;
    bool stretched = false; ///< The view is stretched to the window (no bars), see follow_window.
    float min_aspect = 0.0F;
    float max_aspect = 0.0F;
    /// The fingers on the screen, in the order they came: slot i is `touch:down{i+1}`. 0 = free.
    std::vector<SDL_FingerID> fingers = std::vector<SDL_FingerID>(max_fingers, 0);
};

SdlBackend::SdlBackend(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

// SDL shuts down with the last of the backend, its renderer and its sound
// device, which a game may keep longer than the backend.
SdlBackend::~SdlBackend() = default;

std::string user_data_directory(const std::string& game) {
    char* path = SDL_GetPrefPath("ge", game.c_str());
    if (path == nullptr) {
        return {};
    }
    std::string directory = path;
    SDL_free(path);
    return directory;
}

std::string base_directory() {
    const char* path = SDL_GetBasePath();
    return path == nullptr ? std::string() : std::string(path);
}

void show_error(const std::string& title, const std::string& message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), message.c_str(), nullptr);
}

std::unique_ptr<SdlBackend> SdlBackend::create(const WindowOptions& options, std::string& error) {
    if (options.headless) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    }
    // A touch is reported once, as a finger (`touch:` sources) that the backend also turns into a
    // click: not SDL's own mouse events on top of it. On Android the Back button arrives as a key
    // (`key:ac_back`) instead of closing the game at once.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        error = "cannot start SDL: " + sdl_error();
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->headless = options.headless;
    impl->capture = options.capture;
    impl->session = std::make_shared<Session>(); // owns SDL from here on
    // On a phone the game fills the screen: no status bar, no navigation bar (they come back with
    // a swipe from the edge). Without it the window is a few dozen pixels shorter than the screen
    // and the bar covers the top of the game.
#if defined(__ANDROID__)
    constexpr SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN;
#else
    constexpr SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE;
#endif
    const auto open_window = [&options, &impl]() {
        return SDL_CreateWindowAndRenderer(options.title.c_str(), options.width, options.height,
                                           window_flags, &impl->session->window,
                                           &impl->session->renderer);
    };
    if (options.software) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    } else if (!options.render_driver.empty()) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, options.render_driver.c_str());
    }
    // A computer without a usable graphics card still runs the game: draw with the processor.
    bool opened = open_window();
    if (!opened && !options.software) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        opened = open_window();
    }
    if (!opened) {
        error = "cannot open a window: " + sdl_error();
        return nullptr;
    }
    if (!options.headless) {
        // Said both ways: some drivers ("gpu") wait for the screen unless told not to.
        SDL_SetRenderVSync(impl->session->renderer, options.vsync ? 1 : 0);
    }
    SDL_SetRenderLogicalPresentation(impl->session->renderer, options.width, options.height,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    impl->width = options.width;
    impl->height = options.height;
    impl->design_width = options.width;
    impl->design_height = options.height;
    impl->renderer = std::make_shared<SdlRenderer>(impl->session, options.capture, options.width,
                                                   options.height);
    impl->audio = std::make_shared<SdlAudio>(impl->session);
    impl->start_ns = SDL_GetTicksNS();
    return std::unique_ptr<SdlBackend>(new SdlBackend(std::move(impl)));
}

/// Gives the drawing area the shape of the window when the game asked for it, so that a phone
/// of any shape is filled; the window can change (a resize, a turn of the phone) at any time.
void SdlBackend::follow_window() {
    Impl& impl = *impl_;
    if (!impl.adapt) {
        return;
    }
    int pixel_width = 0;
    int pixel_height = 0;
    SDL_GetWindowSizeInPixels(impl.session->window, &pixel_width, &pixel_height);
    const auto [width, height] =
        adapted_view(impl.design_width, impl.design_height, impl.min_aspect, impl.max_aspect,
                     pixel_width, pixel_height);
    // The height is a whole number of pixels, so it is a hair off the screen's shape: inside the
    // limits the view is stretched by that hair (no bar), beyond them bars are left.
    const float aspect = static_cast<float>(pixel_width) / static_cast<float>(pixel_height);
    const bool inside = pixel_height > 0 && aspect >= impl.min_aspect && aspect <= impl.max_aspect;
    if (width == impl.width && height == impl.height && inside == impl.stretched) {
        return;
    }
    impl.stretched = inside;
    SDL_SetRenderLogicalPresentation(impl.session->renderer, width, height,
                                     inside ? SDL_LOGICAL_PRESENTATION_STRETCH
                                            : SDL_LOGICAL_PRESENTATION_LETTERBOX);
    impl.renderer->set_view(width, height);
    impl.width = width;
    impl.height = height;
}

bool SdlBackend::poll(std::vector<RawEvent>& events) {
    follow_window();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            impl_->quit = true;
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            if (event.key.repeat) {
                break;
            }
            const std::string source = key_source(SDL_GetKeyName(event.key.key));
            if (!source.empty()) {
                events.push_back(RawEvent{source, event.key.down ? 1.0F : 0.0F});
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const std::string source = mouse_source(event.button.button);
            if (!source.empty()) {
                events.push_back(RawEvent{source, event.button.down ? 1.0F : 0.0F});
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            // In the pixels of the game, not of the window (they differ when it is resized
            // or letterboxed).
            float x = event.motion.x;
            float y = event.motion.y;
            SDL_RenderCoordinatesFromWindow(impl_->session->renderer, x, y, &x, &y);
            events.push_back(RawEvent{"mouse:x", x});
            events.push_back(RawEvent{"mouse:y", y});
            break;
        }
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            push_finger(impl_->fingers, *impl_->session, event.tfinger, events);
            break;
        default:
            break;
        }
    }
    if (impl_->headless) {
        SDL_Delay(16); // no vsync without a window: pace the loop like one
    }
    return !impl_->quit;
}

std::chrono::nanoseconds SdlBackend::now() {
    return std::chrono::nanoseconds(static_cast<std::int64_t>(SDL_GetTicksNS() - impl_->start_ns));
}

std::shared_ptr<app::Renderer> SdlBackend::renderer() {
    return impl_->renderer;
}

std::shared_ptr<app::AudioDevice> SdlBackend::audio() {
    return impl_->audio;
}

bool SdlBackend::resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    SDL_SetWindowSize(impl_->session->window, width, height);
    SDL_SetRenderLogicalPresentation(impl_->session->renderer, width, height,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    impl_->renderer->set_view(width, height);
    impl_->width = width;
    impl_->height = height;
    impl_->design_width = width;
    impl_->design_height = height;
    impl_->adapt = false;
    impl_->stretched = false;
    return true;
}

void SdlBackend::adapt_view(float min_aspect, float max_aspect) {
    impl_->adapt = true;
    impl_->min_aspect = min_aspect;
    impl_->max_aspect = max_aspect;
    follow_window();
}

std::pair<int, int> SdlBackend::view_size() const {
    return impl_->adapt ? std::pair<int, int>{impl_->width, impl_->height}
                        : std::pair<int, int>{0, 0};
}

void SdlBackend::reset_assets() {
    impl_->renderer =
        std::make_shared<SdlRenderer>(impl_->session, impl_->capture, impl_->width, impl_->height);
    impl_->audio = std::make_shared<SdlAudio>(impl_->session);
}

bool SdlBackend::load_sound(const std::string& name, const std::string& path, std::string& error) {
    return impl_->audio->load(name, path, error);
}

bool SdlBackend::inject_key(std::string_view key_name, bool down) {
    const std::string name(key_name);
    const SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
    if (key == SDLK_UNKNOWN) {
        return false;
    }
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.key = key;
    event.key.down = down;
    event.key.repeat = false;
    return SDL_PushEvent(&event);
}

bool SdlBackend::inject_mouse_button(std::string_view button, bool down) {
    std::uint8_t id = 0;
    if (button == "left") {
        id = SDL_BUTTON_LEFT;
    } else if (button == "middle") {
        id = SDL_BUTTON_MIDDLE;
    } else if (button == "right") {
        id = SDL_BUTTON_RIGHT;
    } else {
        return false;
    }
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = id;
    event.button.down = down;
    return SDL_PushEvent(&event);
}

bool SdlBackend::inject_finger(std::uint64_t id, float x, float y, std::string_view phase) {
    SDL_Event event{};
    if (phase == "down") {
        event.type = SDL_EVENT_FINGER_DOWN;
    } else if (phase == "motion") {
        event.type = SDL_EVENT_FINGER_MOTION;
    } else if (phase == "up") {
        event.type = SDL_EVENT_FINGER_UP;
    } else {
        return false;
    }
    event.tfinger.touchID = 1;
    event.tfinger.fingerID = id;
    event.tfinger.x = x;
    event.tfinger.y = y;
    return SDL_PushEvent(&event);
}

bool SdlBackend::inject_window_size(int width, int height) {
    if (width <= 0 || height <= 0 || !SDL_SetWindowSize(impl_->session->window, width, height)) {
        return false;
    }
    SDL_SyncWindow(impl_->session->window);
    return true;
}

void SdlBackend::inject_quit() {
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
}

const Frame& SdlBackend::last_frame() const noexcept {
    return impl_->renderer->last_frame();
}

std::string SdlBackend::render_driver() const {
    const char* name = SDL_GetRendererName(impl_->session->renderer);
    return name == nullptr ? std::string() : std::string(name);
}

bool SdlBackend::save_frame_bmp(const std::string& path, std::string& error) const {
    const Frame& frame = impl_->renderer->last_frame();
    if (frame.pixels.empty()) {
        error = "no frame was captured (is capture on, and did a frame run?)";
        return false;
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(frame.pixels.size() * 4U);
    for (const std::uint32_t pixel : frame.pixels) {
        bytes.push_back(static_cast<std::uint8_t>((pixel >> 24U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((pixel >> 16U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((pixel >> 8U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>(pixel & 0xFFU));
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(frame.width, frame.height, SDL_PIXELFORMAT_RGBA32,
                                                 bytes.data(), frame.width * 4);
    if (surface == nullptr) {
        error = sdl_error();
        return false;
    }
    const bool saved = SDL_SaveBMP(surface, path.c_str());
    if (!saved) {
        error = "cannot write \"" + path + "\": " + sdl_error();
    }
    SDL_DestroySurface(surface);
    return saved;
}

std::uint64_t SdlBackend::frames_presented() const noexcept {
    return impl_->renderer->presented();
}

} // namespace ge::platform::sdl
