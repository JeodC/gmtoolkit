// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>

enum class TxtrFormat : uint8_t {
    UNKNOWN = 0,
    ZOQ,
    QOIF,
    PNG,
    DDS,
};

struct block_info {
    const char* name;
    int bx, by;
    uint64_t pvr_code;
};
