// SPDX-License-Identifier: GPL-3.0-or-later

#include "GMSLib/GMSIO.h"

#include "GMSLib/GMSData.h"
#include "GMSLib/SaveBackend/Pools.h"
#include "Toolkit/IO.h"
#include "Toolkit/Log.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace GMSLib {

using Gmtoolkit::r_u16;
using Gmtoolkit::r_u32;

// STRG layout: count + table of absolute file offsets, each pointing at a
// (u32 length, char[length], NUL) record elsewhere in the buffer.
static int ParseSTRG(GMSData& D) {
    auto It = D.Chunks.find("STRG");
    if (It == D.Chunks.end())
        return 0;
    const std::uint8_t* Base = D.Buffer.data() + It->second.PayloadOffset;
    std::size_t Sz = It->second.PayloadSize;
    if (Sz < 4)
        return -1;
    std::uint32_t Count = r_u32(Base);
    D.Strings.reserve(Count);
    D.StringByContent.reserve(Count);
    for (std::uint32_t i = 0; i < Count; i++) {
        if (4 + i * 4 + 4 > Sz)
            return -1;
        std::uint32_t Ptr = r_u32(Base + 4 + i * 4);
        if (Ptr + 4 > D.Buffer.size())
            return -1;
        std::uint32_t Len = r_u32(D.Buffer.data() + Ptr);
        if (Ptr + 4 + Len > D.Buffer.size())
            return -1;
        auto Str =
            std::make_unique<GMSString>(std::string(reinterpret_cast<const char*>(D.Buffer.data() + Ptr + 4), Len));
        // Record the offset of the char data (after the length prefix); other
        // chunks reference strings by this exact file offset.
        Str->SourcePayloadOffset = static_cast<std::int64_t>(Ptr) + 4;
        Str->Id = static_cast<std::int32_t>(i);
        D.StringByContent.emplace(Str->Content, Str.get());
        D.Strings.push_back(std::move(Str));
    }
    return 0;
}

static GMSString* ResolveStrPtr(const GMSData& D, std::uint32_t Ptr) {
    for (const auto& Up : D.Strings) {
        if (Up->SourcePayloadOffset == static_cast<std::int64_t>(Ptr))
            return Up.get();
    }
    return nullptr;
}

// Pulls just the fields needed to gate format-version behavior elsewhere; the
// 24 bytes from GameID..major are skipped because they're irrelevant here.
static int ParseGEN8(GMSData& D) {
    auto It = D.Chunks.find("GEN8");
    if (It == D.Chunks.end())
        return 0;
    if (It->second.PayloadSize < 56)
        return -1;
    const std::uint8_t* B = D.Buffer.data() + It->second.PayloadOffset;
    auto& G = D.GeneralInfo;
    G.IsDebuggerDisabled = B[0];
    G.BytecodeVersion = B[1];
    G.Unknown = r_u16(B + 2);
    G.Filename = ResolveStrPtr(D, r_u32(B + 4));
    G.Config = ResolveStrPtr(D, r_u32(B + 8));
    G.LastObj = r_u32(B + 12);
    G.LastTile = r_u32(B + 16);
    G.GameID = r_u32(B + 20);
    G.Version.major = r_u32(B + 44);
    G.Version.minor = r_u32(B + 48);
    G.Version.release = r_u32(B + 52);
    // Older GEN8 (pre-2.x) stopped at release; build is optional tail data.
    if (It->second.PayloadSize >= 60)
        G.Version.build = r_u32(B + 56);
    G.Version.bytecode_version = G.BytecodeVersion;
    // Snapshot GEN8's declared version before detection bumps it; some gates
    // (e.g. UsingStringRealOptimizations for late GMS1.4 builds) key off the
    // raw declared build number.
    G.Version.gen8_major = G.Version.major;
    G.Version.gen8_minor = G.Version.minor;
    G.Version.gen8_release = G.Version.release;
    G.Version.gen8_build = G.Version.build;
    G.Version.loaded = true;
    return 0;
}

static int ParseVARI(GMSData& D) {
    auto It = D.Chunks.find("VARI");
    if (It == D.Chunks.end())
        return 0;
    const std::uint8_t* B = D.Buffer.data() + It->second.PayloadOffset;
    std::size_t Sz = It->second.PayloadSize;
    std::size_t Pos = 0;
    // Bytecode 15+ prepends a 12-byte header (counts of instance/var/local
    // entries); older versions dive straight into the entries.
    bool HasHeader = D.GeneralInfo.BytecodeVersion >= 15;
    if (HasHeader) {
        if (Sz < 12)
            return -1;
        Pos = 12;
    }
    constexpr std::size_t EntrySize = 20;
    while (Pos + EntrySize <= Sz) {
        auto V = std::make_unique<GMSVariable>();
        V->NameRef = ResolveStrPtr(D, r_u32(B + Pos));
        V->InstType =
            static_cast<Underanalyzer::IGMInstruction::InstanceType>(static_cast<std::int32_t>(r_u32(B + Pos + 4)));
        V->VarID = static_cast<std::int32_t>(r_u32(B + Pos + 8));
        V->Occurrences = r_u32(B + Pos + 12);
        V->FirstAddress = r_u32(B + Pos + 16);
        if (V->NameRef != nullptr)
            D.VariableByName.try_emplace(V->NameRef->Content, V.get());
        D.Variables.push_back(std::move(V));
        Pos += EntrySize;
    }
    return 0;
}

static int ParseFUNC(GMSData& D) {
    auto It = D.Chunks.find("FUNC");
    if (It == D.Chunks.end())
        return 0;
    const std::uint8_t* B = D.Buffer.data() + It->second.PayloadOffset;
    std::size_t Sz = It->second.PayloadSize;
    if (Sz < 4)
        return 0;
    std::uint32_t Count = r_u32(B);
    std::size_t Pos = 4;
    for (std::uint32_t i = 0; i < Count; i++) {
        if (Pos + 12 > Sz)
            return -1;
        auto F = std::make_unique<GMSFunction>();
        F->NameRef = ResolveStrPtr(D, r_u32(B + Pos));
        F->Occurrences = r_u32(B + Pos + 4);
        F->FirstAddress = r_u32(B + Pos + 8);
        if (F->NameRef != nullptr)
            D.FunctionByName.try_emplace(F->NameRef->Content, F.get());
        D.Functions.push_back(std::move(F));
        Pos += 12;
    }

    // 2024.8 moved code-local tables out of FUNC entirely; before that they
    // ride along as a trailer past the function table.
    if (!D.IsVersionAtLeast(2024, 8) && Pos + 4 <= Sz) {
        std::uint32_t LocalsCount = r_u32(B + Pos);
        Pos += 4;
        D.CodeLocals.reserve(LocalsCount);
        for (std::uint32_t i = 0; i < LocalsCount; i++) {
            if (Pos + 8 > Sz)
                return -1;
            auto CL = std::make_unique<GMSCodeLocals>();
            std::uint32_t LCount = r_u32(B + Pos);
            CL->NameRef = ResolveStrPtr(D, r_u32(B + Pos + 4));
            Pos += 8;
            CL->Locals.reserve(LCount);
            for (std::uint32_t j = 0; j < LCount; j++) {
                if (Pos + 8 > Sz)
                    return -1;
                GMSCodeLocals::LocalVar LV;
                LV.Index = r_u32(B + Pos);
                LV.NameRef = ResolveStrPtr(D, r_u32(B + Pos + 4));
                CL->Locals.push_back(LV);
                Pos += 8;
            }
            D.CodeLocals.push_back(std::move(CL));
        }
    }
    return 0;
}

static int ParseCODE(GMSData& D) {
    auto It = D.Chunks.find("CODE");
    if (It == D.Chunks.end())
        return 0;
    // 2.3+ adds a trailing Offset field that locates sub-function bytecode
    // within the parent's blob; pre-2.3 entries are 4 bytes shorter.
    bool Has23Offset = D.IsVersionAtLeast(2, 3);
    std::size_t EntrySize = Has23Offset ? 20 : 16;
    const std::uint8_t* B = D.Buffer.data() + It->second.PayloadOffset;
    std::size_t Sz = It->second.PayloadSize;
    if (Sz < 4)
        return -1;
    std::uint32_t Count = r_u32(B);
    if (4 + Count * 4 > Sz)
        return -1;

    D.Code.reserve(Count);
    std::vector<std::size_t> EntryFileOffsets;
    EntryFileOffsets.reserve(Count);
    for (std::uint32_t i = 0; i < Count; i++) {
        std::uint32_t EntryPtr = r_u32(B + 4 + i * 4);
        if (EntryPtr < It->second.PayloadOffset || EntryPtr + EntrySize > It->second.PayloadOffset + Sz)
            return -1;
        std::size_t EntryOff = EntryPtr - It->second.PayloadOffset;
        const std::uint8_t* E = B + EntryOff;
        auto C = std::make_unique<GMSCode>();
        C->NameRef = ResolveStrPtr(D, r_u32(E));
        C->Length = r_u32(E + 4);
        C->LocalsCount = r_u16(E + 8);
        C->ArgumentsCount = r_u16(E + 10);
        C->BytecodeRelativeAddress = static_cast<std::int32_t>(r_u32(E + 12));
        if (Has23Offset)
            C->Offset = r_u32(E + 16);
        // BytecodeRelativeAddress is a signed delta from the field's own file
        // position, so the absolute address is anchored at EntryPtr+12.
        C->BytecodeAbsoluteAddress =
            static_cast<std::size_t>(static_cast<std::int64_t>(EntryPtr + 12) + C->BytecodeRelativeAddress);
        if (C->NameRef != nullptr)
            D.CodeByName.try_emplace(C->NameRef->Content, C.get());
        EntryFileOffsets.push_back(EntryPtr + 12);
        D.Code.push_back(std::move(C));
    }

    // 2.3+ stores anonymous/struct sub-functions as additional CODE entries
    // sharing the parent's bytecode address; the first hit is the parent.
    std::unordered_map<std::size_t, GMSCode*> ByAddr;
    ByAddr.reserve(Count);
    for (auto& Up : D.Code) {
        auto [It2, Inserted] = ByAddr.try_emplace(Up->BytecodeAbsoluteAddress, Up.get());
        if (!Inserted) {
            Up->ParentEntry = It2->second;
            It2->second->ChildEntries.push_back(Up.get());
        }
    }
    return 0;
}

static int ParseSCPT(GMSData& D) {
    auto It = D.Chunks.find("SCPT");
    if (It == D.Chunks.end())
        return 0;
    const std::uint8_t* B = D.Buffer.data() + It->second.PayloadOffset;
    std::size_t Sz = It->second.PayloadSize;
    if (Sz < 4)
        return -1;
    std::uint32_t Count = r_u32(B);
    if (4 + Count * 4 > Sz)
        return -1;
    std::size_t Pos = 4 + Count * 4;
    for (std::uint32_t i = 0; i < Count; i++) {
        if (Pos + 8 > Sz)
            return -1;
        auto S = std::make_unique<GMSScript>();
        S->NameRef = ResolveStrPtr(D, r_u32(B + Pos));
        std::int32_t RawId = static_cast<std::int32_t>(r_u32(B + Pos + 4));
        S->RawCodeId = RawId;
        // Constructor scripts are flagged by setting the high bit on the
        // code id; mask it off to recover the real index.
        if (RawId < -1) {
            S->IsConstructor = true;
            RawId = static_cast<std::int32_t>(static_cast<std::uint32_t>(RawId) & 0x7FFFFFFFu);
        }
        if (RawId >= 0 && static_cast<std::size_t>(RawId) < D.Code.size()) {
            S->CodeRef = D.Code[RawId].get();
        }
        if (S->NameRef != nullptr)
            D.ScriptByName.try_emplace(S->NameRef->Content, S.get());
        D.Scripts.push_back(std::move(S));
        Pos += 8;
    }
    return 0;
}

static int ParseGLOB(GMSData& D) {
    auto It = D.Chunks.find("GLOB");
    if (It == D.Chunks.end())
        return 0;
    const std::uint8_t* B = D.Buffer.data() + It->second.PayloadOffset;
    std::size_t Sz = It->second.PayloadSize;
    if (Sz < 4)
        return -1;
    std::uint32_t Count = r_u32(B);
    if (4 + Count * 4 > Sz)
        return -1;
    for (std::uint32_t i = 0; i < Count; i++) {
        auto G = std::make_unique<GMSGlobalInit>();
        G->RawCodeId = static_cast<std::int32_t>(r_u32(B + 4 + i * 4));
        if (G->RawCodeId >= 0 && static_cast<std::size_t>(G->RawCodeId) < D.Code.size()) {
            G->CodeRef = D.Code[G->RawCodeId].get();
        }
        D.GlobalInits.push_back(std::move(G));
    }
    return 0;
}

int LoadFromFile(const std::string& Path, GMSData& OutData) {
    // Prefer copy-on-write mapping so the parsers can reference the file
    // directly; fall back to a full read for filesystems that can't mmap.
    if (OutData.Buffer.load_cow(Path.c_str()) != 0) {
        std::vector<std::uint8_t> tmp;
        if (Gmtoolkit::slurp(Path.c_str(), tmp) != 0) {
            Gmtoolkit::err("GMSIO::Load: failed to load %s", Path.c_str());
            return -1;
        }
        OutData.Buffer = std::move(tmp);
    }
    if (IndexChunks(OutData.Buffer.data(), OutData.Buffer.size(), OutData.Chunks) != 0) {
        Gmtoolkit::tprint("GMSIO::Load: IndexChunks failed\n");
        return -1;
    }

    // Order matters: STRG seeds the string table everything else points into,
    // GEN8 supplies the version gates downstream parsers branch on, and CODE
    // must precede SCPT/GLOB which look up entries by index.
    struct {
        int (*fn)(GMSData&);
        const char* name;
    } parsers[] = {
        { ParseSTRG, "STRG" }, { ParseGEN8, "GEN8" }, { ParseVARI, "VARI" }, { ParseFUNC, "FUNC" },
        { ParseCODE, "CODE" }, { ParseSCPT, "SCPT" }, { ParseGLOB, "GLOB" },
    };
    for (auto& p : parsers) {
        if (p.fn(OutData) != 0) {
            Gmtoolkit::err("GMSIO::Load: Parse%s failed", p.name);
            return -1;
        }
    }

    GMSLib::SaveBackend::Pools P;
    P.buf = std::move(OutData.Buffer);
    P.chunks.reserve(OutData.Chunks.size());
    for (const auto& [Tag, Loc] : OutData.Chunks) {
        P.chunks[Tag] = GMSLib::SaveBackend::Pools::ChunkLoc{ Loc.PayloadOffset, Loc.PayloadSize };
    }

    P.version = OutData.GeneralInfo.Version;
    P.version.loaded = true;
    // Bytecode 17 only ships in 2.3+ runtimes; some GEN8 headers underreport
    // the version, so promote it to keep later behavior gates consistent.
    if (P.version.bytecode_version >= 17 && !P.version.is_at_least(2, 3)) {
        P.version.bump_to(2, 3);
    }
    P.detect_format_versions();
    OutData.GeneralInfo.Version = P.version;
    OutData.Buffer = std::move(P.buf);

    // Freeze the pristine counts so SaveToFile can tell which entries were
    // appended by mods and need to be patched into the corresponding chunks.
    OutData.OriginalStringCount = OutData.Strings.size();
    OutData.OriginalVariableCount = OutData.Variables.size();
    OutData.OriginalFunctionCount = OutData.Functions.size();
    OutData.OriginalScriptCount = OutData.Scripts.size();
    OutData.OriginalCodeCount = OutData.Code.size();
    return 0;
}

} // namespace GMSLib
