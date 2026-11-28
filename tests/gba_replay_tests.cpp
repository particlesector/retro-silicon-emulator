// SPDX-License-Identifier: Apache-2.0
#include "gba_replay.hpp"
#include <rse/systems/gba/display.hpp>
#include <array>
#include <fstream>
#include <iostream>

namespace {
namespace fs = std::filesystem;
using namespace rse::desktop;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void write_script(const fs::path& script, std::string_view text) {
    std::ofstream out(script);
    out << text;
    out.close();
    check(bool(out), "Cannot write replay fixture");
}

std::array<unsigned char, 3> pixel(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string format, dimensions, maximum;
    check(bool(std::getline(in, format) && std::getline(in, dimensions) && std::getline(in, maximum)),
          "Cannot read replay capture");
    check(format == "P6" && dimensions == "240 160" && maximum == "255", "Unexpected capture format");
    std::array<unsigned char, 3> rgb{};
    check(bool(in.read(reinterpret_cast<char*>(rgb.data()), rgb.size())), "Cannot read checkpoint pixel");
    return rgb;
}

void run(const fs::path& prefix) {
    const auto script = fs::path(prefix).concat(".txt");
    const auto rom = fs::path(prefix).concat(".gba");
    const auto bios = fs::path(prefix).concat(".bin");
    const auto captures = fs::path(prefix).concat("-captures");
    write_script(script, "# frame count, mask, capture\n2 1 pressed\n2 0 released\n");
    DesktopLoad load;
    DesktopFinish finish;
    configure_gba_replay(script, captures, bios, load, finish);
    auto machine = load(rom);
    bool incomplete = false;
    try { finish(*machine); }
    catch (const std::runtime_error&) { incomplete = true; }
    check(incomplete, "Replay completion checks the final frame");
    const auto port = machine->info().input_ports.front();
    machine->schedule_input(1, port.id, {}); // A live release must not cancel the script.
    const auto end = rse::gba::first_frame_tick + 3 * rse::gba::frame_ticks;
    machine->advance_to(end);
    finish(*machine);
    check(machine->status().video_frames == 4, "Replay advances to the requested frame");
    check(pixel(captures / "pressed.ppm") == std::array<unsigned char, 3>{247, 255, 0}, "A pressed at frame two");
    check(pixel(captures / "released.ppm") == std::array<unsigned char, 3>{255, 255, 0}, "A released at frame four");
    const auto audio = machine->take_audio();
    machine->reset();
    machine->advance_to(end);
    check(!audio.empty() && machine->take_audio() == audio, "Reset repeats the replay and PCM");

    for (auto invalid : {"0 0 -\n", "1 400 -\n", "216001 0 -\n", "1 0 ../escape\n",
                         "1 0 same\n1 0 same\n", "1 0 missing extra\n", "# empty\n"}) {
        write_script(script, invalid);
        bool rejected = false;
        try { configure_gba_replay(script, captures, bios, load, finish); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Malformed replay was accepted");
    }
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::invalid_argument("Expected authored GBA media prefix");
        run(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
