#include "ge/core/path.hpp"
#include "ge/game/game.hpp"

#include "../support/mini_test.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("path: UTF-8 text is a path, and back") {
    const std::string text = "dossier \xC3\xA9/jeu.json"; // "dossier é/jeu.json"
    const fs::path path = ge::core::path_from_utf8(text);
    CHECK(path.filename() == ge::core::path_from_utf8("jeu.json"));
    CHECK(ge::core::path_to_utf8(path) == text);
    CHECK(ge::core::path_to_utf8(path.parent_path()) == "dossier \xC3\xA9");
}

TEST_CASE("path: a file in a folder with an accent can be read") {
    const std::string folder_name = "ge_path_test_dossier_\xC3\xA9";
    const fs::path folder = fs::temp_directory_path() / ge::core::path_from_utf8(folder_name);
    std::error_code error;
    fs::remove_all(folder, error);
    fs::create_directories(folder, error);
    { std::ofstream(folder / "a.txt", std::ios::binary) << "hello"; }
    const std::string file = ge::core::path_to_utf8(folder / "a.txt");
    const auto text = ge::game::disk_reader()(file);
    CHECK(text.has_value());
    CHECK(text.value_or("") == "hello");
    fs::remove_all(folder, error);
}
