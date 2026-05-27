// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace Gmtoolkit {

// LTS2022_0 forked the 2022.0 line; later 2022/2023/2024 features only apply to Post2022_0 builds.
enum class BranchType : uint8_t {
    Pre2022_0,
    LTS2022_0,
    Post2022_0,
};

struct Version {
    uint32_t major = 1;
    uint32_t minor = 0;
    uint32_t release = 0;
    uint32_t build = 1337;
    BranchType branch = BranchType::Pre2022_0;

    uint8_t bytecode_version = 0x10;
    bool loaded = false;
    bool is_yyc = false;
    uint32_t game_id = 0;
    uint32_t gen8_major = 0;
    uint32_t gen8_minor = 0;
    uint32_t gen8_release = 0;
    uint32_t gen8_build = 0;

    // major/minor/release/build start as GEN8's declared version, then chunk-shape heuristics may bump.

    constexpr bool is_at_least(uint32_t M, uint32_t m = 0, uint32_t r = 0, uint32_t b = 0) const {
        if (major != M)
            return major > M;
        if (minor != m)
            return minor > m;
        if (release != r)
            return release > r;
        if (build != b)
            return build > b;
        return true;
    }

    constexpr bool is_gms2() const {
        return is_at_least(2);
    }

    constexpr bool is_non_lts_at_least(uint32_t M, uint32_t m = 0, uint32_t r = 0, uint32_t b = 0) const {
        if (branch < BranchType::Post2022_0)
            return false;
        return is_at_least(M, m, r, b);
    }

    void bump_to(uint32_t M, uint32_t m = 0, uint32_t r = 0, uint32_t b = 0) {
        if (is_at_least(M, m, r, b))
            return;
        major = M;
        minor = m;
        release = r;
        build = b;
    }

    constexpr bool using_self_to_builtin() const {
        return is_at_least(2024, 2);
    }
    constexpr bool using_extended_sound_info() const {
        return is_at_least(2024, 6);
    }
    constexpr bool using_room_tile_uint() const {
        return is_at_least(2024, 4);
    }
    constexpr bool using_extended_particles() const {
        return is_at_least(2024, 11);
    }
    constexpr bool using_gms2_3() const {
        return is_at_least(2, 3);
    }
    constexpr bool using_2022_5_random_uid() const {
        return is_at_least(2022, 5);
    }
    constexpr bool using_function_script_references() const {
        return is_at_least(2024, 2);
    }
    constexpr bool using_new_function_variables() const {
        return is_at_least(2024, 2);
    }
    constexpr bool using_constructor_set_static() const {
        return is_at_least(2024, 11);
    }
    constexpr bool using_new_function_resolution() const {
        return is_at_least(2024, 13);
    }
    constexpr bool using_optimized_function_declarations() const {
        return is_at_least(2024, 14);
    }
};

} // namespace Gmtoolkit
