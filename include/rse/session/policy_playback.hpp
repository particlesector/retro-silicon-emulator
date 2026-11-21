// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/systems/nes/session.hpp>
#include <array>
#include <filesystem>

namespace rse::policy {
enum class Provider { cpu, cuda, automatic };
[[nodiscard]] Provider parse_provider(std::string_view name);

// Version 1 is the Signal Run memory policy. See docs/MODEL_PLAYBACK.md.
class Model {
public:
    Model(const std::filesystem::path& bundle,Provider provider);
    ~Model();
    [[nodiscard]] Provider provider() const noexcept;
    [[nodiscard]] std::uint64_t rom_hash() const noexcept;
    [[nodiscard]] std::array<float,3> logits(unsigned position) const;
    [[nodiscard]] unsigned choose(unsigned position) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Synchronous session decoration. First nonempty human input takes control
// until reset. An empty startup input leaves the policy in control.
class Playback final : public session::Session {
public:
    Playback(std::shared_ptr<const Model> model,std::span<const std::uint8_t> image);
    [[nodiscard]] const session::SystemInfo& info() const noexcept override {return info_;}
    [[nodiscard]] session::Status status() const noexcept override;
    [[nodiscard]] session::Inspection* inspection() noexcept override {return machine_.inspection();}
    void reset() override;
    void advance_to(Tick target) override;
    void schedule_input(Tick tick,std::string_view port,std::span<const std::string_view> pressed) override;
    [[nodiscard]] std::optional<session::VideoFrame> video() const noexcept override {return machine_.video();}
    void convert_video(std::span<session::Rgba> output) const override {machine_.convert_video(output);}
    [[nodiscard]] std::vector<std::int16_t> take_audio() override {return machine_.take_audio();}
    [[nodiscard]] bool completed() const noexcept;
    [[nodiscard]] bool manual() const noexcept {return manual_;}
    [[nodiscard]] std::uint8_t active_buttons() const noexcept;
    [[nodiscard]] std::vector<std::uint8_t> replay() const;
    [[nodiscard]] std::vector<std::uint8_t> canonical_machine_state() const {return machine_.snapshot();}
private:
    void apply(Tick tick,std::string_view port,std::span<const std::string_view> pressed);
    std::shared_ptr<const Model> model_;
    nes::Session machine_;
    session::SystemInfo info_;
    std::vector<nes::ReplayAction> actions_;
    Tick next_decision_{757'368};
    unsigned steps_{};
    bool manual_{},stopped_{},faulted_{};
};
[[nodiscard]] std::vector<std::uint8_t> read_file(const std::filesystem::path& path,std::size_t limit);
void write_file(const std::filesystem::path& path,std::span<const std::uint8_t> data);
}
