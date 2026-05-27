// SPDX-License-Identifier: MIT

#include "Toolkit/IO.h"
#include "Toolkit/Platform.h"
#include "Toolkit/Log.h"

#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
using Gmtoolkit::find_chunk;
using Gmtoolkit::r_u32;
using Gmtoolkit::r_u64;
using Gmtoolkit::w_u32;
using Gmtoolkit::w_u64;

struct info_flag {
    const char* name;
    uint32_t value;
};
static const info_flag INFO_FLAGS[] = {
    { "Fullscreen", 0x00001 },  { "SyncVertex1", 0x00002 },      { "SyncVertex2", 0x00004 }, { "Interpolate", 0x00008 },
    { "Scale", 0x00010 },       { "ShowCursor", 0x00020 },       { "Sizeable", 0x00040 },    { "ScreenKey", 0x00080 },
    { "SyncVertex3", 0x00100 }, { "BorderlessWindow", 0x04000 },
};

uint32_t parse_flag_list(const char* s) {
    if (!s)
        return 0;
    uint32_t out = 0;
    while (*s) {
        const char* end = strchr(s, ',');
        size_t len = end ? (size_t)(end - s) : strlen(s);
        bool matched = false;
        for (const auto& f : INFO_FLAGS) {
            if (strlen(f.name) == len && strncmp(s, f.name, len) == 0) {
                out |= f.value;
                matched = true;
                break;
            }
        }
        if (!matched) {
            Gmtoolkit::err("Unknown InfoFlag '%.*s'", (int)len, s);
            return UINT32_MAX;
        }
        s = end ? end + 1 : s + len;
    }
    return out;
}

// Mirrors the GMS2 runtime's InfoNumber: byte-swap timestamp, fold in entropy, mix with header fields.
// One of four slots stores this value, and the runtime refuses to launch if recomputation disagrees.
static uint64_t get_info_number(uint64_t first_random, bool info_ts_offset, uint64_t timestamp, uint32_t game_id,
                                uint32_t default_w, uint32_t default_h, uint32_t info, uint32_t bytecode_version) {
    uint64_t n = timestamp;
    if (info_ts_offset)
        n -= 1000;
    uint64_t t = n;
    t = ((t << 56) & 0xFF00000000000000ULL) | ((t >> 8) & 0x00FF000000000000ULL) | ((t << 32) & 0x0000FF0000000000ULL) |
        ((t >> 16) & 0x000000FF00000000ULL) | ((t << 8) & 0x00000000FF000000ULL) | ((t >> 24) & 0x0000000000FF0000ULL) |
        ((t >> 16) & 0x000000000000FF00ULL) | ((t >> 32) & 0x00000000000000FFULL);
    n = t;
    n ^= first_random;
    n = ~n;
    n ^= ((uint64_t)game_id << 32) | game_id;
    uint64_t wi = (uint64_t)(default_w + info);
    uint64_t hi = (uint64_t)(default_h + info);
    n ^= (wi << 48) | (hi << 32) | (hi << 16) | wi;
    n ^= bytecode_version;
    return n;
}

int toggle_flags_and_uid_in_file(const char* path, uint32_t set_mask, uint32_t clear_mask) {
    FILE* f = fopen(path, "r+b");
    if (!f) {
        perror(path);
        return -1;
    }

    uint8_t hdr[8192];
    size_t nread = fread(hdr, 1, sizeof(hdr), f);
    if (nread < 0x100) {
        Gmtoolkit::err("data file too small to contain GEN8");
        fclose(f);
        return -1;
    }

    size_t gen8_start, gen8_size;
    if (find_chunk(hdr, nread, "GEN8", &gen8_start, &gen8_size) != 0) {
        Gmtoolkit::err("No GEN8 chunk in first 8KB; data file malformed");
        fclose(f);
        return -1;
    }
    (void)gen8_size;

    uint8_t* g = hdr + gen8_start;
    uint8_t bytecode_version = g[1];
    uint32_t game_id = r_u32(g + 0x14);
    uint32_t major = r_u32(g + 0x2C);
    uint32_t default_w = r_u32(g + 0x3C);
    uint32_t default_h = r_u32(g + 0x40);
    uint32_t old_info = r_u32(g + 0x44);
    uint64_t timestamp = r_u64(g + 0x5C);
    size_t room_off = (bytecode_version >= 14) ? 0x80 : 0x7C;
    uint32_t room_count = r_u32(g + room_off);

    uint32_t new_info = (old_info | set_mask) & ~clear_mask;
    if (new_info == old_info) {
        Gmtoolkit::tprint("[INFO] InfoFlags unchanged (0x%08x)\n", old_info);
        fclose(f);
        return 0;
    }

    uint8_t info_buf[4];
    w_u32(info_buf, new_info);

    // GMS1 has no UID table; just rewrite InfoFlags in place.
    if (major < 2) {
        if (fseek(f, (long)(gen8_start + 0x44), SEEK_SET) != 0 || fwrite(info_buf, 1, 4, f) != 4) {
            perror("write InfoFlags (GMS1)");
            fclose(f);
            return -1;
        }
        fflush(f);
        fclose(f);
        Gmtoolkit::tprint("[INFO] InfoFlags BEFORE: 0x%08x\n", old_info);
        Gmtoolkit::tprint("[INFO] InfoFlags AFTER : 0x%08x  (GMS1, no UID recompute)\n", new_info);
        return 0;
    }

    size_t uid_off = gen8_start + room_off + 4 + (size_t)room_count * 4;
    size_t need_end = uid_off + 8 + 4 * 8;
    if (need_end > nread) {
        Gmtoolkit::err("GEN8 UID table at offset 0x%zx extends past 8KB read window", uid_off);
        fclose(f);
        return -1;
    }

    uint64_t first_random = r_u64(hdr + uid_off);

    // GMS2 picks one of four UID slots from a hash of GEN8 fields.
    long a = (long)((timestamp & 0xFFFF) / 7);
    long b = (long)(int32_t)(game_id - default_w);
    long sum = a + b + (long)room_count;
    long loc = (sum < 0 ? -sum : sum) % 4;
    size_t slot_off = uid_off + 8 + (size_t)loc * 8;
    uint64_t stored = r_u64(hdr + slot_off);

    // Newer builds subtract 1000 from the timestamp before mixing; the file tells us which.
    uint64_t expected_true =
        get_info_number(first_random, true, timestamp, game_id, default_w, default_h, old_info, bytecode_version);
    uint64_t expected_false =
        get_info_number(first_random, false, timestamp, game_id, default_w, default_h, old_info, bytecode_version);
    bool info_ts_offset;
    if (stored == expected_true)
        info_ts_offset = true;
    else if (stored == expected_false)
        info_ts_offset = false;
    else {
        Gmtoolkit::err("GMS2RandomUID didn't match either expected value for old Info; "
                       "data file may have been edited externally.");
        fclose(f);
        return -1;
    }

    uint64_t new_uid = get_info_number(first_random, info_ts_offset, timestamp, game_id, default_w, default_h, new_info,
                                       bytecode_version);

    uint8_t uid_buf[8];
    w_u64(uid_buf, new_uid);

    if (fseek(f, (long)(gen8_start + 0x44), SEEK_SET) != 0 || fwrite(info_buf, 1, 4, f) != 4 ||
        fseek(f, (long)slot_off, SEEK_SET) != 0 || fwrite(uid_buf, 1, 8, f) != 8) {
        perror("write InfoFlags/UID");
        fclose(f);
        return -1;
    }
    fflush(f);
    fclose(f);

    Gmtoolkit::tprint("[INFO] InfoFlags BEFORE: 0x%08x\n", old_info);
    Gmtoolkit::tprint("[INFO] InfoFlags AFTER : 0x%08x\n", new_info);
    Gmtoolkit::tprint("[INFO] GMS2RandomUID slot %ld recomputed\n", loc);
    return 0;
}
