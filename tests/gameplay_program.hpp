// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nes_programs.hpp"

// Signal Run, a project-owned mapper-0 task. CPU RAM $10..$13 exposes
// position, points, finish, and collisions. $14..$16 are scratch.
inline NesProgram gameplay_program(bool defective=false) {
    NesProgram p;
    const auto lda=[&](std::uint8_t v){p.emit({0xa9,v});};
    const auto sta=[&](std::uint16_t a){
        p.emit({0x8d,static_cast<std::uint8_t>(a),static_cast<std::uint8_t>(a>>8)});
    };
    const auto poke=[&](std::uint16_t a,std::uint8_t v){lda(v);sta(a);};
    const auto address=[&](std::uint16_t a){
        poke(0x2006,static_cast<std::uint8_t>(a>>8));
        poke(0x2006,static_cast<std::uint8_t>(a));
    };
    p.emit({0x78,0xd8,0xa2,0xff,0x9a}); // SEI, CLD, LDX #$FF, TXS
    lda(0);sta(0x2000);sta(0x2001);
    address(0x0010); // Three solid 8x8 tiles in CHR RAM.
    for(unsigned tile=1;tile<=3;++tile)
        for(unsigned plane=0;plane<2;++plane)
            for(unsigned row=0;row<8;++row) poke(0x2007,tile&(1u<<plane) ? 0xff : 0);
    address(0x3f00);
    for(const auto color:{0x0f,0x01,0x16,0x29}) poke(0x2007,static_cast<std::uint8_t>(color));
    address(0x3f11);poke(0x2007,0x30);
    address(0x21c2);lda(1);
    for(unsigned i=0;i<24;++i) sta(0x2007); // Track at y=112.
    for(const auto cell:{3,7,11}) {
        address(static_cast<std::uint16_t>(0x2182+2*cell)); // Marker at y=96.
        poke(0x2007,cell==11 ? 3 : 2);
    }
    p.jump(0x20,"sprite");
    lda(0);sta(0x2005);sta(0x2005);poke(0x2001,0x1e);

    p.label("vblank");
    p.emit({0x2c,0x02,0x20});p.branch(0x10,"vblank"); // BIT $2002 / BPL
    p.emit({0xe6,0x14,0xa5,0x14,0x29,1});p.branch(0xd0,"vblank");
    p.emit({0xa5,0x12});p.branch(0xd0,"vblank");
    poke(0x4016,1);poke(0x4016,0);
    p.emit({0xa2,8});
    p.label("buttons");
    p.emit({0xad,0x16,0x40,0x4a,0x66,0x15,0xca});p.branch(0xd0,"buttons");
    p.emit({0xa5,0x15,0x29,0x80});p.branch(0xf0,"vblank");
    p.emit({0xa5,0x10,0x18,0x69,1,0x85,0x16,0xc9,3}); // Target cell.
    p.branch(defective ? 0xd0 : 0xf0,defective ? "check_second" : "hazard");
    if(defective) p.jump(0x4c,"clear");
    p.label("check_second");
    p.emit({0xc9,7});p.branch(0xf0,"hazard");
    p.label("clear");
    p.emit({0xa5,0x15,0x29,1});p.branch(0xd0,"collision"); // Unneeded jump stalls.
    p.jump(0x4c,"move");
    p.label("hazard");
    p.emit({0xa5,0x15,0x29,1});p.branch(0xf0,"collision");
    p.label("move");
    p.emit({0xa5,0x16,0x85,0x10,0xe6,0x11,0xc9,11});p.branch(0xd0,"render");
    p.emit({0xa9,1,0x85,0x12});
    p.label("render");
    p.jump(0x20,"sprite");
    p.jump(0x4c,"vblank");
    p.label("collision");
    p.emit({0xe6,0x13});p.jump(0x4c,"vblank");

    p.label("sprite");
    poke(0x2003,0);poke(0x2004,87);poke(0x2004,1);poke(0x2004,0);
    p.emit({0xa5,0x10,0x0a,0x0a,0x0a,0x0a,0x18,0x69,16});sta(0x2004);
    p.emit({0x60});
    return p;
}
