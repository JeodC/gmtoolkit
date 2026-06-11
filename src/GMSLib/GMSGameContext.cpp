// SPDX-License-Identifier: GPL-3.0-or-later

#include "GMSLib/GMSGameContext.h"

#include "GMSLib/GMSData.h"
#include "GMSLib/Models/GMSFunction.h"
#include "GMSLib/Models/GMSScript.h"
#include "GMSLib/Models/GMSString.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace GMSLib {

GMSGameContext::GMSGameContext(GMSData& DataIn)
    : _Data(&DataIn), _Globals(DataIn), _CodeBuilder(*this, DataIn, _Globals) {
    // Mirrors UTMT's BuiltinList(UndertaleData) ctor: FUNC-chunk functions
    // (extension/DLL exports) become callable builtins.
    _Builtins.LoadFunctionsFromData(DataIn);
}

bool GMSGameContext::UsingGMS2OrLater() const {
    return _Data->IsVersionAtLeast(2);
}
bool GMSGameContext::UsingGMLv2() const {
    return _Data->IsVersionAtLeast(2, 3);
}
bool GMSGameContext::UsingStringRealOptimizations() const {
    // UTMT: Major >= 2, or GMS1.4 builds 1539 / >= 1763 (late 1.4 runners
    // gained the same string/real conversion behavior).
    const auto& V = _Data->GeneralInfo.Version;
    return _Data->IsVersionAtLeast(2) || V.gen8_build == 1539 || V.gen8_build >= 1763;
}
bool GMSGameContext::UsingTypedBooleans() const {
    return _Data->IsVersionAtLeast(2, 3, 7);
}
bool GMSGameContext::UsingNullishOperator() const {
    return _Data->IsVersionAtLeast(2, 3, 7);
}
bool GMSGameContext::UsingAssetReferences() const {
    return _Data->IsVersionAtLeast(2023, 8);
}
bool GMSGameContext::UsingRoomInstanceReferences() const {
    return _Data->IsVersionAtLeast(2024, 2);
}
bool GMSGameContext::UsingFunctionScriptReferences() const {
    return _Data->IsVersionAtLeast(2024, 2);
}
bool GMSGameContext::UsingNewFunctionResolution() const {
    return _Data->IsVersionAtLeast(2024, 13);
}
bool GMSGameContext::Bytecode14OrLower() const {
    return _Data->GeneralInfo.BytecodeVersion <= 14;
}
bool GMSGameContext::UsingLogicalShortCircuit() const {
    // Per-game option, not a version feature: GMS1 projects could disable
    // short-circuit evaluation. Detected from the game's own bytecode.
    _EnsureCodeFlagsScanned();
    return _ShortCircuit;
}
bool GMSGameContext::UsingLongCompoundBitwise() const {
    return _Data->IsVersionAtLeast(2, 3, 2);
}
// Inverted gates: these emit-an-extra-op behaviors were dropped in the listed
// versions, so the predicate is true for older targets.
bool GMSGameContext::UsingExtraRepeatInstruction() const {
    // UTMT gates on the non-LTS branch: 2022 LTS keeps the extra instruction.
    return !_Data->GeneralInfo.Version.is_non_lts_at_least(2022, 11);
}
bool GMSGameContext::UsingFinallyBeforeThrow() const {
    return !_Data->IsVersionAtLeast(2024, 6);
}
bool GMSGameContext::UsingConstructorSetStatic() const {
    return _Data->IsVersionAtLeast(2024, 11);
}
bool GMSGameContext::UsingArrayCopyOnWrite() const {
    // Per-game option (2.3 - 2022.1 era), not a version range. The previous
    // version heuristic returned true for every pre-2022.2 game, which made
    // the compiler emit setowner instructions into pre-2.3 data files whose
    // runners don't have that opcode. Detected from the game's own bytecode,
    // matching UTMT (setowner present <=> copy-on-write enabled).
    _EnsureCodeFlagsScanned();
    return _ArrayCopyOnWrite;
}
bool GMSGameContext::UsingNewArrayOwners() const {
    return _Data->IsVersionAtLeast(2, 3, 2);
}
bool GMSGameContext::UsingReentrantStatic() const {
    return !_Data->IsVersionAtLeast(2024, 11);
}
bool GMSGameContext::UsingNewFunctionVariables() const {
    return _Data->IsVersionAtLeast(2024, 2);
}
bool GMSGameContext::UsingSelfToBuiltin() const {
    return _Data->IsVersionAtLeast(2024, 2);
}
bool GMSGameContext::UsingGlobalConstantFunction() const {
    return _Data->IsVersionAtLeast(2023, 11);
}
bool GMSGameContext::UsingObjectFunctionForesight() const {
    return _Data->IsVersionAtLeast(2024, 11);
}
bool GMSGameContext::UsingBetterTryBreakContinue() const {
    return _Data->IsVersionAtLeast(2024, 11);
}
bool GMSGameContext::UsingBuiltinDefaultArguments() const {
    return _Data->IsVersionAtLeast(2024, 11);
}
bool GMSGameContext::UsingOptimizedFunctionDeclarations() const {
    return _Data->IsVersionAtLeast(2024, 14);
}
bool GMSGameContext::UsingNewChainedFunctionArgumentOrder() const {
    return _Data->IsVersionAtLeast(2024, 14, 4);
}

static inline std::uint32_t _ReadU32(const std::uint8_t* P) {
    return (std::uint32_t)P[0] | ((std::uint32_t)P[1] << 8) | ((std::uint32_t)P[2] << 16) | ((std::uint32_t)P[3] << 24);
}

// One pass over every root code entry's instruction stream, mirroring the
// detection UTMT performs while parsing CODE (UndertaleCode.cs):
//   - and.b.b / or.b.b   => the game was compiled without short-circuiting
//   - break -5 (setowner) => array copy-on-write is enabled
// Walks raw words using the same size rules as IGMInstruction::GetSize.
// Bytecode 14 uses the old opcode numbering; only the opcodes the walk needs
// are remapped (UndertaleCode.cs ConvertOldKindToNewKind).
void GMSGameContext::_EnsureCodeFlagsScanned() const {
    if (_CodeFlagsScanned)
        return;
    _CodeFlagsScanned = true;

    const bool Bytecode14 = _Data->GeneralInfo.BytecodeVersion <= 14;
    const std::uint8_t* Buf = _Data->Buffer.data();
    const std::size_t BufSize = _Data->Buffer.size();

    bool FoundNonShortCircuit = false;
    bool FoundSetOwner = false;

    for (const auto& CodeUp : _Data->Code) {
        const GMSCode* C = CodeUp.get();
        if (C == nullptr || C->ParentEntry != nullptr)
            continue; // children share the parent's blob
        std::size_t Pos = C->BytecodeAbsoluteAddress;
        const std::size_t End = Pos + C->Length;
        if (End > BufSize || End < Pos)
            continue;
        while (Pos + 4 <= End) {
            const std::uint32_t W = _ReadU32(Buf + Pos);
            std::uint8_t Op = (std::uint8_t)(W >> 24);
            if (Bytecode14) {
                switch (Op) {
                    case 0x0A: Op = 0x0E; break; // And
                    case 0x0B: Op = 0x0F; break; // Or
                    case 0x41: Op = 0x45; break; // Pop
                    case 0xDA: Op = 0xD9; break; // Call
                    default: break;
                }
            }
            const std::uint8_t T1 = (W >> 16) & 0xF;
            const std::uint8_t T2 = (W >> 20) & 0xF;

            // DataType::Boolean == 4
            if ((Op == 0x0E || Op == 0x0F) && T1 == 4 && T2 == 4)
                FoundNonShortCircuit = true;
            // ExtendedOpcode::SetArrayOwner == -5 in the low int16
            if (Op == 0xFF && (std::int16_t)(W & 0xFFFF) == -5)
                FoundSetOwner = true;

            // Instruction size, per IGMInstruction::GetSize.
            std::size_t Size = 4;
            switch (Op) {
                case 0x45: // Pop: 4-byte variable ref unless PopSwap (Int16)
                    if (T1 != 0xF)
                        Size = 8;
                    break;
                case 0xD9: // Call: 4-byte function ref
                    Size = 8;
                    break;
                case 0xC0: // Push family: inline data sized to Type1
                case 0xC1:
                case 0xC2:
                case 0xC3:
                case 0x84:
                    if (T1 == 0x0 || T1 == 0x3) // Double / Int64
                        Size = 12;
                    else if (T1 == 0xF) // Int16, inline in low word
                        Size = 4;
                    else
                        Size = 8;
                    break;
                case 0xFF: // Extended: Int32 variant carries an int argument
                    if (T1 == 0x2)
                        Size = 8;
                    break;
                default:
                    break;
            }
            Pos += Size;
        }
        if (FoundNonShortCircuit && FoundSetOwner)
            break;
    }

    _ShortCircuit = !FoundNonShortCircuit;
    _ArrayCopyOnWrite = FoundSetOwner;
}

// Asset chunks (OBJT, SPRT, SOND, ...) share a layout: count, table of entry
// offsets, and entries whose first u32 points at a string-table name record.
// SEQN and PSYS additionally lead with alignment padding plus a u32 version
// (== 1) before the count (UndertaleChunks.cs SEQN/PSYS Unserialize).
static void _ParseAssetChunk(const std::uint8_t* B, std::size_t BufSize, std::size_t PayloadOff,
                             std::size_t PayloadSize, std::unordered_map<std::string, int>& Out,
                             bool HasVersionWord = false) {
    if (HasVersionWord) {
        std::size_t End = PayloadOff + PayloadSize;
        while (PayloadOff % 4 != 0 && PayloadOff < End && B[PayloadOff] == 0) {
            PayloadOff++;
            PayloadSize--;
        }
        if (PayloadSize < 4 || _ReadU32(B + PayloadOff) != 1)
            return;
        PayloadOff += 4;
        PayloadSize -= 4;
    }
    if (PayloadSize < 4)
        return;
    std::uint32_t Count = _ReadU32(B + PayloadOff);
    if (Count == 0 || 4 + (std::size_t)Count * 4 > PayloadSize)
        return;
    for (std::uint32_t i = 0; i < Count; i++) {
        std::uint32_t Ent = _ReadU32(B + PayloadOff + 4 + (std::size_t)i * 4);
        if (Ent == 0 || (std::size_t)Ent + 4 > BufSize)
            continue;
        std::uint32_t NamePtr = _ReadU32(B + Ent);
        if (NamePtr < 4 || (std::size_t)NamePtr + 4 > BufSize)
            continue;
        // Name string is preceded by its u32 length (STRG layout), so step
        // back 4 bytes from the pointer to read it.
        std::uint32_t NLen = _ReadU32(B + NamePtr - 4);
        if (NLen == 0 || NLen > 256)
            continue;
        if ((std::size_t)NamePtr + NLen > BufSize)
            continue;
        Out.emplace(std::string((const char*)(B + NamePtr), NLen), (int)i);
    }
}

// Lazy: most callers never need any asset map, so paying the parse cost only
// on first lookup keeps cold-path loads cheap.
void GMSGameContext::_EnsureAssetsLoaded() {
    if (_AssetsLoaded)
        return;
    auto Walk = [&](const char* Tag, std::unordered_map<std::string, int>& Out, bool HasVersionWord = false) {
        auto It = _Data->Chunks.find(Tag);
        if (It == _Data->Chunks.end())
            return;
        _ParseAssetChunk(_Data->Buffer.data(), _Data->Buffer.size(), It->second.PayloadOffset, It->second.PayloadSize,
                         Out, HasVersionWord);
    };
    Walk("OBJT", _AssetObj);
    Walk("SPRT", _AssetSpr);
    Walk("SOND", _AssetSnd);
    // Audio groups resolve under the Sound tag, mirroring UTMT's
    // GlobalDecompileContext (AudioGroups registered as RefType.Sound).
    Walk("AGRP", _AssetSnd);
    Walk("ROOM", _AssetRoom);
    Walk("BGND", _AssetBgnd);
    Walk("PATH", _AssetPath);
    Walk("FONT", _AssetFont);
    Walk("TMLN", _AssetTmln);
    Walk("SHDR", _AssetShdr);
    Walk("SEQN", _AssetSeqn, true);
    Walk("ACRV", _AssetAcrv);
    // Particle SYSTEMS (PSYS), not emitters (PSEM) -- UTMT registers
    // Data.ParticleSystems for the ParticleSystem asset type.
    Walk("PSYS", _AssetPsem, true);
    _AssetsLoaded = true;
}

bool GMSGameContext::GetAssetName(Underanalyzer::AssetType Type, int Index, std::string& OutName) {
    _EnsureAssetsLoaded();
    using AT = Underanalyzer::AssetType;
    const std::unordered_map<std::string, int>* M = nullptr;
    switch (Type) {
        case AT::Object:
            M = &_AssetObj;
            break;
        case AT::Sprite:
            M = &_AssetSpr;
            break;
        case AT::Sound:
            M = &_AssetSnd;
            break;
        case AT::Room:
            M = &_AssetRoom;
            break;
        case AT::Background:
            M = &_AssetBgnd;
            break;
        case AT::Path:
            M = &_AssetPath;
            break;
        case AT::Font:
            M = &_AssetFont;
            break;
        case AT::Timeline:
            M = &_AssetTmln;
            break;
        case AT::Shader:
            M = &_AssetShdr;
            break;
        case AT::Sequence:
            M = &_AssetSeqn;
            break;
        case AT::AnimCurve:
            M = &_AssetAcrv;
            break;
        case AT::ParticleSystem:
            M = &_AssetPsem;
            break;
        default:
            return false;
    }
    for (auto& Kv : *M) {
        if (Kv.second == Index) {
            OutName = Kv.first;
            return true;
        }
    }
    return false;
}

bool GMSGameContext::GetAssetId(const std::string& Name, int& OutId) {
    _EnsureAssetsLoaded();
    auto Try = [&](const std::unordered_map<std::string, int>& M) {
        auto It = M.find(Name);
        if (It == M.end())
            return false;
        OutId = It->second;
        return true;
    };

    if (Try(_AssetObj))
        return true;
    if (Try(_AssetSpr))
        return true;
    if (Try(_AssetSnd))
        return true;
    if (Try(_AssetRoom))
        return true;
    if (Try(_AssetBgnd))
        return true;
    if (Try(_AssetPath))
        return true;
    if (Try(_AssetFont))
        return true;
    if (Try(_AssetTmln))
        return true;
    if (Try(_AssetShdr))
        return true;
    if (Try(_AssetSeqn))
        return true;
    if (Try(_AssetAcrv))
        return true;
    if (Try(_AssetPsem))
        return true;
    return false;
}

// Reports which asset chunk a name lives in, using the same search order as GetAssetId.
// The canonical Underanalyzer::AssetType is what the serializer remaps to the on-disk tag.
bool GMSGameContext::GetAssetType(const std::string& Name, Underanalyzer::AssetType& OutType) {
    _EnsureAssetsLoaded();
    using AT = Underanalyzer::AssetType;
    auto Try = [&](const std::unordered_map<std::string, int>& M, AT Type) {
        if (M.find(Name) == M.end())
            return false;
        OutType = Type;
        return true;
    };
    return Try(_AssetObj, AT::Object) || Try(_AssetSpr, AT::Sprite) || Try(_AssetSnd, AT::Sound) ||
           Try(_AssetRoom, AT::Room) || Try(_AssetBgnd, AT::Background) || Try(_AssetPath, AT::Path) ||
           Try(_AssetFont, AT::Font) || Try(_AssetTmln, AT::Timeline) || Try(_AssetShdr, AT::Shader) ||
           Try(_AssetSeqn, AT::Sequence) || Try(_AssetAcrv, AT::AnimCurve) || Try(_AssetPsem, AT::ParticleSystem);
}

// Resolves the GameMaker convention of referencing a placed room instance by
// the synthetic identifier "inst_<numeric id>" (ids start at 100000).
bool GMSGameContext::GetRoomInstanceId(const std::string& Name, int& OutId) {
    static constexpr char kPrefix[] = "inst_";
    constexpr std::size_t kPrefixLen = sizeof(kPrefix) - 1;
    if (Name.size() <= kPrefixLen || std::memcmp(Name.data(), kPrefix, kPrefixLen) != 0) {
        return false;
    }
    long long InstanceId = 0;
    const char* P = Name.data() + kPrefixLen;
    const char* End = Name.data() + Name.size();
    if (P == End)
        return false;
    for (; P < End; P++) {
        if (*P < '0' || *P > '9')
            return false;
        InstanceId = InstanceId * 10 + (*P - '0');
        if (InstanceId > 0x7FFFFFFF)
            return false;
    }
    if (InstanceId < 100000)
        return false;
    int Adapted = static_cast<int>(InstanceId);
    // 2024.2+ tags the upper byte so the runtime can disambiguate from plain
    // object ids. Use the CANONICAL AssetType (13); the serializer remaps it to
    // the on-disk id (14) via AdaptAssetTypeId — a raw 14 here matches no case
    // there and throws at emit.
    if (UsingRoomInstanceReferences()) {
        Adapted |= ((static_cast<int>(Underanalyzer::AssetType::RoomInstance) & 0x7f) << 24);
    }
    OutId = Adapted;
    return true;
}

bool GMSGameContext::GetScriptId(const std::string& ScriptName, int& AssetId) {
    auto It = _Data->ScriptByName.find(ScriptName);
    if (It == _Data->ScriptByName.end())
        return false;
    for (std::size_t i = 0; i < _Data->Scripts.size(); i++) {
        if (_Data->Scripts[i].get() == It->second) {
            AssetId = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

bool GMSGameContext::GetScriptIdByFunctionName(const std::string& FunctionName, int& AssetId) {
    Underanalyzer::IGMFunction* Func = nullptr;
    if (!_Globals.TryGetFunction(FunctionName, Func))
        return false;
    auto* F = static_cast<GMSFunction*>(Func);
    if (F->NameRef == nullptr)
        return false;
    return GetScriptId(F->NameRef->Content, AssetId);
}

} // namespace GMSLib
