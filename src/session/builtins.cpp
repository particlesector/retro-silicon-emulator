// SPDX-License-Identifier: Apache-2.0
#include <rse/session/builtins.hpp>
#include "gba_save.hpp"
#include <rse/systems/nes/session.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <string>

namespace rse::session {
namespace {
constexpr std::array<std::string_view,2> ids{"nes-ntsc","gba"};
void validate(std::string_view id) {
    if(std::ranges::find(ids,id)==ids.end())throw std::invalid_argument("Unknown built-in system: "+std::string(id));
}

std::vector<std::uint8_t> read_gba_media(const std::filesystem::path& path, bool firmware) {
    const std::string name = firmware ? "GBA BIOS" : "GBA cartridge";
    if (firmware && path.empty()) throw std::invalid_argument("GBA BIOS is required (supply a 16384-byte BIOS path)");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open " + name + ": " + path.string());
    const auto size = file.tellg();
    if (size < 0) throw std::runtime_error("Cannot determine " + name + " size: " + path.string());
    if (firmware && size != gba::bios_size)
        throw std::invalid_argument("GBA BIOS must be exactly 16384 bytes: " + path.string());
    if (!firmware && size < gba::cartridge_header_size)
        throw std::invalid_argument("Truncated GBA cartridge: the header requires at least 192 bytes: " + path.string());
    if (!firmware && size > gba::cartridge_max_size)
        throw std::invalid_argument("GBA cartridge exceeds the 32 MiB Game Pak ROM limit: " + path.string());
    std::vector<std::uint8_t> image(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(image.size())))
        throw std::runtime_error("Cannot read complete " + name + ": " + path.string());
    return image;
}
}
std::span<const std::string_view> builtin_ids() noexcept {return ids;}
std::unique_ptr<Session> create(std::string_view id,std::span<const std::uint8_t> image) {
    return create(id,image,{});
}
std::unique_ptr<Session> create(std::string_view id,std::span<const std::uint8_t> image,
    std::span<const std::uint8_t> bios) {
    validate(id);
    if(id=="gba") return std::make_unique<gba::Session>(bios,image);
    if(!bios.empty()) throw std::invalid_argument("A BIOS is only supported for the gba system");
    return std::make_unique<nes::Session>(nes::Cartridge::parse(image));
}
std::unique_ptr<Session> load(std::string_view id,const std::filesystem::path& path) {
    return load(id,path,{});
}
std::unique_ptr<Session> load(std::string_view id,const std::filesystem::path& path,
    const std::filesystem::path& bios) {
    validate(id);
    if(id=="gba") {
        const auto firmware = read_gba_media(bios,true);
        return std::make_unique<FileBackedGba>(firmware,read_gba_media(path,false),path);
    }
    if(!bios.empty()) throw std::invalid_argument("A BIOS is only supported for the gba system");
    return std::make_unique<nes::Session>(nes::Cartridge::load_file(path));
}
}
