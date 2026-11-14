// SPDX-License-Identifier: Apache-2.0
#include "ppu_test_bus.hpp"
#include <rse/systems/nes/ppu_bus.hpp>
#include <iostream>

int main() {
    try {
        for (bool vertical : {false, true}) for (bool chr_ram : {false, true}) {
            std::vector<std::uint8_t> image(16+16384+(chr_ram ? 0 : 8192), 0);
            image[0]='N'; image[1]='E'; image[2]='S'; image[3]=0x1a;
            image[4]=1; image[5]=chr_ram ? 0 : 1; image[6]=vertical ? 1 : 0;
            if (!chr_ram) image[16+16384+0x10]=0x3c;
            rse::nes::Bus memory_bus(rse::nes::Cartridge::parse(image));
            rse::nes::PpuMemory adapter(memory_bus);
            rse::video::Rp2c02 ppu(adapter);
            memory(ppu,0x2000,0x55);
            address(ppu,vertical ? 0x2800 : 0x2400);
            (void)ppu.cpu_read(7);
            check(ppu.cpu_read(7)==0x55, "PPU port through NES nametable wiring");
            memory(ppu,0x0010,0xa5);
            address(ppu,0x0010); (void)ppu.cpu_read(7);
            check(ppu.cpu_read(7)==(chr_ram ? 0xa5 : 0x3c), "PPU CHR RAM/ROM protection through cartridge wiring");
            memory(ppu,0x3f10,0x33);
            address(ppu,0x3f00);
            check((ppu.cpu_read(7)&63)==0x33 && memory_bus.ppu_read(0x3f00)==0,
                  "palette must stay chip-local rather than external bus storage");
        }
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
    return 0;
}
