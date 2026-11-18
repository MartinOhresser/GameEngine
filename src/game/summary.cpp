#include "ge/game/summary.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace ge::game {

std::string default_title(std::string_view name) {
    std::string title(name);
    for (char& c : title) {
        if (c == '_') {
            c = ' ';
        }
    }
    if (!title.empty() && title.front() >= 'a' && title.front() <= 'z') {
        title.front() = static_cast<char>(title.front() - 'a' + 'A');
    }
    return title;
}

std::string default_id(std::string_view name) {
    return "ge.games." + std::string(name);
}

GameSummary summarize(const LoadedGame& loaded, std::string_view directory,
                      std::string_view fallback_name) {
    const Manifest& manifest = loaded.game.manifest;
    GameSummary summary;
    summary.directory = std::string(directory);
    summary.name = manifest.name.empty() ? std::string(fallback_name) : manifest.name;
    summary.title = manifest.title.empty() ? default_title(summary.name) : manifest.title;
    summary.description = manifest.description;
    summary.version = manifest.version;
    summary.id = manifest.id.empty() ? default_id(summary.name) : manifest.id;
    summary.icon = manifest.icon_file;
    summary.window_width = manifest.window_width;
    summary.window_height = manifest.window_height;
    summary.modules = manifest.modules;
    summary.prefabs = loaded.game.prefabs.size();
    summary.scenes = loaded.game.scenes.size();
    summary.scenarios = loaded.game.scenarios.size();
    summary.valid = loaded.ok();
    for (const module::Diagnostic& problem : loaded.diagnostics) {
        summary.problems.push_back(problem.to_string());
    }
    return summary;
}

json::Value to_json(const GameSummary& summary) {
    using json::Value;
    Value window = Value::object();
    window.set("width", Value::integer(summary.window_width));
    window.set("height", Value::integer(summary.window_height));
    Value modules = Value::array();
    for (const std::string& module : summary.modules) {
        modules.push_back(Value::string(module));
    }
    Value problems = Value::array();
    for (const std::string& problem : summary.problems) {
        problems.push_back(Value::string(problem));
    }
    Value object = Value::object();
    object.set("directory", Value::string(summary.directory));
    object.set("name", Value::string(summary.name));
    object.set("title", Value::string(summary.title));
    object.set("description", Value::string(summary.description));
    object.set("version", Value::string(summary.version));
    object.set("id", Value::string(summary.id));
    object.set("icon", Value::string(summary.icon));
    object.set("window", std::move(window));
    object.set("modules", std::move(modules));
    object.set("prefabs", Value::integer(static_cast<std::int64_t>(summary.prefabs)));
    object.set("scenes", Value::integer(static_cast<std::int64_t>(summary.scenes)));
    object.set("scenarios", Value::integer(static_cast<std::int64_t>(summary.scenarios)));
    object.set("valid", Value::boolean(summary.valid));
    object.set("problems", std::move(problems));
    return object;
}

bool discover_games(const std::string& directory, const FileReader& read, const Catalog& catalog,
                    std::vector<GameSummary>& games, std::string& error) {
    namespace fs = std::filesystem;
    std::vector<fs::path> directories;
    std::error_code failure;
    for (fs::directory_iterator it(directory, failure), end; !failure && it != end;
         it.increment(failure)) {
        if (it->is_directory(failure) && fs::exists(it->path() / "game.json", failure)) {
            directories.push_back(it->path());
        }
    }
    if (failure) {
        error = directory + ": cannot read the games directory";
        return false;
    }
    std::sort(directories.begin(), directories.end());
    for (const fs::path& path : directories) {
        const LoadedGame loaded = load_game((path / "game.json").generic_string(), read, catalog);
        games.push_back(summarize(loaded, path.generic_string(), path.filename().string()));
    }
    return true;
}

} // namespace ge::game
