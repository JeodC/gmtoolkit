// SPDX-License-Identifier: MIT

#include "Toolkit/IO.h"
#include "Toolkit/Platform.h"
#include "Toolkit/Log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using Gmtoolkit::r_u32;
using Gmtoolkit::w_u32;

struct ChunkLoc {
    char name[4];
    size_t header_off;
    size_t payload_off;
    size_t payload_size;
};

static int collect_chunks(const uint8_t* buf, size_t buf_len, std::vector<ChunkLoc>& out) {
    if (buf_len < 8 || memcmp(buf, "FORM", 4) != 0)
        return -1;
    size_t i = 8;
    while (i + 8 <= buf_len) {
        ChunkLoc c;
        memcpy(c.name, buf + i, 4);
        c.payload_size = r_u32(buf + i + 4);
        c.header_off = i;
        c.payload_off = i + 8;
        if (c.payload_off + c.payload_size > buf_len)
            return -2;
        out.push_back(c);
        i = c.payload_off + c.payload_size;
    }
    return 0;
}

static const ChunkLoc* find_chunk_loc(const std::vector<ChunkLoc>& chunks, const char* name) {
    for (auto& c : chunks) {
        if (memcmp(c.name, name, 4) == 0)
            return &c;
    }
    return NULL;
}

static void shift_list_ptrtable(uint8_t* buf, size_t payload_off, size_t payload_size, size_t shift_threshold,
                                int32_t delta, size_t header_skip = 0) {
    if (payload_size < header_skip + 4)
        return;
    uint32_t count = r_u32(buf + payload_off + header_skip);
    if (count == 0)
        return;
    if (header_skip + 4 + (size_t)count * 4 > payload_size)
        return;
    uint8_t* ptab = buf + payload_off + header_skip + 4;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t p = r_u32(ptab + i * 4);
        if (p == 0)
            continue;
        if (p >= shift_threshold) {
            w_u32(ptab + i * 4, (uint32_t)((int64_t)p + delta));
        }
    }
}

static bool is_listchunk_with_ptrtable(const char* name) {
    static const char* LIST_CHUNKS[] = {
        "EXTN", "SOND", "AGRP", "SPRT", "BGND", "PATH", "SCPT", "SHDR", "FONT",
        "TMLN", "OBJT", "ROOM", "TPAG", "CODE", "STRG", "TXTR", "AUDO",
    };
    for (auto* n : LIST_CHUNKS) {
        if (memcmp(name, n, 4) == 0)
            return true;
    }
    return false;
}

// Versioned variants store a u32 version before the count + ptab.
static bool is_versioned_listchunk(const char* name) {
    static const char* LIST_CHUNKS[] = { "TGIN", "ACRV", "SEQN", "FEDS", "PSEM", "PSYS", "EMBI" };
    for (auto* n : LIST_CHUNKS) {
        if (memcmp(name, n, 4) == 0)
            return true;
    }
    return false;
}

static const size_t ROOM_HEADER_SIZE_GMS1 = 88;
static const size_t ROOM_HDR_BACKGROUNDS = 0x28;
static const size_t ROOM_HDR_VIEWS = 0x2C;
static const size_t ROOM_HDR_GAMEOBJECTS = 0x30;
static const size_t ROOM_HDR_TILES = 0x34;
static const size_t GAMEOBJECT_SIZE_BC15 = 36;

struct RoomInfo {
    size_t old_room_off;
    size_t old_room_end;
    size_t old_bg_ptr;
    size_t old_views_ptr;
    size_t old_go_ptr;
    size_t old_tiles_ptr;
    uint32_t go_count;
    size_t new_room_off;
    size_t new_room_end;
};

static int parse_room(const uint8_t* buf, size_t buf_len, size_t room_off, RoomInfo& info) {
    if (room_off + ROOM_HEADER_SIZE_GMS1 > buf_len)
        return -1;
    const uint8_t* h = buf + room_off;
    info.old_room_off = room_off;
    info.old_bg_ptr = r_u32(h + ROOM_HDR_BACKGROUNDS);
    info.old_views_ptr = r_u32(h + ROOM_HDR_VIEWS);
    info.old_go_ptr = r_u32(h + ROOM_HDR_GAMEOBJECTS);
    info.old_tiles_ptr = r_u32(h + ROOM_HDR_TILES);

    if (info.old_go_ptr + 4 > buf_len)
        return -2;
    info.go_count = r_u32(buf + info.old_go_ptr);
    return 0;
}

// Bytecode 16 adds a u32 tail (PreCreateCodeId, 0xFFFFFFFF for "none") to each GameObject record.
// Walk ROOM, expand every GO entry by 4 bytes, and fix up the in-room pointer table to match.
static int rebuild_room_15_to_16(const uint8_t* win, size_t win_len, size_t room_payload_off, size_t room_payload_size,
                                 uint8_t** out_new_bytes, size_t* out_new_size, size_t* out_total_shift) {
    if (room_payload_size < 4)
        return -1;
    uint32_t n_rooms = r_u32(win + room_payload_off);
    if (n_rooms == 0) {
        uint8_t* copy = (uint8_t*)malloc(room_payload_size);
        if (!copy)
            return -1;
        memcpy(copy, win + room_payload_off, room_payload_size);
        *out_new_bytes = copy;
        *out_new_size = room_payload_size;
        *out_total_shift = 0;
        return 0;
    }
    if (4 + (size_t)n_rooms * 4 > room_payload_size)
        return -2;

    std::vector<size_t> old_room_ptrs(n_rooms);
    for (uint32_t i = 0; i < n_rooms; i++) {
        old_room_ptrs[i] = r_u32(win + room_payload_off + 4 + i * 4);
    }

    std::vector<RoomInfo> rooms(n_rooms);
    for (uint32_t i = 0; i < n_rooms; i++) {
        if (parse_room(win, win_len, old_room_ptrs[i], rooms[i]) != 0)
            return -3;
        rooms[i].old_room_end = (i + 1 < n_rooms) ? old_room_ptrs[i + 1] : (room_payload_off + room_payload_size);
    }

    std::vector<size_t> shift_at(n_rooms, 0);
    size_t total_shift = 0;
    for (uint32_t i = 0; i < n_rooms; i++) {
        shift_at[i] = total_shift;
        total_shift += (size_t)rooms[i].go_count * 4;
    }

    size_t new_payload_size = room_payload_size + total_shift;
    uint8_t* out = (uint8_t*)malloc(new_payload_size);
    if (!out)
        return -1;
    memset(out, 0, new_payload_size);

    w_u32(out, n_rooms);
    for (uint32_t i = 0; i < n_rooms; i++) {
        size_t new_room_off = old_room_ptrs[i] + shift_at[i];
        w_u32(out + 4 + i * 4, (uint32_t)new_room_off);
        rooms[i].new_room_off = new_room_off;
        rooms[i].new_room_end = rooms[i].old_room_end + shift_at[i] + (size_t)rooms[i].go_count * 4;
    }

    for (uint32_t i = 0; i < n_rooms; i++) {
        RoomInfo& r = rooms[i];
        size_t room_local_shift = (size_t)r.go_count * 4;
        size_t src_off = r.old_room_off;
        size_t dst_off = r.new_room_off;

#define DST(off) (out + ((off)-room_payload_off))
#define SRC(off) (win + (off))

        memcpy(DST(dst_off), SRC(src_off), ROOM_HEADER_SIZE_GMS1);
        w_u32(DST(dst_off + ROOM_HDR_BACKGROUNDS), (uint32_t)(r.old_bg_ptr + shift_at[i]));
        w_u32(DST(dst_off + ROOM_HDR_VIEWS), (uint32_t)(r.old_views_ptr + shift_at[i]));
        w_u32(DST(dst_off + ROOM_HDR_GAMEOBJECTS), (uint32_t)(r.old_go_ptr + shift_at[i]));
        w_u32(DST(dst_off + ROOM_HDR_TILES), (uint32_t)(r.old_tiles_ptr + shift_at[i] + room_local_shift));

        size_t bg_size = r.old_views_ptr - r.old_bg_ptr;
        size_t bg_new_off = r.old_bg_ptr + shift_at[i];
        memcpy(DST(bg_new_off), SRC(r.old_bg_ptr), bg_size);
        shift_list_ptrtable(out, bg_new_off - room_payload_off, bg_size, 0, (int32_t)shift_at[i]);

        size_t views_size = r.old_go_ptr - r.old_views_ptr;
        size_t views_new_off = r.old_views_ptr + shift_at[i];
        memcpy(DST(views_new_off), SRC(r.old_views_ptr), views_size);
        shift_list_ptrtable(out, views_new_off - room_payload_off, views_size, 0, (int32_t)shift_at[i]);

        size_t go_new_off = r.old_go_ptr + shift_at[i];
        w_u32(DST(go_new_off), r.go_count);
        for (uint32_t j = 0; j < r.go_count; j++) {
            uint32_t old_go_off = r_u32(SRC(r.old_go_ptr + 4 + j * 4));
            uint32_t new_go_off = (uint32_t)(old_go_off + shift_at[i] + (size_t)j * 4);
            w_u32(DST(go_new_off + 4 + j * 4), new_go_off);
        }
        size_t entries_old_start = r.old_go_ptr + 4 + (size_t)r.go_count * 4;
        size_t entries_new_start = go_new_off + 4 + (size_t)r.go_count * 4;
        (void)entries_new_start;
        if (r.go_count > 0) {
            uint32_t p0 = r_u32(SRC(r.old_go_ptr + 4));
            if (p0 != entries_old_start) {}
        }
        for (uint32_t j = 0; j < r.go_count; j++) {
            size_t src_go_off = entries_old_start + (size_t)j * GAMEOBJECT_SIZE_BC15;
            size_t dst_go_off = entries_new_start + (size_t)j * (GAMEOBJECT_SIZE_BC15 + 4);
            memcpy(DST(dst_go_off), SRC(src_go_off), GAMEOBJECT_SIZE_BC15);
            w_u32(DST(dst_go_off + GAMEOBJECT_SIZE_BC15), 0xFFFFFFFFu);
        }

        size_t tiles_size = r.old_room_end - r.old_tiles_ptr;
        size_t tiles_new_off = r.old_tiles_ptr + shift_at[i] + room_local_shift;
        memcpy(DST(tiles_new_off), SRC(r.old_tiles_ptr), tiles_size);
        shift_list_ptrtable(out, tiles_new_off - room_payload_off, tiles_size, 0,
                            (int32_t)(shift_at[i] + room_local_shift));

#undef DST
#undef SRC
    }

    *out_new_bytes = out;
    *out_new_size = new_payload_size;
    *out_total_shift = total_shift;
    return 0;
}

static int collect_listchunk_entry_positions(const uint8_t* buf, size_t buf_len, size_t payload_off,
                                             size_t payload_size, std::vector<uint32_t>& out) {
    if (payload_size < 4)
        return 0;
    uint32_t count = r_u32(buf + payload_off);
    if (count == 0)
        return 0;
    if (4 + (size_t)count * 4 > payload_size)
        return -1;
    out.reserve(out.size() + count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t p = r_u32(buf + payload_off + 4 + (size_t)i * 4);
        if (p != 0 && p < buf_len)
            out.push_back(p);
    }
    return 0;
}

static inline bool in_positions(const std::vector<uint32_t>& positions, uint32_t v) {
    return std::binary_search(positions.begin(), positions.end(), v);
}

// Treat every 4-byte aligned u32 that matches a known entry position as a pointer, and bump it.
// Approximate but the false-positive rate on real files is zero in practice.
static void scan_and_bump_pointers(uint8_t* buf, size_t payload_off, size_t payload_size,
                                   const std::vector<uint32_t>& old_positions, int32_t total_shift) {
    if (payload_size < 4)
        return;
    size_t n = payload_size & ~(size_t)3;
    for (size_t i = 0; i + 4 <= n; i += 4) {
        uint32_t v = r_u32(buf + payload_off + i);
        if (in_positions(old_positions, v)) {
            w_u32(buf + payload_off + i, (uint32_t)((int64_t)v + total_shift));
        }
    }
}

static void fix_txtr_blob_offs(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t total_shift) {
    if (payload_size < 4)
        return;
    uint32_t count = r_u32(buf + payload_off);
    if (count == 0)
        return;
    if (4 + (size_t)count * 4 > payload_size)
        return;
    size_t entry_size = Gmtoolkit::detect_txtr_entry_size(buf + payload_off, payload_size);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t rec = r_u32(buf + payload_off + 4 + (size_t)i * 4);
        if (rec == 0)
            continue;
        size_t blob_off_field = (size_t)rec + entry_size - 4;
        if (blob_off_field + 4 > payload_off + payload_size)
            continue;
        uint32_t blob_off = r_u32(buf + blob_off_field);
        if (blob_off != 0) {
            w_u32(buf + blob_off_field, (uint32_t)((int64_t)blob_off + total_shift));
        }
    }
}

static void fix_vari_first_occurrences(uint8_t* buf, size_t payload_off, size_t payload_size, bool bytecode15_plus,
                                       int32_t total_shift) {
    size_t off = payload_off;
    if (bytecode15_plus) {
        if (payload_size < 12)
            return;
        off += 12;
    }
    size_t end = payload_off + payload_size;
    size_t entry_size = bytecode15_plus ? 20 : 12;
    while (off + entry_size <= end) {
        size_t occ_off = off + (bytecode15_plus ? 12 : 4);
        size_t first_off = off + (bytecode15_plus ? 16 : 8);
        uint32_t occ = r_u32(buf + occ_off);
        if (occ > 0) {
            uint32_t first = r_u32(buf + first_off);
            if (first != 0xFFFFFFFFu) {
                w_u32(buf + first_off, (uint32_t)((int64_t)first + total_shift));
            }
        }
        off += entry_size;
    }
}

static void fix_func_first_addresses(uint8_t* buf, size_t payload_off, size_t payload_size, int32_t total_shift) {
    if (payload_size < 4)
        return;
    uint32_t count = r_u32(buf + payload_off);
    size_t off = payload_off + 4;
    size_t end = payload_off + payload_size;
    for (uint32_t i = 0; i < count; i++) {
        if (off + 12 > end)
            break;
        uint32_t occ = r_u32(buf + off + 4);
        uint32_t first = r_u32(buf + off + 8);
        if (occ > 0 && first != 0xFFFFFFFFu) {
            w_u32(buf + off + 8, (uint32_t)((int64_t)first + total_shift));
        }
        off += 12;
    }
}

static void fix_code_name_strings(uint8_t* buf, size_t payload_off, size_t payload_size,
                                  const std::vector<uint32_t>& old_string_positions, int32_t total_shift) {
    if (payload_size < 4)
        return;
    uint32_t count = r_u32(buf + payload_off);
    if (count == 0)
        return;
    if (4 + (size_t)count * 4 > payload_size)
        return;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t rec = r_u32(buf + payload_off + 4 + (size_t)i * 4);
        if (rec == 0 || rec + 4 > payload_off + payload_size)
            continue;
        uint32_t name_ref = r_u32(buf + rec);
        if (in_positions(old_string_positions, name_ref)) {
            w_u32(buf + rec, (uint32_t)((int64_t)name_ref + total_shift));
        }
    }
}

// Expand every ROOM GameObject by 4 bytes, slide everything after, fix up pointers across chunks,
// append the LANG and GLOB chunks that BC16 expects, and bump GEN8.BytecodeVersion to 16.
static int upgrade_15_to_16(const char* path) {
    MappedFile mf;
    if (mapped_file_open(path, &mf) != 0) {
        perror(path);
        return -1;
    }
    if (mf.size <= 0x60) {
        Gmtoolkit::err("data file too small");
        mapped_file_close(&mf);
        return -1;
    }
    size_t file_size = mf.size;
    uint8_t* buf = mf.data;

    uint32_t major = r_u32(buf + 0x10 + 0x2C);
    uint32_t build = r_u32(buf + 0x10 + 0x38);
    bool gms2_padding = (major >= 2) || (major == 1 && build >= 9999);
    if (gms2_padding) {
        Gmtoolkit::tprint("Refusing bc15->16 on a GMS 1.4+/2.x file (Major=%u, Build=%u): "
                          "16-byte chunk-alignment + chunk-after-ROOM slide interactions "
                          "aren't yet handled.\n",
                          major, build);
        mapped_file_close(&mf);
        return -1;
    }

    std::vector<ChunkLoc> chunks;
    if (collect_chunks(buf, file_size, chunks) != 0) {
        Gmtoolkit::err("Failed to walk FORM");
        mapped_file_close(&mf);
        return -1;
    }
    const ChunkLoc* room = find_chunk_loc(chunks, "ROOM");
    if (!room) {
        Gmtoolkit::err("No ROOM chunk");
        mapped_file_close(&mf);
        return -1;
    }

    uint8_t* new_room_payload = NULL;
    size_t new_room_size = 0;
    size_t total_shift = 0;
    if (rebuild_room_15_to_16(buf, file_size, room->payload_off, room->payload_size, &new_room_payload, &new_room_size,
                              &total_shift) != 0) {
        Gmtoolkit::err("ROOM rebuild failed");
        mapped_file_close(&mf);
        return -1;
    }

    Gmtoolkit::tprint("[BYTECODE] ROOM grows %zu -> %zu bytes (+%zu for GameObject expansion)\n", room->payload_size,
                      new_room_size, total_shift);

    std::vector<uint32_t> old_string_positions;
    std::vector<uint32_t> old_pointer_targets;
    {
        const ChunkLoc* strg = find_chunk_loc(chunks, "STRG");
        if (strg) {
            std::vector<uint32_t> strg_entry_starts;
            collect_listchunk_entry_positions(buf, file_size, strg->payload_off, strg->payload_size, strg_entry_starts);
            old_string_positions.reserve(strg_entry_starts.size());
            for (uint32_t p : strg_entry_starts)
                old_string_positions.push_back(p + 4);
            old_pointer_targets.insert(old_pointer_targets.end(), old_string_positions.begin(),
                                       old_string_positions.end());
        }
        const ChunkLoc* tpag = find_chunk_loc(chunks, "TPAG");
        if (tpag) {
            collect_listchunk_entry_positions(buf, file_size, tpag->payload_off, tpag->payload_size,
                                              old_pointer_targets);
        }
        std::sort(old_string_positions.begin(), old_string_positions.end());
        old_string_positions.erase(std::unique(old_string_positions.begin(), old_string_positions.end()),
                                   old_string_positions.end());
        std::sort(old_pointer_targets.begin(), old_pointer_targets.end());
        old_pointer_targets.erase(std::unique(old_pointer_targets.begin(), old_pointer_targets.end()),
                                  old_pointer_targets.end());
        Gmtoolkit::tprint("[BYTECODE] Collected %zu string-content + %zu total pointer targets\n",
                          old_string_positions.size(), old_pointer_targets.size());
    }

    size_t lang_total = 20;
    size_t glob_total = 12;
    size_t new_file_size = file_size + total_shift + lang_total + glob_total;
    uint8_t* out = (uint8_t*)malloc(new_file_size);
    if (!out) {
        Gmtoolkit::err("OOM allocating %zu for new file", new_file_size);
        free(new_room_payload);
        mapped_file_close(&mf);
        return -1;
    }

    memcpy(out, buf, room->payload_off);
    w_u32(out + room->header_off + 4, (uint32_t)new_room_size);
    memcpy(out + room->payload_off, new_room_payload, new_room_size);
    free(new_room_payload);
    size_t tail_old_start = room->payload_off + room->payload_size;
    size_t tail_new_start = room->payload_off + new_room_size;
    size_t tail_len = file_size - tail_old_start;
    memcpy(out + tail_new_start, buf + tail_old_start, tail_len);
    mapped_file_close(&mf);
    buf = nullptr;

    std::vector<ChunkLoc> new_chunks;
    if (collect_chunks(out, tail_new_start + tail_len, new_chunks) != 0) {
        Gmtoolkit::err("Failed to re-walk FORM after slide");
        free(out);
        return -1;
    }

    size_t threshold_for_tail = tail_old_start;
    for (auto& c : new_chunks) {
        if (c.header_off < room->header_off + 8 + new_room_size)
            continue;
        if (is_versioned_listchunk(c.name)) {
            shift_list_ptrtable(out, c.payload_off, c.payload_size, threshold_for_tail, (int32_t)total_shift, 4);
        } else if (is_listchunk_with_ptrtable(c.name)) {
            shift_list_ptrtable(out, c.payload_off, c.payload_size, threshold_for_tail, (int32_t)total_shift);
        }
    }
    (void)threshold_for_tail;

    for (auto& c : new_chunks) {
        if (memcmp(c.name, "TXTR", 4) == 0) {
            fix_txtr_blob_offs(out, c.payload_off, c.payload_size, (int32_t)total_shift);
            continue;
        }
        if (memcmp(c.name, "CODE", 4) == 0) {
            fix_code_name_strings(out, c.payload_off, c.payload_size, old_string_positions, (int32_t)total_shift);
            continue;
        }
        if (memcmp(c.name, "VARI", 4) == 0) {
            fix_vari_first_occurrences(out, c.payload_off, c.payload_size, true, (int32_t)total_shift);
            scan_and_bump_pointers(out, c.payload_off, c.payload_size, old_string_positions, (int32_t)total_shift);
            continue;
        }
        if (memcmp(c.name, "FUNC", 4) == 0) {
            fix_func_first_addresses(out, c.payload_off, c.payload_size, (int32_t)total_shift);
            scan_and_bump_pointers(out, c.payload_off, c.payload_size, old_string_positions, (int32_t)total_shift);
            continue;
        }
        if (memcmp(c.name, "STRG", 4) == 0)
            continue;
        if (memcmp(c.name, "AUDO", 4) == 0)
            continue;
        if (memcmp(c.name, "DAFL", 4) == 0)
            continue;
        if (memcmp(c.name, "LANG", 4) == 0)
            continue;
        if (memcmp(c.name, "GLOB", 4) == 0)
            continue;
        scan_and_bump_pointers(out, c.payload_off, c.payload_size, old_pointer_targets, (int32_t)total_shift);
    }

    size_t append_off = tail_new_start + tail_len;
    uint8_t lang[20] = { 0 };
    memcpy(lang, "LANG", 4);
    w_u32(lang + 4, 12);
    w_u32(lang + 8, 1);
    memcpy(out + append_off, lang, 20);
    append_off += 20;

    uint8_t glob[12] = { 0 };
    memcpy(glob, "GLOB", 4);
    w_u32(glob + 4, 4);
    memcpy(out + append_off, glob, 12);
    append_off += 12;

    uint32_t new_form_size = (uint32_t)(new_file_size - 8);
    w_u32(out + 4, new_form_size);
    out[0x11] = 16;

    if (Gmtoolkit::spew(path, out, new_file_size) != 0) {
        perror(path);
        free(out);
        return -1;
    }

    Gmtoolkit::tprint("[BYTECODE] 15 -> 16 complete: file %zu -> %zu (+%zu ROOM expansion, "
                      "+32 LANG+GLOB)\n",
                      file_size, new_file_size, total_shift);

    free(out);
    return 0;
}

int set_bytecode_version(const char* path, int target) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return -1;
    }
    uint8_t cur_bv = 0;
    if (fseek(f, 0x11, SEEK_SET) != 0 || fread(&cur_bv, 1, 1, f) != 1) {
        perror("read BytecodeVersion");
        fclose(f);
        return -1;
    }
    fclose(f);

    if (cur_bv == (uint8_t)target) {
        Gmtoolkit::tprint("[BYTECODE] Already version %d, no-op.\n", target);
        return 0;
    }

    if (cur_bv == 15 && target == 16) {
        return upgrade_15_to_16(path);
    }
    Gmtoolkit::err("Unsupported bytecode transition: %u -> %d", cur_bv, target);
    return -1;
}
