#include "ge/platform/sdl/sdl_backend.hpp"

#include "ge/platform/sdl/assets.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
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

    void draw(const app::DrawCommand& command) override {
        open_frame();
        if (command.atlas >= 0) {
            draw_sprite(command);
        } else {
            draw_square(command);
        }
    }

    bool load_atlas(int index, const app::AtlasInfo& atlas, std::string& error) override {
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
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST); // pixel art stays sharp
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

    void set_camera(const app::CameraView& camera) override { camera_ = camera; }

    void end_frame() override {
        open_frame(); // a frame with nothing in it is still a frame: the screen is cleared
        if (capture_) {
            capture_frame();
        }
        SDL_RenderPresent(renderer_);
        open_ = false;
        camera_.reset();
        ++presented_;
    }

    [[nodiscard]] const Frame& last_frame() const noexcept { return frame_; }
    [[nodiscard]] std::uint64_t presented() const noexcept { return presented_; }

private:
    static constexpr float default_size = 16.0F;

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
        screen_x = (world_x - camera_->x) * camera_->zoom + half_width_;
        screen_y = (world_y - camera_->y) * camera_->zoom + half_height_;
    }

    [[nodiscard]] float zoom(const app::DrawCommand& command) const {
        return camera_ && !command.screen ? camera_->zoom : 1.0F;
    }

    void draw_square(const app::DrawCommand& command) {
        const std::uint32_t c = command.color;
        SDL_SetRenderDrawColor(renderer_, static_cast<std::uint8_t>((c >> 24U) & 0xFFU),
                               static_cast<std::uint8_t>((c >> 16U) & 0xFFU),
                               static_cast<std::uint8_t>((c >> 8U) & 0xFFU),
                               static_cast<std::uint8_t>(c & 0xFFU));
        const float size = (command.size > 0.0F ? command.size : default_size) * zoom(command);
        float x = 0.0F;
        float y = 0.0F;
        to_screen(command, x, y);
        const SDL_FRect rect{x - size / 2.0F, y - size / 2.0F, size, size};
        SDL_RenderFillRect(renderer_, &rect);
    }

    void draw_sprite(const app::DrawCommand& command) {
        const auto index = static_cast<std::size_t>(command.atlas);
        if (index >= atlases_.size() || atlases_[index].texture == nullptr) {
            return; // an atlas that failed to load draws nothing
        }
        const Atlas& atlas = atlases_[index];
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
            zoom(command);
        const float height =
            (command.height > 0.0F ? command.height : static_cast<float>(atlas.cell_height)) *
            zoom(command);
        float x = 0.0F;
        float y = 0.0F;
        to_screen(command, x, y);
        const SDL_FRect target{x - width / 2.0F, y - height / 2.0F, width, height};
        const std::uint32_t c = command.color;
        SDL_SetTextureColorMod(atlas.texture, static_cast<std::uint8_t>((c >> 24U) & 0xFFU),
                               static_cast<std::uint8_t>((c >> 16U) & 0xFFU),
                               static_cast<std::uint8_t>((c >> 8U) & 0xFFU));
        SDL_SetTextureAlphaMod(atlas.texture, static_cast<std::uint8_t>(c & 0xFFU));
        SDL_RenderTextureRotated(renderer_, atlas.texture, &source, &target, 0.0, nullptr,
                                 command.flip_x ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    }

    void open_frame() {
        if (!open_) {
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
    std::optional<app::CameraView> camera_;
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
    const auto open_window = [&options, &impl]() {
        return SDL_CreateWindowAndRenderer(options.title.c_str(), options.width, options.height,
                                           SDL_WINDOW_RESIZABLE, &impl->session->window,
                                           &impl->session->renderer);
    };
    if (options.software) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
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
        SDL_SetRenderVSync(impl->session->renderer, 1);
    }
    SDL_SetRenderLogicalPresentation(impl->session->renderer, options.width, options.height,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    impl->width = options.width;
    impl->height = options.height;
    impl->renderer = std::make_shared<SdlRenderer>(impl->session, options.capture, options.width,
                                                   options.height);
    impl->audio = std::make_shared<SdlAudio>(impl->session);
    impl->start_ns = SDL_GetTicksNS();
    return std::unique_ptr<SdlBackend>(new SdlBackend(std::move(impl)));
}

bool SdlBackend::poll(std::vector<RawEvent>& events) {
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
    return true;
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

void SdlBackend::inject_quit() {
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
}

const Frame& SdlBackend::last_frame() const noexcept {
    return impl_->renderer->last_frame();
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
