// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/systems/nes/cartridge.hpp>
#include <algorithm>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// Tiny fixture assembler for project-owned NROM programs, not a product API.
struct NesProgram {
    std::vector<std::uint8_t> prg=std::vector<std::uint8_t>(16384,0xea);
    std::uint16_t pc{0x8000};
    std::map<std::string,std::uint16_t> labels;
    struct Patch { std::uint16_t address; std::string label; bool relative; };
    std::vector<Patch> patches;
    void at(std::uint16_t value) { pc=value; }
    void label(const std::string& name) { if (!labels.emplace(name,pc).second) throw std::runtime_error("duplicate fixture label"); }
    void emit(std::initializer_list<std::uint8_t> bytes) { for (auto byte:bytes) prg.at((pc++-0x8000)&0x3fff)=byte; }
    void jump(std::uint8_t opcode,const std::string& label) {
        emit({opcode}); patches.push_back({pc,label,false}); emit({0,0});
    }
    void branch(std::uint8_t opcode,const std::string& label) {
        emit({opcode}); patches.push_back({pc,label,true}); emit({0});
    }
    rse::nes::Cartridge cartridge() {
        for (const auto& patch:patches) {
            const auto target=labels.at(patch.label);
            const auto offset=patch.address-0x8000;
            if (patch.relative) {
                const int distance=int(target)-int(patch.address+1);
                if (distance < -128 || distance > 127) throw std::runtime_error("fixture branch out of range");
                prg.at(offset)=static_cast<std::uint8_t>(distance);
            } else {
                prg.at(offset)=static_cast<std::uint8_t>(target);
                prg.at(offset+1)=static_cast<std::uint8_t>(target>>8);
            }
        }
        prg[0x3ffa]=0; prg[0x3ffb]=0x90;
        prg[0x3ffc]=0; prg[0x3ffd]=0x80;
        prg[0x3ffe]=0; prg[0x3fff]=0x91;
        std::vector<std::uint8_t> image(16+prg.size(),0);
        image[0]='N'; image[1]='E'; image[2]='S'; image[3]=0x1a; image[4]=1;
        std::copy(prg.begin(),prg.end(),image.begin()+16);
        return rse::nes::Cartridge::parse(image);
    }
};
inline rse::nes::Cartridge loop_program() {
    NesProgram p; p.emit({0xe6,0x10,0x4c,0,0x80}); return p.cartridge();
}
inline rse::nes::Cartridge video_input_program() {
    NesProgram p;
    p.emit({0x78,0xd8,0xa2,0xff,0x9a,0xa9,0,0x8d,0,0x20,0x8d,1,0x20});
    p.emit({0xa9,0x3f,0x8d,6,0x20,0xa9,0,0x8d,6,0x20,0xa9,0x0f,0x8d,7,0x20,
            0xa9,0x21,0x8d,7,0x20,0xa9,0,0x8d,6,0x20,0x8d,6,0x20,0xa2,8,0xa9,0xff});
    p.label("pattern"); p.emit({0x8d,7,0x20,0xca}); p.branch(0xd0,"pattern");
    p.emit({0xa9,0,0x8d,6,0x20,0x8d,6,0x20,0x8d,5,0x20,0x8d,5,0x20,
            0xa9,0x80,0x8d,0,0x20,0xa9,0x0a,0x8d,1,0x20,0x58});
    p.label("idle"); p.jump(0x4c,"idle");
    p.at(0x9000); p.emit({0x48,0x8a,0x48,0xe6,0,0xa9,1,0x8d,0x16,0x40,
                        0xa9,0,0x8d,0x16,0x40,0xa2,8});
    p.label("buttons"); p.emit({0xad,0x16,0x40,0x29,1,0x4a,0x66,0x10,0xca}); p.branch(0xd0,"buttons");
    p.emit({0x68,0xaa,0x68,0x40});
    p.at(0x9100); p.emit({0xe6,1,0x40});
    return p.cartridge();
}
