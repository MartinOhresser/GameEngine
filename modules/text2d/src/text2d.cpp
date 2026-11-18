#include "ge/app/render.hpp"
#include "ge/app/text.hpp"
#include "ge/mod/text2d/text2d.gen.hpp"
#include "ge/mod/transform2d/transform2d.gen.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ge::mod::text2d {

namespace {

std::uint32_t channel(std::uint32_t value) {
    return std::min<std::uint32_t>(value, 255U);
}

/// 0xRRGGBBAA.
std::uint32_t pack(const Text& text) {
    return (channel(text.red) << 24U) | (channel(text.green) << 16U) | (channel(text.blue) << 8U) |
           channel(text.alpha);
}

/// A number as it reads in a text: 12, not 12.000000; 2.5 with at most two decimals.
std::string format_number(float value) {
    std::array<char, 48> buffer{};
    const bool whole = value == std::floor(value) && std::fabs(value) < 1.0e9F;
    const auto done = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                    std::chars_format::fixed, whole ? 0 : 2);
    return {buffer.data(), done.ptr};
}

/// The text with its {value} replaced by the number.
std::string with_value(const std::string& source, float value) {
    constexpr std::string_view marker = "{value}";
    std::string out;
    std::size_t from = 0;
    while (true) {
        const std::size_t at = source.find(marker, from);
        if (at == std::string::npos) {
            out += source.substr(from);
            return out;
        }
        out.append(source, from, at - from);
        out += format_number(value);
        from = at + marker.size();
    }
}

/// The lines of a text, split at newlines, as characters.
std::vector<std::u32string> lines_of(const std::u32string& characters) {
    std::vector<std::u32string> lines(1);
    for (const char32_t character : characters) {
        if (character == U'\n') {
            lines.emplace_back();
        } else {
            lines.back().push_back(character);
        }
    }
    return lines;
}

/// The cell of a character in the font, or -1 when the font has no such letter.
int glyph_index(const app::FontInfo& font, char32_t character) {
    const std::size_t at = font.glyphs.find(character);
    return at == std::u32string::npos ? -1 : static_cast<int>(at);
}

void draw_text(app::Renderer& renderer, const app::TextTable& table, const Text& text,
               const transform2d::Position& position) {
    if (text.string >= table.texts.size() || text.font >= table.fonts.size()) {
        return;
    }
    const app::FontInfo& font = table.fonts[text.font];
    if (font.cell_height <= 0 || font.cell_width <= 0) {
        return;
    }
    const float scale = text.size > 0.0F ? text.size / static_cast<float>(font.cell_height) : 1.0F;
    const float glyph_width = static_cast<float>(font.cell_width) * scale;
    const float glyph_height = static_cast<float>(font.cell_height) * scale;
    const float advance = glyph_width + text.spacing;
    const float line_height = glyph_height + text.line_spacing;

    const std::vector<std::u32string> lines =
        lines_of(app::decode_utf8(with_value(table.texts[text.string], text.value)));
    float widest = 0.0F;
    for (const std::u32string& line : lines) {
        widest = std::max(widest, static_cast<float>(line.size()) * advance - text.spacing);
    }
    const float height = static_cast<float>(lines.size()) * line_height - text.line_spacing;
    const float left = position.x - text.pivot_x * widest;
    const float top = position.y - text.pivot_y * height;

    for (std::size_t row = 0; row < lines.size(); ++row) {
        // Each line is placed by the same pivot, so a centred text has centred lines.
        const float line_width = static_cast<float>(lines[row].size()) * advance - text.spacing;
        const float x0 = left + text.pivot_x * (widest - line_width);
        const float y = top + static_cast<float>(row) * line_height + glyph_height / 2.0F;
        for (std::size_t column = 0; column < lines[row].size(); ++column) {
            const int glyph = glyph_index(font, lines[row][column]);
            if (glyph < 0) {
                continue; // a letter the font lacks leaves a gap
            }
            const float x = x0 + static_cast<float>(column) * advance + glyph_width / 2.0F;
            app::DrawCommand command{"sprite", x, y, 0.0F, pack(text)};
            command.atlas = static_cast<int>(text.font);
            command.frame = static_cast<std::uint32_t>(glyph);
            command.width = glyph_width;
            command.height = glyph_height;
            command.screen = text.screen;
            renderer.draw(command);
        }
    }
}

} // namespace

void draw(ecs::World& world) {
    auto* backend = world.resource<app::RenderBackend>();
    const auto* table = world.resource<app::TextTable>();
    if (backend == nullptr || table == nullptr) {
        return;
    }
    app::Renderer& renderer = backend->renderer();
    world.each<const Text, const transform2d::Position>(
        [&](ecs::Entity, const Text& text, const transform2d::Position& position) {
            draw_text(renderer, *table, text, position);
        });
}

} // namespace ge::mod::text2d
