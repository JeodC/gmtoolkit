// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <string_view>

namespace GMSLib::SaveBackend {

inline uint64_t hash_var_key(std::string_view s, int32_t inst) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (char c : s) {
        h ^= (uint8_t)c;
        h *= 0x100000001b3ULL;
    }
    h ^= (uint32_t)inst * 0x9E3779B185EBCA87ULL;
    return h;
}

} // namespace GMSLib::SaveBackend
