#pragma once

// The Android target of `gectl export` (ADR 0012): the game's modules and SDL become libmain.so
// with the NDK, then Gradle puts it, SDL's Java classes and the game's files in an APK.

#include "export.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ge::tools {

/// The value of an environment variable, empty when it is not set. Tests give their own.
using Environment = std::function<std::string(const std::string& name)>;

/// The environment of this process.
[[nodiscard]] Environment process_environment();

/// What an Android package needs on the machine that makes it.
struct AndroidTools {
    std::filesystem::path sdk;
    std::filesystem::path ndk;
    std::filesystem::path ninja;
    std::filesystem::path java;
    /// The newest platform installed in the SDK (35 for `platforms/android-35`), 0 when none.
    int platform = 0;
    /// One sentence for each thing that is missing, saying where to get it.
    std::vector<std::string> missing;

    [[nodiscard]] bool ok() const { return missing.empty(); }
};

[[nodiscard]] AndroidTools find_android_tools(const Environment& env);

/// An application identifier Android accepts, from the manifest's `id`: letters, digits and `_`
/// in at least two parts that each start with a letter ("ge.games.doodle_jump").
[[nodiscard]] std::string android_application_id(const std::string& id);

/// 1.2.3 gives 10203: the number Android compares to know which version is newer. 1 when the
/// version is not made of numbers.
[[nodiscard]] std::uint32_t android_version_code(const std::string& version);

/// The screen orientation the window's shape asks for: "portrait", "sensorLandscape" or
/// "unspecified".
[[nodiscard]] std::string android_orientation(std::uint32_t width, std::uint32_t height);

/// Text that can sit in an XML file.
[[nodiscard]] std::string xml_escape(const std::string& text);

/// The Android target, finding its tools through `env`.
[[nodiscard]] std::unique_ptr<ExportTarget> make_android_target(Environment env);

} // namespace ge::tools
