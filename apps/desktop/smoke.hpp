// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace rse::desktop {
struct SmokeMetrics {
    std::uint64_t first_us{},min_us{},max_us{},sum_us{};
    unsigned responses{};
    void record(std::uint64_t latency);
    void report(std::ostream& output) const;
};

// Internal smoke-test view. Frame samples are read-only and valid until the
// application's next step. No window, gamepad or machine ownership is exposed.
struct SmokeObservation {
    bool running{},loaded{};
    std::uint8_t buttons{};
    std::uint64_t tick{},frame_number{},samples{};
    unsigned loads{},resets{},errors{},readbacks{},starvation{};
    std::span<const std::uint16_t> frame_samples;
};
class SmokeHost {
public:
    virtual ~SmokeHost()=default;
    [[nodiscard]] virtual SmokeObservation observation() const=0;
    [[nodiscard]] virtual std::optional<std::uint8_t> inspect_memory(std::uint16_t address) const=0;
    virtual void select_gamepad(SDL_JoystickID id)=0;
    virtual void request_readback()=0;
    virtual void resize_window(int width,int height)=0;
    virtual void minimize_window(bool minimized)=0;
    virtual void pump()=0;
};

class Smoke {
public:
    explicit Smoke(std::filesystem::path fixture);
    ~Smoke();
    void tick(SmokeHost& host,std::uint64_t elapsed_ms);
    void finish(SmokeHost& host);
    [[nodiscard]] const SmokeMetrics& metrics() const noexcept {return metrics_;}
private:
    void response(std::uint16_t sample);
    std::filesystem::path path_;
    std::string drop_;
    unsigned stage_{};
    SDL_JoystickID id_{};
    std::uint64_t input_start_{};
    std::uint16_t expected_sample_{};
    SmokeMetrics metrics_;
    std::unique_ptr<SDL_Joystick,decltype(&SDL_CloseJoystick)> joystick_{nullptr,SDL_CloseJoystick};
};
}
