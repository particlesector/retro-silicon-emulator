// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <rse/core/component.hpp>
#include <memory>
#include <vector>
namespace rse {
class Machine {
public:
    void add(std::unique_ptr<Component> component);
    void reset();
    void advance_to(Tick target);
    [[nodiscard]] Tick tick() const noexcept { return tick_; }
private:
    Tick tick_{};
    std::vector<std::unique_ptr<Component>> components_;
};
}
