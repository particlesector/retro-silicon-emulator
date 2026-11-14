// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"
#include <rse/systems/nes/machine.hpp>
#include <charconv>
#include <filesystem>
#include <optional>

namespace rse::cli {
int run(std::span<const std::string_view> args,std::ostream& output,std::ostream& error) {
    if (args.size()==1 && args[0]=="--help") {
        output<<"Usage: rse-cli --help|--version\n"
              <<"       rse-cli run <rom.nes> --ticks <master-ticks>\n"
              <<"       rse-cli run <rom.nes> --frames <count>\n";
        return 0;
    }
    if (args.size()==1 && args[0]=="--version") { output<<"Retro Silicon Emulator 0.1.0\n"; return 0; }
    if (args.size()!=4 || args[0]!="run" || args[1].empty() || (args[2]!="--ticks" && args[2]!="--frames")) {
        error<<"Invalid arguments. Use --help.\n"; return 2;
    }
    if (args[3].empty()) { error<<"Count is required.\n"; return 2; }
    std::uint64_t count{};
    const auto parsed=std::from_chars(args[3].data(),args[3].data()+args[3].size(),count);
    if (parsed.ec!=std::errc{} || parsed.ptr!=args[3].data()+args[3].size()) {
        error<<"Count must be an unsigned decimal integer within 64-bit range.\n"; return 2;
    }
    std::optional<nes::Cartridge> cartridge;
    try { cartridge=nes::Cartridge::load_file(std::filesystem::path(args[1])); }
    catch(const std::invalid_argument& e) { error<<e.what()<<'\n'; return 3; }
    catch(const std::runtime_error& e) { error<<e.what()<<'\n'; return 5; }
    catch(const std::exception& e) { error<<e.what()<<'\n'; return 6; }
    try {
        nes::Machine machine(std::move(*cartridge));
        if (args[2]=="--ticks") machine.advance_to(count);
        else for (std::uint64_t i=0;i<count;++i) (void)machine.run_frame();
        const auto state=machine.state();
        output<<"{\"format\":\"rse.run.v1\",\"ticks\":"<<state.tick<<",\"cpu_cycles\":"<<state.cpu_cycles
              <<",\"frames\":"<<state.ppu.frames<<",\"pc\":"<<state.cpu.pc<<"}\n";
        return 0;
    } catch(const std::exception& e) { error<<e.what()<<'\n'; return 4; }
}
} // namespace rse::cli
