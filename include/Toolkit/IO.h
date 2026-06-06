// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Gmtoolkit {

inline uint8_t r_u8(const uint8_t* p) {
    return p[0];
}
inline uint16_t r_u16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
inline uint32_t r_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline uint64_t r_u64(const uint8_t* p) {
    return (uint64_t)r_u32(p) | ((uint64_t)r_u32(p + 4) << 32);
}
inline int32_t r_i32(const uint8_t* p) {
    return (int32_t)r_u32(p);
}

inline void w_u16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
inline void w_u32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
inline void w_u64(uint8_t* p, uint64_t v) {
    w_u32(p, (uint32_t)v);
    w_u32(p + 4, (uint32_t)(v >> 32));
}

template <typename Buf> inline int slurp(const char* path, Buf& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::perror(path);
        return -1;
    }
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return -1;
    }
    long sz = std::ftell(f);
    if (sz < 0) {
        std::fclose(f);
        return -1;
    }
    std::rewind(f);
    out.resize((size_t)sz);
    size_t n = sz > 0 ? std::fread(out.data(), 1, (size_t)sz, f) : 0;
    std::fclose(f);
    if (n != (size_t)sz) {
        std::fprintf(stderr, "[ERROR] short read from %s\n", path);
        return -1;
    }
    return 0;
}

inline int spew(const char* path, const uint8_t* data, size_t n) {
    FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::perror(path);
        return -1;
    }
    size_t w = std::fwrite(data, 1, n, f);
    std::fclose(f);
    if (w != n) {
        std::fprintf(stderr, "[ERROR] short write to %s\n", path);
        return -1;
    }
    return 0;
}

inline int find_chunk(const uint8_t* buf, size_t len, const char* tag, size_t* payload_off, size_t* payload_size) {
    if (len < 8 || std::memcmp(buf, "FORM", 4) != 0)
        return -1;
    for (size_t i = 8; i + 8 <= len;) {
        uint32_t sz = r_u32(buf + i + 4);
        if (std::memcmp(buf + i, tag, 4) == 0) {
            if (i + 8 + sz > len)
                return -2;
            *payload_off = i + 8;
            *payload_size = sz;
            return 0;
        }
        i += 8 + sz;
    }
    return -3;
}

// STRG entries are length-prefixed at ptr-4 and NUL-terminated at ptr+len; we read just the content.
inline std::string read_strg_at(const uint8_t* buf, size_t len, uint32_t data_ptr) {
    if (data_ptr < 4 || (size_t)data_ptr >= len)
        return {};
    uint32_t slen = r_u32(buf + data_ptr - 4);
    if ((size_t)data_ptr + slen > len)
        return {};
    return std::string((const char*)(buf + data_ptr), slen);
}

inline bool is_riff(const uint8_t* p, size_t n) {
    return n >= 4 && p[0] == 'R' && p[1] == 'I' && p[2] == 'F' && p[3] == 'F';
}
inline bool is_oggs(const uint8_t* p, size_t n) {
    return n >= 4 && p[0] == 'O' && p[1] == 'g' && p[2] == 'g' && p[3] == 'S';
}

inline bool valid_txtr_entry_size_for_single(size_t sz, const uint8_t* entry, size_t entry_avail, size_t payload_size) {
    if (sz > entry_avail)
        return false;
    if (sz >= 16) {
        uint32_t block_size = r_u32(entry + 8);

        if (block_size > payload_size)
            return false;
    }
    if (sz == 28) {
        uint32_t width = r_u32(entry + 12);
        uint32_t height = r_u32(entry + 16);
        uint32_t idx = r_u32(entry + 20);
        if (width == 0 || width > 16384)
            return false;
        if (height == 0 || height > 16384)
            return false;
        if (idx > 1000)
            return false;
    }
    return true;
}

inline size_t detect_txtr_entry_size_from_ptrs(uint32_t count, const uint32_t* ptrs) {
    if (count >= 2) {
        size_t diff = (size_t)(ptrs[1] - ptrs[0]);
        if (diff == 8 || diff == 12 || diff == 16 || diff == 28)
            return diff;
    }
    return 16;
}

// A correctly-sized TXTR entry ends in the absolute TextureData pointer. With a
// single texture there's no stride to measure, and the structural probe below
// can't tell an 8-byte (GMS1/pre-2.0.6) entry from a 16-byte one when the
// would-be intermediate fields happen to read as zero -- which is exactly the
// case that left old single-texture data files with an unrelocated blob pointer.
// Confirm a candidate size by checking that its trailing pointer lands on a
// known image blob (PNG / 2zoq / fioq / DDS). The offset is taken relative to
// the first entry, so this holds whether or not the chunk has been
// position-shifted (a uniform shift cancels out).
inline bool txtr_single_size_hits_blob(const uint8_t* txtr_payload, size_t payload_size, size_t sz) {
    if (8 + sz > payload_size)
        return false;
    uint32_t first_entry_ptr = r_u32(txtr_payload + 4);
    uint32_t data_ptr = r_u32(txtr_payload + 8 + sz - 4);
    if (data_ptr == 0 || data_ptr < first_entry_ptr)
        return false;
    size_t rel = 8 + (size_t)(data_ptr - first_entry_ptr);
    if (rel + 4 > payload_size)
        return false;
    const uint8_t* p = txtr_payload + rel;
    if (p[0] == '2' && p[1] == 'z' && p[2] == 'o' && p[3] == 'q')
        return true; // 2zoq: BZ2 + YYG-QOIF
    if (p[0] == 'f' && p[1] == 'i' && p[2] == 'o' && p[3] == 'q')
        return true; // fioq: bare YYG-QOIF
    if (p[0] == 'D' && p[1] == 'D' && p[2] == 'S' && p[3] == ' ')
        return true; // DDS
    if (p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G')
        return true; // PNG
    return false;
}

// TXTR entry stride varies by version (8/12/16/28). Two entries give an exact answer; otherwise
// confirm by where the trailing pointer lands, falling back to a structural sanity probe.
inline size_t detect_txtr_entry_size(const uint8_t* txtr_payload, size_t payload_size) {
    uint32_t count = r_u32(txtr_payload);
    if (count >= 2) {
        uint32_t p0 = r_u32(txtr_payload + 4);
        uint32_t p1 = r_u32(txtr_payload + 8);
        uint32_t ptrs[2] = { p0, p1 };
        return detect_txtr_entry_size_from_ptrs(2, ptrs);
    }
    if (count == 0 || payload_size < 16)
        return 16;
    // Smallest-first: the tightest size whose trailing pointer resolves to a
    // real blob is the right one. A shorter candidate's would-be pointer field
    // is an intermediate scalar (GeneratedMips / block size / dimensions) that
    // won't alias a blob offset.
    for (size_t sz : { (size_t)8, (size_t)12, (size_t)16, (size_t)28 }) {
        if (txtr_single_size_hits_blob(txtr_payload, payload_size, sz))
            return sz;
    }
    const uint8_t* entry = txtr_payload + 8;
    size_t entry_avail = payload_size - 8;
    for (size_t sz : { (size_t)28, (size_t)16, (size_t)12, (size_t)8 }) {
        if (valid_txtr_entry_size_for_single(sz, entry, entry_avail, payload_size))
            return sz;
    }
    return 16;
}

} // namespace Gmtoolkit
