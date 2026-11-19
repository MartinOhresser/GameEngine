#include "export.hpp"

#include "ge/game/game.hpp"
#include "ge/game/package.hpp"
#include "ge/game/scenario.hpp"
#include "ge/json/json.hpp"

#include "registry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <ostream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace ge::tools {

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------------------
// Targets

class WindowsTarget final : public ExportTarget {
public:
    [[nodiscard]] std::string name() const override { return "windows"; }
    [[nodiscard]] std::string label() const override { return "Windows (64-bit)"; }
    [[nodiscard]] bool buildable_here() const override {
#ifdef _WIN32
        return true;
#else
        return false;
#endif
    }
    [[nodiscard]] std::string needs() const override {
        return "A Windows package is made on Windows, with Visual Studio (or its Build Tools) "
               "and CMake installed.";
    }
    [[nodiscard]] std::string executable_suffix() const override { return ".exe"; }
    [[nodiscard]] std::vector<std::string> configure_arguments() const override {
        // The C runtime inside the program: no Visual C++ redistributable to install.
        return {"-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"};
    }
    [[nodiscard]] std::string readme_platform_notes(const std::string& executable,
                                                    const std::string& game_name) const override {
        std::string notes;
        notes += "HOW TO PLAY\n";
        notes += "  Unzip this folder, then double-click \"" + executable + "\".\n";
        notes += "  Nothing to install. Keep the \"data\" folder next to the program.\n";
        notes += "\n";
        notes += "IF WINDOWS WARNS YOU\n";
        notes +=
            "  This program is not signed, so Windows may say \"Windows protected your PC\".\n";
        notes += "  Click \"More info\", then \"Run anyway\". Your antivirus may also check it\n";
        notes += "  the first time; the game does not touch anything outside its own folders.\n";
        notes += "\n";
        notes += "YOUR PROGRESS\n";
        notes += "  Saved games are kept in %APPDATA%\\ge\\" + game_name + "\\\n";
        return notes;
    }
};

class LinuxTarget final : public ExportTarget {
public:
    [[nodiscard]] std::string name() const override { return "linux"; }
    [[nodiscard]] std::string label() const override { return "Linux (64-bit)"; }
    [[nodiscard]] bool hidden() const override { return true; }
    [[nodiscard]] bool buildable_here() const override {
#ifdef __linux__
        return true;
#else
        return false;
#endif
    }
    [[nodiscard]] std::string needs() const override {
        return "A Linux package is made on Linux, with a C++ compiler and CMake installed.";
    }
    [[nodiscard]] std::string executable_suffix() const override { return {}; }
    [[nodiscard]] std::vector<std::string> configure_arguments() const override { return {}; }
    [[nodiscard]] std::string readme_platform_notes(const std::string& executable,
                                                    const std::string& game_name) const override {
        std::string notes;
        notes += "HOW TO PLAY\n";
        notes += "  Unzip this folder, then run \"./" + executable + "\" from it.\n";
        notes += "  Keep the \"data\" folder next to the program.\n";
        notes += "\n";
        notes += "YOUR PROGRESS\n";
        notes += "  Saved games are kept in ~/.local/share/ge/" + game_name + "/\n";
        return notes;
    }
};

// ---------------------------------------------------------------------------------------
// Files

std::optional<std::string> read_text(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool write_text(const fs::path& path, const std::string& text) {
    std::error_code ignored;
    fs::create_directories(path.parent_path(), ignored);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(stream);
}

/// The folder of a manifest path, with its separator, or empty.
std::string folder_of(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string quote_argument(const std::string& argument) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (const char c : argument) {
        if (c == '"') {
            quoted += '\\';
        }
        quoted += c;
    }
    return quoted + '"';
#else
    std::string quoted = "'";
    for (const char c : argument) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    return quoted + '\'';
#endif
}

std::string join(const std::vector<std::string>& parts, const std::string& separator) {
    std::string text;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        text += i == 0 ? parts[i] : separator + parts[i];
    }
    return text;
}

/// A key as a player reads it: "key:right" gives "Right", "mouse:left" gives "Mouse left".
std::string readable_source(const std::string& source) {
    std::string text = source.starts_with("key:") ? source.substr(4) : source;
    std::ranges::replace(text, ':', ' ');
    std::ranges::replace(text, '_', ' ');
    if (!text.empty()) {
        text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    }
    return text;
}

/// "- move x: Right, D, Left, A" for each action of the input file.
std::string controls_of(const std::string& input_text) {
    const json::ParseResult parsed = json::parse(input_text, json::relaxed);
    if (!parsed.ok() || !parsed.value.is_object()) {
        return {};
    }
    const json::Value* bindings = parsed.value.find("bindings");
    if (bindings == nullptr || !bindings->is_array()) {
        return {};
    }
    std::string text;
    for (const json::Value& binding : bindings->items()) {
        const json::Value* action = binding.find("action");
        const json::Value* sources = binding.find("sources");
        if (action == nullptr || !action->is_string() || sources == nullptr ||
            !sources->is_object()) {
            continue;
        }
        std::vector<std::string> keys;
        for (const json::Value::Member& member : sources->members()) {
            // The pointer's position is not something a player presses.
            if (member.first != "mouse:x" && member.first != "mouse:y") {
                keys.push_back(readable_source(member.first));
            }
        }
        if (keys.empty()) {
            continue;
        }
        std::string name = action->as_string();
        std::ranges::replace(name, '_', ' ');
        text += "  - " + name + ": " + join(keys, ", ") + "\n";
    }
    return text;
}

} // namespace

// ---------------------------------------------------------------------------------------
// Public helpers

std::vector<const ExportTarget*> export_targets(bool include_hidden) {
    static const WindowsTarget windows;
    static const LinuxTarget linux_target;
    std::vector<const ExportTarget*> targets{&windows};
    if (include_hidden) {
        targets.push_back(&linux_target);
    }
    return targets;
}

const ExportTarget* find_export_target(const std::string& name) {
    for (const ExportTarget* target : export_targets(true)) {
        if (target->name() == name) {
            return target;
        }
    }
    return nullptr;
}

void print_export_targets(std::ostream& out) {
    for (const ExportTarget* target : export_targets(false)) {
        out << target->name() << "  " << target->label() << "  "
            << (target->buildable_here() ? "ready" : "not on this machine") << '\n';
    }
}

std::string safe_file_name(const std::string& text) {
    std::string name;
    for (const char c : text) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' || c == '.';
        name += plain ? c : '_';
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) {
        name.pop_back();
    }
    while (!name.empty() && name.front() == ' ') {
        name.erase(name.begin());
    }
    return name.empty() ? "game" : name;
}

bool png_to_ico(const std::string& png, std::string& ico, std::string& error) {
    constexpr std::size_t header_size = 24; // signature, IHDR length and type, width, height
    constexpr std::string_view signature = "\x89PNG\r\n\x1a\n";
    if (png.size() < header_size || png.compare(0, signature.size(), signature) != 0 ||
        png.compare(12, 4, "IHDR") != 0) {
        error = "the icon is not a PNG picture";
        return false;
    }
    const auto big_endian = [&png](std::size_t at) {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            value = (value << 8U) | static_cast<unsigned char>(png[at + i]);
        }
        return value;
    };
    const std::uint32_t width = big_endian(16);
    const std::uint32_t height = big_endian(20);
    if (width == 0 || height == 0 || width > 256 || height > 256) {
        error = "the icon is " + std::to_string(width) + "x" + std::to_string(height) +
                " pixels; Windows icons are 256x256 at most";
        return false;
    }
    const auto little_endian = [](std::uint32_t value, std::size_t bytes) {
        std::string text;
        for (std::size_t i = 0; i < bytes; ++i) {
            text += static_cast<char>((value >> (8U * i)) & 0xFFU);
        }
        return text;
    };
    constexpr std::uint32_t image_offset = 6 + 16;
    ico.clear();
    ico += little_endian(0, 2);                         // reserved
    ico += little_endian(1, 2);                         // type: icon
    ico += little_endian(1, 2);                         // one picture
    ico += static_cast<char>(width == 256 ? 0 : width); // 0 stands for 256
    ico += static_cast<char>(height == 256 ? 0 : height);
    ico += little_endian(0, 2);  // no palette, reserved
    ico += little_endian(1, 2);  // planes
    ico += little_endian(32, 2); // bits per pixel
    ico += little_endian(static_cast<std::uint32_t>(png.size()), 4);
    ico += little_endian(image_offset, 4);
    ico += png;
    return true;
}

std::string make_readme(const game::GameSummary& summary, const std::string& input_text,
                        const ExportTarget& target, const std::string& executable) {
    std::string text = summary.title;
    if (!summary.version.empty()) {
        text += " " + summary.version;
    }
    text += "\n" + std::string(text.size(), '=') + "\n\n";
    if (!summary.description.empty()) {
        text += summary.description + "\n\n";
    }
    text += target.readme_platform_notes(executable, summary.name) + "\n";
    if (const std::string controls = controls_of(input_text); !controls.empty()) {
        text += "CONTROLS\n" + controls + "  - quit: Escape, or the window's close button\n\n";
    } else {
        text += "QUITTING\n  Press Escape, or use the window's close button.\n\n";
    }
    text += "ABOUT\n  Made with the Game Engine. The licenses of what this program contains are\n";
    text += "  in LICENSES.txt.\n";
    return text;
}

std::string make_licenses(const std::string& engine_license, const std::string& sdl_license) {
    const std::string line(72, '-');
    std::string text = "This program is made of the following, under these licenses.\n\n";
    text += line + "\nThe Game Engine\n" + line + "\n";
    text += engine_license.empty()
                ? "MIT License (see https://github.com/MartinOhresser/GameEngine)\n"
                : engine_license;
    text += "\n" + line + "\nSDL3 (https://libsdl.org)\n" + line + "\n";
    text +=
        sdl_license.empty() ? "zlib License (see https://libsdl.org/license.php)\n" : sdl_license;
    text += "\n" + line + "\nstb_image (https://github.com/nothings/stb)\n" + line + "\n";
    text += "Public domain, or MIT license, as its author chose.\n";
    return text;
}

std::string package_name(const game::GameSummary& summary, const ExportTarget& target) {
    std::string name = safe_file_name(summary.title);
    std::ranges::replace(name, ' ', '_');
    if (!summary.version.empty()) {
        name += "-" + safe_file_name(summary.version);
    }
    return name + "-" + target.name();
}

namespace {

/// The command as one line for the system's shell, with `suffix` (a redirection) after it.
std::string command_line(const std::vector<std::string>& command, const std::string& suffix) {
    std::string line;
    for (const std::string& argument : command) {
        line += line.empty() ? quote_argument(argument) : " " + quote_argument(argument);
    }
    line += suffix;
#ifdef _WIN32
    return "\"" + line + "\""; // cmd.exe drops the outer pair of quotes
#else
    return line;
#endif
}

} // namespace

int run_process(const std::vector<std::string>& command) {
    const std::string line = command_line(command, "");
    // gectl is a single-threaded command: nothing else touches the environment meanwhile.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    return std::system(line.c_str()) == 0 ? 0 : 1;
}

int run_process_lines(const std::vector<std::string>& command,
                      const std::function<void(const std::string&)>& on_line) {
    const std::string line = command_line(command, " 2>&1");
#ifdef _WIN32
    FILE* pipe = _popen(line.c_str(), "r");
#else
    FILE* pipe = popen(line.c_str(), "r");
#endif
    if (pipe == nullptr) {
        return 1;
    }
    std::string text;
    std::array<char, 512> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        text += buffer.data();
        if (text.ends_with('\n')) {
            text.pop_back();
            on_line(text);
            text.clear();
        }
    }
    if (!text.empty()) {
        on_line(text);
    }
#ifdef _WIN32
    const int status = _pclose(pipe);
#else
    const int status = pclose(pipe);
#endif
    return status == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------
// The export

namespace {

/// Everything the steps share.
struct Plan {
    const ExportTarget* target = nullptr;
    game::LoadedGame loaded;
    game::GameSummary summary;
    std::string folder;
    std::vector<std::string> modules;
    fs::path build_dir;
    fs::path out_dir;
    std::string executable; ///< "Doodle jump.exe"
    std::string package;    ///< "Doodle_jump-1.0.0-windows"
};

/// Replays every scenario. Returns false when one fails.
bool replay_scenarios(const Plan& plan, std::ostream& out, std::ostream& err) {
    std::size_t failed = 0;
    for (const game::ScenarioData& scenario : plan.loaded.game.scenarios) {
        const game::ScenarioReport report =
            game::run_scenario(plan.loaded.game, scenario, ge::runner::linked_modules());
        if (report.passed()) {
            continue;
        }
        ++failed;
        err << "scenario " << plan.summary.name << '/' << report.name << " fails:\n";
        for (const std::string& failure : report.failures) {
            err << "  " << failure << '\n';
        }
    }
    if (failed != 0) {
        err << "The game's tests fail, so nothing was exported (--skip-tests ignores them).\n";
        return false;
    }
    out << plan.loaded.game.scenarios.size() << " scenarios pass.\n";
    return true;
}

/// Writes the icon as an .ico file. `icon` is left empty when the game has none.
bool prepare_icon(const Plan& plan, std::string& icon, std::ostream& err) {
    icon.clear();
    if (plan.summary.icon.empty()) {
        return true;
    }
    const std::optional<std::string> png = read_text(plan.folder + plan.summary.icon);
    std::string ico;
    std::string problem;
    if (!png.has_value()) {
        err << "cannot read the icon " << plan.folder + plan.summary.icon << '\n';
        return false;
    }
    if (!png_to_ico(*png, ico, problem)) {
        err << plan.folder + plan.summary.icon << ": " << problem << '\n';
        return false;
    }
    const fs::path path = plan.build_dir / "icon.ico";
    if (!write_text(path, ico)) {
        err << "cannot write " << path.string() << '\n';
        return false;
    }
    icon = path.generic_string();
    return true;
}

/// Where the compiled player is, whichever the generator (single or multi-configuration).
std::optional<fs::path> built_player(const Plan& plan) {
    const std::string file = "ge_player" + plan.target->executable_suffix();
    for (const fs::path& candidate : {plan.build_dir / "platform" / "sdl" / file,
                                      plan.build_dir / "platform" / "sdl" / "Release" / file}) {
        std::error_code ignored;
        if (fs::is_regular_file(candidate, ignored)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<fs::path> build_player(const Plan& plan, const ExportRequest& request,
                                     const std::string& icon, const CommandRunner& run,
                                     std::ostream& out, std::ostream& err) {
    std::vector<std::string> configure{"cmake",
                                       "-S",
                                       fs::absolute(request.source_dir).generic_string(),
                                       "-B",
                                       plan.build_dir.generic_string(),
                                       "-DCMAKE_BUILD_TYPE=Release",
                                       "-DGE_BUILD_PLATFORM=ON",
                                       "-DGE_BUILD_TESTS=OFF",
                                       "-DGE_BUILD_TOOLS=OFF",
                                       "-DGE_BUILD_EXAMPLES=OFF",
                                       "-DGE_WARNINGS_AS_ERRORS=OFF",
                                       "-DGE_PLAYER_MODULES=" + join(plan.modules, ";")};
    if (!icon.empty()) {
        configure.push_back("-DGE_PLAYER_ICON=" + icon);
    }
    for (const std::string& argument : plan.target->configure_arguments()) {
        configure.push_back(argument);
    }
    for (const std::string& argument : request.cmake_arguments) {
        configure.push_back(argument);
    }
    out << "Building the program (the first time takes a few minutes)...\n";
    if (run(configure) != 0) {
        err << "CMake could not prepare the build (see above).\n";
        return std::nullopt;
    }
    const std::vector<std::string> build{"cmake",    "--build",   plan.build_dir.generic_string(),
                                         "--target", "ge_player", "--config",
                                         "Release"};
    if (run(build) != 0) {
        err << "The program could not be built (see above).\n";
        return std::nullopt;
    }
    std::optional<fs::path> player = built_player(plan);
    if (!player.has_value()) {
        err << "The build succeeded but ge_player is not in " << plan.build_dir.string() << '\n';
    }
    return player;
}

bool copy_into(const fs::path& from, const fs::path& to, std::ostream& err) {
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

/// Writes the game's files to `<package>/data`.
bool stage_data(const Plan& plan, const std::string& manifest_path, const fs::path& data,
                std::ostream& err) {
    for (const std::string& file : game::package_files(plan.loaded, manifest_path)) {
        if (!copy_into(fs::path(plan.folder) / file, data / file, err)) {
            return false;
        }
    }
    const std::optional<std::string> manifest = read_text(manifest_path);
    std::string problem;
    const std::string shipped =
        manifest.has_value() ? game::package_manifest(*manifest, problem) : std::string();
    if (shipped.empty()) {
        err << manifest_path << ": " << (problem.empty() ? "cannot be read" : problem) << '\n';
        return false;
    }
    return write_text(data / "game.json", shipped);
}

bool stage_package(const Plan& plan, const ExportRequest& request, const fs::path& player,
                   const fs::path& package_dir, std::ostream& err) {
    std::error_code error;
    fs::remove_all(package_dir, error);
    fs::create_directories(package_dir, error);
    if (error) {
        err << "cannot create " << package_dir.string() << ": " << error.message() << '\n';
        return false;
    }
    if (!copy_into(player, package_dir / plan.executable, err) ||
        !stage_data(plan, request.manifest, package_dir / "data", err)) {
        return false;
    }
    const std::string input_text =
        plan.loaded.game.manifest.input_file.empty()
            ? std::string()
            : read_text(plan.folder + plan.loaded.game.manifest.input_file).value_or("");
    const fs::path source = request.source_dir;
    const std::string engine_license = read_text(source / "LICENSE").value_or("");
    const std::string sdl_license =
        read_text(plan.build_dir / "_deps" / "sdl3-src" / "LICENSE.txt").value_or("");
    return write_text(package_dir / "README.txt",
                      make_readme(plan.summary, input_text, *plan.target, plan.executable)) &&
           write_text(package_dir / "LICENSES.txt", make_licenses(engine_license, sdl_license));
}

} // namespace

int export_game(const ExportRequest& request, std::ostream& out, std::ostream& err,
                const CommandRunner& run) {
    Plan plan;
    plan.target = find_export_target(request.target);
    if (plan.target == nullptr) {
        err << "unknown target \"" << request.target << "\"; the targets are:\n";
        for (const ExportTarget* target : export_targets(false)) {
            err << "  " << target->name() << "  " << target->label() << '\n';
        }
        return 1;
    }
    if (!plan.target->buildable_here()) {
        err << plan.target->label() << " cannot be made on this machine. " << plan.target->needs()
            << '\n';
        return 1;
    }

    plan.loaded = game::load_game(request.manifest, game::disk_reader(),
                                  game::catalog_of(ge::runner::linked_modules()));
    for (const module::Diagnostic& problem : plan.loaded.diagnostics) {
        err << problem.to_string() << '\n';
    }
    if (!plan.loaded.ok()) {
        return 1;
    }
    plan.folder = folder_of(request.manifest);
    plan.summary = game::summarize(plan.loaded, plan.folder, plan.loaded.game.manifest.name);
    plan.modules = game::required_modules(plan.loaded.game.manifest,
                                          game::catalog_of(ge::runner::linked_modules()));
    plan.executable = safe_file_name(plan.summary.title) + plan.target->executable_suffix();
    plan.package = package_name(plan.summary, *plan.target);
    plan.out_dir = fs::absolute(request.out_dir);
    plan.build_dir = request.build_dir.empty()
                         ? fs::absolute(fs::path(request.source_dir) / "build" / "export" /
                                        plan.target->name() / plan.summary.name)
                         : fs::absolute(request.build_dir);

    out << "Exporting " << plan.summary.title << " for " << plan.target->label() << " ("
        << join(plan.modules, ", ") << ")\n";
    if (!request.skip_tests && !replay_scenarios(plan, out, err)) {
        return 1;
    }
    std::string icon;
    if (!prepare_icon(plan, icon, err)) {
        return 1;
    }
    const std::optional<fs::path> player = build_player(plan, request, icon, run, out, err);
    if (!player.has_value()) {
        return 1;
    }
    const fs::path package_dir = plan.out_dir / plan.package;
    if (!stage_package(plan, request, *player, package_dir, err)) {
        return 1;
    }
    const std::string zip = plan.package + ".zip";
    std::error_code ignored;
    fs::remove(plan.out_dir / zip, ignored);
    const std::vector<std::string> pack{
        "cmake", "-E",           "chdir", plan.out_dir.generic_string(),
        "cmake", "-E",           "tar",   "cf",
        zip,     "--format=zip", "--",    plan.package};
    if (run(pack) != 0) {
        err << "The zip could not be made (see above); the folder " << package_dir.string()
            << " is complete.\n";
        return 1;
    }
    out << "Done: " << (plan.out_dir / zip).string() << "\n"
        << "Send this zip: it runs on its own, with nothing to install.\n";
    return 0;
}

} // namespace ge::tools
