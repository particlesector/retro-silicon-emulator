// SPDX-License-Identifier: Apache-2.0
#include "gba_replay.hpp"
#include <rse/session/builtins.hpp>
#include <rse/systems/gba/display.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace rse::desktop {
namespace {
constexpr unsigned max_frames = 216'000;
constexpr unsigned max_captures = 128;
constexpr unsigned keypad_mask = 0x3ff;

struct Step {
    unsigned frames, keys;
    std::string capture;
};

unsigned number(std::string_view token, int base, unsigned line) {
    unsigned value{};
    const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value, base);
    if (error != std::errc{} || end != token.data() + token.size())
        throw std::invalid_argument("Invalid GBA replay number on line " + std::to_string(line));
    return value;
}

std::vector<Step> read_script(const std::filesystem::path& path, bool capture_enabled) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open GBA replay script");
    std::vector<Step> steps;
    std::unordered_set<std::string> names;
    unsigned total{}, line{};
    std::string text;
    while (std::getline(input, text)) {
        ++line;
        std::istringstream row(text);
        std::string frames, keys, name, extra;
        if (!(row >> frames) || frames.starts_with('#')) continue;
        if (!(row >> keys >> name) || row >> extra)
            throw std::invalid_argument("Expected frame count, keypad mask and capture name on GBA replay line " +
                                        std::to_string(line));
        const auto count = number(frames, 10, line);
        const auto mask = number(keys, 16, line);
        if (count == 0 || count > max_frames - total || mask > keypad_mask)
            throw std::invalid_argument("GBA replay count or keypad mask out of range on line " + std::to_string(line));
        if (name != "-") {
            constexpr std::string_view allowed = "abcdefghijklmnopqrstuvwxyz0123456789_-";
            if (!capture_enabled || names.size() == max_captures || name.size() > 48 ||
                name.find_first_not_of(allowed) != std::string::npos || !names.insert(name).second)
                throw std::invalid_argument("Invalid or duplicate GBA replay capture name on line " + std::to_string(line));
        }
        steps.push_back({count, mask, name == "-" ? std::string{} : std::move(name)});
        total += count;
    }
    if (!input.eof() || steps.empty()) throw std::invalid_argument("GBA replay script has no readable input steps");
    return steps;
}

Tick frame_tick(unsigned frame) {
    return gba::first_frame_tick + Tick(frame - 1) * gba::frame_ticks;
}

class ReplaySession final : public session::Session {
public:
    ReplaySession(std::unique_ptr<session::Session> machine, std::span<const Step> steps)
        : machine_(std::move(machine)), steps_(steps.begin(), steps.end()) { schedule(); }

    const session::SystemInfo& info() const noexcept override { return machine_->info(); }
    session::Status status() const noexcept override { return machine_->status(); }
    void reset() override { machine_->reset(); schedule(); }
    void flush_save() override {
        if (status().video_frames >= final_frame_) machine_->flush_save();
    }
    // Playback's live input must not supersede the recorded keypad state.
    void schedule_input(Tick, std::string_view, std::span<const std::string_view>) override {}
    void advance_to(Tick target) override {
        while (next_capture_ < captures_.size() && captures_[next_capture_].tick <= target) {
            auto& capture = captures_[next_capture_];
            machine_->advance_to(capture.tick);
            capture.pixels.resize(gba::screen_width * gba::screen_height);
            machine_->convert_video(capture.pixels);
            ++next_capture_;
        }
        machine_->advance_to(target);
    }
    std::optional<session::VideoFrame> video() const noexcept override { return machine_->video(); }
    void convert_video(std::span<session::Rgba> pixels) const override { machine_->convert_video(pixels); }
    std::vector<std::int16_t> take_audio() override { return machine_->take_audio(); }

    void finish(const std::filesystem::path& directory) const {
        if (status().video_frames < final_frame_)
            throw std::runtime_error("GBA replay ended before the last scripted frame");
        if (captures_.empty()) return;
        std::filesystem::create_directories(directory);
        for (const auto& capture : captures_) {
            std::ofstream output(directory / (capture.name + ".ppm"), std::ios::binary);
            output << "P6\n" << gba::screen_width << ' ' << gba::screen_height << "\n255\n";
            for (const auto& pixel : capture.pixels) {
                const std::array rgb{pixel.r, pixel.g, pixel.b};
                output.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
            }
            output.close();
            if (!output) throw std::runtime_error("Cannot write GBA replay capture " + capture.name);
        }
    }

private:
    struct Capture {
        Tick tick;
        std::string name;
        std::vector<session::Rgba> pixels;
    };

    void schedule() {
        const auto& port = info().input_ports.front();
        const auto schedule_keys = [&](Tick tick, unsigned mask) {
            std::vector<std::string_view> keys;
            for (unsigned i = 0; i < port.actions.size(); ++i)
                if (mask & (1u << i)) keys.push_back(port.actions[i]);
            machine_->schedule_input(tick, port.id, keys);
        };
        unsigned frame{}, previous{};
        captures_.clear();
        next_capture_ = 0;
        for (const auto& step : steps_) {
            if (step.keys != previous) schedule_keys(frame ? frame_tick(frame) + 1 : 1, step.keys);
            previous = step.keys;
            frame += step.frames;
            if (!step.capture.empty()) captures_.push_back({frame_tick(frame), step.capture, {}});
        }
        if (previous) schedule_keys(frame_tick(frame) + 1, 0);
        final_frame_ = frame;
    }

    std::unique_ptr<session::Session> machine_;
    std::vector<Step> steps_;
    std::vector<Capture> captures_;
    std::size_t next_capture_{};
    unsigned final_frame_{};
};
}

void configure_gba_replay(const std::filesystem::path& script, const std::filesystem::path& captures,
                          const std::filesystem::path& bios, DesktopLoad& load, DesktopFinish& finish) {
    auto steps = read_script(script, !captures.empty());
    load = [steps = std::move(steps), bios](const auto& cartridge) {
        return std::make_unique<ReplaySession>(session::load("gba", cartridge, bios), steps);
    };
    finish = [captures](const auto& machine) { dynamic_cast<const ReplaySession&>(machine).finish(captures); };
}

}
