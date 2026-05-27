// SPDX-License-Identifier: MIT

#include "GMSLib/SaveBackend/ScptGrow.h"
#include "Toolkit/Log.h"

#include "GMSLib/SaveBackend/ChunkShift.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace GMSLib::SaveBackend {

static bool detect_inst_creation_field(const uint8_t* buf, size_t buf_len, size_t room_pay_off) {
    uint32_t count = r_u32(buf + room_pay_off);
    if (count == 0)
        return false;
    uint32_t r0 = r_u32(buf + room_pay_off + 4);
    if (r0 == 0)
        return false;
    size_t plus_38 = (size_t)r0 + 0x38;
    if (plus_38 + 4 > buf_len)
        return false;
    uint32_t v = r_u32(buf + plus_38);
    if (v < 2)
        return false;
    return v < (uint32_t)buf_len;
}

// Insert new SCPT entries without rewriting the whole file: extend the ptr table + struct list,
// round the inflate to 16 bytes to keep alignment, then shift every chunk that lives after SCPT.
int grow_scpt_in_place(Pools& P, const std::vector<Pools::ScptInsert>& inserts) {
    if (inserts.empty())
        return 0;

    auto& buf = P.buf;
    auto& chunks = P.chunks;
    auto scpt_it = chunks.find("SCPT");
    if (scpt_it == chunks.end()) {
        Gmtoolkit::err("scpt_grow: no SCPT chunk");
        return -1;
    }
    size_t scpt_pay = scpt_it->second.payload_off;
    size_t scpt_size = scpt_it->second.size;

    if (scpt_size < 4)
        return -1;
    uint32_t old_count = r_u32(buf.data() + scpt_pay);
    size_t ptab_end_old = scpt_pay + 4 + (size_t)old_count * 4;
    size_t entries_end_old = ptab_end_old + (size_t)old_count * 8;
    size_t slack = (scpt_pay + scpt_size) - entries_end_old;
    size_t need = 12 * inserts.size();
    int32_t d_scpt = (need > slack) ? (int32_t)(need - slack) : 0;
    if (d_scpt & 15)
        d_scpt = (d_scpt + 15) & ~15;

    size_t old_buf_size = buf.size();

    if (d_scpt > 0) {
        buf.resize(old_buf_size + (size_t)d_scpt);
        size_t scpt_end_old = scpt_pay + scpt_size;
        std::memmove(buf.data() + scpt_end_old + (size_t)d_scpt, buf.data() + scpt_end_old,
                     old_buf_size - scpt_end_old);
        std::memset(buf.data() + scpt_end_old, 0, (size_t)d_scpt);
    }

    std::vector<uint32_t> tpag_positions;
    auto tpag_it = chunks.find("TPAG");
    size_t tpag_pay_old = (tpag_it != chunks.end()) ? tpag_it->second.payload_off : 0;
    size_t tpag_size_old = (tpag_it != chunks.end()) ? tpag_it->second.size : 0;
    if (tpag_it != chunks.end()) {
        collect_listchunk_entry_positions(buf.data(), tpag_pay_old + (size_t)d_scpt, tpag_size_old, tpag_positions);
        std::sort(tpag_positions.begin(), tpag_positions.end());
    }

    for (auto& kv : chunks) {
        if (kv.first == "SCPT")
            continue;
        if (kv.second.payload_off > scpt_pay) {
            kv.second.payload_off += (size_t)d_scpt;
        }
    }

    size_t new_scpt_size = scpt_size + need;
    new_scpt_size = scpt_size + (size_t)d_scpt;
    scpt_it->second.size = new_scpt_size;
    w_u32(buf.data() + (scpt_pay - 4), (uint32_t)new_scpt_size);

    uint32_t form_size = r_u32(buf.data() + 4);
    w_u32(buf.data() + 4, form_size + (uint32_t)d_scpt);

    uint32_t new_count = old_count + (uint32_t)inserts.size();
    size_t ptr_growth = 4 * inserts.size();

    if (old_count > 0) {
        std::memmove(buf.data() + ptab_end_old + ptr_growth, buf.data() + ptab_end_old, (size_t)old_count * 8);
    }
    for (uint32_t i = 0; i < old_count; i++) {
        size_t pp = scpt_pay + 4 + (size_t)i * 4;
        uint32_t v = r_u32(buf.data() + pp);
        if (v != 0)
            w_u32(buf.data() + pp, v + (uint32_t)ptr_growth);
    }

    size_t new_entries_start = ptab_end_old + ptr_growth + (size_t)old_count * 8;
    for (size_t k = 0; k < inserts.size(); k++) {
        size_t entry_pos = new_entries_start + k * 8;
        size_t pp = scpt_pay + 4 + ((size_t)old_count + k) * 4;
        w_u32(buf.data() + pp, (uint32_t)entry_pos);
        // Both name pointer and code_id are placeholders; PoolsCommit resolves
        // them after the final CODE layout is known.
        w_u32(buf.data() + entry_pos + 0, 0xCAFEBABEu);
        w_u32(buf.data() + entry_pos + 4, 0xCAFEBABEu);
        (void)inserts;
    }

    w_u32(buf.data() + scpt_pay, new_count);

    if (d_scpt > 0) {
        auto is_after_code = [&](size_t payload_off) {
            auto cit = chunks.find("CODE");
            if (cit == chunks.end())
                return false;
            return payload_off >= cit->second.payload_off;
        };

        for (auto& kv : chunks) {
            if (kv.first == "SCPT")
                continue;
            if (kv.second.payload_off <= scpt_pay)
                continue;
            if (is_after_code(kv.second.payload_off))
                continue;
            // Versioned list-chunks store a u32 version before the count; TGIN has its
            // own internals walker below and skips this dispatch.
            if (is_versioned_listchunk(kv.first.c_str())) {
                if (kv.first != "TGIN") {
                    shift_versioned_chunk_ptab(buf.data(), kv.second.payload_off, kv.second.size, d_scpt);
                }
                continue;
            }
            if (!is_listchunk_with_ptrtable(kv.first.c_str()))
                continue;
            shift_chunk_ptab(buf.data(), kv.second.payload_off, kv.second.size, d_scpt);
            size_t orig_off = kv.second.payload_off - (size_t)d_scpt;
            if (kv.first == "SHDR") {
                shift_shdr_internals(buf.data(), kv.second.payload_off, kv.second.size, orig_off, d_scpt,
                                     P.version.bytecode_version);
            } else if (kv.first == "FONT") {
                FontEntryLayout layout{};
                layout.has_ascender_offset = P.version.bytecode_version >= 17;
                layout.has_ascender = P.version.is_at_least(2022, 2);
                layout.has_sdf_spread = P.version.is_non_lts_at_least(2023, 2);
                layout.has_line_height = P.version.is_at_least(2023, 6);
                layout.align_4_after = P.version.is_at_least(2024, 14);
                shift_font_internals(buf.data(), kv.second.payload_off, kv.second.size, orig_off, d_scpt, layout,
                                     tpag_positions);
            }
        }

        auto code_it = chunks.find("CODE");
        if (code_it != chunks.end() && code_it->second.payload_off > scpt_pay) {
            shift_chunk_ptab(buf.data(), code_it->second.payload_off, code_it->second.size, d_scpt);
        }

        auto objt_it = chunks.find("OBJT");
        if (objt_it != chunks.end() && objt_it->second.payload_off > scpt_pay) {
            shift_objt_internals(buf.data(), objt_it->second.payload_off, objt_it->second.size, d_scpt,
                                 P.version.is_at_least(2022, 5));
        }
        auto room_it = chunks.find("ROOM");
        if (room_it != chunks.end() && room_it->second.payload_off > scpt_pay) {
            RoomShiftOptions opt;
            opt.gms2 = P.version.is_at_least(2);
            opt.gms2_3 = P.version.using_gms2_3();
            opt.has_inst_creation = P.version.is_at_least(2024, 13) ||
                                    detect_inst_creation_field(buf.data(), buf.size(), room_it->second.payload_off);
            opt.is_2022_1 = P.version.is_at_least(2022, 1);
            opt.is_2_3_2 = P.version.is_at_least(2, 3, 2);
            opt.is_non_lts_2023_2 = P.version.is_non_lts_at_least(2023, 2);
            opt.is_2024_6 = P.version.is_at_least(2024, 6);
            opt.is_2024_2 = P.version.is_at_least(2024, 2);
            opt.is_2024_4 = P.version.is_at_least(2024, 4);
            shift_room_internals(buf.data(), room_it->second.payload_off, room_it->second.size, d_scpt, opt);
        }

        if (!tpag_positions.empty()) {
            for (const char* tag : { "SPRT", "BGND", "EMBI" }) {
                auto it = chunks.find(tag);
                if (it == chunks.end())
                    continue;
                scan_and_bump_pointers(buf.data(), it->second.payload_off, it->second.size, tpag_positions, d_scpt);
            }
        }

        auto tgin_it = chunks.find("TGIN");
        if (tgin_it != chunks.end() && tgin_it->second.payload_off > scpt_pay) {
            shift_tgin_internals(buf.data(), tgin_it->second.payload_off, tgin_it->second.size, d_scpt);
        }
        auto vari_it = chunks.find("VARI");
        if (vari_it != chunks.end() && vari_it->second.payload_off > scpt_pay) {
            shift_vari_first_addrs(buf.data(), vari_it->second.payload_off, vari_it->second.size, d_scpt);
        }
        auto func_it = chunks.find("FUNC");
        if (func_it != chunks.end() && func_it->second.payload_off > scpt_pay) {
            shift_func_first_addrs(buf.data(), func_it->second.payload_off, func_it->second.size, d_scpt);
        }
        auto tags_it = chunks.find("TAGS");
        if (tags_it != chunks.end() && tags_it->second.payload_off > scpt_pay) {
            size_t orig_off = tags_it->second.payload_off - (size_t)d_scpt;
            shift_tags_internals(buf.data(), tags_it->second.payload_off, tags_it->second.size, orig_off, d_scpt);
        }
    }

    return 0;
}

} // namespace GMSLib::SaveBackend
