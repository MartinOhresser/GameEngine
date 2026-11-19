#pragma once

// The window backend: SDL3 behind the `ge::platform::Backend` interface.
// No SDL header is included here, or anywhere outside platform/sdl: the rest of
// the engine never learns that SDL exists.

#include "ge/platform/backend.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ge::platform::sdl {

struct WindowOptions {
    std::string title = "game";
    int width = 800;
    int height = 600;
    /// No window, no sound card: SDL's "dummy" drivers. For tests and CI; the
    /// game is drawn into memory and can still be captured.
    bool headless = false;
    /// Keep a copy of every presented frame (see `SdlBackend::last_frame`).
    bool capture = false;
};

/// A presented frame, as 0xRRGGBBAA pixels, row by row.
struct Frame {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;

    /// The pixel at x, y; 0 outside the frame.
    [[nodiscard]] std::uint32_t at(int x, int y) const;
};

/// A folder where the program may keep files of its own for `game` (high
/// scores, settings), created if needed and ending with a separator: the
/// user's application-data folder. Empty when the machine has none.
[[nodiscard]] std::string user_data_directory(const std::string& game);

/// The folder the program was started from (where its executable is), ending with a
/// separator: where an exported game finds its `data/` folder. Empty when unknown.
[[nodiscard]] std::string base_directory();

/// Shows an error in a dialog box, for a program that has no console to print to (an exported
/// game started by a double-click). Does nothing when the machine cannot show one.
void show_error(const std::string& title, const std::string& message);

/// What a key is called in an input file: "key:" plus SDL's name for it in
/// lower case, spaces turned into underscores ("Left Shift" -> "key:left_shift").
[[nodiscard]] std::string key_source(std::string_view sdl_key_name);

class SdlBackend final : public Backend {
public:
    /// Opens the window. Returns null and fills `error` if SDL cannot start.
    [[nodiscard]] static std::unique_ptr<SdlBackend> create(const WindowOptions& options,
                                                            std::string& error);

    ~SdlBackend() override;
    SdlBackend(const SdlBackend&) = delete;
    SdlBackend& operator=(const SdlBackend&) = delete;
    SdlBackend(SdlBackend&&) = delete;
    SdlBackend& operator=(SdlBackend&&) = delete;

    bool poll(std::vector<RawEvent>& events) override;
    [[nodiscard]] std::chrono::nanoseconds now() override;
    [[nodiscard]] std::shared_ptr<app::Renderer> renderer() override;
    [[nodiscard]] std::shared_ptr<app::AudioDevice> audio() override;
    bool resize(int width, int height) override;
    void reset_assets() override;
    bool load_sound(const std::string& name, const std::string& path, std::string& error) override;

    /// Pushes a key press or release into SDL's event queue, as if the player
    /// had done it, so tests exercise the real event path. `key_name` is SDL's
    /// name for the key ("Right", "Space", "A"). False for an unknown key.
    bool inject_key(std::string_view key_name, bool down);

    /// Same for a mouse button: "left", "middle" or "right".
    bool inject_mouse_button(std::string_view button, bool down);

    /// Asks the window to close, as the close button does.
    void inject_quit();

    /// The last presented frame; empty unless `WindowOptions::capture` is set.
    [[nodiscard]] const Frame& last_frame() const noexcept;

    /// Writes the last presented frame as a .bmp file. False, with `error`
    /// filled, if there is none or the file cannot be written.
    bool save_frame_bmp(const std::string& path, std::string& error) const;

    /// Frames presented so far.
    [[nodiscard]] std::uint64_t frames_presented() const noexcept;

private:
    struct Impl;
    explicit SdlBackend(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace ge::platform::sdl
