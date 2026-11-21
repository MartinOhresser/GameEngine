#include "export.hpp"

#include "ge/core/path.hpp"
#include "ge/game/game.hpp"
#include "ge/game/package.hpp"
#include "ge/game/scenario.hpp"
#include "ge/json/json.hpp"

#include "export_android.hpp"
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

/// What the desktop systems have in common: CMake compiles `ge_player` for the game's modules,
/// and the program, the game's files, a README and the licenses become a zip.
class DesktopTarget : public ExportTarget {
public:
    [[nodiscard]] bool build(const ExportJob& job, std::string& program, std::ostream& out,
                             std::ostream& err) const override;
    [[nodiscard]] bool assemble(const ExportJob& job, const std::string& program,
                                std::string& artifact, std::ostream& out,
                                std::ostream& err) const override;
};

class WindowsTarget final : public DesktopTarget {
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
        // The C runtime inside the program: no Visual C++ redistributable to install. With the
        // Visual Studio generator, MSBuild compiles the files of a project one by one unless it
        // is told to use every core (SDL alone is several hundred files).
        return {"-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
                "-DCMAKE_VS_GLOBALS=UseMultiToolTask=true;EnforceProcessCountAcrossBuilds=true"};
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

class LinuxTarget final : public DesktopTarget {
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
    static const std::unique_ptr<ExportTarget> android = make_android_target(process_environment());
    std::vector<const ExportTarget*> targets{&windows};
    targets.push_back(android.get());
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
        const bool ready = target->buildable_here();
        out << target->name() << "  " << target->label() << "  "
            << (ready ? "ready" : "not on this machine") << '\n';
        if (!ready) {
            out << "    " << target->needs() << '\n';
        }
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
// The steps

namespace {

/// Replays every scenario. Returns false when one fails.
bool replay_scenarios(const ExportJob& job, std::ostream& out, std::ostream& err) {
    std::size_t failed = 0;
    for (const game::ScenarioData& scenario : job.loaded.game.scenarios) {
        const game::ScenarioReport report =
            game::run_scenario(job.loaded.game, scenario, ge::runner::linked_modules());
        if (report.passed()) {
            continue;
        }
        ++failed;
        err << "scenario " << job.summary.name << '/' << report.name << " fails:\n";
        for (const std::string& failure : report.failures) {
            err << "  " << failure << '\n';
        }
    }
    if (failed != 0) {
        err << "The game's tests fail, so nothing was exported (--skip-tests ignores them).\n";
        return false;
    }
    out << job.loaded.game.scenarios.size() << " scenarios pass.\n";
    return true;
}

/// Writes the icon as an .ico file. `icon` is left empty when the game has none.
bool prepare_icon(const ExportJob& job, std::string& icon, std::ostream& err) {
    icon.clear();
    if (job.summary.icon.empty()) {
        return true;
    }
    const std::optional<std::string> png = read_text(job.folder + job.summary.icon);
    std::string ico;
    std::string problem;
    if (!png.has_value()) {
        err << "cannot read the icon " << job.folder + job.summary.icon << '\n';
        return false;
    }
    if (!png_to_ico(*png, ico, problem)) {
        err << job.folder + job.summary.icon << ": " << problem << '\n';
        return false;
    }
    const fs::path path = job.build_dir / "icon.ico";
    if (!write_text(path, ico)) {
        err << "cannot write " << path.string() << '\n';
        return false;
    }
    icon = path.generic_string();
    return true;
}

/// Where the compiled player is, whichever the generator (single or multi-configuration).
std::optional<fs::path> built_player(const ExportJob& job, const ExportTarget& target) {
    const std::string file = "ge_player" + target.executable_suffix();
    for (const fs::path& candidate : {job.build_dir / "platform" / "sdl" / file,
                                      job.build_dir / "platform" / "sdl" / "Release" / file}) {
        std::error_code ignored;
        if (fs::is_regular_file(candidate, ignored)) {
            return candidate;
        }
    }
    return std::nullopt;
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

/// Gathers the package's folder: the program, the game's files, the README, the licenses.
bool stage_package(const ExportJob& job, const ExportTarget& target, const fs::path& player,
                   const std::string& executable, const fs::path& package_dir, std::ostream& err) {
    std::error_code error;
    fs::remove_all(package_dir, error);
    fs::create_directories(package_dir, error);
    if (error) {
        err << "cannot create " << package_dir.string() << ": " << error.message() << '\n';
        return false;
    }
    if (!copy_into(player, package_dir / executable, err) ||
        !stage_game_files(job, package_dir / "data", err)) {
        return false;
    }
    const std::string input_text =
        job.loaded.game.manifest.input_file.empty()
            ? std::string()
            : read_text(job.folder + job.loaded.game.manifest.input_file).value_or("");
    const fs::path source = job.request->source_dir;
    const std::string engine_license = read_text(source / "LICENSE").value_or("");
    const std::string sdl_license =
        read_text(job.build_dir / "_deps" / "sdl3-src" / "LICENSE.txt").value_or("");
    return write_text(package_dir / "README.txt",
                      make_readme(job.summary, input_text, target, executable)) &&
           write_text(package_dir / "LICENSES.txt", make_licenses(engine_license, sdl_license));
}

/// Writes `files.txt`, the list of everything in `data_dir` (one path a line, `/` separators): an
/// APK has no folders to walk, so the program reads the list (the same list on every system).
bool write_file_index(const fs::path& data_dir, std::ostream& err) {
    std::vector<std::string> files;
    std::error_code error;
    for (fs::recursive_directory_iterator it(data_dir, error), end; !error && it != end;
         it.increment(error)) {
        if (it->is_regular_file(error)) {
            files.push_back(core::path_to_utf8(it->path().lexically_relative(data_dir)));
        }
    }
    std::ranges::sort(files);
    std::string text;
    for (const std::string& file : files) {
        text += file + '\n';
    }
    if (!write_text(data_dir / "files.txt", text)) {
        err << "cannot write " << (data_dir / "files.txt").string() << '\n';
        return false;
    }
    return true;
}

} // namespace

bool stage_game_files(const ExportJob& job, const fs::path& data_dir, std::ostream& err) {
    const std::string& manifest_path = job.request->manifest;
    for (const std::string& file : game::package_files(job.loaded, manifest_path)) {
        if (!copy_into(fs::path(job.folder) / file, data_dir / file, err)) {
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
    if (!write_text(data_dir / "game.json", shipped)) {
        return false;
    }
    return write_file_index(data_dir, err);
}

// ---------------------------------------------------------------------------------------
// The desktop steps

bool DesktopTarget::build(const ExportJob& job, std::string& program, std::ostream& out,
                          std::ostream& err) const {
    std::string icon;
    if (!prepare_icon(job, icon, err)) {
        return false;
    }
    const ExportRequest& request = *job.request;
    std::vector<std::string> configure{"cmake",
                                       "-S",
                                       fs::absolute(request.source_dir).generic_string(),
                                       "-B",
                                       job.build_dir.generic_string(),
                                       "-DCMAKE_BUILD_TYPE=Release",
                                       "-DGE_BUILD_PLATFORM=ON",
                                       "-DGE_BUILD_TESTS=OFF",
                                       "-DGE_BUILD_TOOLS=OFF",
                                       "-DGE_BUILD_EXAMPLES=OFF",
                                       "-DGE_WARNINGS_AS_ERRORS=OFF",
                                       "-DGE_PLAYER_MODULES=" + join(job.modules, ";")};
    if (!icon.empty()) {
        configure.push_back("-DGE_PLAYER_ICON=" + icon);
    }
    for (const std::string& argument : configure_arguments()) {
        configure.push_back(argument);
    }
    for (const std::string& argument : request.cmake_arguments) {
        configure.push_back(argument);
    }
    out << "Building the program (the first time takes a few minutes)...\n";
    if (job.run(configure) != 0) {
        err << "CMake could not prepare the build (see above).\n";
        return false;
    }
    const std::vector<std::string> build_command{
        "cmake",   "--build",   job.build_dir.generic_string(), "--target", "ge_player", "--config",
        "Release", "--parallel"};
    if (job.run(build_command) != 0) {
        err << "The program could not be built (see above).\n";
        return false;
    }
    const std::optional<fs::path> player = built_player(job, *this);
    if (!player.has_value()) {
        err << "The build succeeded but ge_player is not in " << job.build_dir.string() << '\n';
        return false;
    }
    program = player->generic_string();
    return true;
}

bool DesktopTarget::assemble(const ExportJob& job, const std::string& program,
                             std::string& artifact, std::ostream& out, std::ostream& err) const {
    const std::string executable = safe_file_name(job.summary.title) + executable_suffix();
    const fs::path package_dir = job.out_dir / job.package;
    if (!stage_package(job, *this, program, executable, package_dir, err)) {
        return false;
    }
    const std::string zip = job.package + ".zip";
    std::error_code ignored;
    fs::remove(job.out_dir / zip, ignored);
    const std::vector<std::string> pack{
        "cmake", "-E",           "chdir", job.out_dir.generic_string(),
        "cmake", "-E",           "tar",   "cf",
        zip,     "--format=zip", "--",    job.package};
    if (job.run(pack) != 0) {
        err << "The zip could not be made (see above); the folder " << package_dir.string()
            << " is complete.\n";
        return false;
    }
    artifact = (job.out_dir / zip).string();
    out << "Done: " << artifact << "\n"
        << "Send this zip: it runs on its own, with nothing to install.\n";
    return true;
}

// ---------------------------------------------------------------------------------------
// The export

bool prepare_export_job(const ExportRequest& request, const ExportTarget& target,
                        const CommandRunner& run, ExportJob& job, std::ostream& err) {
    job = ExportJob();
    job.request = &request;
    job.run = run;
    job.loaded = game::load_game(request.manifest, game::disk_reader(),
                                 game::catalog_of(ge::runner::linked_modules()));
    for (const module::Diagnostic& problem : job.loaded.diagnostics) {
        err << problem.to_string() << '\n';
    }
    if (!job.loaded.ok()) {
        return false;
    }
    job.folder = folder_of(request.manifest);
    job.summary = game::summarize(job.loaded, job.folder, job.loaded.game.manifest.name);
    job.modules = game::required_modules(job.loaded.game.manifest,
                                         game::catalog_of(ge::runner::linked_modules()));
    job.package = package_name(job.summary, target);
    job.out_dir = fs::absolute(request.out_dir);
    job.build_dir = request.build_dir.empty()
                        ? fs::absolute(fs::path(request.source_dir) / "build" / "export" /
                                       target.name() / job.summary.name)
                        : fs::absolute(request.build_dir);

    return true;
}

int export_game(const ExportRequest& request, std::ostream& out, std::ostream& err,
                const CommandRunner& run) {
    const ExportTarget* target = find_export_target(request.target);
    if (target == nullptr) {
        err << "unknown target \"" << request.target << "\"; the targets are:\n";
        for (const ExportTarget* known : export_targets(false)) {
            err << "  " << known->name() << "  " << known->label() << '\n';
        }
        return 1;
    }
    if (!target->buildable_here()) {
        err << target->label() << " cannot be made on this machine. " << target->needs() << '\n';
        return 1;
    }

    ExportJob job;
    if (!prepare_export_job(request, *target, run, job, err)) {
        return 1;
    }

    out << "Exporting " << job.summary.title << " for " << target->label() << " ("
        << join(job.modules, ", ") << ")\n";
    if (!request.skip_tests && !replay_scenarios(job, out, err)) {
        return 1;
    }
    std::string program;
    std::string artifact;
    if (!target->build(job, program, out, err) ||
        !target->assemble(job, program, artifact, out, err)) {
        return 1;
    }
    return 0;
}

} // namespace ge::tools
