#include "ge/app/save.hpp"

#include <charconv>
#include <system_error>

namespace ge::app {

std::int64_t SaveData::get(std::string_view key, std::int64_t fallback) const {
    const auto found = values_.find(key);
    return found == values_.end() ? fallback : found->second;
}

bool SaveData::has(std::string_view key) const {
    return values_.find(key) != values_.end();
}

void SaveData::set(std::string_view key, std::int64_t value) {
    const auto found = values_.find(key);
    if (found == values_.end()) {
        values_.emplace(std::string(key), value);
        dirty_ = true;
    } else if (found->second != value) {
        found->second = value;
        dirty_ = true;
    }
}

std::string SaveData::to_text() const {
    std::string text;
    for (const auto& [key, value] : values_) {
        text += key;
        text += ' ';
        text += std::to_string(value);
        text += '\n';
    }
    return text;
}

void SaveData::from_text(std::string_view text) {
    values_.clear();
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        const std::size_t space = line.rfind(' ');
        if (space == std::string_view::npos || space == 0) {
            continue;
        }
        const std::string_view number = line.substr(space + 1);
        std::int64_t value = 0;
        const auto [stop, error] = std::from_chars(number.data(), number.data() + number.size(), value);
        if (error == std::errc{} && stop == number.data() + number.size()) {
            values_[std::string(line.substr(0, space))] = value;
        }
    }
    dirty_ = false;
}

} // namespace ge::app
