// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace GMSLib::SaveBackend {

struct RoomShiftOptions {
    bool gms2;
    bool gms2_3;
    bool has_inst_creation;

    bool is_2022_1;
    bool is_2_3_2;
    bool is_non_lts_2023_2;
    bool is_2024_6;
    bool is_2024_2;
    bool is_2024_4;
};

inline uint32_t r_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline void w_u32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

bool is_listchunk_with_ptrtable(const char* tag);
// Chunks with a u32 version prefix before the count + ptr table; skipping
// that prefix avoids reading the version as the entry count.
bool is_versioned_listchunk(const char* tag);
void shift_chunk_ptab(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta);
void shift_versioned_chunk_ptab(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta);
void shift_txtr_blob_offs(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta);
void shift_vari_first_addrs(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta);
void shift_func_first_addrs(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta);
void shift_audo_ptrs(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta);
void shift_objt_internals(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta, bool gms_2022_5_plus);
void shift_tgin_internals(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta);
void shift_shdr_internals(uint8_t* buf, size_t new_payload_off, size_t payload_size, size_t orig_payload_off,
                          int32_t delta, uint8_t bytecode_version);
struct FontEntryLayout {
    bool has_ascender_offset;
    bool has_ascender;
    bool has_sdf_spread;
    bool has_line_height;
    bool align_4_after;
};
void shift_font_internals(uint8_t* buf, size_t new_payload_off, size_t payload_size, size_t orig_payload_off,
                          int32_t delta, const FontEntryLayout& layout,
                          const std::vector<uint32_t>& sorted_tpag_positions);
void shift_tags_internals(uint8_t* buf, size_t new_payload_off, size_t payload_size, size_t orig_payload_off,
                          int32_t delta);
void shift_room_internals(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta,
                          const RoomShiftOptions& opt);
typedef void (*EntryCallback)(uint8_t* buf, size_t entry_off, int32_t delta, void* user);
void shift_pointer_list(uint8_t* buf, size_t buf_len, size_t list_off, int32_t delta, EntryCallback cb, void* user);
size_t collect_listchunk_entry_positions(const uint8_t* buf, size_t payload_off, size_t payload_size,
                                         std::vector<uint32_t>& out);
void scan_and_bump_pointers(uint8_t* buf, size_t payload_off, size_t payload_size,
                            const std::vector<uint32_t>& sorted_targets, int32_t delta);

} // namespace GMSLib::SaveBackend
