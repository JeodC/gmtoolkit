// SPDX-License-Identifier: MIT

#include "GMSLib/SaveBackend/Pools.h"

#include "GMSLib/GMSData.h"
#include "GMSLib/Models/GMSCode.h"
#include "GMSLib/Models/GMSCodeLocals.h"
#include "GMSLib/Models/GMSFunction.h"
#include "GMSLib/Models/GMSString.h"
#include "GMSLib/Models/GMSVariable.h"
#include "GMSLib/SaveBackend/CodeChunk.h"
#include "GMSLib/SaveBackend/ScptGrow.h"
#include "Toolkit/IO.h"
#include "Toolkit/Log.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_set>

namespace GMSLib::SaveBackend {

namespace {

using Gmtoolkit::r_u16;
using Gmtoolkit::r_u32;
using Gmtoolkit::w_u32;

inline int32_t read_i32(const uint8_t* p) {
    return (int32_t)r_u32(p);
}

// FNV-1a folded with the instance type so {name, inst_type} pairs hash uniquely.
uint64_t hash_var_key(std::string_view s, int32_t inst) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (char c : s) {
        h ^= (uint8_t)c;
        h *= 0x100000001b3ULL;
    }
    h ^= (uint32_t)inst * 0x9E3779B185EBCA87ULL;
    return h;
}

std::string read_strg_at(const uint8_t* buf, size_t buf_size, uint32_t data_ptr) {
    if (data_ptr < 4 || (size_t)data_ptr >= buf_size)
        return {};
    uint32_t len = r_u32(buf + data_ptr - 4);
    if ((size_t)data_ptr + len > buf_size)
        return {};
    return std::string((const char*)(buf + data_ptr), len);
}

} // namespace

int Pools::open(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) {
        fclose(f);
        return -1;
    }
    buf.resize((size_t)sz, 0);
    if (fread(buf.data(), 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        return -1;
    }
    fclose(f);

    if (buf.size() < 8 || memcmp(buf.data(), "FORM", 4) != 0)
        return -1;
    uint32_t form_size = r_u32(buf.data() + 4);
    size_t end = std::min<size_t>(8 + form_size, buf.size());
    size_t p = 8;
    while (p + 8 <= end) {
        std::string tag((const char*)(buf.data() + p), 4);
        uint32_t csz = r_u32(buf.data() + p + 4);
        chunks[tag] = { p + 8, csz };
        p += 8 + csz;
    }

    auto gen8_it = chunks.find("GEN8");
    if (gen8_it != chunks.end() && gen8_it->second.size >= 0x3C) {
        const uint8_t* g = buf.data() + gen8_it->second.payload_off;
        version.bytecode_version = g[0x01];
        version.game_id = r_u32(g + 0x14);
        version.major = r_u32(g + 0x2C);
        version.minor = r_u32(g + 0x30);
        version.release = r_u32(g + 0x34);
        version.build = r_u32(g + 0x38);
        version.gen8_major = version.major;
        version.gen8_minor = version.minor;
        version.gen8_release = version.release;
        version.gen8_build = version.build;
        version.loaded = true;

        // BC17 implies 2.3+ regardless of what GEN8 declares.
        if (version.bytecode_version >= 17 && !version.is_at_least(2, 3)) {
            version.bump_to(2, 3);
        }
    }

    auto code_it = chunks.find("CODE");
    if (code_it == chunks.end() || code_it->second.size == 0) {
        version.is_yyc = true;
    }

    detect_format_versions();

    if (version.is_yyc) {
        Gmtoolkit::msg("YYC-compiled data file detected (no CODE chunk). "
                       "Audio + texture ops still work; code patches will be refused.");
    }

    auto it = chunks.find("STRG");
    if (it == chunks.end())
        return -2;
    size_t strg_off = it->second.payload_off;
    size_t strg_sz = it->second.size;
    if (strg_sz < 4)
        return -2;
    uint32_t scount = r_u32(buf.data() + strg_off);
    strg_ptr_table.resize(scount);
    for (uint32_t i = 0; i < scount; i++) {
        if (strg_off + 4 + i * 4ull + 4 > buf.size())
            return -2;
        uint32_t raw = r_u32(buf.data() + strg_off + 4 + i * 4);
        strg_ptr_table[i] = raw + 4;
    }

    strg_index.reserve(scount);
    for (uint32_t i = 0; i < scount; i++) {
        std::string s = read_strg_at(buf.data(), buf.size(), strg_ptr_table[i]);

        auto ins = strg_index.emplace(std::move(s), i);
        (void)ins;
    }

    auto vi = chunks.find("VARI");
    if (vi != chunks.end()) {
        size_t off = vi->second.payload_off;
        size_t cs = vi->second.size;
        bool bc15p = version.bytecode_version >= 15;
        size_t entry_size = bc15p ? 20 : 12;
        size_t q = off + (bc15p ? 12 : 0);
        size_t qend = off + cs;
        if (q <= qend) {
            while (q + entry_size <= qend) {
                VariEntry e;
                uint32_t name_ptr = r_u32(buf.data() + q + 0);
                if (bc15p) {
                    e.inst_type = read_i32(buf.data() + q + 4);
                    e.var_id = read_i32(buf.data() + q + 8);
                    e.occurrences = r_u32(buf.data() + q + 12);
                    uint32_t first_or_t = r_u32(buf.data() + q + 16);
                    e.first_addr = e.occurrences > 0 ? first_or_t : 0;
                } else {
                    e.inst_type = -1;
                    e.var_id = 0;
                    e.occurrences = r_u32(buf.data() + q + 4);
                    uint32_t first_or_t = r_u32(buf.data() + q + 8);
                    e.first_addr = e.occurrences > 0 ? first_or_t : 0;
                }
                e.name = read_strg_at(buf.data(), buf.size(), name_ptr);
                uint64_t k = hash_var_key(e.name, e.inst_type);
                vari_index.emplace(k, (uint32_t)vari_entries.size());
                vari_entries.push_back(std::move(e));
                q += entry_size;
            }
        }
    }

    auto fi = chunks.find("FUNC");
    if (fi != chunks.end()) {
        size_t off = fi->second.payload_off;
        size_t cs = fi->second.size;
        if (cs >= 4) {
            uint32_t fcount = r_u32(buf.data() + off);
            size_t q = off + 4;
            bool gms_2_3 = version.using_gms2_3();
            for (uint32_t i = 0; i < fcount; i++) {
                if (q + 12 > off + cs)
                    break;
                FuncEntry e;
                uint32_t name_ptr = r_u32(buf.data() + q + 0);
                e.occurrences = r_u32(buf.data() + q + 4);
                uint32_t first_or_t = r_u32(buf.data() + q + 8);
                if (e.occurrences > 0) {
                    e.first_addr = gms_2_3 && first_or_t >= 4 ? first_or_t - 4 : first_or_t;
                } else {
                    e.first_addr = 0;
                }
                e.name = read_strg_at(buf.data(), buf.size(), name_ptr);
                func_index.emplace(e.name, (uint32_t)func_entries.size());
                func_entries.push_back(std::move(e));
                q += 12;
            }

            if (!version.is_at_least(2024, 8) && q + 4 <= off + cs) {
                uint32_t lcount = r_u32(buf.data() + q);
                if (lcount < 1000000 && q + 4 + (size_t)lcount * 8 <= off + cs) {
                    code_locals_off = q;
                    has_code_locals = true;
                    q += 4;
                    for (uint32_t i = 0; i < lcount; i++) {
                        if (q + 8 > off + cs)
                            break;
                        uint32_t vcount = r_u32(buf.data() + q + 0);
                        uint32_t namep = r_u32(buf.data() + q + 4);
                        q += 8;
                        CodeLocal cl;
                        cl.name = read_strg_at(buf.data(), buf.size(), namep);
                        for (uint32_t j = 0; j < vcount; j++) {
                            if (q + 8 > off + cs)
                                break;
                            CodeLocal::Var v;
                            v.index = r_u32(buf.data() + q + 0);
                            uint32_t vname_ptr = r_u32(buf.data() + q + 4);
                            v.name = read_strg_at(buf.data(), buf.size(), vname_ptr);
                            cl.vars.push_back(std::move(v));
                            q += 8;
                        }
                        code_locals_index.emplace(cl.name, (uint32_t)code_locals.size());
                        code_locals.push_back(std::move(cl));
                    }
                }
            }
        }
    }

    var_occurrences.resize(vari_entries.size());
    for (size_t i = 0; i < vari_entries.size(); i++) {
        const VariEntry& v = vari_entries[i];
        if (v.occurrences == 0)
            continue;
        uint32_t operand_addr = v.first_addr + 4;
        for (uint32_t o = 0; o < v.occurrences; o++) {
            if ((size_t)operand_addr + 4 > buf.size())
                break;
            uint32_t val = r_u32(buf.data() + operand_addr);
            uint8_t var_type = (uint8_t)((val >> 24) & 0xF8);
            var_occurrences[i].push_back({ operand_addr, var_type });
            uint32_t next_off = val & 0x07FFFFFFu;
            if (o + 1 == v.occurrences || next_off == 0)
                break;
            operand_addr += next_off;
        }
    }
    func_occurrences.resize(func_entries.size());
    for (size_t i = 0; i < func_entries.size(); i++) {
        const FuncEntry& fe = func_entries[i];
        if (fe.occurrences == 0)
            continue;
        uint32_t operand_addr = fe.first_addr + 4;
        for (uint32_t o = 0; o < fe.occurrences; o++) {
            if ((size_t)operand_addr + 4 > buf.size())
                break;
            uint32_t val = r_u32(buf.data() + operand_addr);
            func_occurrences[i].push_back({ operand_addr, 0 });
            uint32_t next_off = val & 0x07FFFFFFu;
            if (o + 1 == fe.occurrences || next_off == 0)
                break;
            operand_addr += next_off;
        }
    }

    return 0;
}

int Pools::adopt_from_gmsdata(GMSLib::GMSData& data) {
    buf = std::move(data.Buffer);
    chunks.clear();
    chunks.reserve(data.Chunks.size());
    for (const auto& [Tag, Loc] : data.Chunks) {
        chunks[Tag] = ChunkLoc{ Loc.PayloadOffset, Loc.PayloadSize };
    }
    data.Chunks.clear();

    version = data.GeneralInfo.Version;
    version.loaded = true;

    auto code_it = chunks.find("CODE");
    version.is_yyc = (code_it == chunks.end() || code_it->second.size == 0);

    const size_t OrigStrCount = std::min(data.OriginalStringCount, data.Strings.size());
    strg_ptr_table.clear();
    strg_ptr_table.reserve(OrigStrCount);
    strg_index.clear();
    strg_index.reserve(OrigStrCount);
    for (size_t i = 0; i < OrigStrCount; i++) {
        const auto& S = *data.Strings[i];
        if (S.SourcePayloadOffset < 0) {
            strg_ptr_table.push_back(0);
        } else {
            strg_ptr_table.push_back(static_cast<uint32_t>(S.SourcePayloadOffset));
        }
        strg_index.emplace(S.Content, static_cast<uint32_t>(i));
    }
    pending_strings.clear();
    for (size_t i = OrigStrCount; i < data.Strings.size(); i++) {
        pending_strings.push_back(data.Strings[i]->Content);
    }

    const size_t OrigVarCount = std::min(data.OriginalVariableCount, data.Variables.size());
    vari_entries.clear();
    vari_entries.reserve(OrigVarCount);
    vari_index.clear();
    for (size_t i = 0; i < OrigVarCount; i++) {
        const auto& V = *data.Variables[i];
        VariEntry e;
        e.name = V.NameRef != nullptr ? V.NameRef->Content : std::string{};
        e.inst_type = static_cast<int32_t>(V.InstType);
        e.var_id = V.VarID;
        e.occurrences = V.Occurrences;
        e.first_addr = V.Occurrences > 0 ? V.FirstAddress : 0;
        vari_index.emplace(hash_var_key(e.name, e.inst_type), static_cast<uint32_t>(i));
        vari_entries.push_back(std::move(e));
    }
    pending_vars.clear();
    for (size_t i = OrigVarCount; i < data.Variables.size(); i++) {
        const auto& V = *data.Variables[i];
        if (V.NameRef == nullptr)
            continue;
        pending_vars.emplace_back(V.NameRef->Content, static_cast<int32_t>(V.InstType));
    }

    const size_t OrigFnCount = std::min(data.OriginalFunctionCount, data.Functions.size());
    func_entries.clear();
    func_entries.reserve(OrigFnCount);
    func_index.clear();
    bool gms_2_3 = version.using_gms2_3();
    for (size_t i = 0; i < OrigFnCount; i++) {
        const auto& F = *data.Functions[i];
        FuncEntry e;
        e.name = F.NameRef != nullptr ? F.NameRef->Content : std::string{};
        e.occurrences = F.Occurrences;
        if (F.Occurrences > 0) {
            e.first_addr = gms_2_3 && F.FirstAddress >= 4 ? F.FirstAddress - 4 : F.FirstAddress;
        } else {
            e.first_addr = 0;
        }
        func_index.emplace(e.name, static_cast<uint32_t>(i));
        func_entries.push_back(std::move(e));
    }
    pending_funcs.clear();
    for (size_t i = OrigFnCount; i < data.Functions.size(); i++) {
        const auto& F = *data.Functions[i];
        if (F.NameRef == nullptr)
            continue;
        pending_funcs.push_back(F.NameRef->Content);
    }

    has_code_locals = !data.CodeLocals.empty();
    code_locals.clear();
    code_locals_index.clear();
    if (has_code_locals) {
        code_locals.reserve(data.CodeLocals.size());
        for (size_t i = 0; i < data.CodeLocals.size(); i++) {
            const auto& CL = *data.CodeLocals[i];
            CodeLocal out;
            out.name = CL.NameRef != nullptr ? CL.NameRef->Content : std::string{};
            out.vars.reserve(CL.Locals.size());
            for (const auto& LV : CL.Locals) {
                out.vars.push_back({
                    LV.NameRef != nullptr ? LV.NameRef->Content : std::string{},
                    LV.Index,
                });
            }
            code_locals_index.emplace(out.name, static_cast<uint32_t>(i));
            code_locals.push_back(std::move(out));
        }
    }

    var_occurrences.resize(vari_entries.size());
    for (size_t i = 0; i < vari_entries.size(); i++) {
        const VariEntry& v = vari_entries[i];
        if (v.occurrences == 0)
            continue;
        uint32_t operand_addr = v.first_addr + 4;
        for (uint32_t o = 0; o < v.occurrences; o++) {
            if ((size_t)operand_addr + 4 > buf.size())
                break;
            uint32_t val = r_u32(buf.data() + operand_addr);
            uint8_t var_type = (uint8_t)((val >> 24) & 0xF8);
            var_occurrences[i].push_back({ operand_addr, var_type });
            uint32_t next_off = val & 0x07FFFFFFu;
            if (o + 1 == v.occurrences || next_off == 0)
                break;
            operand_addr += next_off;
        }
    }
    func_occurrences.resize(func_entries.size());
    for (size_t i = 0; i < func_entries.size(); i++) {
        const FuncEntry& fe = func_entries[i];
        if (fe.occurrences == 0)
            continue;
        uint32_t operand_addr = fe.first_addr + 4;
        for (uint32_t o = 0; o < fe.occurrences; o++) {
            if ((size_t)operand_addr + 4 > buf.size())
                break;
            uint32_t val = r_u32(buf.data() + operand_addr);
            func_occurrences[i].push_back({ operand_addr, 0 });
            uint32_t next_off = val & 0x07FFFFFFu;
            if (o + 1 == fe.occurrences || next_off == 0)
                break;
            operand_addr += next_off;
        }
    }

    return 0;
}

void Pools::return_to_gmsdata(GMSLib::GMSData& data) {
    data.Buffer = std::move(buf);
}

int32_t Pools::find_string(std::string_view s) {
    auto it = strg_index.find(std::string(s));
    if (it != strg_index.end())
        return (int32_t)it->second;
    for (size_t i = 0; i < pending_strings.size(); i++) {
        if (pending_strings[i] == s)
            return (int32_t)(strg_ptr_table.size() + i);
    }
    return -1;
}

int32_t Pools::find_variable(std::string_view name, int32_t inst_type) {
    uint64_t k = hash_var_key(name, inst_type);
    auto range = vari_index.equal_range(k);
    for (auto it = range.first; it != range.second; ++it) {
        const VariEntry& e = vari_entries[it->second];
        if (e.name == name && e.inst_type == inst_type)
            return (int32_t)it->second;
    }
    for (size_t i = 0; i < pending_vars.size(); i++) {
        if (pending_vars[i].first == name && pending_vars[i].second == inst_type)
            return (int32_t)(vari_entries.size() + i);
    }
    return -1;
}

int32_t Pools::find_function(std::string_view name) {
    auto it = func_index.find(std::string(name));
    if (it != func_index.end())
        return (int32_t)it->second;
    for (size_t i = 0; i < pending_funcs.size(); i++) {
        if (pending_funcs[i] == name)
            return (int32_t)(func_entries.size() + i);
    }
    return -1;
}

static void parse_asset_chunk(const Pools& P, const std::string& tag, std::unordered_map<std::string, int32_t>& out) {
    auto it = P.chunks.find(tag);
    if (it == P.chunks.end())
        return;
    size_t pay = it->second.payload_off;
    size_t sz = it->second.size;
    if (sz < 4)
        return;
    const uint8_t* b = P.buf.data();
    uint32_t count = r_u32(b + pay);
    if (count == 0 || 4 + (size_t)count * 4 > sz)
        return;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t ent = r_u32(b + pay + 4 + (size_t)i * 4);
        if (ent == 0 || (size_t)ent + 4 > P.buf.size())
            continue;
        uint32_t name_ptr = r_u32(b + ent);
        if (name_ptr < 4 || (size_t)name_ptr + 4 > P.buf.size())
            continue;
        uint32_t nlen = r_u32(b + name_ptr - 4);
        if (nlen == 0 || nlen > 256)
            continue;
        if ((size_t)name_ptr + nlen > P.buf.size())
            continue;
        std::string nm((const char*)(b + name_ptr), nlen);
        out[std::move(nm)] = (int32_t)i;
    }
}

int32_t Pools::asset_lookup(std::string_view name) {
    if (!assets_loaded) {
        parse_asset_chunk(*this, "OBJT", asset_objt);
        parse_asset_chunk(*this, "SPRT", asset_sprt);
        parse_asset_chunk(*this, "SOND", asset_sond);
        parse_asset_chunk(*this, "ROOM", asset_room);
        parse_asset_chunk(*this, "BGND", asset_bgnd);
        parse_asset_chunk(*this, "FONT", asset_font);
        parse_asset_chunk(*this, "PATH", asset_path);
        assets_loaded = true;
    }
    std::string key(name);
    auto chk = [&](const std::unordered_map<std::string, int32_t>& m) -> int32_t {
        auto it = m.find(key);
        return it != m.end() ? it->second : -1;
    };
    int32_t r = chk(asset_objt);
    if (r >= 0)
        return r;
    r = chk(asset_sprt);
    if (r >= 0)
        return r;
    r = chk(asset_sond);
    if (r >= 0)
        return r;
    r = chk(asset_room);
    if (r >= 0)
        return r;
    r = chk(asset_bgnd);
    if (r >= 0)
        return r;
    r = chk(asset_font);
    if (r >= 0)
        return r;
    r = chk(asset_path);
    if (r >= 0)
        return r;
    return -1;
}

int32_t Pools::intern_string(std::string_view s) {
    int32_t idx = find_string(s);
    if (idx >= 0)
        return idx;
    int32_t promised = (int32_t)(strg_ptr_table.size() + pending_strings.size());
    pending_strings.emplace_back(s);
    return promised;
}

int32_t Pools::add_variable(std::string_view name, int32_t inst_type) {
    int32_t idx = find_variable(name, inst_type);
    if (idx >= 0)
        return idx;
    int32_t promised = (int32_t)(vari_entries.size() + pending_vars.size());
    pending_vars.emplace_back(std::string(name), inst_type);

    intern_string(name);
    return promised;
}

int32_t Pools::add_local_for_patch(std::string_view name) {
    if (version.bytecode_version <= 14) {
        int32_t existing = find_variable(name, -1);
        if (existing >= 0)
            return existing;
        int32_t promised = (int32_t)(vari_entries.size() + pending_vars.size());
        pending_vars.emplace_back(std::string(name), -1);
        intern_string(name);
        return promised;
    }

    int32_t promised = (int32_t)(vari_entries.size() + pending_vars.size());
    pending_vars.emplace_back(std::string(name), -7);
    intern_string(name);
    return promised;
}

int32_t Pools::add_function(std::string_view name) {
    int32_t idx = find_function(name);
    if (idx >= 0)
        return idx;
    int32_t promised = (int32_t)(func_entries.size() + pending_funcs.size());
    pending_funcs.emplace_back(name);
    intern_string(name);
    return promised;
}

int Pools::add_code_patch(CodePatch p) {
    pending_code.push_back(std::move(p));
    return 0;
}

} // namespace GMSLib::SaveBackend
