// SPDX-License-Identifier: MIT

#include "GMSLib/SaveBackend/CodeChunk.h"

#include "Toolkit/IO.h"

#include <algorithm>
#include <cstring>

namespace GMSLib::SaveBackend {

namespace {

using Gmtoolkit::r_u16;
using Gmtoolkit::r_u32;
using Gmtoolkit::w_u32;

inline int32_t read_i32(const uint8_t* p) {
    return (int32_t)r_u32(p);
}

static bool find_chunk_b(const uint8_t* win, size_t win_size, const char* tag, size_t* out_start, size_t* out_size) {
    return Gmtoolkit::find_chunk(win, win_size, tag, out_start, out_size) == 0;
}

} // namespace

bool find_code_chunk(const uint8_t* win, size_t win_size, size_t* out_start, size_t* out_size) {
    return find_chunk_b(win, win_size, "CODE", out_start, out_size);
}
bool find_strg_chunk(const uint8_t* win, size_t win_size, size_t* out_start, size_t* out_size) {
    return find_chunk_b(win, win_size, "STRG", out_start, out_size);
}
bool find_vari_chunk(const uint8_t* win, size_t win_size, size_t* out_start, size_t* out_size) {
    return find_chunk_b(win, win_size, "VARI", out_start, out_size);
}
bool find_func_chunk(const uint8_t* win, size_t win_size, size_t* out_start, size_t* out_size) {
    return find_chunk_b(win, win_size, "FUNC", out_start, out_size);
}

std::string read_strg_string(const uint8_t* win, size_t win_size, uint32_t data_ptr) {
    if (data_ptr < 4 || (size_t)data_ptr >= win_size)
        return std::string();
    uint32_t len = r_u32(win + data_ptr - 4);
    if ((size_t)data_ptr + len > win_size)
        return std::string();
    return std::string((const char*)(win + data_ptr), len);
}

// CODE struct is 16 bytes pre-2.3, 20 from 2.3 onwards (the self_offset tail came with sub-entries).
// bytecode_offset is stored as a relative i32 from the entry+12, not as an absolute pointer.
bool parse_code_entries(const uint8_t* win, size_t win_size, size_t code_start, size_t code_size,
                        std::vector<CodeEntry>* out, uint8_t bytecode_version, bool using_gms_2_3) {
    out->clear();
    if (code_size < 4)
        return false;
    if (bytecode_version <= 14) {
        return false;
    }
    const size_t entry_size = using_gms_2_3 ? 20 : 16;
    uint32_t count = r_u32(win + code_start);
    size_t ptab = code_start + 4;
    if (ptab + 4ull * count > code_start + code_size)
        return false;
    out->reserve(count);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t ent = r_u32(win + ptab + i * 4);
        if ((size_t)ent + entry_size > win_size)
            return false;
        CodeEntry e;
        e.entry_offset = ent;
        uint32_t name_ptr = r_u32(win + ent + 0);
        e.length = r_u32(win + ent + 4);
        e.locals_count = r_u16(win + ent + 8);
        uint16_t a = r_u16(win + ent + 10);
        e.weird_local_flag = (a & 0x8000) != 0;
        e.args_count = (uint16_t)(a & 0x7FFF);
        int32_t br = read_i32(win + ent + 12);
        e.bytecode_offset = (uint32_t)((int32_t)(ent + 12) + br);

        e.self_offset = (entry_size >= 20) ? r_u32(win + ent + 16) : 0;
        e.name = read_strg_string(win, win_size, name_ptr);
        out->push_back(std::move(e));
    }
    return true;
}

bool parse_references(const uint8_t* win, size_t win_size, size_t chunk_start, size_t chunk_size, bool is_vari,
                      std::vector<Reference>* out, uint8_t bytecode_version) {
    out->clear();
    size_t p = chunk_start;
    size_t end = chunk_start + chunk_size;
    const bool bc15p = bytecode_version >= 15;
    const bool gms_2_3 = bytecode_version >= 17;

    if (is_vari) {
        const size_t pre_sz = bc15p ? 12 : 0;
        const size_t ent_sz = bc15p ? 20 : 12;
        if (chunk_size < pre_sz)
            return false;
        p += pre_sz;
        while (p + ent_sz <= end) {
            Reference r;
            uint32_t name_ptr = r_u32(win + p + 0);
            uint32_t occ_off = bc15p ? 12 : 4;
            uint32_t fa_off = bc15p ? 16 : 8;
            r.occurrence_count = r_u32(win + p + occ_off);
            uint32_t first_or_term = r_u32(win + p + fa_off);
            r.name = read_strg_string(win, win_size, name_ptr);
            if (r.occurrence_count > 0) {
                r.first_address = first_or_term;
                r.name_string_id = 0;
            } else {
                r.first_address = 0;
                r.name_string_id = (int32_t)first_or_term;
            }
            out->push_back(std::move(r));
            p += ent_sz;
        }
    } else {
        if (chunk_size < 4)
            return false;
        uint32_t count = r_u32(win + p);
        p += 4;
        for (uint32_t i = 0; i < count; i++) {
            if (p + 12 > end)
                return false;
            Reference r;
            uint32_t name_ptr = r_u32(win + p + 0);
            r.occurrence_count = r_u32(win + p + 4);
            uint32_t first_or_term = r_u32(win + p + 8);
            r.name = read_strg_string(win, win_size, name_ptr);
            if (r.occurrence_count > 0) {
                r.first_address = (gms_2_3 && first_or_term >= 4) ? first_or_term - 4 : first_or_term;
                r.name_string_id = 0;
            } else {
                r.first_address = 0;
                r.name_string_id = (int32_t)first_or_term;
            }
            out->push_back(std::move(r));
            p += 12;
        }
    }
    return true;
}

void build_address_labels(const uint8_t* win, size_t win_size, const std::vector<Reference>& refs,
                          std::vector<AddressLabel>* out) {
    out->clear();
    for (uint32_t ri = 0; ri < refs.size(); ri++) {
        const Reference& r = refs[ri];
        if (r.occurrence_count == 0 || r.first_address == 0)
            continue;
        uint32_t operand_addr = r.first_address + 4;
        for (uint32_t i = 0; i < r.occurrence_count; i++) {
            if ((size_t)operand_addr + 4 > win_size)
                break;
            AddressLabel lbl;
            lbl.address = operand_addr - 4;
            lbl.ref_index = ri;
            out->push_back(lbl);
            uint32_t val = r_u32(win + operand_addr);
            uint32_t next_off = val & 0x07FFFFFFu;
            if (i + 1 == r.occurrence_count || next_off == 0)
                break;
            operand_addr += next_off;
        }
    }
    std::sort(out->begin(), out->end(),
              [](const AddressLabel& a, const AddressLabel& b) { return a.address < b.address; });
}

const AddressLabel* find_label(const std::vector<AddressLabel>& labels, uint32_t address) {
    auto it = std::lower_bound(labels.begin(), labels.end(), address,
                               [](const AddressLabel& a, uint32_t v) { return a.address < v; });
    if (it == labels.end() || it->address != address)
        return nullptr;
    return &*it;
}

} // namespace GMSLib::SaveBackend
