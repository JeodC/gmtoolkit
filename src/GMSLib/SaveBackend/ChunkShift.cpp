// SPDX-License-Identifier: MIT

#include "GMSLib/SaveBackend/ChunkShift.h"
#include "Toolkit/Log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace GMSLib::SaveBackend {

namespace {

struct RoomCtx {
    size_t buf_len;
    const RoomShiftOptions* opt;
};

} // namespace

size_t collect_listchunk_entry_positions(const uint8_t* buf, size_t payload_off, size_t payload_size,
                                         std::vector<uint32_t>& out) {
    if (payload_size < 4)
        return 0;
    uint32_t count = r_u32(buf + payload_off);
    if (count == 0)
        return 0;
    if (4 + (size_t)count * 4 > payload_size)
        return 0;
    size_t added = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t p = r_u32(buf + payload_off + 4 + (size_t)i * 4);
        if (p != 0) {
            out.push_back(p);
            added++;
        }
    }
    return added;
}

// Conservative whole-chunk pointer fixup: any aligned u32 that matches a known target gets bumped.
// Relies on FORM offsets being globally unique enough that false-positive matches don't happen.
void scan_and_bump_pointers(uint8_t* buf, size_t payload_off, size_t payload_size,
                            const std::vector<uint32_t>& sorted_targets, int32_t delta) {
    if (payload_size < 4 || delta == 0 || sorted_targets.empty())
        return;
    size_t end = payload_off + (payload_size & ~(size_t)3);
    for (size_t q = payload_off; q + 4 <= end; q += 4) {
        uint32_t v = r_u32(buf + q);
        if (v == 0)
            continue;
        if (std::binary_search(sorted_targets.begin(), sorted_targets.end(), v)) {
            w_u32(buf + q, (uint32_t)((int64_t)v + delta));
        }
    }
}

bool is_listchunk_with_ptrtable(const char* tag) {
    static const char* L[] = {
        "EXTN", "SOND", "AGRP", "SPRT", "BGND", "PATH", "SCPT", "SHDR", "FONT",
        "TMLN", "OBJT", "ROOM", "TPAG", "CODE", "STRG", "TXTR", "AUDO",
    };
    for (auto* n : L)
        if (memcmp(tag, n, 4) == 0)
            return true;
    return false;
}

bool is_versioned_listchunk(const char* tag) {
    // Versioned *pointer-table* list chunks: a u32 version, then a count, then a
    // pointer table whose entries are absolute offsets that move with the chunk.
    static const char* L[] = { "ACRV", "SEQN", "FEDS", "PSEM", "PSYS", "TGIN" };
    for (auto* n : L)
        if (memcmp(tag, n, 4) == 0)
            return true;
    return false;
}

// SHDR entries embed pointers back into their own chunk (vertex attrib tables); the generic
// listchunk shifter only handles the top ptr table, so we touch the inner refs here.
void shift_shdr_internals(uint8_t* buf, size_t new_payload_off, size_t payload_size, size_t orig_payload_off,
                          int32_t delta, uint8_t bytecode_version) {
    if (payload_size < 4 || delta == 0)
        return;
    uint32_t count = r_u32(buf + new_payload_off);
    if (count == 0)
        return;
    if (4 + (size_t)count * 4 > payload_size)
        return;
    size_t chunk_end_new = new_payload_off + payload_size;
    uint32_t orig_lo = (uint32_t)orig_payload_off;
    uint32_t orig_hi = (uint32_t)(orig_payload_off + payload_size);
    auto bump_back_ref = [&](size_t pos) {
        if (pos + 4 > chunk_end_new)
            return;
        uint32_t v = r_u32(buf + pos);
        if (v >= orig_lo && v < orig_hi)
            w_u32(buf + pos, (uint32_t)((int64_t)v + delta));
    };
    for (uint32_t i = 0; i < count; i++) {
        size_t ent = r_u32(buf + new_payload_off + 4 + (size_t)i * 4);
        if (ent == 0 || ent + 44 > chunk_end_new)
            continue;

        bump_back_ref(ent + 32);
        bump_back_ref(ent + 36);

        size_t attr_pos = ent + 40;
        uint32_t attr_count = r_u32(buf + attr_pos);
        size_t after_attrs = attr_pos + 4 + (size_t)attr_count * 4;
        if (bytecode_version <= 13 || after_attrs + 4 > chunk_end_new)
            continue;

        int32_t version = (int32_t)r_u32(buf + after_attrs);
        size_t cursor = after_attrs + 4;

        for (int k = 0; k < 4; k++) {
            bump_back_ref(cursor);
            cursor += 8;
        }
        if (version >= 2) {

            for (int k = 0; k < 2; k++) {
                bump_back_ref(cursor);
                cursor += 8;
            }
        }
    }
}

void shift_font_internals(uint8_t* buf, size_t new_payload_off, size_t payload_size, size_t orig_payload_off,
                          int32_t delta, const FontEntryLayout& layout,
                          const std::vector<uint32_t>& sorted_tpag_positions) {
    if (payload_size < 4 || delta == 0)
        return;
    uint32_t count = r_u32(buf + new_payload_off);
    if (count == 0)
        return;
    if (4 + (size_t)count * 4 > payload_size)
        return;
    size_t chunk_end_new = new_payload_off + payload_size;
    uint32_t orig_lo = (uint32_t)orig_payload_off;
    uint32_t orig_hi = (uint32_t)(orig_payload_off + payload_size);
    auto probe_candidate = [&](size_t ent, size_t opt) -> bool {
        size_t count_pos = ent + 40 + opt;
        if (count_pos + 4 > chunk_end_new)
            return false;
        uint32_t g_count = r_u32(buf + count_pos);
        if (g_count == 0 || g_count > 0xFFFF)
            return false;
        size_t ptab_start = count_pos + 4;
        if (ptab_start + (size_t)g_count * 4 > chunk_end_new)
            return false;
        for (uint32_t g = 0; g < g_count; g++) {
            uint32_t v = r_u32(buf + ptab_start + (size_t)g * 4);
            if (v < orig_lo || v >= orig_hi)
                return false;
        }
        return true;
    };
    size_t base_optional = 0;
    if (layout.has_ascender_offset)
        base_optional += 4;
    if (layout.has_ascender)
        base_optional += 4;
    if (layout.has_sdf_spread)
        base_optional += 4;
    if (layout.has_line_height)
        base_optional += 4;
    for (uint32_t i = 0; i < count; i++) {
        size_t ent = r_u32(buf + new_payload_off + 4 + (size_t)i * 4);
        if (ent == 0)
            continue;
        if (ent + 32 <= chunk_end_new) {
            size_t tex_pos = ent + 28;
            uint32_t tex = r_u32(buf + tex_pos);
            if (tex != 0 && std::binary_search(sorted_tpag_positions.begin(), sorted_tpag_positions.end(), tex)) {
                w_u32(buf + tex_pos, (uint32_t)((int64_t)tex + delta));
            }
        }

        size_t pick = SIZE_MAX;
        if (probe_candidate(ent, base_optional)) {
            pick = base_optional;
        } else {
            for (size_t opt : { (size_t)0, (size_t)4, (size_t)8, (size_t)12, (size_t)16 }) {
                if (opt == base_optional)
                    continue;
                if (probe_candidate(ent, opt)) {
                    pick = opt;
                    break;
                }
            }
        }
        if (pick == SIZE_MAX)
            continue;
        size_t ptab_start = ent + 40 + pick + 4;
        uint32_t g_count = r_u32(buf + ent + 40 + pick);
        for (uint32_t g = 0; g < g_count; g++) {
            size_t pp = ptab_start + (size_t)g * 4;
            uint32_t v = r_u32(buf + pp);
            if (v >= orig_lo && v < orig_hi)
                w_u32(buf + pp, (uint32_t)((int64_t)v + delta));
        }
    }
}

void shift_tags_internals(uint8_t* buf, size_t new_payload_off, size_t payload_size, size_t orig_payload_off,
                          int32_t delta) {
    if (payload_size < 8 || delta == 0)
        return;

    size_t cursor = new_payload_off + 4;
    size_t chunk_end_new = new_payload_off + payload_size;

    if (cursor + 4 > chunk_end_new)
        return;
    uint32_t tags_count = r_u32(buf + cursor);
    cursor += 4;
    if (tags_count > 0x10000)
        return;
    if (cursor + (size_t)tags_count * 4 > chunk_end_new)
        return;
    cursor += (size_t)tags_count * 4;

    if (cursor + 4 > chunk_end_new)
        return;
    uint32_t asset_count = r_u32(buf + cursor);
    cursor += 4;
    if (asset_count > 0x100000)
        return;
    if (cursor + (size_t)asset_count * 4 > chunk_end_new)
        return;
    uint32_t orig_lo = (uint32_t)orig_payload_off;
    uint32_t orig_hi = (uint32_t)(orig_payload_off + payload_size);
    for (uint32_t i = 0; i < asset_count; i++) {
        size_t pp = cursor + (size_t)i * 4;
        uint32_t v = r_u32(buf + pp);
        if (v >= orig_lo && v < orig_hi)
            w_u32(buf + pp, (uint32_t)((int64_t)v + delta));
    }
}

void shift_tgin_internals(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta) {
    if (payload_size < 8 || delta == 0)
        return;
    uint32_t count = r_u32(buf + payload_off + 4);
    if (count == 0)
        return;
    if (8 + (size_t)count * 4 > payload_size)
        return;
    for (uint32_t i = 0; i < count; i++) {
        size_t pp = payload_off + 8 + (size_t)i * 4;
        uint32_t v = r_u32(buf + pp);
        if (v != 0)
            w_u32(buf + pp, (uint32_t)((int64_t)v + delta));
    }
    uint32_t orig_start = (uint32_t)(payload_off - (size_t)delta);
    uint32_t orig_end = (uint32_t)(payload_off + payload_size - (size_t)delta);
    size_t tgin_end = payload_off + payload_size;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t ent = r_u32(buf + payload_off + 8 + (size_t)i * 4);

        if ((size_t)ent + 4 > tgin_end)
            continue;
        size_t off = (size_t)ent + 4;
        if (off + 12 <= tgin_end) {
            uint32_t maybe_dir = r_u32(buf + off);
            uint32_t maybe_ext = r_u32(buf + off + 4);
            uint32_t maybe_load = r_u32(buf + off + 8);
            bool dir_looks_strg = (maybe_dir == 0) || (maybe_dir > 0x100000);
            bool ext_looks_strg = (maybe_ext == 0) || (maybe_ext > 0x100000);
            bool load_in_range = (maybe_load <= 2);
            if (dir_looks_strg && ext_looks_strg && load_in_range) {
                off += 12;
            }
        }

        for (int k = 0; k < 5; k++) {
            if (off + 4 > tgin_end)
                break;
            uint32_t v = r_u32(buf + off);
            if (v < orig_start || v >= orig_end)
                break;
            w_u32(buf + off, (uint32_t)((int64_t)v + delta));
            off += 4;
        }
    }
}

void shift_chunk_ptab(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta) {
    if (payload_size < 4 || delta == 0)
        return;
    uint32_t count = r_u32(buf + payload_off);
    if (count == 0)
        return;
    if (4 + (size_t)count * 4 > payload_size)
        return;
    uint8_t* ptab = buf + payload_off + 4;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t v = r_u32(ptab + i * 4);
        if (v == 0)
            continue;
        w_u32(ptab + i * 4, (uint32_t)((int64_t)v + delta));
    }
}

void shift_versioned_chunk_ptab(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta) {
    if (payload_size < 8 || delta == 0)
        return;
    uint32_t count = r_u32(buf + payload_off + 4);
    if (count == 0)
        return;
    if (8 + (size_t)count * 4 > payload_size)
        return;
    uint8_t* ptab = buf + payload_off + 8;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t v = r_u32(ptab + i * 4);
        if (v == 0)
            continue;
        w_u32(ptab + i * 4, (uint32_t)((int64_t)v + delta));
    }
}

void shift_txtr_blob_offs(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta) {
    if (payload_size < 4 || delta == 0)
        return;
    uint32_t count = r_u32(buf + payload_off);
    if (count == 0)
        return;
    if (4 + (size_t)count * 4 > payload_size)
        return;

    size_t entry_size = 16;
    if (count >= 2) {
        uint32_t p0 = r_u32(buf + payload_off + 4);
        uint32_t p1 = r_u32(buf + payload_off + 8);
        size_t diff = (size_t)(p1 - p0);
        if (diff == 8 || diff == 12 || diff == 16 || diff == 28)
            entry_size = diff;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t rec = r_u32(buf + payload_off + 4 + (size_t)i * 4);
        if (rec == 0)
            continue;
        size_t blob_off_field = (size_t)rec + entry_size - 4;
        uint32_t blob = r_u32(buf + blob_off_field);
        if (blob != 0) {
            w_u32(buf + blob_off_field, (uint32_t)((int64_t)blob + delta));
        }
    }
}

void shift_vari_first_addrs(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta) {
    if (payload_size < 12 || delta == 0)
        return;
    size_t header = 12;
    size_t off = payload_off + header;
    size_t end = payload_off + payload_size;
    while (off + 20 <= end) {
        uint32_t occ = r_u32(buf + off + 12);
        uint32_t first = r_u32(buf + off + 16);
        if (occ > 0 && first != 0xFFFFFFFFu) {
            w_u32(buf + off + 16, (uint32_t)((int64_t)first + delta));
        }
        off += 20;
    }
}

void shift_func_first_addrs(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta) {
    if (payload_size < 4 || delta == 0)
        return;
    uint32_t count = r_u32(buf + payload_off);
    if (4 + (size_t)count * 12 > payload_size)
        return;
    size_t off = payload_off + 4;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t occ = r_u32(buf + off + 4);
        uint32_t first = r_u32(buf + off + 8);
        if (occ > 0 && first != 0xFFFFFFFFu) {
            w_u32(buf + off + 8, (uint32_t)((int64_t)first + delta));
        }
        off += 12;
    }
}

void shift_audo_ptrs(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta) {
    shift_chunk_ptab(buf, payload_off, payload_size, delta);
}

void shift_pointer_list(uint8_t* buf, size_t buf_len, size_t list_off, int32_t delta, EntryCallback cb, void* user) {
    if (delta == 0)
        return;
    if (list_off + 4 > buf_len)
        return;
    uint32_t count = r_u32(buf + list_off);
    if (count > 100000)
        return;
    if (list_off + 4 + (size_t)count * 4 > buf_len)
        return;
    for (uint32_t i = 0; i < count; i++) {
        size_t pp = list_off + 4 + (size_t)i * 4;
        uint32_t v = r_u32(buf + pp);
        if (v == 0)
            continue;
        uint32_t nv = (uint32_t)((int64_t)v + delta);
        w_u32(buf + pp, nv);
        if (cb)
            cb(buf, nv, delta, user);
    }
}

static void walk_event_actions(uint8_t* buf, size_t buf_len, size_t event_off, int32_t delta) {
    if (event_off + 8 > buf_len)
        return;
    size_t actions_list_off = event_off + 4;
    shift_pointer_list(buf, buf_len, actions_list_off, delta, nullptr, nullptr);
}

static void walk_inner_event_list(uint8_t* buf, size_t entry_off, int32_t delta, void* user) {
    size_t buf_len = *(size_t*)user;
    walk_event_actions(buf, buf_len, entry_off, delta);
}

static void walk_outer_event_list(uint8_t* buf, size_t entry_off, int32_t delta, void* user) {
    size_t buf_len = *(size_t*)user;
    shift_pointer_list(buf, buf_len, entry_off, delta, walk_inner_event_list, user);
}

void shift_objt_internals(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta, bool gms_2022_5_plus) {
    if (payload_size < 4 || delta == 0)
        return;
    size_t buf_len_for_cb = payload_off + payload_size;
    uint32_t count = r_u32(buf + payload_off);
    if (count == 0)
        return;
    size_t hdr_through_kin = gms_2022_5_plus ? 84 : 80;
    size_t pvcount_off = gms_2022_5_plus ? 0x44 : 0x40;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t obj_off = r_u32(buf + payload_off + 4 + (size_t)i * 4);
        if (obj_off == 0)
            continue;
        if ((size_t)obj_off + pvcount_off + 4 > buf_len_for_cb)
            continue;
        uint32_t pvcount = r_u32(buf + (size_t)obj_off + pvcount_off);
        if (pvcount > 1000)
            continue;
        size_t events_off = (size_t)obj_off + hdr_through_kin + (size_t)pvcount * 8;
        size_t buf_len_user = buf_len_for_cb;
        shift_pointer_list(buf, buf_len_for_cb, events_off, delta, walk_outer_event_list, &buf_len_user);
    }
}

static void walk_background_entry(uint8_t*, size_t, int32_t, void*) {
}

static void walk_view_entry(uint8_t*, size_t, int32_t, void*) {
}

static void walk_room_gameobject_entry(uint8_t*, size_t, int32_t, void*) {
}

static void walk_tile_entry(uint8_t*, size_t, int32_t, void*) {
}

static void walk_layer_entry(uint8_t* buf, size_t layer_off, int32_t delta, void* user) {
    const RoomCtx* ctx = (const RoomCtx*)user;
    size_t buf_len = ctx->buf_len;
    const RoomShiftOptions& opt = *ctx->opt;
    if (layer_off + 36 > buf_len)
        return;
    uint32_t layer_type = r_u32(buf + layer_off + 8);
    size_t off = layer_off + 36;

    if (opt.is_2022_1) {
        if (off + 12 > buf_len)
            return;
        uint32_t effect_props_count = r_u32(buf + off + 8);
        if (effect_props_count > 100000)
            return;
        off += 12 + (size_t)effect_props_count * 12;
        if (off > buf_len)
            return;
    }

    if (layer_type != 3) {

        return;
    }

    size_t ptrs_start = off;
    if (ptrs_start + 8 > buf_len)
        return;

    uint32_t legacy_tiles_ptr = r_u32(buf + ptrs_start);
    int sub_count = 2;
    int64_t gap = (int64_t)legacy_tiles_ptr - (int64_t)ptrs_start + (int64_t)delta;
    if (gap > 0 && (gap % 4) == 0) {
        int n = (int)(gap / 4);
        if (n >= 2 && n <= 6)
            sub_count = n;
    }

    if (ptrs_start + (size_t)sub_count * 4 > buf_len)
        return;

    for (int k = 0; k < sub_count; k++) {
        size_t slot = ptrs_start + (size_t)k * 4;
        uint32_t v = r_u32(buf + slot);
        if (v == 0)
            continue;
        uint32_t nv = (uint32_t)((int64_t)v + delta);
        w_u32(buf + slot, nv);
        shift_pointer_list(buf, buf_len, (size_t)nv, delta, nullptr, nullptr);
    }
}

void shift_room_internals(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t delta,
                          const RoomShiftOptions& opt) {
    if (payload_size < 4 || delta == 0)
        return;
    uint32_t count = r_u32(buf + payload_off);
    if (count == 0)
        return;
    size_t buf_len_user = payload_off + payload_size;
    RoomCtx ctx{ buf_len_user, &opt };
    size_t bgs_off = 0x28;
    size_t views_off = 0x2C;
    size_t gobjs_off = 0x30;
    size_t tiles_off = 0x34;
    size_t after_tiles = 0x38;
    size_t ico_off = opt.has_inst_creation ? after_tiles : (size_t)-1;
    size_t world_off = after_tiles + (opt.has_inst_creation ? 4 : 0);
    size_t after_floats = world_off + 32;
    size_t layers_off = opt.gms2 ? after_floats : (size_t)-1;
    size_t seq_off = opt.gms2_3 ? (after_floats + 4) : (size_t)-1;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t room_off = r_u32(buf + payload_off + 4 + (size_t)i * 4);
        if (room_off == 0)
            continue;

        auto bump_field = [&](size_t fld_off) {
            if (fld_off == (size_t)-1)
                return;
            size_t p = (size_t)room_off + fld_off;
            if (p + 4 > buf_len_user)
                return;
            uint32_t v = r_u32(buf + p);
            if (v != 0)
                w_u32(buf + p, (uint32_t)((int64_t)v + delta));
        };
        bump_field(bgs_off);
        bump_field(views_off);
        bump_field(gobjs_off);
        bump_field(tiles_off);
        bump_field(ico_off);
        bump_field(layers_off);
        bump_field(seq_off);

        auto follow_pl = [&](size_t fld_off, EntryCallback cb) {
            if (fld_off == (size_t)-1)
                return;
            size_t p = (size_t)room_off + fld_off;
            if (p + 4 > buf_len_user)
                return;
            uint32_t new_off = r_u32(buf + p);
            if (new_off == 0)
                return;
            shift_pointer_list(buf, buf_len_user, (size_t)new_off, delta, cb, &ctx);
        };
        follow_pl(bgs_off, walk_background_entry);
        follow_pl(views_off, walk_view_entry);
        follow_pl(gobjs_off, walk_room_gameobject_entry);
        follow_pl(tiles_off, walk_tile_entry);
        follow_pl(layers_off, walk_layer_entry);
    }
}

} // namespace GMSLib::SaveBackend
