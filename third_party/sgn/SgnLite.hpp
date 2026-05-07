#pragma once

#include <cstdint>
#include <vector>

namespace sgnlite {

std::vector<std::uint8_t> encodeX64(const std::vector<std::uint8_t> &payload);

}
