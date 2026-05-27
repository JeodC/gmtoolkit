// SPDX-License-Identifier: GPL-3.0-or-later

#include "GMSLib/SaveBackend/Pools.h"
#include "Toolkit/IO.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace GMSLib::SaveBackend {

using Gmtoolkit::r_u32;

static void bump_with_log(Gmtoolkit::Version& v, const char*, uint32_t M, uint32_t m = 0, uint32_t r = 0,
                          uint32_t b = 0) {
    if (v.is_at_least(M, m, r, b))
        return;
    v.bump_to(M, m, r, b);
}

// GEN8 can lie or stay stale across IDE upgrades, so we shape-probe chunks for the real version.
// Each detect_* call only bumps the version forward; nothing here downgrades.
void Pools::detect_format_versions() {
    if (chunks.find("UILR") != chunks.end()) {
        bump_with_log(version, "UILR-presence", 2024, 13);
        version.branch = Gmtoolkit::BranchType::Post2022_0;
    } else if (chunks.find("PSEM") != chunks.end()) {
        bump_with_log(version, "PSEM-presence", 2023, 2);
        version.branch = Gmtoolkit::BranchType::Post2022_0;
    } else if (chunks.find("FEAT") != chunks.end()) {
        bump_with_log(version, "FEAT-presence", 2022, 8);
    }

    detect_room_2022_1();
    detect_font_2022_2();
    detect_txtr_2022_3();
    detect_objt_2022_5();
    detect_txtr_2022_5();
    detect_extn_2022_6();
    detect_tgin_2022_9();
    detect_tgin_2023_1();
    detect_extn_2023_4();
    detect_psem_2023_x();
    detect_font_2023_6();
    detect_room_2024_2();
    detect_sond_2024_6();
    detect_sprt_2024_6();
    detect_func_2024_8();
    detect_font_2024_11();
    detect_agrp_2024_14();
    detect_font_2024_14();
    detect_bgnd_2024_14_1();
}

void Pools::detect_sond_2024_6() {
    if (!version.is_non_lts_at_least(2023, 2) || version.is_at_least(2024, 6))
        return;
    auto it = chunks.find("SOND");
    if (it == chunks.end())
        return;
    size_t cs = it->second.size;
    size_t off = it->second.payload_off;
    if (cs < 4)
        return;
    uint32_t cnt = r_u32(buf.data() + off);
    if (cnt == 0 || 4 + (size_t)cnt * 4 > cs)
        return;

    std::vector<uint32_t> ptrs;
    ptrs.reserve(cnt);
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t p = r_u32(buf.data() + off + 4 + i * 4);
        if (p != 0)
            ptrs.push_back(p);
    }
    // Old entry-0 end vs entry-1 start: 4-byte gap means the new AudioLength field is present.
    if (ptrs.size() >= 2) {
        if (ptrs[0] + (4 * 9) == ptrs[1] - 4) {
            bump_with_log(version, "SOND", 2024, 6);
        }
    } else if (ptrs.size() == 1) {
        // Single entry: peek at the would-be AudioLength field; non-zero confirms the new layout.
        size_t probe = ptrs[0] + 4 * 9;
        if (probe + 4 <= buf.size() && r_u32(buf.data() + probe) != 0) {
            bump_with_log(version, "SOND", 2024, 6);
        }
    }
}

void Pools::detect_sprt_2024_6() {
    if (!version.is_non_lts_at_least(2023, 2) || version.is_at_least(2024, 6))
        return;
    auto it = chunks.find("SPRT");
    if (it == chunks.end())
        return;
    size_t chunk_start = it->second.payload_off;
    size_t chunk_size = it->second.size;
    if (chunk_size < 4)
        return;

    uint32_t sprite_count = r_u32(buf.data() + chunk_start);
    if (sprite_count == 0)
        return;

    for (uint32_t i = 0; i < sprite_count; i++) {
        size_t slot = chunk_start + 4 + (size_t)i * 4;
        if (slot + 4 > buf.size())
            break;
        uint32_t sprite_ptr = r_u32(buf.data() + slot);
        if (sprite_ptr == 0)
            continue;

        uint32_t next_sprite_ptr = 0;
        for (uint32_t j = i + 1; j < sprite_count; j++) {
            size_t s = chunk_start + 4 + (size_t)j * 4;
            if (s + 4 > buf.size())
                break;
            uint32_t p = r_u32(buf.data() + s);
            if (p != 0) {
                next_sprite_ptr = p;
                break;
            }
        }

        size_t hdr = sprite_ptr + 4;
        if (hdr + 24 > buf.size())
            continue;
        int32_t full_w = (int32_t)r_u32(buf.data() + hdr + 0);
        int32_t full_h = (int32_t)r_u32(buf.data() + hdr + 4);
        int32_t ml = (int32_t)r_u32(buf.data() + hdr + 8);
        int32_t mr = (int32_t)r_u32(buf.data() + hdr + 12);
        int32_t mb = (int32_t)r_u32(buf.data() + hdr + 16);
        int32_t mt = (int32_t)r_u32(buf.data() + hdr + 20);
        int32_t bbox_w = mr - ml + 1;
        int32_t bbox_h = mb - mt + 1;
        if (bbox_w == full_w && bbox_h == full_h)
            continue;

        size_t cur = hdr + 24 + 28;
        if (cur + 12 > buf.size())
            continue;
        int32_t special = (int32_t)r_u32(buf.data() + cur);
        if (special != -1)
            continue;
        uint32_t s_version = r_u32(buf.data() + cur + 4);
        uint32_t s_sprite_type = r_u32(buf.data() + cur + 8);
        if (s_sprite_type != 0)
            continue;
        cur += 12 + 8;
        if (s_version != 3)
            continue;

        if (cur + 12 > buf.size())
            continue;
        uint32_t sequence_off = r_u32(buf.data() + cur + 0);
        uint32_t nine_slice_off = r_u32(buf.data() + cur + 4);
        cur += 8;
        uint32_t texture_count = r_u32(buf.data() + cur);
        cur += 4 + (size_t)texture_count * 4;
        if (cur + 4 > buf.size())
            continue;
        uint32_t mask_count = r_u32(buf.data() + cur);
        cur += 4;
        if (mask_count == 0)
            continue;

        auto pad4 = [](uint32_t v) { return (v % 4) ? v + (4 - (v % 4)) : v; };
        uint32_t full_len = pad4((uint32_t)(((full_w + 7) / 8) * full_h * (int32_t)mask_count));
        uint32_t bbox_len = pad4((uint32_t)(((bbox_w + 7) / 8) * bbox_h * (int32_t)mask_count));

        uint64_t expected_end;
        bool lenient = false;
        if (sequence_off != 0)
            expected_end = sequence_off;
        else if (nine_slice_off != 0)
            expected_end = nine_slice_off;
        else if (next_sprite_ptr != 0)
            expected_end = next_sprite_ptr;
        else {
            expected_end = chunk_start + chunk_size;
            lenient = true;
        }

        uint64_t full_end = (uint64_t)cur + full_len;
        if (full_end == expected_end)
            break;
        if (lenient && (full_end % 16) != 0 && full_end + (16 - (full_end % 16)) == expected_end)
            break;

        uint64_t bbox_end = (uint64_t)cur + bbox_len;
        if (bbox_end == expected_end ||
            (lenient && (bbox_end % 16) != 0 && bbox_end + (16 - (bbox_end % 16)) == expected_end)) {
            bump_with_log(version, "SPRT", 2024, 6);
            break;
        }
    }
}

void Pools::detect_room_2024_2() {
    if (!version.is_at_least(2023, 2) || version.is_at_least(2024, 4))
        return;
    auto it = chunks.find("ROOM");
    if (it == chunks.end())
        return;
    size_t room_off = it->second.payload_off;
    size_t room_cs = it->second.size;
    if (room_cs < 4)
        return;

    uint32_t room_count = r_u32(buf.data() + room_off);
    if (room_count == 0)
        return;

    bool checked_2024_2 = version.is_at_least(2024, 2);
    bool found_non_aligned = false;

    for (uint32_t i = 0; i < room_count && !checked_2024_2; i++) {
        size_t room_ptr_off = room_off + 4 + (size_t)i * 4;
        if (room_ptr_off + 4 > buf.size())
            break;
        uint32_t room_ptr = r_u32(buf.data() + room_ptr_off);

        if ((size_t)room_ptr + 22 * 4 + 8 > buf.size())
            continue;
        uint32_t layer_list_ptr = r_u32(buf.data() + room_ptr + 22 * 4);
        uint32_t seqn_ptr = r_u32(buf.data() + room_ptr + 22 * 4 + 4);
        if ((size_t)layer_list_ptr + 4 > buf.size())
            continue;

        int32_t layer_count = (int32_t)r_u32(buf.data() + layer_list_ptr);
        if (layer_count <= 0)
            continue;

        for (int32_t L = 0; L < layer_count; L++) {
            size_t layer_slot = (size_t)layer_list_ptr + 4 + (size_t)L * 4;
            if (layer_slot + 4 > buf.size())
                break;
            uint32_t layer_ptr = r_u32(buf.data() + layer_slot);
            uint32_t next_off = (L == layer_count - 1)
                                    ? seqn_ptr
                                    : (layer_slot + 4 + 4 <= buf.size() ? r_u32(buf.data() + layer_slot + 4) : 0);

            if ((size_t)layer_ptr + 4 > buf.size())
                continue;
            uint32_t layer_type = r_u32(buf.data() + layer_ptr);

            if (layer_type != 1)
                continue;

            size_t cur = (size_t)layer_ptr + 4 + 32;
            if (cur + 4 > buf.size())
                continue;
            uint32_t effect_count = r_u32(buf.data() + cur);
            cur += 4 + (size_t)effect_count * 12 + 4;
            if (cur + 8 > buf.size())
                continue;
            int32_t tile_w = (int32_t)r_u32(buf.data() + cur);
            int32_t tile_h = (int32_t)r_u32(buf.data() + cur + 4);
            cur += 8;

            if (next_off > cur && (int64_t)(next_off - cur) != (int64_t)tile_w * tile_h * 4) {
                bump_with_log(version, "ROOM", 2024, 2);
                checked_2024_2 = true;
                break;
            }

            if ((layer_slot % 4) != 0)
                found_non_aligned = true;
        }
    }

    if (version.is_at_least(2024, 2) && !found_non_aligned) {

        bump_with_log(version, "ROOM", 2024, 4);
    }
}

void Pools::detect_extn_2022_6() {

    if (version.is_at_least(2022, 6) || !version.is_at_least(2, 3))
        return;
    auto it = chunks.find("EXTN");
    if (it == chunks.end())
        return;
    size_t chunk_start = it->second.payload_off;
    size_t chunk_size = it->second.size;
    size_t chunk_end = chunk_start + chunk_size;
    if (chunk_size < 4)
        return;

    int32_t ext_count = (int32_t)r_u32(buf.data() + chunk_start);
    if (ext_count <= 0)
        return;
    size_t ptr_table = chunk_start + 4;
    if (ptr_table + 4 > buf.size())
        return;
    uint32_t first_ext = r_u32(buf.data() + ptr_table);
    uint32_t first_ext_end =
        (ext_count >= 2 && ptr_table + 8 <= buf.size()) ? r_u32(buf.data() + ptr_table + 4) : (uint32_t)chunk_end;

    size_t cur = (size_t)first_ext + 12;
    if (cur + 8 > buf.size())
        return;
    uint32_t files_ptr = r_u32(buf.data() + cur);
    uint32_t options_ptr = r_u32(buf.data() + cur + 4);
    cur += 8;
    if (files_ptr != cur)
        return;
    if (options_ptr <= cur || options_ptr >= chunk_end)
        return;

    cur = options_ptr;
    if (cur + 4 > buf.size())
        return;
    uint32_t option_count = r_u32(buf.data() + cur);
    cur += 4;
    if (option_count > 0) {
        size_t check = cur + 4 * (option_count - 1);
        if (check >= chunk_end)
            return;
        cur += 4 * (option_count - 1);
        if (cur + 4 > buf.size())
            return;
        uint32_t past_last = r_u32(buf.data() + cur) + 12;
        if (past_last >= chunk_end)
            return;
        cur = past_last;
    }
    if (ext_count == 1) {
        cur += 16;
        if ((cur % 16) != 0)
            cur += 16 - (cur % 16);
    }
    if (cur == first_ext_end)
        bump_with_log(version, "EXTN", 2022, 6);
}

void Pools::detect_extn_2023_4() {

    if (version.is_at_least(2023, 4) || !version.is_at_least(2022, 6))
        return;
    auto it = chunks.find("EXTN");
    if (it == chunks.end())
        return;
    size_t chunk_start = it->second.payload_off;
    if (it->second.size < 4)
        return;
    int32_t ext_count = (int32_t)r_u32(buf.data() + chunk_start);
    if (ext_count <= 0)
        return;
    size_t ptr_table = chunk_start + 4;
    if (ptr_table + 4 > buf.size())
        return;
    uint32_t first_ext = r_u32(buf.data() + ptr_table);
    size_t cur = (size_t)first_ext + 12;
    if (cur + 8 > buf.size())
        return;
    uint32_t files_ptr = r_u32(buf.data() + cur);
    uint32_t options_ptr = r_u32(buf.data() + cur + 4);
    if (files_ptr > options_ptr)
        bump_with_log(version, "EXTN", 2023, 4);
}

void Pools::detect_objt_2022_5() {
    if (version.is_at_least(2022, 5) || !version.is_at_least(2, 3))
        return;
    auto it = chunks.find("OBJT");
    if (it == chunks.end())
        return;
    size_t chunk_start = it->second.payload_off;
    size_t chunk_size = it->second.size;
    size_t chunk_end = chunk_start + chunk_size;
    if (chunk_size < 8)
        return;

    uint32_t obj_count = r_u32(buf.data() + chunk_start);
    if (obj_count == 0)
        return;
    if (chunk_start + 8 > buf.size())
        return;
    uint32_t first_obj_ptr = r_u32(buf.data() + chunk_start + 4);

    size_t cur = (size_t)first_obj_ptr + 64;
    if (cur + 4 > buf.size())
        return;
    uint32_t vertex_count = r_u32(buf.data() + cur);
    cur += 4;
    if (cur + 12 + (size_t)vertex_count * 8 >= chunk_end)
        return;
    cur += 12 + (size_t)vertex_count * 8;

    if (cur + 4 > buf.size())
        return;
    uint32_t event_type_count = r_u32(buf.data() + cur);
    cur += 4;
    if (event_type_count != 15) {
        bump_with_log(version, "OBJT", 2022, 5);
        return;
    }
    if (cur + 4 > buf.size())
        return;
    uint32_t sub_event_ptr = r_u32(buf.data() + cur);
    cur += 4;
    if (cur + 56 != sub_event_ptr) {

        bump_with_log(version, "OBJT", 2022, 5);
    }
}

void Pools::detect_txtr_2022_3() {
    if (version.is_at_least(2022, 3) || !version.is_at_least(2, 3))
        return;
    auto it = chunks.find("TXTR");
    if (it == chunks.end())
        return;
    size_t chunk_start = it->second.payload_off;
    size_t chunk_size = it->second.size;
    if (chunk_size < 4)
        return;

    uint32_t tex_count = r_u32(buf.data() + chunk_start);
    bool is_2022_3 = false;
    if (tex_count == 1) {

        size_t cur = chunk_start + 4 + 16;
        if (cur + 4 <= buf.size() && r_u32(buf.data() + cur) > 0) {
            is_2022_3 = true;
        }
    } else if (tex_count > 1) {
        if (chunk_start + 4 + 8 > buf.size())
            return;
        uint32_t a = r_u32(buf.data() + chunk_start + 4);
        uint32_t b = r_u32(buf.data() + chunk_start + 8);
        if (a + 16 == b)
            is_2022_3 = true;
    }
    if (is_2022_3)
        bump_with_log(version, "TXTR", 2022, 3);
}

void Pools::detect_txtr_2022_5() {
    if (version.is_at_least(2022, 5) || !version.is_at_least(2022, 3))
        return;
    auto it = chunks.find("TXTR");
    if (it == chunks.end())
        return;
    size_t chunk_start = it->second.payload_off;
    size_t chunk_size = it->second.size;
    if (chunk_size < 4)
        return;

    uint32_t tex_count = r_u32(buf.data() + chunk_start);
    for (uint32_t i = 0; i < tex_count; i++) {
        size_t slot = chunk_start + 4 + (size_t)i * 4;
        if (slot + 4 > buf.size())
            break;
        uint32_t tex_ptr = r_u32(buf.data() + slot);
        if ((size_t)tex_ptr + 12 + 4 > buf.size())
            continue;

        uint32_t data_ptr_field = r_u32(buf.data() + tex_ptr + 12);
        uint32_t tex_data_ptr = data_ptr_field;
        if ((size_t)tex_data_ptr + 4 > buf.size())
            continue;

        const uint8_t magic[4] = { 0x32, 0x7a, 0x6f, 0x71 };
        if (std::memcmp(buf.data() + tex_data_ptr, magic, 4) != 0)
            continue;

        size_t cur = (size_t)tex_data_ptr + 4 + 4;
        if (cur + 4 > buf.size())
            return;
        bool is_2022_5 = false;
        if (buf[cur + 0] != 'B' || buf[cur + 1] != 'Z' || buf[cur + 2] != 'h') {
            is_2022_5 = true;
        } else {

            cur += 4;
            if (cur + 6 > buf.size())
                return;
            auto r_u24_be = [&](size_t off) -> uint32_t {
                return ((uint32_t)buf[off] << 16) | ((uint32_t)buf[off + 1] << 8) | (uint32_t)buf[off + 2];
            };
            uint32_t m1 = r_u24_be(cur);
            uint32_t m2 = r_u24_be(cur + 3);
            if (m1 != 0x594131 || m2 != 0x595326)
                is_2022_5 = true;
        }
        if (is_2022_5)
            bump_with_log(version, "TXTR", 2022, 5);
        break;
    }
}

void Pools::detect_func_2024_8() {
    if (version.is_at_least(2024, 8) || version.bytecode_version <= 14)
        return;
    auto it = chunks.find("FUNC");
    if (it == chunks.end())
        return;
    size_t chunk_start = it->second.payload_off;
    size_t chunk_size = it->second.size;
    size_t chunk_end = chunk_start + chunk_size;
    if (chunk_size < 4)
        return;

    uint32_t func_count = r_u32(buf.data() + chunk_start);
    size_t past_funcs = chunk_start + 4 + (size_t)func_count * 12;
    if (past_funcs > chunk_end)
        return;
    if (past_funcs == chunk_end) {
        bump_with_log(version, "FUNC", 2024, 8);
        return;
    }

    size_t cur = past_funcs;
    int pad_bytes = 0;
    while ((cur & 15) != 0) {
        if (cur >= chunk_end)
            return;
        if (buf[cur] != 0)
            return;
        cur++;
        pad_bytes++;
    }
    if (cur == chunk_end && pad_bytes < 4) {
        bump_with_log(version, "FUNC", 2024, 8);
    } else if (cur == chunk_end) {

        auto code_it = chunks.find("CODE");
        if (code_it != chunks.end() && code_it->second.size >= 4 &&
            r_u32(buf.data() + code_it->second.payload_off) > 0) {
            bump_with_log(version, "FUNC", 2024, 8);
        }
    }
}

void Pools::detect_font_2022_2() {
    if (version.is_at_least(2022, 2) || version.bytecode_version < 17)
        return;
    auto it = chunks.find("FONT");
    if (it == chunks.end())
        return;
    size_t chunk_start = it->second.payload_off;
    size_t chunk_size = it->second.size;
    if (chunk_size < 4)
        return;

    uint32_t font_count = r_u32(buf.data() + chunk_start);
    if (font_count == 0)
        return;
    uint32_t first_font = 0;
    for (uint32_t i = 0; i < font_count; i++) {
        size_t slot = chunk_start + 4 + (size_t)i * 4;
        if (slot + 4 > buf.size())
            return;
        uint32_t fp = r_u32(buf.data() + slot);
        if (fp != 0) {
            first_font = fp;
            break;
        }
    }
    if (first_font == 0)
        return;

    size_t cur = (size_t)first_font + 48;
    if (cur + 4 > buf.size())
        return;
    uint32_t glyphs_len = r_u32(buf.data() + cur);
    cur += 4;
    if ((uint64_t)glyphs_len * 4 > chunk_size)
        return;
    if (glyphs_len == 0) {

        bump_with_log(version, "FONT", 2022, 2);
        return;
    }

    std::vector<uint32_t> glyph_ptrs;
    glyph_ptrs.reserve(glyphs_len);
    for (uint32_t i = 0; i < glyphs_len; i++) {
        if (cur + 4 > buf.size())
            return;
        uint32_t gp = r_u32(buf.data() + cur);
        if (gp == 0)
            return;
        glyph_ptrs.push_back(gp);
        cur += 4;
    }
    bool ok = true;
    for (uint32_t gp : glyph_ptrs) {
        if (cur != gp) {
            ok = false;
            break;
        }
        cur += 14;
        if (cur + 2 > buf.size()) {
            ok = false;
            break;
        }
        uint16_t kerning = (uint16_t)(buf[cur] | ((uint16_t)buf[cur + 1] << 8));
        cur += 2 + (size_t)kerning * 4;
    }
    if (ok)
        bump_with_log(version, "FONT", 2022, 2);
}
void Pools::detect_font_2023_6() {
    if (version.is_at_least(2023, 6) || !version.is_at_least(2022, 8))
        return;
    auto it = chunks.find("FONT");
    if (it == chunks.end())
        return;
    size_t chunk_start = it->second.payload_off;
    size_t chunk_size = it->second.size;
    if (chunk_size < 4)
        return;

    uint32_t font_count = r_u32(buf.data() + chunk_start);
    if (font_count == 0)
        return;

    uint32_t first_font = 0, next_font = 0;
    for (uint32_t i = 0; i < font_count; i++) {
        size_t slot = chunk_start + 4 + (size_t)i * 4;
        if (slot + 4 > buf.size())
            return;
        uint32_t p = r_u32(buf.data() + slot);
        if (p == 0)
            continue;
        if (first_font == 0)
            first_font = p;
        else {
            next_font = p;
            break;
        }
    }
    if (first_font == 0)
        return;

    if (next_font == 0)
        next_font = (uint32_t)(chunk_start + chunk_size - 512);

    size_t cur = (size_t)first_font + 52;

    bool has_psem = chunks.find("PSEM") != chunks.end() || chunks.find("PSYS") != chunks.end();
    if (has_psem)
        cur += 4;
    if (cur + 4 > buf.size())
        return;
    uint32_t glyph_count = r_u32(buf.data() + cur);
    cur += 4;
    if ((uint64_t)glyph_count * 4 > (uint64_t)(next_font - cur))
        return;
    if (glyph_count == 0) {
        bump_with_log(version, "FONT", 2023, 6);
        return;
    }

    std::vector<uint32_t> glyph_ptrs;
    glyph_ptrs.reserve(glyph_count);
    for (uint32_t i = 0; i < glyph_count; i++) {
        if (cur + 4 > buf.size())
            return;
        uint32_t gp = r_u32(buf.data() + cur);
        if (gp == 0)
            return;
        glyph_ptrs.push_back(gp);
        cur += 4;
    }

    bool ok_2023_6 = true;
    bool detected_unknown_zero = false;
    bool failed_unknown_zero = false;
    for (size_t i = 0; i < glyph_ptrs.size(); i++) {
        if (cur != glyph_ptrs[i]) {
            ok_2023_6 = false;
            break;
        }
        cur += 14;
        if (cur + 2 > buf.size()) {
            ok_2023_6 = false;
            break;
        }
        uint16_t kerning = (uint16_t)(buf[cur] | ((uint16_t)buf[cur + 1] << 8));
        cur += 2;

        if (!failed_unknown_zero) {
            uint64_t next_glyph = (i + 1 < glyph_ptrs.size()) ? (uint64_t)glyph_ptrs[i + 1] : (uint64_t)next_font;
            if (detected_unknown_zero) {

                if (cur + 2 > buf.size()) {
                    ok_2023_6 = false;
                    break;
                }
                kerning = (uint16_t)(buf[cur] | ((uint16_t)buf[cur + 1] << 8));
                cur += 2;
            } else {
                uint64_t end_no_unk = (uint64_t)cur + (uint64_t)kerning * 4;
                if (end_no_unk != next_glyph) {

                    if (cur + 2 > buf.size()) {
                        ok_2023_6 = false;
                        break;
                    }
                    kerning = (uint16_t)(buf[cur] | ((uint16_t)buf[cur + 1] << 8));
                    cur += 2;
                    detected_unknown_zero = true;
                } else {
                    failed_unknown_zero = true;
                }
            }
        }
        cur += (size_t)kerning * 4;
    }

    if (ok_2023_6) {
        if (detected_unknown_zero) {
            bump_with_log(version, "FONT", 2024, 11);
        } else {
            bump_with_log(version, "FONT", 2023, 6);
        }
    }
}

void Pools::detect_font_2024_11() {
}
void Pools::detect_font_2024_14() {
    if (version.is_at_least(2024, 14) || !version.is_at_least(2024, 13))
        return;
    auto it = chunks.find("FONT");
    if (it == chunks.end())
        return;
    size_t chunk_start = it->second.payload_off;
    size_t chunk_size = it->second.size;
    size_t chunk_end = chunk_start + chunk_size;
    if (chunk_size < 4)
        return;

    uint32_t font_count = r_u32(buf.data() + chunk_start);

    uint32_t last_font = 0;
    for (uint32_t i = 0; i < font_count; i++) {
        size_t slot = chunk_start + 4 + (size_t)i * 4;
        if (slot + 4 > buf.size())
            return;
        uint32_t p = r_u32(buf.data() + slot);
        if (p != 0)
            last_font = p;
    }

    size_t cur;
    if (last_font != 0) {

        cur = (size_t)last_font + 56;
        if (cur + 4 > buf.size())
            return;
        uint32_t glyph_count = r_u32(buf.data() + cur);
        if (glyph_count == 0)
            return;
        cur += 4 + (size_t)(glyph_count - 1) * 4;
        if (cur + 4 > buf.size())
            return;
        uint32_t last_glyph_ptr = r_u32(buf.data() + cur);
        cur = (size_t)last_glyph_ptr + 16;
        if (cur + 2 > buf.size())
            return;
        uint16_t kerning_count = (uint16_t)(buf[cur] | ((uint16_t)buf[cur + 1] << 8));
        cur += 2 + (size_t)kerning_count * 4;
    } else {
        cur = chunk_start + 4 + (size_t)font_count * 4;
    }

    if (cur + 512 > chunk_end) {
        bump_with_log(version, "FONT", 2024, 14);
    }
}
void Pools::detect_psem_2023_x() {
    if (version.is_at_least(2023, 8))
        return;
    auto it = chunks.find("PSEM");
    if (it == chunks.end())
        return;
    size_t chunk_start = it->second.payload_off;
    size_t chunk_size = it->second.size;
    if (chunk_size < 4)
        return;

    uint32_t count = r_u32(buf.data() + chunk_start);

    if (count < 11) {
        bump_with_log(version, "PSEM", 2023, 4);
    }

    if (count == 0)
        return;

    if (count == 1) {

        if (chunk_size == 0xF8)
            bump_with_log(version, "PSEM", 2023, 8);
        else if (chunk_size == 0xD8)
            bump_with_log(version, "PSEM", 2023, 6);
        else if (chunk_size == 0xC8)
            bump_with_log(version, "PSEM", 2023, 4);
        return;
    }

    if (chunk_start + 4 + 8 > buf.size())
        return;
    uint32_t first_ptr = r_u32(buf.data() + chunk_start + 4);
    uint32_t second_ptr = r_u32(buf.data() + chunk_start + 8);
    if (second_ptr <= first_ptr)
        return;
    uint32_t stride = second_ptr - first_ptr;
    if (stride == 0xEC)
        bump_with_log(version, "PSEM", 2023, 8);
    else if (stride == 0xC0)
        bump_with_log(version, "PSEM", 2023, 6);
    else if (stride == 0xBC)
        bump_with_log(version, "PSEM", 2023, 4);
}
void Pools::detect_tgin_2022_9() {
    if (version.is_at_least(2022, 9) || !version.is_at_least(2, 3))
        return;
    auto it = chunks.find("TGIN");
    if (it == chunks.end())
        return;
    size_t chunk_start = it->second.payload_off;
    size_t chunk_size = it->second.size;
    if (chunk_size < 8)
        return;

    uint32_t version_word = r_u32(buf.data() + chunk_start);
    if (version_word != 1)
        return;
    size_t cur = chunk_start + 4;

    uint32_t tgin_count = r_u32(buf.data() + cur);
    cur += 4;
    if (tgin_count == 0)
        return;
    if (cur + 4 > buf.size())
        return;
    uint32_t first_tgin = r_u32(buf.data() + cur);
    cur += 4;
    uint32_t second_tgin =
        (tgin_count >= 2 && cur + 4 <= buf.size()) ? r_u32(buf.data() + cur) : (uint32_t)(chunk_start + chunk_size);

    if ((size_t)first_tgin + 8 > buf.size())
        return;
    uint32_t inner_ptr = r_u32(buf.data() + first_tgin + 4);
    if (inner_ptr < first_tgin || inner_ptr >= second_tgin) {
        bump_with_log(version, "TGIN", 2022, 9);
    }
}

void Pools::detect_tgin_2023_1() {
    if (version.is_at_least(2023, 1) || !version.is_at_least(2022, 9))
        return;
    auto it = chunks.find("TGIN");
    if (it == chunks.end())
        return;
    size_t chunk_start = it->second.payload_off;
    if (it->second.size < 12)
        return;

    size_t cur = chunk_start + 4 + 4;
    if (cur + 4 > buf.size())
        return;
    uint32_t first_entry = r_u32(buf.data() + cur);

    size_t probe = (size_t)first_entry + 16 + 12;
    if (probe + 8 > buf.size())
        return;
    uint32_t fourth_ptr = r_u32(buf.data() + probe);
    uint32_t maybe_count = r_u32(buf.data() + probe + 4);
    if (maybe_count <= fourth_ptr) {
        bump_with_log(version, "TGIN", 2023, 1);
        version.branch = Gmtoolkit::BranchType::Post2022_0;
    }
}
void Pools::detect_bgnd_2024_14_1() {
    if (version.is_at_least(2024, 14, 1) || !version.is_at_least(2024, 13))
        return;
    auto it = chunks.find("BGND");
    if (it == chunks.end())
        return;
    size_t chunk_start = it->second.payload_off;
    size_t chunk_size = it->second.size;
    size_t chunk_end = chunk_start + chunk_size;
    if (chunk_size < 4)
        return;

    uint32_t bg_count = r_u32(buf.data() + chunk_start);
    for (uint32_t i = 0; i < bg_count; i++) {
        size_t slot = chunk_start + 4 + (size_t)i * 4;
        if (slot + 4 > buf.size())
            break;
        uint32_t bg_ptr = r_u32(buf.data() + slot);
        if (bg_ptr == 0)
            continue;

        uint32_t next_bg_ptr = 0;
        for (uint32_t j = i + 1; j < bg_count; j++) {
            size_t s = chunk_start + 4 + (size_t)j * 4;
            if (s + 4 > buf.size())
                break;
            uint32_t p = r_u32(buf.data() + s);
            if (p != 0) {
                next_bg_ptr = p;
                break;
            }
        }

        size_t probe = (size_t)bg_ptr + 11 * 4;
        if (probe + 8 > buf.size())
            continue;
        uint32_t items_per_tile = r_u32(buf.data() + probe);
        uint32_t tile_count = r_u32(buf.data() + probe + 4);

        uint64_t theo_end = (uint64_t)bg_ptr + 16 * 4 + (uint64_t)items_per_tile * tile_count * 4;
        if (next_bg_ptr == 0) {
            if ((theo_end % 16) != 0)
                theo_end += 16 - (theo_end % 16);
            if (theo_end != chunk_end) {
                bump_with_log(version, "BGND", 2024, 14, 1);
                break;
            }
        } else {
            if ((theo_end % 8) != 0)
                theo_end += 8 - (theo_end % 8);
            if (theo_end != next_bg_ptr) {
                bump_with_log(version, "BGND", 2024, 14, 1);
                break;
            }
        }
    }
}
void Pools::detect_agrp_2024_14() {
    if (version.is_at_least(2024, 14))
        return;
    auto it = chunks.find("AGRP");
    if (it == chunks.end())
        return;
    size_t chunk_start = it->second.payload_off;
    size_t chunk_size = it->second.size;
    size_t chunk_end = chunk_start + chunk_size;
    if (chunk_size < 4)
        return;

    uint32_t agrp_count = r_u32(buf.data() + chunk_start);
    if (agrp_count == 0)
        return;

    uint32_t first = 0, second = 0;
    uint32_t i = 0;
    while (i < agrp_count) {
        size_t slot = chunk_start + 4 + (size_t)i * 4;
        if (slot + 4 > buf.size())
            return;
        first = r_u32(buf.data() + slot);
        i++;
        if (first != 0)
            break;
    }
    while (i < agrp_count) {
        size_t slot = chunk_start + 4 + (size_t)i * 4;
        if (slot + 4 > buf.size())
            return;
        second = r_u32(buf.data() + slot);
        i++;
        if (second != 0)
            break;
    }

    if (first == 0)
        return;
    if (second == 0) {

        size_t probe = (size_t)first + 4;
        if (probe + 4 > chunk_end)
            return;
        uint32_t path_ptr = r_u32(buf.data() + probe);
        if (path_ptr != 0)
            bump_with_log(version, "AGRP", 2024, 14);
    } else {

        if (second > first && (second - first) != 4) {
            bump_with_log(version, "AGRP", 2024, 14);
        }
    }
}
void Pools::detect_room_2022_1() {
    if (version.is_at_least(2022, 1) || !version.is_at_least(2, 3))
        return;
    auto it = chunks.find("ROOM");
    if (it == chunks.end())
        return;
    size_t room_off = it->second.payload_off;
    size_t room_cs = it->second.size;
    if (room_cs < 4)
        return;

    uint32_t room_count = r_u32(buf.data() + room_off);
    for (uint32_t i = 0; i < room_count; i++) {
        size_t room_ptr_off = room_off + 4 + (size_t)i * 4;
        if (room_ptr_off + 4 > buf.size())
            break;
        uint32_t room_ptr = r_u32(buf.data() + room_ptr_off);

        if ((size_t)room_ptr + 22 * 4 + 8 > buf.size())
            continue;
        uint32_t layer_list_ptr = r_u32(buf.data() + room_ptr + 22 * 4);
        int32_t seqn_ptr = (int32_t)r_u32(buf.data() + room_ptr + 22 * 4 + 4);
        if ((size_t)layer_list_ptr + 4 > buf.size())
            continue;

        int32_t layer_count = (int32_t)r_u32(buf.data() + layer_list_ptr);
        if (layer_count <= 0)
            continue;

        size_t layer_slot = (size_t)layer_list_ptr + 4;
        if (layer_slot + 4 > buf.size())
            continue;
        uint32_t layer_ptr = r_u32(buf.data() + layer_slot);

        uint32_t next_off = (layer_count == 1)
                                ? (uint32_t)seqn_ptr
                                : (layer_slot + 4 + 4 <= buf.size() ? r_u32(buf.data() + layer_slot + 4) : 0);

        if ((size_t)layer_ptr + 4 > buf.size())
            continue;
        uint32_t layer_type = r_u32(buf.data() + layer_ptr);

        if (layer_type == 6 || layer_type == 7)
            continue;

        size_t cur = (size_t)layer_ptr + 4;
        bool bumped = false;
        switch (layer_type) {
            case 1: {
                if (next_off > cur && (next_off - cur) > 16 * 4)
                    bumped = true;
                break;
            }
            case 2: {
                cur += 6 * 4;
                if (cur + 4 > buf.size())
                    break;
                int32_t inst_count = (int32_t)r_u32(buf.data() + cur);
                cur += 4;
                if (next_off > cur && (int64_t)(next_off - cur) != (int64_t)inst_count * 4)
                    bumped = true;
                break;
            }
            case 3: {
                cur += 6 * 4;
                if (cur + 4 > buf.size())
                    break;
                int32_t tile_off = (int32_t)r_u32(buf.data() + cur);
                cur += 4;
                if (tile_off != (int32_t)(cur + 8) && tile_off != (int32_t)(cur + 12))
                    bumped = true;
                break;
            }
            case 4: {
                cur += 7 * 4;
                if (cur + 8 > buf.size())
                    break;
                int32_t tw = (int32_t)r_u32(buf.data() + cur);
                int32_t th = (int32_t)r_u32(buf.data() + cur + 4);
                cur += 8;
                if (next_off > cur && (int64_t)(next_off - cur) != (int64_t)tw * th * 4)
                    bumped = true;
                break;
            }
            case 5: {
                cur += 7 * 4;
                if (cur + 4 > buf.size())
                    break;
                int32_t prop_count = (int32_t)r_u32(buf.data() + cur);
                cur += 4;
                if (next_off > cur && (int64_t)(next_off - cur) != (int64_t)prop_count * 3 * 4)
                    bumped = true;
                break;
            }
            default:
                break;
        }
        if (bumped) {
            bump_with_log(version, "ROOM", 2022, 1);
            return;
        }

        break;
    }
}

} // namespace GMSLib::SaveBackend
