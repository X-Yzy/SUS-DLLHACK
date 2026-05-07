#include "SgnLite.hpp"

#include <chrono>

namespace sgnlite {
namespace {
void appendU32(std::vector<std::uint8_t> &out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
}

std::uint8_t seedKey()
{
    const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::uint8_t key = static_cast<std::uint8_t>((ticks ^ (ticks >> 17) ^ (ticks >> 31)) & 0xff);
    return key == 0 ? 0x5a : key;
}
}

std::vector<std::uint8_t> encodeX64(const std::vector<std::uint8_t> &payload)
{
    if (payload.empty() || payload.size() > 0x7fffffffU) {
        return {};
    }

    const std::uint8_t initialKey = seedKey();
    std::uint8_t key = initialKey;
    std::vector<std::uint8_t> encoded;
    encoded.reserve(payload.size());
    for (std::uint8_t byte : payload) {
        encoded.push_back(static_cast<std::uint8_t>(byte ^ key));
        key = static_cast<std::uint8_t>(key + byte);
    }

    std::vector<std::uint8_t> out;
    out.reserve(payload.size() + 32);

    out.push_back(0xe8);
    appendU32(out, static_cast<std::uint32_t>(encoded.size()));
    out.insert(out.end(), encoded.begin(), encoded.end());

    out.push_back(0x5e);
    out.push_back(0xb9);
    appendU32(out, static_cast<std::uint32_t>(encoded.size()));
    out.push_back(0xb3);
    out.push_back(initialKey);
    out.push_back(0x30);
    out.push_back(0x1e);
    out.push_back(0x02);
    out.push_back(0x1e);
    out.push_back(0x48);
    out.push_back(0xff);
    out.push_back(0xc6);
    out.push_back(0xe2);
    out.push_back(0xf7);
    out.push_back(0x48);
    out.push_back(0x81);
    out.push_back(0xee);
    appendU32(out, static_cast<std::uint32_t>(encoded.size()));
    out.push_back(0xff);
    out.push_back(0xe6);
    return out;
}

}
