#include "ge/game/package.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <utility>

namespace ge::game {

namespace {

/// The folder of a path, with its separator ("games/x/game.json" gives "games/x/").
std::string folder_of(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

/// The `.wav` files of `<folder>/sounds`, relative to the folder.
std::vector<std::string> sound_files(const std::string& folder) {
    namespace fs = std::filesystem;
    std::vector<std::string> found;
    std::error_code ignored;
    const fs::path directory = fs::path(folder.empty() ? "." : folder) / "sounds";
    if (!fs::is_directory(directory, ignored)) {
        return found;
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, ignored)) {
        if (entry.is_regular_file(ignored) && entry.path().extension() == ".wav") {
            found.push_back("sounds/" + entry.path().filename().generic_string());
        }
    }
    return found;
}

} // namespace

std::vector<std::string> required_modules(const Manifest& manifest, const Catalog& catalog) {
    std::set<std::string> needed;
    std::vector<std::string> pending = manifest.modules;
    while (!pending.empty()) {
        const std::string name = std::move(pending.back());
        pending.pop_back();
        if (!needed.insert(name).second) {
            continue;
        }
        if (const ModuleDef* module = catalog.find_module(name); module != nullptr) {
            pending.insert(pending.end(), module->depends.begin(), module->depends.end());
        }
    }
    return {needed.begin(), needed.end()};
}

std::vector<std::string> package_files(const LoadedGame& loaded, const std::string& manifest_path) {
    const std::string folder = folder_of(manifest_path);
    const Manifest& manifest = loaded.game.manifest;
    std::set<std::string> files;
    const auto add_all = [&files](const std::vector<std::string>& paths) {
        files.insert(paths.begin(), paths.end());
    };
    add_all(manifest.prefab_files);
    add_all(manifest.scene_files);
    add_all(manifest.strings_files);
    add_all(manifest.atlas_files);
    for (const AtlasData& atlas : loaded.game.atlases) {
        // `image` is the path the reader understands: the folder, then the path in it.
        files.insert(atlas.image.starts_with(folder) ? atlas.image.substr(folder.size())
                                                     : atlas.image);
    }
    for (const std::string& path : {manifest.input_file, manifest.icon_file}) {
        if (!path.empty()) {
            files.insert(path);
        }
    }
    add_all(sound_files(folder));
    return {files.begin(), files.end()};
}

std::string package_manifest(std::string_view text, std::string& error) {
    const json::ParseResult parsed = json::parse(text, json::relaxed);
    if (!parsed.ok() || !parsed.value.is_object()) {
        error = "game.json is not a JSON object";
        return {};
    }
    json::Value shipped = json::Value::object();
    for (const auto& member : parsed.value.members()) {
        if (member.first != "scenarios") {
            shipped.set(member.first, member.second);
        }
    }
    return json::dump(shipped) + "\n";
}

} // namespace ge::game
