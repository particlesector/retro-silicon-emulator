// SPDX-License-Identifier: Apache-2.0
#include <rse/session/builtins.hpp>
#include <rse/systems/nes/session.hpp>
#include <array>
#include <string>

namespace rse::session {
namespace {
constexpr std::array<std::string_view,1> ids{"nes-ntsc"};
void validate(std::string_view id) {
    if(id!=ids[0])throw std::invalid_argument("Unknown built-in system: "+std::string(id));
}
}
std::span<const std::string_view> builtin_ids() noexcept {return ids;}
std::unique_ptr<Session> create(std::string_view id,std::span<const std::uint8_t> image) {
    validate(id);
    return std::make_unique<nes::Session>(nes::Cartridge::parse(image));
}
std::unique_ptr<Session> load(std::string_view id,const std::filesystem::path& path) {
    validate(id);
    return std::make_unique<nes::Session>(nes::Cartridge::load_file(path));
}
}
