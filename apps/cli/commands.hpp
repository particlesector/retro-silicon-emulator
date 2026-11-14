// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <ostream>
#include <span>
#include <string_view>
namespace rse::cli {
// Arguments exclude executable name. Command/exit contract: docs/NES_SYSTEM.md.
int run(std::span<const std::string_view> args,std::ostream& output,std::ostream& error);
}
