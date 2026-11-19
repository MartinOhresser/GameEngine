#include "export_android.hpp"

#include "ge/core/path.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <ostream>
#include <ranges>
#include <sstream>
#include <system_error>
#include <utility>

namespace ge::tools {

namespace fs = std::filesystem;

namespace {

constexpr const char* default_abi = "arm64-v8a";

#ifdef _WIN32
constexpr const char* program_suffix = ".exe";
constexpr char path_separator = ';';
#else
constexpr const char* program_suffix = "";
constexpr char path_separator = ':';
#endif

std::optional<std::string> read_text(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool write_text(const fs::path& path, const std::string& text) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
    return static_cast<bool>(stream);
}

std::string join(const std::vector<std::string>& parts, const std::string& separator) {
    std::string text;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        text += (i == 0 ? "" : separator) + parts[i];
    }
    return text;
}

std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    for (std::size_t at = text.find(from); at != std::string::npos;
         at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

bool folder_exists(const fs::path& path) {
    std::error_code ignored;
    return fs::is_directory(path, ignored);
}

bool file_exists(const fs::path& path) {
    std::error_code ignored;
    return fs::is_regular_file(path, ignored);
}

/// The subfolders of `directory`, by name.
std::vector<fs::path> subfolders(const fs::path& directory) {
    std::vector<fs::path> found;
    std::error_code error;
    for (fs::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
        if (it->is_directory(error)) {
            found.push_back(it->path());
        }
    }
    std::ranges::sort(found);
    return found;
}

/// The program called `name` in one of the folders of the PATH.
fs::path find_in_path(const std::string& name, const Environment& env) {
    std::istringstream folders(env("PATH"));
    for (std::string folder; std::getline(folders, folder, path_separator);) {
        if (folder.empty()) {
            continue;
        }
        const fs::path candidate = core::path_from_utf8(folder) / (name + program_suffix);
        if (file_exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

fs::path find_sdk(const Environment& env) {
    std::vector<fs::path> candidates;
    for (const char* variable : {"ANDROID_HOME", "ANDROID_SDK_ROOT"}) {
        if (const std::string value = env(variable); !value.empty()) {
            candidates.push_back(core::path_from_utf8(value));
        }
    }
    if (const std::string home = env("HOME"); !home.empty()) {
        candidates.push_back(core::path_from_utf8(home) / "Android" / "Sdk");
        candidates.push_back(core::path_from_utf8(home) / "Library" / "Android" / "sdk");
    }
    if (const std::string local = env("LOCALAPPDATA"); !local.empty()) {
        candidates.push_back(core::path_from_utf8(local) / "Android" / "Sdk");
    }
    for (const fs::path& candidate : candidates) {
        if (folder_exists(candidate / "platforms")) {
            return candidate;
        }
    }
    return {};
}

/// The newest `platforms/android-N` of the SDK, N alone.
int newest_platform(const fs::path& sdk) {
    int newest = 0;
    for (const fs::path& folder : subfolders(sdk / "platforms")) {
        const std::string name = folder.filename().string();
        const std::string digits = name.starts_with("android-") ? name.substr(8) : std::string();
        const bool numeric = !digits.empty() && std::ranges::all_of(digits, [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        });
        if (numeric) {
            newest = std::max(newest, std::stoi(digits));
        }
    }
    return newest;
}

bool has_toolchain(const fs::path& ndk) {
    return file_exists(ndk / "build" / "cmake" / "android.toolchain.cmake");
}

fs::path find_ndk(const fs::path& sdk, const Environment& env) {
    for (const char* variable :
         {"ANDROID_NDK_LATEST_HOME", "ANDROID_NDK_ROOT", "ANDROID_NDK_HOME"}) {
        if (const std::string value = env(variable);
            !value.empty() && has_toolchain(core::path_from_utf8(value))) {
            return core::path_from_utf8(value);
        }
    }
    if (!sdk.empty()) {
        const std::vector<fs::path> installed = subfolders(sdk / "ndk");
        for (const fs::path& candidate : std::ranges::reverse_view(installed)) {
            if (has_toolchain(candidate)) {
                return candidate;
            }
        }
    }
    return {};
}

fs::path find_java(const Environment& env) {
    if (const std::string home = env("JAVA_HOME"); !home.empty()) {
        const fs::path candidate =
            core::path_from_utf8(home) / "bin" / (std::string("java") + program_suffix);
        if (file_exists(candidate)) {
            return candidate;
        }
    }
    return find_in_path("java", env);
}

fs::path find_ninja(const fs::path& sdk, const Environment& env) {
    if (fs::path found = find_in_path("ninja", env); !found.empty()) {
        return found;
    }
    if (!sdk.empty()) {
        const std::vector<fs::path> versions = subfolders(sdk / "cmake");
        for (const fs::path& version : std::ranges::reverse_view(versions)) {
            const fs::path candidate = version / "bin" / (std::string("ninja") + program_suffix);
            if (file_exists(candidate)) {
                return candidate;
            }
        }
    }
    return {};
}

std::string environment_variable(const std::string& name) {
#ifdef _WIN32
    // The C runtime of Windows wants its own copy (getenv is deprecated there).
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name.c_str()) != 0 || value == nullptr) {
        return {};
    }
    std::string text(value);
    std::free(value); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    return text;
#else
    // Read once, at the start of an export: nothing sets variables meanwhile.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* value = std::getenv(name.c_str());
    return value == nullptr ? std::string() : std::string(value);
#endif
}

} // namespace

Environment process_environment() {
    return environment_variable;
}

AndroidTools find_android_tools(const Environment& env) {
    AndroidTools tools;
    tools.sdk = find_sdk(env);
    if (tools.sdk.empty()) {
        tools.missing.emplace_back("the Android SDK (install Android Studio, which brings it, or "
                                   "set ANDROID_HOME to the SDK's folder)");
    } else {
        tools.platform = newest_platform(tools.sdk);
        if (tools.platform < 30) {
            tools.missing.emplace_back("an Android SDK platform (Android Studio: Settings, SDK "
                                       "Manager, SDK Platforms, tick the newest Android)");
        }
        if (subfolders(tools.sdk / "build-tools").empty()) {
            tools.missing.emplace_back("the SDK Build-Tools (SDK Manager, SDK Tools)");
        }
    }
    tools.ndk = find_ndk(tools.sdk, env);
    if (tools.ndk.empty()) {
        tools.missing.emplace_back("the NDK (SDK Manager, SDK Tools, \"NDK (Side by side)\", or "
                                   "set ANDROID_NDK_ROOT)");
    }
    tools.java = find_java(env);
    if (tools.java.empty()) {
        tools.missing.emplace_back("a JDK, version 17 or newer (Android Studio brings one: set "
                                   "JAVA_HOME to its \"jbr\" folder)");
    }
    tools.ninja = find_ninja(tools.sdk, env);
    if (tools.ninja.empty()) {
        tools.missing.emplace_back("Ninja (SDK Manager, SDK Tools, \"CMake\" brings it; or "
                                   "install it and add it to the PATH)");
    }
    return tools;
}

std::string android_application_id(const std::string& id) {
    std::vector<std::string> parts;
    std::string part;
    const auto finish = [&] {
        if (!part.empty()) {
            parts.push_back(
                std::isalpha(static_cast<unsigned char>(part.front())) != 0 ? part : "g" + part);
        }
        part.clear();
    };
    for (const char c : id) {
        if (c == '.') {
            finish();
        } else if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') {
            part += c;
        } else {
            part += '_';
        }
    }
    finish();
    if (parts.empty()) {
        parts.emplace_back("game");
    }
    if (parts.size() < 2) {
        parts.insert(parts.begin(), "ge");
    }
    return join(parts, ".");
}

std::uint32_t android_version_code(const std::string& version) {
    std::uint32_t code = 0;
    std::size_t start = 0;
    for (int part = 0; part < 3; ++part) {
        const std::size_t end = version.find('.', start);
        const std::string piece =
            version.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const bool numeric =
            !piece.empty() && piece.size() <= 2 && std::ranges::all_of(piece, [](char c) {
                return std::isdigit(static_cast<unsigned char>(c)) != 0;
            });
        if (!numeric) {
            return part == 0 ? 1U : code;
        }
        code = code * 100U + static_cast<std::uint32_t>(std::stoi(piece));
        if (end == std::string::npos) {
            for (int rest = part + 1; rest < 3; ++rest) {
                code *= 100U;
            }
            return code;
        }
        start = end + 1;
    }
    return code;
}

std::string android_orientation(std::uint32_t width, std::uint32_t height) {
    if (height > width) {
        return "portrait";
    }
    if (width > height) {
        return "sensorLandscape";
    }
    return "unspecified";
}

std::string xml_escape(const std::string& text) {
    std::string escaped;
    for (const char c : text) {
        switch (c) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        case '\'':
            escaped += "\\'"; // Android resource strings escape the apostrophe
            break;
        default:
            escaped += c;
            break;
        }
    }
    return escaped;
}

namespace {

bool copy_tree(const fs::path& from, const fs::path& to, std::ostream& err) {
    std::error_code error;
    fs::create_directories(to, error);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
    if (error) {
        err << "cannot copy " << from.string() << " to " << to.string() << ": " << error.message()
            << '\n';
        return false;
    }
    return true;
}

bool copy_one(const fs::path& from, const fs::path& to, std::ostream& err) {
    std::error_code error;
    fs::create_directories(to.parent_path(), error);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, error);
    if (error) {
        err << "cannot copy " << from.string() << " to " << to.string() << ": " << error.message()
            << '\n';
        return false;
    }
    return true;
}

class AndroidTarget final : public ExportTarget {
public:
    explicit AndroidTarget(Environment env) : env_(std::move(env)) {}

    [[nodiscard]] std::string name() const override { return "android"; }
    [[nodiscard]] std::string label() const override { return "Android (APK)"; }
    // Not offered to players until the whole path is tried on an emulator in CI (A6, A7).
    [[nodiscard]] bool hidden() const override { return true; }
    [[nodiscard]] bool buildable_here() const override { return find_android_tools(env_).ok(); }
    [[nodiscard]] std::string needs() const override {
        const AndroidTools tools = find_android_tools(env_);
        return tools.ok()
                   ? std::string()
                   : "An Android package needs, on this machine: " + join(tools.missing, "; ") +
                         ".";
    }
    [[nodiscard]] std::string
    readme_platform_notes(const std::string& /*executable*/,
                          const std::string& /*game_name*/) const override {
        return "HOW TO PLAY\n  Copy the .apk file to the phone and open it (allow the installation "
               "of\n"
               "  applications from this source if Android asks), or run: adb install FILE.apk\n";
    }

    [[nodiscard]] bool build(const ExportJob& job, std::string& program, std::ostream& out,
                             std::ostream& err) const override;
    [[nodiscard]] bool assemble(const ExportJob& job, const std::string& program,
                                std::string& artifact, std::ostream& out,
                                std::ostream& err) const override;

private:
    Environment env_;
};

bool AndroidTarget::build(const ExportJob& job, std::string& program, std::ostream& out,
                          std::ostream& err) const {
    const AndroidTools tools = find_android_tools(env_);
    if (!tools.ok()) {
        err << needs() << '\n';
        return false;
    }
    const ExportRequest& request = *job.request;
    std::vector<std::string> abis = request.abis;
    if (abis.empty()) {
        abis.emplace_back(default_abi);
    }
    const fs::path libraries = job.build_dir / "jniLibs";
    std::error_code ignored;
    fs::remove_all(libraries, ignored);
    out << "Building the program for " << join(abis, ", ")
        << " (the first time takes several minutes)...\n";
    for (const std::string& abi : abis) {
        const fs::path dir = job.build_dir / abi;
        const std::vector<std::string> configure{
            "cmake",
            "-S",
            fs::absolute(request.source_dir).generic_string(),
            "-B",
            dir.generic_string(),
            "-G",
            "Ninja",
            "-DCMAKE_MAKE_PROGRAM=" + tools.ninja.generic_string(),
            "-DCMAKE_TOOLCHAIN_FILE=" +
                (tools.ndk / "build" / "cmake" / "android.toolchain.cmake").generic_string(),
            "-DANDROID_ABI=" + abi,
            "-DANDROID_PLATFORM=android-21",
            "-DANDROID_STL=c++_static",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DGE_BUILD_PLATFORM=ON",
            "-DGE_BUILD_TESTS=OFF",
            "-DGE_BUILD_TOOLS=OFF",
            "-DGE_BUILD_EXAMPLES=OFF",
            "-DGE_WARNINGS_AS_ERRORS=OFF",
            "-DGE_PLAYER_MODULES=" + join(job.modules, ";")};
        std::vector<std::string> command = configure;
        command.insert(command.end(), request.cmake_arguments.begin(),
                       request.cmake_arguments.end());
        if (job.run(command) != 0) {
            err << "CMake could not prepare the build for " << abi << " (see above).\n";
            return false;
        }
        if (job.run({"cmake", "--build", dir.generic_string(), "--target", "ge_player"}) != 0) {
            err << "The program could not be built for " << abi << " (see above).\n";
            return false;
        }
        const fs::path library = dir / "platform" / "sdl" / "libmain.so";
        if (!file_exists(library)) {
            err << "The build succeeded but libmain.so is not in " << dir.string() << '\n';
            return false;
        }
        if (!copy_one(library, libraries / abi / "libmain.so", err)) {
            return false;
        }
    }
    program = libraries.generic_string();
    return true;
}

bool AndroidTarget::assemble(const ExportJob& job, const std::string& program,
                             std::string& artifact, std::ostream& out, std::ostream& err) const {
    const AndroidTools tools = find_android_tools(env_);
    const ExportRequest& request = *job.request;
    const fs::path template_dir =
        fs::path(request.source_dir) / "platform" / "android" / "template";
    if (!folder_exists(template_dir)) {
        err << "The Android project template is not in " << template_dir.string()
            << " (--source says where the engine's sources are).\n";
        return false;
    }
    std::vector<std::string> abis = request.abis;
    if (abis.empty()) {
        abis.emplace_back(default_abi);
    }
    // SDL's own Android project: its Java classes, icons and styles, and a Gradle wrapper.
    const fs::path sdl_project =
        job.build_dir / abis.front() / "_deps" / "sdl3-src" / "android-project";
    if (!folder_exists(sdl_project)) {
        err << "SDL's Android project is not in " << sdl_project.string() << '\n';
        return false;
    }

    const fs::path project = job.build_dir / "project";
    std::error_code ignored;
    fs::remove_all(project, ignored);
    const fs::path main = project / "app" / "src" / "main";
    if (!copy_tree(sdl_project / "gradle", project / "gradle", err) ||
        !copy_one(sdl_project / "gradlew", project / "gradlew", err) ||
        !copy_one(sdl_project / "gradlew.bat", project / "gradlew.bat", err) ||
        !copy_one(sdl_project / "gradle.properties", project / "gradle.properties", err) ||
        !copy_tree(sdl_project / "app" / "src" / "main" / "java", main / "java", err) ||
        !copy_tree(sdl_project / "app" / "src" / "main" / "res", main / "res", err) ||
        !copy_tree(template_dir, project, err) || !copy_tree(program, main / "jniLibs", err) ||
        !stage_game_files(job, main / "assets" / "data", err)) {
        return false;
    }
    fs::permissions(project / "gradlew", fs::perms::owner_exec | fs::perms::group_exec,
                    fs::perm_options::add, ignored);

    // The game's icon is used at every size: Android scales a PNG itself.
    std::string icon = "@mipmap/ic_launcher";
    if (!job.summary.icon.empty()) {
        if (!copy_one(fs::path(job.folder) / job.summary.icon,
                      main / "res" / "drawable-nodpi" / "ic_launcher.png", err)) {
            return false;
        }
        icon = "@drawable/ic_launcher";
    }

    const auto fill = [&](const fs::path& file,
                          const std::vector<std::pair<std::string, std::string>>& values) {
        std::optional<std::string> text = read_text(file);
        if (!text.has_value()) {
            err << "cannot read " << file.string() << '\n';
            return false;
        }
        for (const auto& [token, value] : values) {
            *text = replace_all(*text, token, value);
        }
        return write_text(file, *text);
    };
    const std::string version = job.summary.version.empty() ? "1.0.0" : job.summary.version;
    if (!fill(project / "app" / "build.gradle",
              {{"@APPLICATION_ID@", android_application_id(job.summary.id)},
               {"@VERSION_CODE@", std::to_string(android_version_code(version))},
               {"@VERSION_NAME@", version}}) ||
        !fill(main / "AndroidManifest.xml",
              {{"@ICON@", icon},
               {"@ORIENTATION@",
                android_orientation(job.summary.window_width, job.summary.window_height)}}) ||
        !write_text(main / "res" / "values" / "strings.xml",
                    "<resources>\n    <string name=\"app_name\">" + xml_escape(job.summary.title) +
                        "</string>\n</resources>\n") ||
        !write_text(project / "local.properties", "sdk.dir=" + tools.sdk.generic_string() + "\n")) {
        return false;
    }

    out << "Making the APK with Gradle (the first time downloads it, a few minutes)...\n";
#ifdef _WIN32
    const std::string gradlew = "gradlew.bat";
#else
    const std::string gradlew = "./gradlew";
#endif
    const std::vector<std::string> gradle{
        "cmake",        "-E",          "chdir", project.generic_string(),
        gradlew,        "--no-daemon", "-q",    "-PcompileSdk=" + std::to_string(tools.platform),
        "assembleDebug"};
    if (job.run(gradle) != 0) {
        err << "Gradle could not make the APK (see above).\n";
        return false;
    }
    const fs::path apk = project / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk";
    if (!file_exists(apk)) {
        err << "Gradle succeeded but the APK is not in " << apk.string() << '\n';
        return false;
    }
    const fs::path result = job.out_dir / (job.package + ".apk");
    if (!copy_one(apk, result, err)) {
        return false;
    }
    artifact = result.string();
    out << "Done: " << artifact << "\n"
        << "Copy this file to an Android phone and open it, or: adb install \"" << artifact
        << "\"\n";
    return true;
}

} // namespace

std::unique_ptr<ExportTarget> make_android_target(Environment env) {
    return std::make_unique<AndroidTarget>(std::move(env));
}

} // namespace ge::tools
