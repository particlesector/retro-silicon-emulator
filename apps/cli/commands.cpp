// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"
#include <rse/session/builtins.hpp>
#include "execution.hpp"
#include "nes_output.hpp"
#include <charconv>
#include <filesystem>
#include <optional>

namespace rse::cli {
int run(std::span<const std::string_view> args,std::ostream& output,std::ostream& error) {
    if (args.size()==1 && args[0]=="--help") {
        output<<"Usage: rse-cli --help|--version\n"
              <<"       rse-cli run <rom.nes> --ticks <master-ticks>\n"
              <<"       rse-cli run <rom.nes> --frames <count>\n"
              <<"       Optional: --system nes-ntsc (default)\n";
        return 0;
    }
    if (args.size()==1 && args[0]=="--version") { output<<"Retro Silicon Emulator 0.1.0\n"; return 0; }
    std::string_view system="nes-ntsc";
    if(args.size()==6 && args[4]=="--system") {system=args[5];args=args.first(4);}
    if (args.size()!=4 || args[0]!="run" || args[1].empty() || (args[2]!="--ticks" && args[2]!="--frames")) {
        error<<"Invalid arguments. Use --help.\n"; return 2;
    }
    if (args[3].empty()) { error<<"Count is required.\n"; return 2; }
    std::uint64_t count{};
    const auto parsed=std::from_chars(args[3].data(),args[3].data()+args[3].size(),count);
    if (parsed.ec!=std::errc{} || parsed.ptr!=args[3].data()+args[3].size()) {
        error<<"Count must be an unsigned decimal integer within 64-bit range.\n"; return 2;
    }
    std::unique_ptr<session::Session> machine;
    try { machine=session::load(system,std::filesystem::path(args[1])); }
    catch(const std::invalid_argument& e) { error<<e.what()<<'\n'; return 3; }
    catch(const std::runtime_error& e) { error<<e.what()<<'\n'; return 5; }
    catch(const std::exception& e) { error<<e.what()<<'\n'; return 6; }
    try {
        execute(*machine,args[2]=="--frames",count);
        if(machine->info().id=="nes-ntsc")write_nes_v1(*machine,output);
        else {
            const auto state=machine->status();
            output<<"{\"format\":\"rse.session.run.v1\",\"ticks\":"<<state.tick
                  <<",\"frames\":"<<state.video_frames<<"}\n";
        }
        return 0;
    } catch(const std::exception& e) { error<<e.what()<<'\n'; return 4; }
}
} // namespace rse::cli
