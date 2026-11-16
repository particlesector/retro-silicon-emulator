// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"
#include "execution.hpp"
#include "fake_machine_session.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
struct Files {
    std::filesystem::path directory="rse-cli-test-files";
    std::filesystem::path valid=directory/"valid.nes",invalid=directory/"invalid.nes",unsupported=directory/"unsupported.nes";
    Files() {
        check(std::filesystem::create_directory(directory),"CLI fixture directory already exists");
        std::vector<std::uint8_t> bytes(16+16384,0);
        bytes[0]='N'; bytes[1]='E'; bytes[2]='S'; bytes[3]=0x1a; bytes[4]=1;
        bytes[16]=0xe6; bytes[17]=0x10; bytes[18]=0x4c; bytes[19]=0; bytes[20]=0x80;
        bytes[16+0x3ffc]=0; bytes[16+0x3ffd]=0x80;
        write(valid,bytes); bytes[16]=2; write(unsupported,bytes); write(invalid,{1,2});
    }
    static void write(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes) {
        std::ofstream output(path,std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        output.close(); check(bool(output),"could not write CLI fixture");
    }
    ~Files() {
        std::error_code ignored;
        for (const auto& path:{valid,invalid,unsupported}) std::filesystem::remove(path,ignored);
        std::filesystem::remove(directory,ignored);
    }
};
void invoke(std::vector<std::string_view> args,int expected,std::string_view text={}) {
    std::ostringstream output,error;
    check(rse::cli::run(args,output,error)==expected,"CLI exit code mismatch");
    if (!expected) check(error.str().empty() && output.str().find(text)!=std::string::npos,"CLI output/diagnostics mismatch");
    else check(output.str().empty() && !error.str().empty(),"CLI errors must use stderr only");
}
}
int main() {
    try {
        rse::testing::FakeMachineSession fake;
        check(rse::session::ticks_for_us({236250000,11},1000)==21478,"NES host request conversion");
        check(rse::session::ticks_for_us({1000,3},1000)==1,"rational host request rounding");
        rse::cli::execute(fake,false,2000000);
        check(fake.status().tick==2000000 && fake.take_audio().empty(),"generic CLI did not bound/discard audio");
        fake.reset();rse::cli::execute(fake,true,3);check(fake.status().video_frames==3,"generic CLI frame count");
        rse::testing::FakeMachineSession headless(false,false);
        rse::cli::execute(headless,false,5000);check(headless.status().tick==5000,"generic headless CLI");
        bool unsupported_frames=false;try{rse::cli::execute(headless,true,1);}catch(const rse::session::UnsupportedOperation&){unsupported_frames=true;}
        check(unsupported_frames,"generic CLI hid unsupported frame stepping");
        Files files;
        const auto valid=files.valid.string(),invalid=files.invalid.string(),unsupported=files.unsupported.string();
        const auto missing=(files.directory/"missing.nes").string();
        invoke({"--help"},0,"--ticks"); invoke({"--version"},0,"0.1.0");
        invoke({"run",valid,"--ticks","120"},0,"\"ticks\":120,\"cpu_cycles\":10,\"frames\":0,\"pc\":32770");
        invoke({"run",valid,"--frames","1"},0,"\"ticks\":330096,\"cpu_cycles\":27508,\"frames\":1");
        invoke({"run",valid,"--ticks","0"},0,"\"ticks\":0,\"cpu_cycles\":0,\"frames\":0,\"pc\":0");
        invoke({"run",valid,"--frames","0"},0,"\"frames\":0");
        invoke({"run",valid,"--ticks","120","--system","nes-ntsc"},0,"\"ticks\":120");
        invoke({"run",missing,"--ticks","120","--system","unknown"},3);
        invoke({"run",valid,"--ticks","24000000"},0,"\"ticks\":24000000");
        invoke({},2); invoke({"run",valid},2); invoke({"run",valid,"--invalid","1"},2);
        for (auto count:{"-1","+1","0x10","","1x","18446744073709551616"}) invoke({"run",valid,"--ticks",count},2);
        invoke({"run",missing,"--ticks","120"},5); invoke({"run",invalid,"--ticks","120"},3);
        invoke({"run",unsupported,"--ticks","120"},4);
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
    return 0;
}
