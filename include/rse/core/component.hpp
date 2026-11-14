// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
namespace rse {
using Tick = std::uint64_t;
class Component {
public:
    virtual ~Component() = default;
    virtual void reset() = 0;
    virtual void advance_to(Tick target) = 0;
};
} // namespace rse
