#pragma once

#include <cstdint>
#include <vector>

namespace ge::app {

/// A game asks to stop (a Quit button, a menu entry): a system sets `requested`,
/// and whatever runs the game reads it after each frame. A player of the game
/// closes it; a launcher goes back to its list. The simulation does not stop by
/// itself, and the request is not part of the world's state.
struct QuitRequest {
    bool requested = false;
};

/// One thing a game wants from its host, as a command and a number.
struct HostRequest {
    /// The command: its index in the game's `host_command` list.
    std::uint32_t command = 0;
    std::uint32_t argument = 0;
};

/// What the game asks of the program that runs it (play another game, export
/// one): the game only leaves requests, and the host reads and clears them
/// between frames. The game decides nothing about what a command does.
struct HostRequests {
    std::vector<HostRequest> pending;
};

/// What the host tells the game about itself. A mobile host closes applications
/// by itself (a "Quit" button is useless there, and iOS forbids it).
struct HostInfo {
    bool mobile = false;
};

} // namespace ge::app
