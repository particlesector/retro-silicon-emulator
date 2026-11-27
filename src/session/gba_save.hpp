// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/systems/gba/session.hpp>
#include <filesystem>

namespace rse::session {

class FileBackedGba final : public gba::Session {
public:
    FileBackedGba(std::span<const std::uint8_t> bios, std::span<const std::uint8_t> rom,
                  const std::filesystem::path& cartridge);
    void flush_save() override;

private:
    std::filesystem::path save_;
};

}
