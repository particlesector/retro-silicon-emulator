// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "types.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace rse::session {
// Host request sizing only. The machine still advances integer ticks exactly.
inline Tick ticks_for_us(Rational clock,std::uint64_t us) {
    if(!clock.numerator || !clock.denominator)throw std::invalid_argument("Invalid session clock");
    const auto ticks=std::ceil(static_cast<long double>(clock.numerator)*us/clock.denominator/1'000'000);
    if(ticks>=static_cast<long double>(std::numeric_limits<Tick>::max()))throw std::overflow_error("Session duration overflow");
    return static_cast<Tick>(ticks);
}
}
