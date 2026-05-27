// SPDX-License-Identifier: MIT

#include "Toolkit/Verify.h"
#include "Toolkit/IO.h"
#include "Toolkit/Log.h"
#include "Toolkit/Platform.h"

#include <cstring>
#include <string>
#include <unordered_map>

namespace Gmtoolkit {

namespace {

struct Chunk {
    size_t pay_off;
    size_t pay_size;
};

int walk_form(const uint8_t* d, size_t sz, size_t form_size, std::unordered_map<std::string, Chunk>& out_chunks) {
    size_t i = 8;
    size_t end = 8 + form_size;
    if (end > sz) {
        err("verify: FORM size 0x%zx extends past file (size 0x%zx)", form_size, sz);
        return -1;
    }
    while (i + 8 <= end) {
        std::string tag((const char*)(d + i), 4);
        uint32_t csz = r_u32(d + i + 4);
        size_t pay = i + 8;
        if (pay + csz > end) {
            err("verify: chunk %s @0x%zx extends past FORM end", tag.c_str(), i);
            return -1;
        }
        out_chunks[tag] = { pay, csz };
        i = pay + csz;
    }
    if (i != end) {
        err("verify: chunk walk ended at 0x%zx but FORM ends at 0x%zx", i, end);
        return -1;
    }
    return 0;
}

// Ptr table -> length-prefixed strings; each must be NUL-terminated past its declared length.
int check_strg(const uint8_t* d, size_t sz, const Chunk& c) {
    if (c.pay_size < 4) {
        err("verify: STRG too small");
        return -1;
    }
    uint32_t count = r_u32(d + c.pay_off);
    if (4 + (size_t)count * 4 > c.pay_size) {
        err("verify: STRG ptr-table overflows chunk (count=%u)", count);
        return -1;
    }
    size_t chunk_end = c.pay_off + c.pay_size;
    for (uint32_t k = 0; k < count; k++) {
        uint32_t ptr = r_u32(d + c.pay_off + 4 + (size_t)k * 4);

        if (ptr + 4 > sz) {
            err("verify: STRG[%u] ptr 0x%x len-field OOB", k, ptr);
            return -1;
        }
        uint32_t slen = r_u32(d + ptr);
        if ((size_t)ptr + 4 + slen + 1 > sz) {
            err("verify: STRG[%u] ptr 0x%x len=%u extends past file", k, ptr, slen);
            return -1;
        }
        if ((size_t)ptr < c.pay_off || (size_t)ptr + 4 + slen + 1 > chunk_end) {
            err("verify: STRG[%u] entry [0x%x..0x%zx) outside chunk [0x%zx..0x%zx)", k, ptr, (size_t)ptr + 4 + slen + 1,
                c.pay_off, chunk_end);
            return -1;
        }
        if (d[ptr + 4 + slen] != 0) {
            err("verify: STRG[%u] missing null terminator at 0x%zx", k, (size_t)ptr + 4 + slen);
            return -1;
        }
    }
    return 0;
}

int check_ptr_table_in_chunk(const char* name, const uint8_t* d, size_t sz, const Chunk& c) {
    if (c.pay_size < 4)
        return 0;
    uint32_t count = r_u32(d + c.pay_off);
    if (count == 0)
        return 0;
    if (4 + (size_t)count * 4 > c.pay_size) {
        err("verify: %s ptr-table overflows chunk (count=%u)", name, count);
        return -1;
    }
    size_t chunk_end = c.pay_off + c.pay_size;
    for (uint32_t k = 0; k < count; k++) {
        uint32_t ent = r_u32(d + c.pay_off + 4 + (size_t)k * 4);
        if (ent == 0)
            continue;
        if ((size_t)ent + 4 > sz) {
            err("verify: %s[%u] entry 0x%x OOB (file size 0x%zx)", name, k, ent, sz);
            return -1;
        }
        if ((size_t)ent < c.pay_off || (size_t)ent >= chunk_end) {
            err("verify: %s[%u] entry 0x%x outside chunk [0x%zx..0x%zx)", name, k, ent, c.pay_off, chunk_end);
            return -1;
        }
    }
    return 0;
}

// UTMT expects every TXTR blob pointer to be 0x80-aligned absolute; non-aligned data fails to round-trip.
int check_txtr_blob_alignment(const uint8_t* d, size_t sz, const Chunk& c) {
    if (c.pay_size < 4)
        return 0;
    uint32_t count = r_u32(d + c.pay_off);
    if (count == 0)
        return 0;
    if (4 + (size_t)count * 4 > c.pay_size)
        return 0;
    size_t entry_size = 16;
    if (count >= 2) {
        uint32_t p0 = r_u32(d + c.pay_off + 4);
        uint32_t p1 = r_u32(d + c.pay_off + 8);
        size_t diff = (size_t)(p1 - p0);
        if (diff == 8 || diff == 12 || diff == 16 || diff == 28)
            entry_size = diff;
    }
    size_t ptr_off_in_entry = entry_size - 4;
    for (uint32_t k = 0; k < count; k++) {
        uint32_t ent = r_u32(d + c.pay_off + 4 + (size_t)k * 4);
        if (ent == 0 || (size_t)ent + entry_size > sz)
            continue;
        uint32_t blob = r_u32(d + ent + ptr_off_in_entry);
        if (blob == 0)
            continue;
        if ((blob & 0x7Fu) != 0) {
            err("verify: TXTR[%u] blob ptr 0x%x not 0x80-aligned (UTMT requires absolute 0x80 alignment)", k, blob);
            return -1;
        }
    }
    return 0;
}

int check_audo(const uint8_t* d, size_t sz, const Chunk& c) {
    if (c.pay_size < 4)
        return 0;
    uint32_t count = r_u32(d + c.pay_off);
    if (4 + (size_t)count * 4 > c.pay_size) {
        err("verify: AUDO ptr-table overflows chunk (count=%u)", count);
        return -1;
    }
    for (uint32_t k = 0; k < count; k++) {
        uint32_t ent = r_u32(d + c.pay_off + 4 + (size_t)k * 4);
        if (ent == 0)
            continue;
        if ((size_t)ent + 4 > sz) {
            err("verify: AUDO[%u] entry 0x%x OOB", k, ent);
            return -1;
        }
        uint32_t blob_sz = r_u32(d + ent);
        if ((size_t)ent + 4 + blob_sz > sz) {
            err("verify: AUDO[%u] entry 0x%x blob size %u extends past file", k, ent, blob_sz);
            return -1;
        }
    }
    return 0;
}

} // namespace

int verify_output(const char* data_path) {
    MappedFile mf;
    if (mapped_file_open(data_path, &mf) != 0) {
        err("verify: cannot open %s", data_path);
        return -1;
    }
    const uint8_t* d = mf.data;
    size_t sz = mf.size;

    if (sz < 8 || memcmp(d, "FORM", 4) != 0) {
        err("verify: not a FORM file");
        mapped_file_close(&mf);
        return -1;
    }
    uint32_t form_size = r_u32(d + 4);
    if ((size_t)form_size + 8 != sz) {
        err("verify: FORM size 0x%x + 8 != file size 0x%zx", form_size, sz);
        mapped_file_close(&mf);
        return -1;
    }

    std::unordered_map<std::string, Chunk> chunks;
    if (walk_form(d, sz, form_size, chunks) != 0) {
        mapped_file_close(&mf);
        return -1;
    }

    int rc = 0;
    auto it = chunks.find("STRG");
    if (it != chunks.end())
        rc |= check_strg(d, sz, it->second);

    for (const char* name : { "TPAG", "SPRT", "BGND", "PATH", "SCPT", "SHDR", "FONT", "TMLN", "OBJT", "ROOM", "CODE",
                              "TXTR", "SOND", "AGRP" }) {
        auto cit = chunks.find(name);
        if (cit != chunks.end())
            rc |= check_ptr_table_in_chunk(name, d, sz, cit->second);
    }

    auto audo_it = chunks.find("AUDO");
    if (audo_it != chunks.end())
        rc |= check_audo(d, sz, audo_it->second);

    auto txtr_it = chunks.find("TXTR");
    if (txtr_it != chunks.end())
        rc |= check_txtr_blob_alignment(d, sz, txtr_it->second);

    mapped_file_close(&mf);
    return rc == 0 ? 0 : -1;
}

} // namespace Gmtoolkit
