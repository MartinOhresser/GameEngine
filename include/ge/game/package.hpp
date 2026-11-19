#pragma once

#include "ge/game/catalog.hpp"
#include "ge/game/game.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ge::game {

/// What of a game goes into a package (an export), as opposed to what only helps to make it.

/// The modules a game needs linked: those of its manifest and, for each, those it depends on,
/// sorted. Names the catalog does not know are kept as they are (validation reports them).
[[nodiscard]] std::vector<std::string> required_modules(const Manifest& manifest,
                                                        const Catalog& catalog);

/// The files the game reads while it runs, as paths relative to the directory of the manifest
/// (with `/`), sorted and without duplicates: prefabs, scenes, texts, atlases and their
/// images, the input file, the icon and the `.wav` files of `sounds/`. Not the manifest itself
/// (see `package_manifest`), not the scenarios and not the sources of the art.
[[nodiscard]] std::vector<std::string> package_files(const LoadedGame& loaded,
                                                     const std::string& manifest_path);

/// The manifest to ship: the same game without its `scenarios` (they are tests, and their
/// files are not shipped). Returns an empty text, with `error` filled, when `text` is not JSON.
[[nodiscard]] std::string package_manifest(std::string_view text, std::string& error);

} // namespace ge::game
