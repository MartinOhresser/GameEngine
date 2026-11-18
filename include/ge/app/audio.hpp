#pragma once

#include "ge/core/hash.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ge::app {

/// Where sounds go. A window backend plays them; the two below let a game run
/// with no sound card at all. Like rendering, sound is presentation: game logic
/// decides that "coin" must be heard, a separate system asks the device.
class AudioDevice {
public:
    AudioDevice() = default;
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;
    AudioDevice(AudioDevice&&) = delete;
    AudioDevice& operator=(AudioDevice&&) = delete;
    virtual ~AudioDevice() = default;

    /// Plays the sound with this name, once. An unknown name plays nothing.
    virtual void play(std::string_view sound) = 0;
};

/// Plays nothing. The default: a game with no sound card still runs.
class NullAudio final : public AudioDevice {
public:
    void play(std::string_view /*sound*/) override {}
};

/// Plays nothing but remembers every request, so a test can check what the
/// game asked for ("the coin sound was played") without a sound card.
class RecordingAudio final : public AudioDevice {
public:
    void play(std::string_view sound) override { played_.emplace_back(sound); }

    [[nodiscard]] const std::vector<std::string>& played() const noexcept { return played_; }
    void clear() { played_.clear(); }

private:
    std::vector<std::string> played_;
};

/// The audio device of an app, stored in the world as a resource. `App`
/// installs a `NullAudio`; a window backend or a test replaces it. Not part of
/// the world fingerprint, for the same reason as `RenderBackend`.
class AudioBackend {
public:
    AudioBackend() : device_(std::make_shared<NullAudio>()) {}
    explicit AudioBackend(std::shared_ptr<AudioDevice> device) : device_(std::move(device)) {}

    [[nodiscard]] AudioDevice& device() const noexcept { return *device_; }

    void hash(Hasher& /*hasher*/) const noexcept {}

private:
    std::shared_ptr<AudioDevice> device_;
};

} // namespace ge::app
