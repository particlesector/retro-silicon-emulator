// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "application.hpp"

namespace rse::desktop {

// Install a file-backed GBA session with deterministic keypad input.
// The finish hook verifies completion and exports the requested frames.
void configure_gba_replay(const std::filesystem::path& script, const std::filesystem::path& captures,
                          const std::filesystem::path& bios, DesktopLoad& load, DesktopFinish& finish);

}
