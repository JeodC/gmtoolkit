// SPDX-License-Identifier: MIT

#include "GMSLib/SaveBackend/CodeChunk.h"
#include "GMSLib/SaveBackend/Pools.h"
#include "GMSLib/SaveBackend/ScptGrow.h"
#include "Toolkit/IO.h"
#include "Toolkit/Log.h"
#include "Toolkit/Platform.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>

namespace GMSLib::SaveBackend {

using Gmtoolkit::r_u32;
using Gmtoolkit::w_u32;

namespace {

inline size_t vari_entry_size(uint8_t bcv) {
    return bcv >= 15 ? 20 : 12;
}
inline size_t vari_preamble_size(uint8_t bcv) {
    return bcv >= 15 ? 12 : 0;
}

inline size_t code_struct_size(uint8_t bcv, bool gms_2_3) {
    if (bcv <= 14)
        return 0;
    return gms_2_3 ? 20 : 16;
}

size_t strg_entry_bytes(const std::string& s) {
    size_t raw = 4 + s.size() + 1;
    return (raw + 3) & ~size_t(3);
}

bool chunk_is_binary_skip(std::string_view tag) {
    return tag == "TXTR" || tag == "AUDO" || tag == "TPAG" || tag == "ROOM" || tag == "STRG";
}

} // namespace

// Replace one CODE entry's bytecode blob in place, slide everything after it by the size delta,
// and update sibling entry offsets that crossed the splice point.
static bool apply_one_code_patch_inplace(Pools& P, std::vector<CodeEntry>& entries, int target_idx,
                                         const Pools::CodePatch& patch) {
    Gmtoolkit::Buffer& buf = P.buf;
    auto& chunks = P.chunks;
    const std::vector<uint8_t>& new_bc = patch.bytecode;
    uint16_t locals_count = patch.locals_count;
    uint16_t args_count = patch.args_count;
    auto it = chunks.find("CODE");
    if (it == chunks.end()) {
        Gmtoolkit::err("splice: no CODE chunk");
        return false;
    }
    size_t code_off = it->second.payload_off;
    size_t code_size = it->second.size;

    const CodeEntry& target = entries[target_idx];
    size_t old_blob_off = target.bytecode_offset;
    size_t old_blob_len = target.length;
    size_t new_blob_len = new_bc.size();
    int64_t delta = (int64_t)new_blob_len - (int64_t)old_blob_len;

    size_t old_buf_size = buf.size();
    size_t tail_start = old_blob_off + old_blob_len;
    if (delta > 0) {
        size_t shift = (size_t)delta;
        buf.resize(old_buf_size + shift);
        std::memmove(buf.data() + tail_start + shift, buf.data() + tail_start, old_buf_size - tail_start);
    } else if (delta < 0) {
        size_t shift = (size_t)(-delta);
        std::memmove(buf.data() + tail_start - shift, buf.data() + tail_start, old_buf_size - tail_start);
        buf.resize(old_buf_size - shift);
    }

    std::memcpy(buf.data() + old_blob_off, new_bc.data(), new_blob_len);

    size_t struct_pos_new = target.entry_offset;
    if (target.entry_offset >= tail_start)
        struct_pos_new += (size_t)delta;
    uint16_t old_args_word = (uint16_t)buf[struct_pos_new + 10] | ((uint16_t)buf[struct_pos_new + 11] << 8);
    uint16_t weird_flag = old_args_word & 0x8000;
    uint16_t new_args_word = (args_count & 0x7FFF) | weird_flag;
    w_u32(buf.data() + struct_pos_new + 4, (uint32_t)new_blob_len);
    buf[struct_pos_new + 8] = (uint8_t)(locals_count & 0xFF);
    buf[struct_pos_new + 9] = (uint8_t)((locals_count >> 8) & 0xFF);
    buf[struct_pos_new + 10] = (uint8_t)(new_args_word & 0xFF);
    buf[struct_pos_new + 11] = (uint8_t)((new_args_word >> 8) & 0xFF);

    for (size_t i = 0; i < entries.size(); i++) {
        size_t struct_pos_old = entries[i].entry_offset;
        if (struct_pos_old < tail_start)
            continue;
        if (entries[i].bytecode_offset >= tail_start)
            continue;
        size_t struct_new = struct_pos_old + (size_t)delta;
        size_t rel_field_pos = struct_new + 12;
        int32_t old_rel = (int32_t)r_u32(buf.data() + rel_field_pos);
        int32_t new_rel = old_rel - (int32_t)delta;
        w_u32(buf.data() + rel_field_pos, (uint32_t)new_rel);
    }

    size_t code_count_field = code_off;
    uint32_t code_count = r_u32(buf.data() + code_count_field);
    size_t ptab_start = code_count_field + 4;
    for (uint32_t i = 0; i < code_count; i++) {
        size_t pp = ptab_start + (size_t)i * 4;
        uint32_t old_ptr = r_u32(buf.data() + pp);
        uint32_t new_ptr;
        if (delta > 0)
            new_ptr = old_ptr + (uint32_t)delta;
        else
            new_ptr = old_ptr - (uint32_t)(-delta);
        w_u32(buf.data() + pp, new_ptr);
    }

    w_u32(buf.data() + (code_off - 4), (uint32_t)((int64_t)code_size + delta));

    uint32_t form_size = r_u32(buf.data() + 4);
    w_u32(buf.data() + 4, (uint32_t)((int64_t)form_size + delta));

    it->second.size = (size_t)((int64_t)code_size + delta);
    if (delta != 0) {
        for (auto& kv : chunks) {
            if (kv.first != "CODE" && kv.second.payload_off > code_off) {
                if (delta > 0)
                    kv.second.payload_off += (size_t)delta;
                else
                    kv.second.payload_off -= (size_t)(-delta);
            }
        }
    }

    auto update_one = [&](std::vector<Pools::Occurrence>& occs, const std::string& target_name,
                          const std::vector<std::pair<size_t, uint8_t>>& new_refs) {
        std::vector<Pools::Occurrence> kept;
        kept.reserve(occs.size());
        for (auto& o : occs) {
            if (o.operand_offset >= old_blob_off && o.operand_offset < tail_start)
                continue;
            Pools::Occurrence nv = o;
            if (o.operand_offset >= tail_start)
                nv.operand_offset += (uint32_t)delta;
            kept.push_back(nv);
        }
        for (auto& r : new_refs) {
            Pools::Occurrence n;
            n.operand_offset = (uint32_t)(old_blob_off + r.first);
            n.var_type = r.second;
            kept.push_back(n);
        }
        occs = std::move(kept);
        (void)target_name;
    };

    auto is_per_script = [](int32_t inst_type) { return inst_type == -7 || inst_type == -15; };
    for (size_t vi = 0; vi < P.vari_entries.size(); vi++) {
        const Pools::VariEntry& v = P.vari_entries[vi];
        bool belongs = false;
        for (auto& o : P.var_occurrences[vi]) {
            if (o.operand_offset >= old_blob_off && o.operand_offset < tail_start) {
                belongs = true;
                break;
            }
        }
        std::vector<std::pair<size_t, uint8_t>> new_refs;
        if (is_per_script(v.inst_type) ? belongs : true) {
            for (auto& vr : patch.var_refs) {
                if (vr.name == v.name && vr.inst_type == v.inst_type) {
                    new_refs.push_back({ vr.byte_offset, vr.var_type });
                }
            }
        }
        update_one(P.var_occurrences[vi], v.name, new_refs);
    }
    for (size_t fi = 0; fi < P.func_entries.size(); fi++) {
        const Pools::FuncEntry& fe = P.func_entries[fi];
        std::vector<std::pair<size_t, uint8_t>> new_refs;
        for (auto& fr : patch.func_refs) {
            if (fr.name == fe.name) {
                new_refs.push_back({ fr.byte_offset, 0 });
            }
        }
        update_one(P.func_occurrences[fi], fe.name, new_refs);
    }

    for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].entry_offset >= tail_start) {
            if (delta > 0)
                entries[i].entry_offset += (uint32_t)delta;
            else
                entries[i].entry_offset -= (uint32_t)(-delta);
        }
        if (entries[i].bytecode_offset >= tail_start) {
            if (delta > 0)
                entries[i].bytecode_offset += (uint32_t)delta;
            else
                entries[i].bytecode_offset -= (uint32_t)(-delta);
        }
    }
    entries[target_idx].length = (uint32_t)new_blob_len;

    for (const auto& child : patch.children) {
        bool found = false;
        for (size_t i = 0; i < entries.size(); i++) {
            if (entries[i].name != child.name)
                continue;
            entries[i].length = (uint32_t)new_blob_len;
            entries[i].bytecode_offset = (uint32_t)old_blob_off;
            entries[i].locals_count = child.locals_count;
            entries[i].args_count = child.args_count;
            entries[i].self_offset = (uint32_t)child.body_offset;
            size_t sp = entries[i].entry_offset;
            uint16_t old_args = (uint16_t)buf[sp + 10] | ((uint16_t)buf[sp + 11] << 8);
            uint16_t weird = old_args & 0x8000;
            uint16_t new_args = (child.args_count & 0x7FFF) | weird;
            w_u32(buf.data() + sp + 4, (uint32_t)new_blob_len);
            buf[sp + 8] = (uint8_t)(child.locals_count & 0xFF);
            buf[sp + 9] = (uint8_t)((child.locals_count >> 8) & 0xFF);
            buf[sp + 10] = (uint8_t)(new_args & 0xFF);
            buf[sp + 11] = (uint8_t)((new_args >> 8) & 0xFF);
            if (sp + 20 <= buf.size()) {
                w_u32(buf.data() + sp + 16, (uint32_t)child.body_offset);
            }
            int32_t new_rel = (int32_t)((int64_t)old_blob_off - (int64_t)(sp + 12));
            w_u32(buf.data() + sp + 12, (uint32_t)new_rel);
            found = true;
            break;
        }
        (void)found;
    }

    return true;
}

static bool append_one_new_glob_entry(Pools& P, uint32_t code_id) {
    auto& buf = P.buf;
    auto& chunks = P.chunks;
    auto glob_it = chunks.find("GLOB");
    if (glob_it == chunks.end()) {
        Gmtoolkit::err("glob: no GLOB chunk");
        return false;
    }
    size_t glob_off = glob_it->second.payload_off;
    size_t glob_size = glob_it->second.size;
    uint32_t old_count = r_u32(buf.data() + glob_off);

    size_t used = 4 + (size_t)old_count * 4;
    size_t slack = glob_size - used;
    if (slack < 4) {
        Gmtoolkit::err("glob: no trailing slack to add new global init "
                       "(have %zu, need 4). Full deserialize/serialize "
                       "would be required to grow GLOB safely.",
                       slack);
        return false;
    }

    w_u32(buf.data() + glob_off + used, code_id);
    w_u32(buf.data() + glob_off, old_count + 1);
    return true;
}

// Grow CODE by appending a new entry: bump the count, extend the ptr table, append a struct + blob,
// then shift every subsequent entry struct (and its ptr-table reference) by the resulting delta.
static bool append_one_new_code_entry(Pools& P, std::vector<CodeEntry>& entries, const std::string& entry_name,
                                      const Pools::CodePatch& patch) {
    auto& buf = P.buf;
    auto& chunks = P.chunks;
    auto it = chunks.find("CODE");
    if (it == chunks.end()) {
        Gmtoolkit::err("append: no CODE chunk");
        return false;
    }
    size_t code_off = it->second.payload_off;
    size_t code_size = it->second.size;
    uint32_t old_count = r_u32(buf.data() + code_off);

    size_t ptab_start = code_off + 4;
    size_t ptab_end_old = ptab_start + 4ull * old_count;
    size_t code_end_old = code_off + code_size;

    if (!patch.children.empty() && !P.version.using_gms2_3()) {
        Gmtoolkit::err("append: nested children in patch (%zu) require GMS 2.3+;"
                       "pre-2.3 needs top-level CODE+SCPT pairs.\n",
                       patch.children.size());
        return false;
    }
    const size_t struct_sz = code_struct_size(P.version.bytecode_version, P.version.using_gms2_3());
    const size_t n_new = 1 + patch.children.size();
    size_t blob_size = patch.bytecode.size();
    size_t ptab_growth = 4 * n_new;
    size_t struct_growth = struct_sz * n_new;
    size_t total_growth = ptab_growth + blob_size + struct_growth;
    uint32_t old_struct_table_start = UINT32_MAX;
    uint32_t old_struct_table_max = 0;
    for (uint32_t i = 0; i < old_count; i++) {
        uint32_t v = r_u32(buf.data() + ptab_start + (size_t)i * 4);
        if (v > 0 && v < old_struct_table_start)
            old_struct_table_start = v;
        if (v > old_struct_table_max)
            old_struct_table_max = v;
    }
    if (old_struct_table_start == UINT32_MAX)
        old_struct_table_start = (uint32_t)code_end_old;
    size_t old_struct_table_end = (size_t)old_struct_table_max + struct_sz;
    if (old_count == 0)
        old_struct_table_end = (size_t)old_struct_table_start;

    size_t old_buf_size = buf.size();
    buf.resize(old_buf_size + total_growth);

    std::memmove(buf.data() + code_end_old + total_growth, buf.data() + code_end_old, old_buf_size - code_end_old);
    std::memmove(buf.data() + (size_t)old_struct_table_start + ptab_growth + blob_size,
                 buf.data() + (size_t)old_struct_table_start, old_struct_table_end - (size_t)old_struct_table_start);
    std::memmove(buf.data() + ptab_end_old + ptab_growth, buf.data() + ptab_end_old,
                 (size_t)old_struct_table_start - ptab_end_old);

    size_t new_blob_off = (size_t)old_struct_table_start + ptab_growth;
    size_t new_struct_off = old_struct_table_end + ptab_growth + blob_size;
    size_t struct_shift = ptab_growth + blob_size;
    for (uint32_t i = 0; i < old_count; i++) {
        size_t pp = ptab_start + (size_t)i * 4;
        uint32_t v = r_u32(buf.data() + pp);
        if (v == 0)
            continue;
        w_u32(buf.data() + pp, v + (uint32_t)struct_shift);
    }

    if (blob_size > 0) {
        for (uint32_t i = 0; i < old_count; i++) {
            uint32_t struct_pos_new = r_u32(buf.data() + ptab_start + (size_t)i * 4);
            if (struct_pos_new == 0)
                continue;
            size_t rel_field = (size_t)struct_pos_new + 12;
            int32_t old_rel = (int32_t)r_u32(buf.data() + rel_field);
            int32_t new_rel = old_rel - (int32_t)blob_size;
            w_u32(buf.data() + rel_field, (uint32_t)new_rel);
        }
    }

    for (size_t i = 0; i < n_new; i++) {
        size_t pos = new_struct_off + i * struct_sz;
        w_u32(buf.data() + ptab_end_old + i * 4, (uint32_t)pos);
    }

    std::memcpy(buf.data() + new_blob_off, patch.bytecode.data(), blob_size);

    bool is_global_script = entry_name.size() > 17 && entry_name.compare(0, 17, "gml_GlobalScript_") == 0;
    uint16_t parent_args_word = patch.args_count & 0x7FFF;
    if (is_global_script)
        parent_args_word |= 0x8000;
    {
        uint8_t st[20] = { 0 };
        w_u32(st + 0, 0xCAFEBABEu);
        w_u32(st + 4, (uint32_t)blob_size);
        st[8] = (uint8_t)(patch.locals_count & 0xFF);
        st[9] = (uint8_t)((patch.locals_count >> 8) & 0xFF);
        st[10] = (uint8_t)(parent_args_word & 0xFF);
        st[11] = (uint8_t)((parent_args_word >> 8) & 0xFF);
        int32_t br = (int32_t)((int64_t)new_blob_off - (int64_t)(new_struct_off + 12));
        w_u32(st + 12, (uint32_t)br);
        if (struct_sz >= 20) {
            w_u32(st + 16, 0);
        }
        std::memcpy(buf.data() + new_struct_off, st, struct_sz);
    }

    for (size_t ci = 0; ci < patch.children.size(); ci++) {
        const auto& c = patch.children[ci];
        size_t struct_pos = new_struct_off + (1 + ci) * struct_sz;
        uint8_t st[20] = { 0 };
        w_u32(st + 0, 0xCAFEBABEu);
        w_u32(st + 4, (uint32_t)blob_size);
        st[8] = (uint8_t)(c.locals_count & 0xFF);
        st[9] = (uint8_t)((c.locals_count >> 8) & 0xFF);
        st[10] = (uint8_t)(c.args_count & 0xFF);
        st[11] = (uint8_t)((c.args_count >> 8) & 0xFF);
        int32_t br = (int32_t)((int64_t)new_blob_off - (int64_t)(struct_pos + 12));
        w_u32(st + 12, (uint32_t)br);
        w_u32(st + 16, (uint32_t)c.body_offset);
        std::memcpy(buf.data() + struct_pos, st, struct_sz);
    }

    size_t code_end_new = code_end_old + total_growth;
    size_t pad_start = new_struct_off + struct_growth;
    if (code_end_new > pad_start) {
        std::memset(buf.data() + pad_start, 0, code_end_new - pad_start);
    }

    w_u32(buf.data() + code_off, old_count + (uint32_t)n_new);

    size_t new_code_size = code_size + total_growth;
    it->second.size = new_code_size;
    w_u32(buf.data() + (code_off - 4), (uint32_t)new_code_size);
    uint32_t form_size = r_u32(buf.data() + 4);
    w_u32(buf.data() + 4, (uint32_t)(form_size + total_growth));
    for (auto& kv : chunks) {
        if (kv.first != "CODE" && kv.second.payload_off > code_off) {
            kv.second.payload_off += total_growth;
        }
    }

    for (auto& occs : P.var_occurrences) {
        for (auto& o : occs) {
            if (o.operand_offset >= ptab_end_old && o.operand_offset < old_struct_table_start) {
                o.operand_offset += (uint32_t)ptab_growth;
            } else if (o.operand_offset >= old_struct_table_start) {
                o.operand_offset += (uint32_t)struct_shift;
            }
        }
    }
    for (auto& occs : P.func_occurrences) {
        for (auto& o : occs) {
            if (o.operand_offset >= ptab_end_old && o.operand_offset < old_struct_table_start) {
                o.operand_offset += (uint32_t)ptab_growth;
            } else if (o.operand_offset >= old_struct_table_start) {
                o.operand_offset += (uint32_t)struct_shift;
            }
        }
    }

    auto find_var_idx = [&](const std::string& name, int32_t inst) -> int {
        for (size_t vi = 0; vi < P.vari_entries.size(); vi++) {
            if (P.vari_entries[vi].name == name && P.vari_entries[vi].inst_type == inst)
                return (int)vi;
        }
        for (size_t pi = P.pending_vars.size(); pi-- > 0;) {
            if (P.pending_vars[pi].first == name && P.pending_vars[pi].second == inst) {
                return (int)(P.vari_entries.size() + pi);
            }
        }
        return -1;
    };
    for (const auto& vr : patch.var_refs) {
        int idx = find_var_idx(vr.name, vr.inst_type);
        if (idx < 0)
            continue;
        if ((size_t)idx >= P.vari_entries.size())
            continue;
        Pools::Occurrence o;
        o.operand_offset = (uint32_t)(new_blob_off + vr.byte_offset);
        o.var_type = vr.var_type;
        if ((size_t)idx >= P.var_occurrences.size())
            P.var_occurrences.resize((size_t)idx + 1);
        P.var_occurrences[(size_t)idx].push_back(o);
    }
    for (const auto& fr : patch.func_refs) {
        int found = -1;
        for (size_t fi = 0; fi < P.func_entries.size(); fi++) {
            if (P.func_entries[fi].name == fr.name) {
                found = (int)fi;
                break;
            }
        }
        if (found >= 0) {
            Pools::Occurrence o;
            o.operand_offset = (uint32_t)(new_blob_off + fr.byte_offset);
            o.var_type = 0;
            P.func_occurrences[found].push_back(o);
        }
    }

    for (size_t i = 0; i < entries.size(); i++) {
        entries[i].entry_offset += (uint32_t)(ptab_growth + blob_size);
        entries[i].bytecode_offset += (uint32_t)ptab_growth;
    }
    CodeEntry ne;
    ne.entry_offset = (uint32_t)new_struct_off;
    ne.length = (uint32_t)blob_size;
    ne.locals_count = patch.locals_count;
    ne.args_count = patch.args_count;
    ne.weird_local_flag = false;
    ne.bytecode_offset = (uint32_t)new_blob_off;
    ne.self_offset = 0;
    ne.name = entry_name;
    entries.push_back(std::move(ne));

    for (size_t ci = 0; ci < patch.children.size(); ci++) {
        const auto& c = patch.children[ci];
        CodeEntry sub;
        sub.entry_offset = (uint32_t)(new_struct_off + (1 + ci) * struct_sz);
        sub.length = (uint32_t)blob_size;
        sub.locals_count = c.locals_count;
        sub.args_count = c.args_count;
        sub.weird_local_flag = false;
        sub.bytecode_offset = (uint32_t)new_blob_off;
        sub.self_offset = (uint32_t)c.body_offset;
        sub.name = c.name;
        entries.push_back(std::move(sub));
    }

    return true;
}

struct NewChildSpec {
    std::string name;
    uint16_t locals_count;
    uint16_t args_count;
    size_t parent_blob_off;
    size_t parent_new_length;
    size_t body_offset;
};

static bool append_extra_children_to_code(Pools& P, std::vector<CodeEntry>& entries,
                                          const std::vector<NewChildSpec>& new_children) {
    if (new_children.empty())
        return true;
    auto& buf = P.buf;
    auto& chunks = P.chunks;
    auto it = chunks.find("CODE");
    if (it == chunks.end()) {
        Gmtoolkit::err("append-extra: no CODE chunk");
        return false;
    }
    if (!P.version.using_gms2_3()) {
        Gmtoolkit::err("append-extra: nested children require GMS 2.3+");
        return false;
    }
    size_t code_off = it->second.payload_off;
    size_t code_size = it->second.size;
    uint32_t old_count = r_u32(buf.data() + code_off);

    size_t ptab_start = code_off + 4;
    size_t ptab_end_old = ptab_start + 4ull * old_count;
    size_t code_end_old = code_off + code_size;

    const size_t struct_sz = code_struct_size(P.version.bytecode_version, P.version.using_gms2_3());
    const size_t n_new = new_children.size();
    const size_t ptab_growth = 4 * n_new;
    const size_t struct_growth = struct_sz * n_new;
    const size_t total_growth = ptab_growth + struct_growth;

    uint32_t old_struct_table_start = UINT32_MAX;
    uint32_t old_struct_table_max = 0;
    for (uint32_t i = 0; i < old_count; i++) {
        uint32_t v = r_u32(buf.data() + ptab_start + (size_t)i * 4);
        if (v > 0 && v < old_struct_table_start)
            old_struct_table_start = v;
        if (v > old_struct_table_max)
            old_struct_table_max = v;
    }
    if (old_struct_table_start == UINT32_MAX)
        old_struct_table_start = (uint32_t)code_end_old;
    size_t old_struct_table_end = (size_t)old_struct_table_max + struct_sz;
    if (old_count == 0)
        old_struct_table_end = (size_t)old_struct_table_start;

    size_t old_buf_size = buf.size();
    buf.resize(old_buf_size + total_growth);

    std::memmove(buf.data() + code_end_old + total_growth, buf.data() + code_end_old, old_buf_size - code_end_old);
    std::memmove(buf.data() + (size_t)old_struct_table_start + ptab_growth, buf.data() + (size_t)old_struct_table_start,
                 old_struct_table_end - (size_t)old_struct_table_start);
    std::memmove(buf.data() + ptab_end_old + ptab_growth, buf.data() + ptab_end_old,
                 (size_t)old_struct_table_start - ptab_end_old);
    size_t new_struct_off = old_struct_table_end + ptab_growth;

    for (uint32_t i = 0; i < old_count; i++) {
        size_t pp = ptab_start + (size_t)i * 4;
        uint32_t v = r_u32(buf.data() + pp);
        if (v == 0)
            continue;
        w_u32(buf.data() + pp, v + (uint32_t)ptab_growth);
    }

    for (size_t i = 0; i < n_new; i++) {
        w_u32(buf.data() + ptab_end_old + i * 4, (uint32_t)(new_struct_off + i * struct_sz));
    }

    for (size_t i = 0; i < n_new; i++) {
        const auto& c = new_children[i];
        size_t struct_pos = new_struct_off + i * struct_sz;
        size_t parent_blob_new = c.parent_blob_off + ptab_growth;
        uint8_t st[20] = { 0 };
        w_u32(st + 0, 0xCAFEBABEu);
        w_u32(st + 4, (uint32_t)c.parent_new_length);
        st[8] = (uint8_t)(c.locals_count & 0xFF);
        st[9] = (uint8_t)((c.locals_count >> 8) & 0xFF);
        st[10] = (uint8_t)(c.args_count & 0xFF);
        st[11] = (uint8_t)((c.args_count >> 8) & 0xFF);
        int32_t br = (int32_t)((int64_t)parent_blob_new - (int64_t)(struct_pos + 12));
        w_u32(st + 12, (uint32_t)br);
        w_u32(st + 16, (uint32_t)c.body_offset);
        std::memcpy(buf.data() + struct_pos, st, struct_sz);
    }

    size_t code_end_new = code_end_old + total_growth;
    size_t pad_start = new_struct_off + struct_growth;
    if (code_end_new > pad_start) {
        std::memset(buf.data() + pad_start, 0, code_end_new - pad_start);
    }

    w_u32(buf.data() + code_off, old_count + (uint32_t)n_new);

    size_t new_code_size = code_size + total_growth;
    it->second.size = new_code_size;
    w_u32(buf.data() + (code_off - 4), (uint32_t)new_code_size);
    uint32_t form_size = r_u32(buf.data() + 4);
    w_u32(buf.data() + 4, (uint32_t)(form_size + total_growth));
    for (auto& kv : chunks) {
        if (kv.first != "CODE" && kv.second.payload_off > code_off) {
            kv.second.payload_off += total_growth;
        }
    }

    for (auto& occs : P.var_occurrences) {
        for (auto& o : occs) {
            if (o.operand_offset >= ptab_end_old && o.operand_offset < old_struct_table_start) {
                o.operand_offset += (uint32_t)ptab_growth;
            }
        }
    }
    for (auto& occs : P.func_occurrences) {
        for (auto& o : occs) {
            if (o.operand_offset >= ptab_end_old && o.operand_offset < old_struct_table_start) {
                o.operand_offset += (uint32_t)ptab_growth;
            }
        }
    }

    for (size_t i = 0; i < entries.size(); i++) {
        entries[i].entry_offset += (uint32_t)ptab_growth;
        entries[i].bytecode_offset += (uint32_t)ptab_growth;
    }
    for (size_t i = 0; i < n_new; i++) {
        const auto& c = new_children[i];
        CodeEntry ne;
        ne.entry_offset = (uint32_t)(new_struct_off + i * struct_sz);
        ne.length = (uint32_t)c.parent_new_length;
        ne.locals_count = c.locals_count;
        ne.args_count = c.args_count;
        ne.weird_local_flag = false;
        ne.bytecode_offset = (uint32_t)(c.parent_blob_off + c.body_offset + ptab_growth);
        ne.self_offset = (uint32_t)c.body_offset;
        ne.name = c.name;
        entries.push_back(std::move(ne));
    }
    return true;
}

// Commit drives the full save: splice/append CODE patches, grow SCPT for new globals, rebuild
// STRG/VARI/FUNC with pending entries, shift everything downstream, and stream the result out.
int Pools::commit(const char* out_path) {
    if (version.is_yyc && !pending_code.empty()) {
        Gmtoolkit::err("YYC file has no CODE chunk -- can't apply %zu code "
                       "patch(es). Run with audio/texture ops only.",
                       pending_code.size());
        return -1;
    }

    std::vector<CodeEntry> code_entries;
    if (!pending_code.empty()) {
        auto code_it0 = chunks.find("CODE");
        if (code_it0 == chunks.end()) {
            Gmtoolkit::err("pools: no CODE chunk");
            return -1;
        }
        if (!parse_code_entries(buf.data(), buf.size(), code_it0->second.payload_off, code_it0->second.size,
                                &code_entries, version.bytecode_version, version.using_gms2_3())) {
            Gmtoolkit::err("pools: CODE parse failed");
            return -1;
        }
    }
    size_t initial_code_count = code_entries.size();

    const std::string prefix = "gml_GlobalScript_";
    size_t next_new_code_idx = code_entries.size();
    for (const CodePatch& p : pending_code) {
        bool exists = false;
        for (const auto& e : code_entries) {
            if (e.name == p.entry_name) {
                exists = true;
                break;
            }
        }
        if (exists)
            continue;
        if (p.entry_name.size() <= prefix.size())
            continue;
        if (p.entry_name.compare(0, prefix.size(), prefix) != 0)
            continue;
        if (!append_one_new_glob_entry(*this, (uint32_t)next_new_code_idx)) {
            return -1;
        }
        next_new_code_idx++;
        for (size_t cidx = 0; cidx < p.children.size(); cidx++)
            next_new_code_idx++;
    }

    size_t pre_scpt_buf_size = 0;
    int64_t total_code_delta = 0;
    std::vector<size_t> orphan_idx;
    for (const CodePatch& p : pending_code) {
        int parent_idx = -1;
        for (size_t i = 0; i < code_entries.size(); i++) {
            if (code_entries[i].name == p.entry_name) {
                parent_idx = (int)i;
                break;
            }
        }
        if (parent_idx < 0)
            continue;
        const auto& parent = code_entries[parent_idx];
        size_t lo = parent.bytecode_offset;
        size_t hi = lo + parent.length;
        std::unordered_set<std::string> kept;
        kept.reserve(p.children.size());
        for (const auto& c : p.children)
            kept.insert(c.name);
        for (size_t i = 0; i < code_entries.size(); i++) {
            if ((int)i == parent_idx)
                continue;
            const auto& child = code_entries[i];
            if (child.bytecode_offset < lo || child.bytecode_offset >= hi)
                continue;
            if (kept.count(child.name))
                continue;
            orphan_idx.push_back(i);
        }
    }
    for (const CodePatch& p : pending_code) {
        int target_idx = -1;
        for (size_t i = 0; i < code_entries.size(); i++) {
            if (code_entries[i].name == p.entry_name) {
                target_idx = (int)i;
                break;
            }
        }
        size_t old_buf_size = buf.size();
        if (target_idx >= 0) {
            if (!apply_one_code_patch_inplace(*this, code_entries, target_idx, p)) {
                return -1;
            }
        } else {
            if (!append_one_new_code_entry(*this, code_entries, p.entry_name, p)) {
                return -1;
            }
        }
        total_code_delta += (int64_t)buf.size() - (int64_t)old_buf_size;
    }

    std::vector<NewChildSpec> new_children;
    for (const CodePatch& p : pending_code) {
        int parent_idx = -1;
        for (size_t i = 0; i < code_entries.size(); i++) {
            if (code_entries[i].name == p.entry_name) {
                parent_idx = (int)i;
                break;
            }
        }
        if (parent_idx < 0)
            continue;
        const auto& parent = code_entries[parent_idx];
        for (const auto& c : p.children) {
            bool found = false;
            for (const auto& e : code_entries) {
                if (e.name == c.name) {
                    found = true;
                    break;
                }
            }
            if (found)
                continue;
            NewChildSpec s;
            s.name = c.name;
            s.locals_count = c.locals_count;
            s.args_count = c.args_count;
            s.parent_blob_off = parent.bytecode_offset;
            s.parent_new_length = parent.length;
            s.body_offset = c.body_offset;
            new_children.push_back(std::move(s));
        }
    }
    if (!new_children.empty()) {
        size_t old_buf_size = buf.size();
        if (!append_extra_children_to_code(*this, code_entries, new_children)) {
            return -1;
        }
        total_code_delta += (int64_t)buf.size() - (int64_t)old_buf_size;
    }
    if (!orphan_idx.empty()) {
        auto code_it = chunks.find("CODE");
        if (code_it != chunks.end()) {
            size_t code_off = code_it->second.payload_off;
            uint32_t safe_blob_off = 0;
            for (const auto& e : code_entries) {
                if (e.bytecode_offset != 0) {
                    safe_blob_off = e.bytecode_offset;
                    break;
                }
            }
            for (size_t oi : orphan_idx) {
                if (oi >= code_entries.size())
                    continue;
                size_t struct_pos = code_entries[oi].entry_offset;
                if (struct_pos + 20 > buf.size())
                    continue;
                w_u32(buf.data() + struct_pos + 4, 0);
                int32_t new_rel = (int32_t)((int64_t)safe_blob_off - (int64_t)struct_pos - 12);
                w_u32(buf.data() + struct_pos + 12, (uint32_t)new_rel);
                code_entries[oi].length = 0;
                code_entries[oi].bytecode_offset = safe_blob_off;
                Gmtoolkit::warn("neutralised orphan child entry '%s' (length=0)", code_entries[oi].name.c_str());
            }
            (void)code_off;
        }
        auto scpt_it = chunks.find("SCPT");
        if (scpt_it != chunks.end()) {
            size_t pay = scpt_it->second.payload_off;
            size_t sz = scpt_it->second.size;
            if (pay + 4 <= buf.size()) {
                uint32_t scnt = r_u32(buf.data() + pay);
                if (4 + (size_t)scnt * 4 <= sz) {
                    std::unordered_set<std::string> orphan_names;
                    for (size_t oi : orphan_idx) {
                        if (oi < code_entries.size())
                            orphan_names.insert(code_entries[oi].name);
                    }
                    std::vector<uint32_t> keep_scpt;
                    keep_scpt.reserve(scnt);
                    for (uint32_t i = 0; i < scnt; i++) {
                        uint32_t ent_pos = r_u32(buf.data() + pay + 4 + (size_t)i * 4);
                        if (ent_pos == 0 || (size_t)ent_pos + 8 > buf.size()) {
                            keep_scpt.push_back(ent_pos);
                            continue;
                        }
                        uint32_t name_ptr = r_u32(buf.data() + ent_pos);
                        std::string nm;
                        if (name_ptr >= 4 && (size_t)name_ptr < buf.size()) {
                            uint32_t nl = r_u32(buf.data() + name_ptr - 4);
                            if ((size_t)name_ptr + nl <= buf.size()) {
                                nm.assign((const char*)(buf.data() + name_ptr), nl);
                            }
                        }
                        if (orphan_names.count(nm))
                            continue;
                        keep_scpt.push_back(ent_pos);
                    }
                    w_u32(buf.data() + pay, (uint32_t)keep_scpt.size());
                    for (size_t i = 0; i < keep_scpt.size(); i++) {
                        w_u32(buf.data() + pay + 4 + i * 4, keep_scpt[i]);
                    }
                    for (size_t i = keep_scpt.size(); i < scnt; i++) {
                        w_u32(buf.data() + pay + 4 + i * 4, 0);
                    }
                }
            }
        }
    }

    auto code_it = chunks.find("CODE");
    if (code_it != chunks.end() && !code_entries.empty()) {
        size_t code_off = code_it->second.payload_off;
        size_t code_size = code_it->second.size;
        size_t entry_size = version.using_gms2_3() ? 20 : 16;
        size_t end_of_entries = 0;
        for (const auto& e : code_entries) {
            size_t end = (size_t)e.entry_offset + entry_size;
            if (end > end_of_entries)
                end_of_entries = end;
        }
        size_t target_end = (end_of_entries + 15) & ~size_t(15);
        size_t target_size = target_end - code_off;
        int64_t delta = (int64_t)target_size - (int64_t)code_size;
        if (delta != 0) {
            size_t old_buf_size = buf.size();
            size_t code_end = code_off + code_size;
            if (delta > 0) {
                size_t pad_bytes = (size_t)delta;
                buf.resize(old_buf_size + pad_bytes);
                if (code_end < old_buf_size) {
                    std::memmove(buf.data() + code_end + pad_bytes, buf.data() + code_end, old_buf_size - code_end);
                }
                std::memset(buf.data() + code_end, 0, pad_bytes);
            } else {
                size_t shrink = (size_t)(-delta);
                if (code_end < old_buf_size) {
                    std::memmove(buf.data() + code_end - shrink, buf.data() + code_end, old_buf_size - code_end);
                }
                buf.resize(old_buf_size - shrink);
            }
            code_it->second.size = target_size;
            w_u32(buf.data() + (code_off - 4), (uint32_t)target_size);
            for (auto& kv : chunks) {
                if (kv.first != "CODE" && kv.second.payload_off > code_off) {
                    if (delta > 0)
                        kv.second.payload_off += (size_t)delta;
                    else
                        kv.second.payload_off -= (size_t)(-delta);
                }
            }
            uint32_t form_size = r_u32(buf.data() + 4);
            w_u32(buf.data() + 4, (uint32_t)((int64_t)form_size + delta));
            total_code_delta += delta;
        }
    }

    pre_scpt_buf_size = buf.size();
    if (!pending_scpt_inserts.empty()) {
        if (grow_scpt_in_place(*this, pending_scpt_inserts) != 0) {
            Gmtoolkit::err("pools: SCPT growth failed");
            return -1;
        }
        size_t d_scpt_local = buf.size() - pre_scpt_buf_size;
        for (auto& e : code_entries) {
            e.entry_offset += (uint32_t)d_scpt_local;
            e.bytecode_offset += (uint32_t)d_scpt_local;
        }
        for (auto& lst : var_occurrences) {
            for (auto& o : lst)
                o.operand_offset += (uint32_t)d_scpt_local;
        }
        for (auto& lst : func_occurrences) {
            for (auto& o : lst)
                o.operand_offset += (uint32_t)d_scpt_local;
        }
    }

    auto vari_it = chunks.find("VARI");
    auto func_it = chunks.find("FUNC");
    auto strg_it = chunks.find("STRG");
    if (strg_it == chunks.end()) {
        Gmtoolkit::err("pools: no STRG chunk");
        return -1;
    }

    size_t vari_off = vari_it != chunks.end() ? vari_it->second.payload_off : 0;
    size_t vari_size = vari_it != chunks.end() ? vari_it->second.size : 0;
    size_t func_off = func_it != chunks.end() ? func_it->second.payload_off : 0;
    size_t func_size = func_it != chunks.end() ? func_it->second.size : 0;
    size_t strg_off = strg_it->second.payload_off;
    size_t strg_size = strg_it->second.size;

    size_t d_strg_table = 4 * pending_strings.size();
    size_t d_strg_data = 0;
    for (const std::string& s : pending_strings)
        d_strg_data += strg_entry_bytes(s);

    const size_t vari_pre_sz = vari_preamble_size(version.bytecode_version);
    const size_t vari_entry_sz = vari_entry_size(version.bytecode_version);
    size_t vari_old_count_pre = vari_it != chunks.end() ? (vari_size - vari_pre_sz) / vari_entry_sz : 0;
    size_t vari_new_count_pre = vari_old_count_pre + pending_vars.size();
    size_t vari_content_new = vari_pre_sz + vari_entry_sz * vari_new_count_pre;
    size_t vari_pad_new = vari_it != chunks.end() ? (16 - (vari_off + vari_content_new) % 16) % 16 : 0;
    size_t new_vari_size = vari_it != chunks.end() ? (vari_content_new + vari_pad_new) : 0;
    int64_t signed_d_vari = (int64_t)new_vari_size - (int64_t)vari_size;
    size_t d_vari = (signed_d_vari >= 0) ? (size_t)signed_d_vari : 0;

    size_t func_content_old = 0;
    if (func_it != chunks.end() && func_size >= 4) {
        uint32_t fcount = r_u32(buf.data() + func_off);
        size_t p = func_off + 4 + (size_t)fcount * 12;
        if (p + 4 <= buf.size() && p + 4 <= func_off + func_size) {
            uint32_t lcount = r_u32(buf.data() + p);
            p += 4;
            bool ok = true;
            for (uint32_t i = 0; i < lcount; i++) {
                if (p + 8 > buf.size() || p + 8 > func_off + func_size) {
                    ok = false;
                    break;
                }
                uint32_t inner = r_u32(buf.data() + p);
                p += 8;
                p += (size_t)inner * 8;
                if (p > func_off + func_size) {
                    ok = false;
                    break;
                }
            }
            if (ok)
                func_content_old = p - func_off;
            else
                func_content_old = func_size;
        } else {
            func_content_old = func_size;
        }
    }

    bool modern_codelocals = version.using_gms2_3();
    const std::string args_name_local = "arguments";
    if (has_code_locals && func_it != chunks.end()) {
        intern_string(args_name_local);

        struct PatchEntry {
            const Pools::CodePatch* patch;
            const std::vector<std::string>* names;
        };
        std::unordered_map<std::string, PatchEntry> by_name;
        by_name.reserve(pending_code.size() * 2);
        for (const auto& p : pending_code) {
            by_name.emplace(p.entry_name, PatchEntry{ &p, &p.local_names });
            for (const auto& c : p.children) {
                by_name.emplace(c.name, PatchEntry{ &p, &c.local_names });
            }
        }
        auto rebuild_vars = [&](CodeLocal& cl, const std::vector<std::string>& names) {
            cl.vars.clear();
            cl.vars.push_back({ args_name_local, modern_codelocals ? (uint32_t)intern_string(args_name_local) : 0u });
            for (size_t i = 0; i < names.size(); i++) {
                cl.vars.push_back(
                    { names[i], modern_codelocals ? (uint32_t)intern_string(names[i]) : (uint32_t)(i + 1) });
            }
        };
        for (auto& cl : code_locals) {
            auto it2 = by_name.find(cl.name);
            if (it2 == by_name.end())
                continue;
            rebuild_vars(cl, *it2->second.names);
        }

        std::unordered_set<std::string> existing_names;
        existing_names.reserve(code_locals.size() * 2);
        for (const auto& cl : code_locals)
            existing_names.insert(cl.name);
        for (const auto& p : pending_code) {
            if (!existing_names.count(p.entry_name)) {
                CodeLocal cl;
                cl.name = p.entry_name;
                rebuild_vars(cl, p.local_names);
                existing_names.insert(p.entry_name);
                code_locals.push_back(std::move(cl));
            }
            for (const auto& c : p.children) {
                if (existing_names.count(c.name))
                    continue;
                CodeLocal cl;
                cl.name = c.name;
                rebuild_vars(cl, c.local_names);
                existing_names.insert(c.name);
                code_locals.push_back(std::move(cl));
            }
        }
    }

    size_t func_content_new = 0;
    if (func_it != chunks.end()) {
        if (has_code_locals) {
            size_t list1_size = 4 + (func_entries.size() + pending_funcs.size()) * 12;
            size_t cl_size = 4;
            for (const auto& cl : code_locals)
                cl_size += 8 + (size_t)cl.vars.size() * 8;
            func_content_new = list1_size + cl_size;
        } else {
            func_content_new = func_content_old + 12 * pending_funcs.size();
        }
    }
    size_t func_pad_new = func_it != chunks.end() ? (16 - (func_off + func_content_new) % 16) % 16 : 0;
    size_t new_func_size = func_it != chunks.end() ? (func_content_new + func_pad_new) : 0;
    int64_t d_func_signed = (int64_t)new_func_size - (int64_t)func_size;
    size_t d_func = (d_func_signed >= 0) ? (size_t)d_func_signed : 0;

    size_t buf_ptab_end = strg_off + 4 + 4 * strg_ptr_table.size();
    // Span by max(string_end) over the ptr table. Walking sequentially with
    // 4-byte alignment breaks GMS1, which packs entries back-to-back. The
    // table stores ORIGINAL file positions, so shift by the in-buffer move
    // applied so far (CODE + SCPT growth) to find the current location.
    size_t d_scpt_so_far = pre_scpt_buf_size > 0 ? (buf.size() - pre_scpt_buf_size) : 0;
    size_t in_buf_shift = (size_t)total_code_delta + d_scpt_so_far;
    size_t strg_chunk_end = strg_off + strg_size;
    size_t max_end = buf_ptab_end;
    for (size_t i = 0; i < strg_ptr_table.size(); i++) {
        uint32_t ptr_orig = strg_ptr_table[i];
        if (ptr_orig < 4)
            continue;
        size_t ptr = (size_t)ptr_orig + in_buf_shift;
        if (ptr < buf_ptab_end || ptr > strg_chunk_end)
            continue;
        uint32_t nl = r_u32(buf.data() + ptr - 4);
        size_t end = ptr + nl + 1;
        if (end > strg_chunk_end)
            continue;
        if (end > max_end)
            max_end = end;
    }
    size_t existing_data_bytes_pre = max_end - buf_ptab_end;
    size_t new_strg_payload_unpadded =
        4 + 4 * (strg_ptr_table.size() + pending_strings.size()) + existing_data_bytes_pre + d_strg_data;
    size_t out_strg_payload_start = strg_off + d_vari + d_func;

    while ((out_strg_payload_start + new_strg_payload_unpadded) % 0x80 != 0) {
        new_strg_payload_unpadded++;
    }
    size_t new_strg_payload_size_total = new_strg_payload_unpadded;
    int64_t d_strg_total = (int64_t)new_strg_payload_size_total - (int64_t)strg_size;

    if (vari_it == chunks.end() && !pending_vars.empty()) {
        Gmtoolkit::err("pools: cannot add variables without VARI chunk");
        return -1;
    }
    if (func_it == chunks.end() && !pending_funcs.empty()) {
        Gmtoolkit::err("pools: cannot add functions without FUNC chunk");
        return -1;
    }

    size_t d_scpt = pre_scpt_buf_size > 0 ? (buf.size() - pre_scpt_buf_size) : 0;

    int64_t signed_shift = total_code_delta + (int64_t)d_scpt + (int64_t)(d_vari + d_func) + (int64_t)d_strg_table;
    int64_t signed_audo_val = total_code_delta + (int64_t)d_scpt + (int64_t)(d_vari + d_func) + d_strg_total;
    int64_t signed_audo_pos = (int64_t)(d_vari + d_func) + d_strg_total;
    size_t strg_ptr_shift = (size_t)signed_shift;
    size_t audo_val_shift = (size_t)signed_audo_val;
    size_t audo_pos_shift = (size_t)signed_audo_pos;

    std::unordered_set<uint32_t> strg_ptr_set;
    strg_ptr_set.reserve(strg_ptr_table.size() + pending_strings.size());
    for (uint32_t p : strg_ptr_table) {
        if (p != 0)
            strg_ptr_set.insert(p);
    }

    size_t out_total = (size_t)((int64_t)buf.size() + (int64_t)(d_vari + d_func) + d_strg_total);

#if defined(__aarch64__) && !defined(__APPLE__)
    std::string out_tmp_path = std::string(out_path) + ".tmp";
    MappedFile out;
    if (mapped_file_create_rw(out_tmp_path.c_str(), out_total, &out) != 0) {
        perror(out_tmp_path.c_str());
        return -1;
    }
#else
    std::vector<uint8_t> out_vec(out_total, 0);
    struct {
        uint8_t* data;
        size_t size;
    } out{ out_vec.data(), out_vec.size() };
#endif

    size_t first_grow = SIZE_MAX;
    if (vari_it != chunks.end())
        first_grow = std::min(first_grow, vari_off + vari_size);
    if (func_it != chunks.end())
        first_grow = std::min(first_grow, func_off + func_size);
    first_grow = std::min(first_grow, strg_off);

    size_t old_cursor = 0;
    size_t new_cursor = 0;

    auto copy_old = [&](size_t old_end) {
        if (old_end <= old_cursor)
            return;
        size_t n = old_end - old_cursor;
        memcpy(out.data + new_cursor, buf.data() + old_cursor, n);
        new_cursor += n;
        old_cursor = old_end;
    };

    size_t vari_old_count = vari_old_count_pre;
    size_t vari_entries_end_old = vari_off;
    uint32_t vc1 = (vari_it != chunks.end()) ? r_u32(buf.data() + vari_off) : 0;
    uint32_t vc2 = (vari_it != chunks.end()) ? r_u32(buf.data() + vari_off + 4) : 0;
    uint32_t mlvc = (vari_it != chunks.end() && vari_pre_sz >= 12) ? r_u32(buf.data() + vari_off + 8) : 0;
    for (const auto& cp : pending_code) {
        if (cp.locals_count > mlvc)
            mlvc = cp.locals_count;
        for (const auto& ch : cp.children)
            if (ch.locals_count > mlvc)
                mlvc = ch.locals_count;
    }
    if (vari_it != chunks.end()) {
        vari_entries_end_old = vari_off + vari_pre_sz + vari_entry_sz * vari_old_count;
        copy_old(vari_entries_end_old);
        for (auto& pv : pending_vars) {
            uint32_t new_name_ptr = 0xCAFEBABEu;
            int32_t name_strg_idx = find_string(pv.first);
            uint32_t var_id;
            if (pv.second == -6) {
                var_id = 0;
            } else if (name_strg_idx >= 0) {
                var_id = (uint32_t)name_strg_idx;
                vc1++;
                vc2 = vc1;
            } else {
                var_id = (uint32_t)-1;
            }
            if (vari_entry_sz == 20) {
                uint8_t entry[20] = { 0 };
                w_u32(entry + 0, new_name_ptr);
                w_u32(entry + 4, (uint32_t)pv.second);
                w_u32(entry + 8, var_id);
                w_u32(entry + 12, 0);
                w_u32(entry + 16, 0xFFFFFFFFu);
                memcpy(out.data + new_cursor, entry, 20);
            } else {
                uint8_t entry[12] = { 0 };
                w_u32(entry + 0, new_name_ptr);
                w_u32(entry + 4, 0);
                w_u32(entry + 8, 0xFFFFFFFFu);
                memcpy(out.data + new_cursor, entry, 12);
            }
            new_cursor += vari_entry_sz;
        }
        while (new_cursor % 16 != 0)
            new_cursor++;
        old_cursor = vari_off + vari_size;
    }

    size_t code_locals_start_new = 0;
    size_t func_first_list_end_old = func_off;
    if (func_it != chunks.end()) {
        if (func_size >= 4) {
            uint32_t old_fcount = r_u32(buf.data() + func_off);
            func_first_list_end_old = func_off + 4 + (size_t)old_fcount * 12;
        }

        copy_old(func_first_list_end_old);

        for (auto& fn : pending_funcs) {
            (void)fn;
            uint32_t new_name_ptr = 0xCAFEBABEu;
            uint8_t entry[12] = { 0 };
            w_u32(entry + 0, new_name_ptr);
            w_u32(entry + 4, 0);
            w_u32(entry + 8, 0xFFFFFFFFu);
            memcpy(out.data + new_cursor, entry, 12);
            new_cursor += 12;
        }

        size_t func_list2_end_old = func_off + func_content_old;
        if (!has_code_locals) {
            copy_old(func_list2_end_old);
        } else {
            code_locals_start_new = new_cursor;
            w_u32(out.data + new_cursor, (uint32_t)code_locals.size());
            new_cursor += 4;
            for (const auto& cl : code_locals) {
                w_u32(out.data + new_cursor + 0, (uint32_t)cl.vars.size());
                w_u32(out.data + new_cursor + 4, 0xCAFEBABEu);
                new_cursor += 8;
                for (const auto& v : cl.vars) {
                    w_u32(out.data + new_cursor + 0, v.index);
                    w_u32(out.data + new_cursor + 4, 0xCAFEBABEu);
                    new_cursor += 8;
                }
            }
            old_cursor = func_list2_end_old;
        }

        while (new_cursor % 16 != 0)
            new_cursor++;
        old_cursor = func_off + func_size;
    }

    copy_old(strg_off - 8);
    size_t strg_hdr_pos_new = new_cursor;
    memcpy(out.data + new_cursor, "STRG", 4);
    new_cursor += 8;

    size_t strg_body_start_new = new_cursor;
    uint32_t new_count = (uint32_t)(strg_ptr_table.size() + pending_strings.size());
    w_u32(out.data + new_cursor, new_count);
    new_cursor += 4;

    for (size_t i = 0; i < strg_ptr_table.size(); i++) {
        uint32_t new_data_pos = strg_ptr_table[i] + (uint32_t)strg_ptr_shift;
        w_u32(out.data + new_cursor, new_data_pos - 4);
        new_cursor += 4;
    }
    size_t pending_ptr_table_start = new_cursor;
    for (size_t i = 0; i < pending_strings.size(); i++) {
        w_u32(out.data + new_cursor, 0);
        new_cursor += 4;
    }

    size_t old_strg_data_start = strg_off + 4 + 4 * strg_ptr_table.size();
    memcpy(out.data + new_cursor, buf.data() + old_strg_data_start, existing_data_bytes_pre);
    new_cursor += existing_data_bytes_pre;

    std::vector<uint32_t> new_strg_data_ptrs;
    new_strg_data_ptrs.reserve(pending_strings.size());
    for (const std::string& s : pending_strings) {
        size_t entry_start = new_cursor;
        w_u32(out.data + new_cursor, (uint32_t)s.size());
        new_cursor += 4;
        size_t data_ptr_new = new_cursor;
        new_strg_data_ptrs.push_back((uint32_t)data_ptr_new);
        memcpy(out.data + new_cursor, s.data(), s.size());
        new_cursor += s.size();
        out.data[new_cursor++] = 0;
        while ((new_cursor - entry_start) & 3)
            new_cursor++;
    }

    for (size_t i = 0; i < pending_strings.size(); i++) {
        w_u32(out.data + pending_ptr_table_start + i * 4, new_strg_data_ptrs[i] - 4);
    }

    while (new_cursor % 0x80 != 0) {
        out.data[new_cursor++] = 0;
    }
    while ((new_cursor - strg_body_start_new) < new_strg_payload_size_total) {
        out.data[new_cursor++] = 0;
    }

    size_t new_strg_payload_size = new_cursor - strg_body_start_new;
    w_u32(out.data + strg_hdr_pos_new + 4, (uint32_t)new_strg_payload_size);

    old_cursor = strg_off + strg_size;

    copy_old(buf.size());

    w_u32(out.data + 4, (uint32_t)(out.size - 8));

    if (vari_it != chunks.end()) {
        w_u32(out.data + (vari_off - 4), (uint32_t)new_vari_size);
        if (vari_pre_sz >= 12) {
            w_u32(out.data + vari_off + 0, vc1);
            w_u32(out.data + vari_off + 4, vc2);
            w_u32(out.data + vari_off + 8, mlvc);
        }
    }
    if (func_it != chunks.end()) {
        size_t func_hdr_new = (func_off - 8) + d_vari;
        w_u32(out.data + func_hdr_new + 4, (uint32_t)new_func_size);
        size_t func_payload_new = func_off + d_vari;
        uint32_t old_fcount = r_u32(out.data + func_payload_new);
        w_u32(out.data + func_payload_new, old_fcount + (uint32_t)pending_funcs.size());
    }

    size_t scan_pos = 8;
    while (scan_pos + 8 <= out.size) {
        std::string tag((const char*)(out.data + scan_pos), 4);
        uint32_t csz = r_u32(out.data + scan_pos + 4);
        size_t pay_start = scan_pos + 8;
        size_t pay_end = pay_start + csz;
        if (pay_end > out.size)
            break;

        if (chunk_is_binary_skip(tag)) {

        } else if (tag == "SHDR") {
            if (csz >= 4) {
                uint32_t shdr_count = r_u32(out.data + pay_start);
                size_t ptab_end = pay_start + 4 + (size_t)shdr_count * 4;
                for (size_t q = pay_start; q + 4 <= ptab_end && q + 4 <= pay_end; q += 4) {
                    uint32_t v = r_u32(out.data + q);
                    if (v != 0 && strg_ptr_set.count(v))
                        w_u32(out.data + q, v + (uint32_t)strg_ptr_shift);
                }
                for (uint32_t s = 0; s < shdr_count; s++) {
                    size_t ent_off = r_u32(out.data + pay_start + 4 + (size_t)s * 4);
                    if (ent_off == 0 || ent_off >= pay_end)
                        continue;

                    size_t end_pos = pay_end;
                    for (uint32_t t = s + 1; t < shdr_count; t++) {
                        uint32_t nxt = r_u32(out.data + pay_start + 4 + (size_t)t * 4);
                        if (nxt != 0 && nxt <= pay_end) {
                            end_pos = nxt;
                            break;
                        }
                    }
                    if (end_pos > pay_end || end_pos <= ent_off)
                        continue;
                    for (size_t q = ent_off; q + 4 <= end_pos; q += 4) {
                        uint32_t v = r_u32(out.data + q);
                        if (v != 0 && strg_ptr_set.count(v))
                            w_u32(out.data + q, v + (uint32_t)strg_ptr_shift);
                    }
                }
            }
        } else {
            for (size_t q = pay_start; q + 4 <= pay_end; q += 4) {
                uint32_t v = r_u32(out.data + q);
                if (v != 0 && strg_ptr_set.count(v)) {
                    w_u32(out.data + q, v + (uint32_t)strg_ptr_shift);
                }
            }
        }
        scan_pos = pay_end;
    }

    auto room_it = chunks.find("ROOM");
    if (room_it != chunks.end() && strg_ptr_shift) {
        size_t rpay = room_it->second.payload_off;
        size_t rsz = room_it->second.size;
        bool is_2022_1 = version.is_at_least(2022, 1);

        auto bump_at = [&](size_t pos) {
            if (pos + 4 > out.size)
                return;
            uint32_t v = r_u32(out.data + pos);
            if (v != 0 && strg_ptr_set.count(v))
                w_u32(out.data + pos, v + (uint32_t)strg_ptr_shift);
        };

        auto walk_strg_list = [&](size_t list_off, size_t name_off, int name2_off) {
            if (list_off + 4 > rpay + rsz)
                return;
            uint32_t cnt = r_u32(out.data + list_off);
            if (cnt > 100000 || list_off + 4 + (size_t)cnt * 4 > rpay + rsz)
                return;
            for (uint32_t i = 0; i < cnt; i++) {
                uint32_t ent = r_u32(out.data + list_off + 4 + (size_t)i * 4);
                if (ent == 0)
                    continue;
                bump_at((size_t)ent + name_off);
                if (name2_off >= 0)
                    bump_at((size_t)ent + (size_t)name2_off);
            }
        };

        if (rsz < 4)
            goto room_strg_done;
        {
            uint32_t rcount = r_u32(out.data + rpay);
            if (rcount >= 10000 || 4 + (size_t)rcount * 4 > rsz)
                goto room_strg_done;
            for (uint32_t i = 0; i < rcount; i++) {
                uint32_t rptr = r_u32(out.data + rpay + 4 + (size_t)i * 4);
                if (rptr == 0)
                    continue;

                bump_at((size_t)rptr + 0x00);
                bump_at((size_t)rptr + 0x04);

                bool is_2024_13 = version.is_at_least(2024, 13);
                uint32_t layers_ptr = 0;
                size_t cands[2] = { is_2024_13 ? 0x5Cu : 0x58u, is_2024_13 ? 0x58u : 0x5Cu };
                for (size_t cand : cands) {
                    if ((size_t)rptr + cand + 4 > out.size)
                        continue;
                    uint32_t lp = r_u32(out.data + rptr + cand);
                    if (lp < rpay || lp + 4 > rpay + rsz)
                        continue;
                    uint32_t lc = r_u32(out.data + lp);
                    if (lc == 0) {
                        if (cand == cands[0]) {
                            layers_ptr = lp;
                            break;
                        }
                        continue;
                    }
                    if (lc < 100 && lp + 4 + lc * 4 <= rpay + rsz) {
                        uint32_t first_layer = r_u32(out.data + lp + 4);
                        if (first_layer >= rpay && first_layer + 36 <= rpay + rsz) {
                            uint32_t first_name = r_u32(out.data + first_layer);
                            if (first_name > 0x1000) {
                                layers_ptr = lp;
                                break;
                            }
                        }
                    }
                }
                if (layers_ptr == 0)
                    continue;

                uint32_t lcount = r_u32(out.data + layers_ptr);
                if (lcount > 100)
                    continue;
                for (uint32_t j = 0; j < lcount; j++) {
                    uint32_t lp = r_u32(out.data + layers_ptr + 4 + (size_t)j * 4);
                    if (lp == 0 || (size_t)lp + 36 > rpay + rsz)
                        continue;

                    bump_at((size_t)lp + 0x00);
                    uint32_t ltype = r_u32(out.data + lp + 0x08);
                    size_t after_hdr = (size_t)lp + 36;
                    size_t after_2022_1 = after_hdr;
                    if (is_2022_1) {
                        if (after_hdr + 12 > rpay + rsz)
                            continue;
                        bump_at(after_hdr + 4);
                        uint32_t epc = r_u32(out.data + after_hdr + 8);
                        if (epc > 100000)
                            continue;
                        if (after_hdr + 12 + (size_t)epc * 12 > rpay + rsz)
                            continue;
                        for (uint32_t k = 0; k < epc; k++) {
                            size_t ep = after_hdr + 12 + (size_t)k * 12;
                            bump_at(ep + 4);
                            bump_at(ep + 8);
                        }
                        after_2022_1 = after_hdr + 12 + (size_t)epc * 12;
                    }

                    if (ltype == 3) {
                        size_t ptrs_start = after_2022_1;
                        if (ptrs_start + 8 > rpay + rsz)
                            continue;
                        uint32_t legacy_ptr = r_u32(out.data + ptrs_start);
                        int sub_count = 2;
                        int64_t gap = (int64_t)legacy_ptr - (int64_t)ptrs_start;
                        if (gap > 0 && (gap % 4) == 0) {
                            int n = (int)(gap / 4);
                            if (n >= 2 && n <= 6)
                                sub_count = n;
                        }
                        for (int sk = 0; sk < sub_count; sk++) {
                            uint32_t sp = r_u32(out.data + ptrs_start + (size_t)sk * 4);
                            if (sp == 0)
                                continue;
                            if (sp < (uint32_t)rpay || sp >= (uint32_t)(rpay + rsz))
                                continue;
                            switch (sk) {
                                case 0:
                                    break;
                                case 1:
                                    walk_strg_list((size_t)sp, 0x00, -1);
                                    break;
                                case 2:
                                    walk_strg_list((size_t)sp, 0x00, -1);
                                    break;
                                case 3:
                                    if (sub_count == 6)
                                        walk_strg_list((size_t)sp, 0x00, -1);
                                    else
                                        walk_strg_list((size_t)sp, 0x00, 0x28);
                                    break;
                                case 4:
                                    walk_strg_list((size_t)sp, 0x00, -1);
                                    break;
                                case 5:
                                    walk_strg_list((size_t)sp, 0x00, 0x28);
                                    break;
                            }
                        }
                    } else if (ltype == 6 && !is_2022_1) {
                        if (after_2022_1 + 8 > rpay + rsz)
                            continue;
                        bump_at(after_2022_1 + 0);
                        uint32_t pc = r_u32(out.data + after_2022_1 + 4);
                        if (pc > 100000 || after_2022_1 + 8 + (size_t)pc * 12 > rpay + rsz)
                            continue;
                        for (uint32_t k = 0; k < pc; k++) {
                            size_t ep = after_2022_1 + 8 + (size_t)k * 12;
                            bump_at(ep + 4);
                            bump_at(ep + 8);
                        }
                    }
                }
            }
        }
    room_strg_done:;
    }

    if (audo_val_shift) {
        auto audo_it = chunks.find("AUDO");
        if (audo_it != chunks.end()) {
            size_t audo_new_pay = audo_it->second.payload_off + audo_pos_shift;
            if (audo_new_pay + 4 <= out.size) {
                uint32_t acount = r_u32(out.data + audo_new_pay);
                for (uint32_t i = 0; i < acount; i++) {
                    size_t pp = audo_new_pay + 4 + i * 4;
                    if (pp + 4 > out.size)
                        break;
                    uint32_t v = r_u32(out.data + pp);
                    if (v == 0)
                        continue;
                    w_u32(out.data + pp, v + (uint32_t)audo_val_shift);
                }
            }
        }
    }

    if (audo_val_shift) {
        auto txtr_it = chunks.find("TXTR");
        if (txtr_it != chunks.end()) {
            size_t txtr_new_pay = txtr_it->second.payload_off + audo_pos_shift;
            if (txtr_new_pay + 4 <= out.size) {
                uint32_t tcount = r_u32(out.data + txtr_new_pay);
                if (tcount > 0 && txtr_new_pay + 4 + (size_t)tcount * 4 <= out.size) {
                    size_t txtr_pay_size = txtr_it->second.size;
                    size_t entry_size = Gmtoolkit::detect_txtr_entry_size(out.data + txtr_new_pay, txtr_pay_size);
                    for (uint32_t i = 0; i < tcount; i++) {
                        size_t pp = txtr_new_pay + 4 + (size_t)i * 4;
                        uint32_t old_entry_ptr = r_u32(out.data + pp);
                        if (old_entry_ptr == 0)
                            continue;
                        uint32_t new_entry_ptr = old_entry_ptr + (uint32_t)audo_val_shift;
                        w_u32(out.data + pp, new_entry_ptr);
                        size_t blob_field = (size_t)new_entry_ptr + entry_size - 4;
                        if (blob_field + 4 > out.size)
                            continue;
                        uint32_t old_blob = r_u32(out.data + blob_field);
                        if (old_blob == 0)
                            continue;
                        w_u32(out.data + blob_field, old_blob + (uint32_t)audo_val_shift);
                    }
                }
            }
        }
    }

    auto resolve_name_ptr = [&](const std::string& nm) -> uint32_t {
        int32_t idx = find_string(nm);
        if (idx < 0)
            return 0;
        if ((size_t)idx < strg_ptr_table.size()) {
            return strg_ptr_table[(size_t)idx] + (uint32_t)strg_ptr_shift;
        }
        size_t pi = (size_t)idx - strg_ptr_table.size();
        if (pi < new_strg_data_ptrs.size())
            return new_strg_data_ptrs[pi];
        return 0;
    };
    if (!pending_vars.empty() && vari_it != chunks.end()) {
        size_t base = vari_entries_end_old;
        for (size_t i = 0; i < pending_vars.size(); i++) {
            size_t ent_off = base + i * vari_entry_sz;
            if (r_u32(out.data + ent_off) == 0xCAFEBABEu) {
                w_u32(out.data + ent_off, resolve_name_ptr(pending_vars[i].first));
            }
        }
    }
    if (!pending_funcs.empty() && func_it != chunks.end()) {
        size_t base = func_first_list_end_old + d_vari;
        for (size_t i = 0; i < pending_funcs.size(); i++) {
            size_t ent_off = base + i * 12;
            if (r_u32(out.data + ent_off) == 0xCAFEBABEu) {
                w_u32(out.data + ent_off, resolve_name_ptr(pending_funcs[i]));
            }
        }
    }

    if (has_code_locals && code_locals_start_new != 0 && code_locals_start_new + 4 <= out.size) {
        size_t p = code_locals_start_new;
        uint32_t lcount = r_u32(out.data + p);
        p += 4;
        for (uint32_t i = 0; i < lcount && i < code_locals.size(); i++) {
            if (p + 8 > out.size)
                break;
            uint32_t vcount = r_u32(out.data + p);
            const CodeLocal& cl = code_locals[i];
            w_u32(out.data + p + 4, resolve_name_ptr(cl.name));
            p += 8;
            for (uint32_t j = 0; j < vcount && j < cl.vars.size(); j++) {
                if (p + 8 > out.size)
                    break;
                w_u32(out.data + p + 4, resolve_name_ptr(cl.vars[j].name));
                p += 8;
            }
        }
    }

    for (size_t i = initial_code_count; i < code_entries.size(); i++) {
        size_t ent_off = code_entries[i].entry_offset;
        if (ent_off + 4 > out.size)
            continue;
        if (r_u32(out.data + ent_off) != 0xCAFEBABEu)
            continue;
        w_u32(out.data + ent_off, resolve_name_ptr(code_entries[i].name));
    }

    if (!pending_scpt_inserts.empty()) {
        auto scpt_it = chunks.find("SCPT");
        if (scpt_it != chunks.end()) {
            // Map script name -> final CODE chunk index. Constructor scripts
            // encode the high bit on the code_id field per UTMT's convention.
            std::unordered_map<std::string, uint32_t> name_to_code_idx;
            name_to_code_idx.reserve(code_entries.size());
            for (size_t i = 0; i < code_entries.size(); i++) {
                name_to_code_idx.emplace(code_entries[i].name, (uint32_t)i);
            }
            size_t pay = scpt_it->second.payload_off;
            if (pay + 4 <= out.size) {
                uint32_t scount = r_u32(out.data + pay);
                if (4 + (size_t)scount * 4 <= scpt_it->second.size) {
                    size_t insert_idx = 0;
                    for (uint32_t i = 0; i < scount; i++) {
                        uint32_t ent = r_u32(out.data + pay + 4 + (size_t)i * 4);
                        if (ent + 8 > out.size)
                            continue;
                        if (r_u32(out.data + ent) != 0xCAFEBABEu)
                            continue;
                        if (insert_idx >= pending_scpt_inserts.size())
                            break;
                        const auto& ins = pending_scpt_inserts[insert_idx];
                        w_u32(out.data + ent, resolve_name_ptr(ins.name));
                        auto cit = name_to_code_idx.find(ins.name);
                        uint32_t code_id = (cit != name_to_code_idx.end()) ? cit->second : 0u;
                        if (ins.is_constructor)
                            code_id |= 0x80000000u;
                        w_u32(out.data + ent + 4, code_id);
                        insert_idx++;
                    }
                }
            }
        }
    }

    std::unordered_map<std::string, uint32_t> patch_blob_off;
    patch_blob_off.reserve(pending_code.size());
    for (const CodePatch& p : pending_code) {
        for (const CodeEntry& e : code_entries) {
            if (e.name == p.entry_name) {
                patch_blob_off[p.entry_name] = e.bytecode_offset;
                break;
            }
        }
    }

    var_occurrences.resize(vari_entries.size() + pending_vars.size());
    func_occurrences.resize(func_entries.size() + pending_funcs.size());
    for (size_t k = 0; k < pending_vars.size(); k++) {
        const auto& pv = pending_vars[k];
        size_t idx = vari_entries.size() + k;
        for (const CodePatch& p : pending_code) {
            auto it = patch_blob_off.find(p.entry_name);
            if (it == patch_blob_off.end())
                continue;
            uint32_t blob_off = it->second;
            for (auto& vr : p.var_refs) {
                if (vr.name == pv.first && vr.inst_type == pv.second) {
                    Occurrence o;
                    o.operand_offset = (uint32_t)(blob_off + vr.byte_offset);
                    o.var_type = vr.var_type;
                    var_occurrences[idx].push_back(o);
                }
            }
        }
    }
    for (size_t k = 0; k < pending_funcs.size(); k++) {
        const std::string& nm = pending_funcs[k];
        size_t idx = func_entries.size() + k;
        for (const CodePatch& p : pending_code) {
            auto it = patch_blob_off.find(p.entry_name);
            if (it == patch_blob_off.end())
                continue;
            uint32_t blob_off = it->second;
            for (auto& fr : p.func_refs) {
                if (fr.name == nm) {
                    Occurrence o;
                    o.operand_offset = (uint32_t)(blob_off + fr.byte_offset);
                    o.var_type = 0;
                    func_occurrences[idx].push_back(o);
                }
            }
        }
    }

    auto write_chain = [&](size_t struct_pos_in_out, const std::vector<Occurrence>& occs, int32_t name_string_id,
                           bool is_func) {
        std::vector<Occurrence> sorted = occs;
        std::sort(sorted.begin(), sorted.end(),
                  [](const Occurrence& a, const Occurrence& b) { return a.operand_offset < b.operand_offset; });
        uint32_t count = (uint32_t)sorted.size();
        uint32_t first_addr_disk;
        if (count == 0) {
            first_addr_disk = 0xFFFFFFFFu;
        } else if (is_func) {
            first_addr_disk = sorted.front().operand_offset;
        } else {
            first_addr_disk = sorted.front().operand_offset - 4;
        }

        size_t occ_field = struct_pos_in_out + (is_func ? 4 : 12);
        size_t first_field = struct_pos_in_out + (is_func ? 8 : 16);
        w_u32(out.data + occ_field, count);
        w_u32(out.data + first_field, first_addr_disk);
        for (size_t i = 0; i < sorted.size(); i++) {
            uint32_t val;
            uint8_t var_type = sorted[i].var_type;
            if (i + 1 < sorted.size()) {
                uint32_t delta_bytes = sorted[i + 1].operand_offset - sorted[i].operand_offset;
                val = (((uint32_t)var_type & 0xF8u) << 24) | (delta_bytes & 0x07FFFFFFu);
            } else {
                val = (((uint32_t)var_type & 0xF8u) << 24) | ((uint32_t)name_string_id & 0xFFFFFFu);
            }
            w_u32(out.data + sorted[i].operand_offset, val);
        }
    };

    size_t vari_off_out = vari_it != chunks.end() ? vari_it->second.payload_off : 0;
    for (size_t i = 0; i < vari_entries.size(); i++) {
        size_t struct_pos = vari_off_out + vari_pre_sz + i * vari_entry_sz;
        int32_t name_id = find_string(vari_entries[i].name);
        write_chain(struct_pos, var_occurrences[i], name_id, false);
    }
    for (size_t k = 0; k < pending_vars.size(); k++) {
        size_t struct_pos = vari_off_out + vari_pre_sz + vari_entry_sz * vari_entries.size() + vari_entry_sz * k;
        int32_t name_id = find_string(pending_vars[k].first);
        write_chain(struct_pos, var_occurrences[vari_entries.size() + k], name_id, false);
    }

    size_t func_off_out = func_it != chunks.end() ? func_it->second.payload_off + d_vari : 0;
    for (size_t i = 0; i < func_entries.size(); i++) {
        size_t struct_pos = func_off_out + 4 + i * 12;
        int32_t name_id = find_string(func_entries[i].name);
        write_chain(struct_pos, func_occurrences[i], name_id, true);
    }
    for (size_t k = 0; k < pending_funcs.size(); k++) {
        size_t struct_pos = func_off_out + 4 + 12 * func_entries.size() + 12 * k;
        int32_t name_id = find_string(pending_funcs[k]);
        write_chain(struct_pos, func_occurrences[func_entries.size() + k], name_id, true);
    }

#if defined(__aarch64__) && !defined(__APPLE__)
    mapped_file_close(&out);
    if (portable_rename(out_tmp_path.c_str(), out_path) != 0) {
        perror(out_path);
        std::remove(out_tmp_path.c_str());
        return -1;
    }
#else
    FILE* f = fopen(out_path, "wb");
    if (!f) {
        perror(out_path);
        return -1;
    }
    if (fwrite(out.data, 1, out.size, f) != out.size) {
        perror(out_path);
        fclose(f);
        return -1;
    }
    fclose(f);
#endif
    return 0;
}

} // namespace GMSLib::SaveBackend
