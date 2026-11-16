// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "session.hpp"
#include <filesystem>
#include <memory>

namespace rse::session {
// Static built-ins only. Unknown IDs fail before reading/parsing media.
[[nodiscard]] std::span<const std::string_view> builtin_ids() noexcept;
[[nodiscard]] std::unique_ptr<Session> create(std::string_view id,std::span<const std::uint8_t> image);
[[nodiscard]] std::unique_ptr<Session> load(std::string_view id,const std::filesystem::path& path);
}
