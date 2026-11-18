// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/session/session.hpp>
#include <vector>
#include <string>

namespace rse::desktop {
struct Frame {
    // Owning, tightly packed RGBA pixels; format describes this decoded image.
    std::uint64_t number{};
    session::VideoFormat format{};
    std::vector<session::Rgba> pixels;
};
struct PortInput {
    std::string port;
    std::vector<std::string> pressed;
    bool operator==(const PortInput&) const=default;
};
}
