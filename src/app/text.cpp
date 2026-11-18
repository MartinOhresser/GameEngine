#include "ge/app/text.hpp"

#include <cstddef>

namespace ge::app {

std::u32string decode_utf8(std::string_view text) {
    std::u32string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        char32_t code = lead;
        if (lead >= 0xF0U) {
            length = 4;
            code = lead & 0x07U;
        } else if (lead >= 0xE0U) {
            length = 3;
            code = lead & 0x0FU;
        } else if (lead >= 0xC0U) {
            length = 2;
            code = lead & 0x1FU;
        } else if (lead >= 0x80U) {
            out.push_back(U'�'); // a continuation byte with no lead
            ++i;
            continue;
        }
        if (i + length > text.size()) {
            out.push_back(U'�');
            break;
        }
        bool valid = true;
        for (std::size_t k = 1; k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0U) != 0x80U) {
                valid = false;
                break;
            }
            code = (code << 6U) | (next & 0x3FU);
        }
        out.push_back(valid ? code : U'�');
        i += valid ? length : 1;
    }
    return out;
}

} // namespace ge::app
