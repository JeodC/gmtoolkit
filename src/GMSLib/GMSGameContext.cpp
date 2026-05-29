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
}

bool GMSGameContext::UsingGMS2OrLater() const {
    return _Data->IsVersionAtLeast(2);
}
bool GMSGameContext::UsingGMLv2() const {
    return _Data->IsVersionAtLeast(2, 3);
}
bool GMSGameContext::UsingStringRealOptimizations() const {
    return _Data->IsVersionAtLeast(2);
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
    return _Data->IsVersionAtLeast(2);
}
bool GMSGameContext::UsingLongCompoundBitwise() const {
    return _Data->IsVersionAtLeast(2, 3, 2);
}
// Inverted gates: these emit-an-extra-op behaviors were dropped in the listed
// versions, so the predicate is true for older targets.
bool GMSGameContext::UsingExtraRepeatInstruction() const {
    return !_Data->IsVersionAtLeast(2022, 11);
}
bool GMSGameContext::UsingFinallyBeforeThrow() const {
    return !_Data->IsVersionAtLeast(2024, 6);
}
bool GMSGameContext::UsingConstructorSetStatic() const {
    return _Data->IsVersionAtLeast(2024, 11);
}
bool GMSGameContext::UsingArrayCopyOnWrite() const {
    return !_Data->IsVersionAtLeast(2022, 2);
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

// Asset chunks (OBJT, SPRT, SOND, ...) share a layout: count, table of entry
// offsets, and entries whose first u32 points at a string-table name record.
static void _ParseAssetChunk(const std::uint8_t* B, std::size_t BufSize, std::size_t PayloadOff,
                             std::size_t PayloadSize, std::unordered_map<std::string, int>& Out) {
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
    auto Walk = [&](const char* Tag, std::unordered_map<std::string, int>& Out) {
        auto It = _Data->Chunks.find(Tag);
        if (It == _Data->Chunks.end())
            return;
        _ParseAssetChunk(_Data->Buffer.data(), _Data->Buffer.size(), It->second.PayloadOffset, It->second.PayloadSize,
                         Out);
    };
    Walk("OBJT", _AssetObj);
    Walk("SPRT", _AssetSpr);
    Walk("SOND", _AssetSnd);
    Walk("ROOM", _AssetRoom);
    Walk("BGND", _AssetBgnd);
    Walk("PATH", _AssetPath);
    Walk("FONT", _AssetFont);
    Walk("TMLN", _AssetTmln);
    Walk("SHDR", _AssetShdr);
    Walk("SEQN", _AssetSeqn);
    Walk("ACRV", _AssetAcrv);
    Walk("PSEM", _AssetPsem);
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
    // 2024.2+ encodes the AssetType tag (14 = RoomInstance) in the upper byte
    // of the id so the runtime can disambiguate from plain object ids.
    if (UsingRoomInstanceReferences()) {
        Adapted |= ((14 & 0x7f) << 24);
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
