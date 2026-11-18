#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace ge::app {

/// What the game keeps between two runs of the program: high scores, unlocked
/// levels. Whole numbers under a name. The simulation reads and writes it, but
/// it is not part of the world's state (it is not hashed, and a scenario starts
/// with an empty one); a player of the game (`geplay`) loads it from a file
/// before the first frame and writes it back whenever `dirty` is true.
class SaveData {
public:
    /// The value saved under `key`, or `fallback`.
    [[nodiscard]] std::int64_t get(std::string_view key, std::int64_t fallback = 0) const;

    [[nodiscard]] bool has(std::string_view key) const;

    /// Saves `value` under `key`; the data becomes dirty only if it changed.
    void set(std::string_view key, std::int64_t value);

    /// True when something changed since the last `mark_clean`.
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    void mark_clean() noexcept { dirty_ = false; }

    /// One "name value" line per entry, sorted by name.
    [[nodiscard]] std::string to_text() const;

    /// Replaces the content with what `to_text` wrote. Lines that make no sense
    /// are skipped. The result is clean.
    void from_text(std::string_view text);

private:
    std::map<std::string, std::int64_t, std::less<>> values_;
    bool dirty_ = false;
};

} // namespace ge::app
