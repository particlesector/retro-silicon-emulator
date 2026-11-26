// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "session.hpp"
#include <filesystem>
#include <memory>

namespace rse::session {
// Static built-ins only. Unknown IDs fail before reading/parsing media.
[[nodiscard]] std::span<const std::string_view> builtin_ids() noexcept;
// GBA requires an external 16 KiB BIOS. NES callers can use the two-argument
// overloads and continue to load a cartridge without firmware.
[[nodiscard]] std::unique_ptr<Session> create(std::string_view id,std::span<const std::uint8_t> image);
[[nodiscard]] std::unique_ptr<Session> create(std::string_view id,std::span<const std::uint8_t> image,
    std::span<const std::uint8_t> bios);
[[nodiscard]] std::unique_ptr<Session> load(std::string_view id,const std::filesystem::path& path);
[[nodiscard]] std::unique_ptr<Session> load(std::string_view id,const std::filesystem::path& path,
    const std::filesystem::path& bios);
}
