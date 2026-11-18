#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ge::app {

/// A bitmap font: an atlas whose cells are letters. `glyphs` lists, in cell
/// order, the characters the cells show (UTF-8; cell n shows the nth character).
struct FontInfo {
    std::u32string glyphs;
    int cell_width = 0;
    int cell_height = 0;
};

/// What a game shows as words, as a resource of the world: the texts of the
/// language in use (index n is item n of the game's `string` list) and the
/// fonts (index n is atlas n). The game runtime fills it; text modules read it.
struct TextTable {
    std::vector<std::string> texts;
    std::vector<FontInfo> fonts;
};

/// The characters of a UTF-8 text; an invalid byte becomes U+FFFD.
[[nodiscard]] std::u32string decode_utf8(std::string_view text);

} // namespace ge::app
