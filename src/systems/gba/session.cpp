// SPDX-License-Identifier: Apache-2.0
#include <rse/systems/gba/session.hpp>
#include <algorithm>

namespace rse::gba {
namespace {
// Indexed by the KEYINPUT bit numbers.
constexpr std::array<std::string_view, 10> actions{"a", "b", "select", "start", "right",
                                                  "left", "up", "down", "r", "l"};
constexpr std::array<session::InputPort, 1> ports{{{"controller-1", actions}}};

session::Rgba rgba(std::uint16_t color) {
    const auto expand = [](unsigned value) { return std::uint8_t((value << 3) | (value >> 2)); };
    return {expand(color & 31), expand((color >> 5) & 31), expand((color >> 10) & 31), 255};
}
}

const session::SystemInfo& Session::info() const noexcept {
    static constexpr session::SystemInfo info{"gba", "Game Boy Advance",
        {clock_hz, 1}, session::VideoFormat{screen_width, screen_height, {3, 2}, session::PixelFormat::bgr555_u16_native},
        session::AudioFormat{audio::GbaApu::sample_rate, 2, session::SampleFormat::signed16_native, Bus::audio_limit},
        ports, true};
    return info;
}

session::Status Session::status() const noexcept {
    const auto& bus = machine_.bus();
    return {bus.ticks(), bus.display().number(), faulted_};
}

void Session::reset() {
    machine_.reset();
    faulted_ = false;
}

void Session::advance_to(Tick target) {
    const auto now = machine_.bus().ticks();
    if (target < now) throw std::invalid_argument("Cannot rewind a GBA session");
    if (target == now) return;
    if (faulted_) throw std::logic_error("Reset the faulted GBA session before advancing");
    try {
        machine_.advance_to(target);
    } catch (...) {
        faulted_ = true;
        throw;
    }
}

void Session::run_frame() {
    const auto tick = machine_.bus().ticks();
    const auto delta = tick < first_frame_tick ? first_frame_tick - tick :
                       frame_ticks - (tick - first_frame_tick) % frame_ticks;
    if (delta > Timers::never - tick) throw std::overflow_error("GBA frame tick is out of range");
    advance_to(tick + delta);
}

void Session::schedule_input(Tick tick, std::string_view port, std::span<const std::string_view> pressed) {
    if (faulted_) throw std::logic_error("Reset the faulted GBA session before scheduling input");
    if (port != ports[0].id) throw std::invalid_argument("Unknown GBA input port");
    std::uint16_t mask = 0;
    for (const auto action : pressed) {
        const auto found = std::ranges::find(actions, action);
        if (found == actions.end()) throw std::invalid_argument("Unknown GBA input action");
        const auto bit = std::uint16_t(1u << (found - actions.begin()));
        if (mask & bit) throw std::invalid_argument("Duplicate GBA input action");
        mask |= bit;
    }
    machine_.schedule_keys(tick, mask);
}

std::optional<session::VideoFrame> Session::video() const noexcept {
    const auto& display = machine_.bus().display();
    if (!display.number()) return std::nullopt;
    return session::VideoFrame{display.number(), screen_width * sizeof(std::uint16_t),
        std::as_bytes(std::span(display.frame()))};
}

void Session::convert_video(std::span<session::Rgba> output) const {
    if (output.size() != screen_width * screen_height)
        throw std::invalid_argument("GBA RGBA output size must match its video dimensions");
    const auto& display = machine_.bus().display();
    if (!display.number()) throw std::logic_error("No published GBA video frame");
    std::ranges::transform(display.frame(), output.begin(), rgba);
}

std::optional<EntryFetch> Session::entry_fetch() const noexcept {
    const auto entry = machine_.bus().entry_fetch();
    if (entry && entry->tick < machine_.bus().ticks()) return entry;
    return std::nullopt;
}

}
