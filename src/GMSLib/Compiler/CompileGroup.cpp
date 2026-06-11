
// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GMSLib/Compiler/CompileGroup.h"

#include "Toolkit/Log.h"
#include "GMSLib/Compiler/BytecodeSerializer.h"
#include "GMSLib/Compiler/CodeBuilder.h"
#include "GMSLib/GMSData.h"
#include "GMSLib/GMSGameContext.h"
#include "GMSLib/Models/GMSCode.h"
#include "GMSLib/Models/GMSFunction.h"
#include "GMSLib/Models/GMSGeneralInfo.h"
#include "GMSLib/Models/GMSInstruction.h"
#include "GMSLib/Models/GMSScript.h"
#include "GMSLib/Models/GMSString.h"
#include "GMSLib/Models/GMSVariable.h"
#include "Underanalyzer/Compiler/Bytecode/FunctionEntry.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Errors/ICompileError.h"
#include "Underanalyzer/Compiler/FunctionScope.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

namespace GMSLib {

// MurmurHash3 x86_32 (public domain). GameMaker derives anonymous function
// suffixes from this exact hash of the parent code-entry name, so the output
// has to be bit-identical to the runtime's expectation.
static std::uint32_t _MurmurScramble(std::uint32_t N) {
    N *= 0xcc9e2d51u;
    N = (N << 15) | (N >> 17);
    return N * 0x1b873593u;
}
static std::uint32_t MurmurHash32(const std::string& S, std::uint32_t Seed = 0) {
    std::uint32_t H = Seed;
    const auto* B = reinterpret_cast<const std::uint8_t*>(S.data());
    std::size_t Len = S.size();
    std::size_t Pos = 0;
    for (std::size_t I = Len >> 2; I > 0; I--) {
        std::uint32_t G = static_cast<std::uint32_t>(B[Pos]) | (static_cast<std::uint32_t>(B[Pos + 1]) << 8) |
                          (static_cast<std::uint32_t>(B[Pos + 2]) << 16) |
                          (static_cast<std::uint32_t>(B[Pos + 3]) << 24);
        H ^= _MurmurScramble(G);
        H = (H << 13) | (H >> 19);
        H = (H * 5u) + 0xe6546b64u;
        Pos += 4;
    }
    std::uint32_t EndGroup = 0;
    for (std::size_t I = Len & 3u; I > 0; I--) {
        EndGroup <<= 8;
        EndGroup |= B[Pos + (I - 1)];
    }
    H ^= _MurmurScramble(EndGroup);
    H ^= static_cast<std::uint32_t>(Len);
    H ^= H >> 16;
    H *= 0x85ebca6bu;
    H ^= H >> 13;
    H *= 0xc2b2ae35u;
    H ^= H >> 16;
    return H;
}

CompileGroup::CompileGroup(GMSGameContext& GameContextIn) : _GameContext(&GameContextIn), _Data(&GameContextIn.Data()) {
}

void CompileGroup::QueueCodeReplace(GMSCode* CodeToModify, std::string GmlCode) {
    if (CodeToModify == nullptr)
        throw std::invalid_argument("CodeToModify is null");
    _QueuedCodeReplacements.push_back({ CodeToModify, std::move(GmlCode) });
}

void CompileGroup::QueueCodeReplace(const std::string& CodeEntryName, std::string GmlCode) {
    if (CodeEntryName.empty())
        throw std::invalid_argument("CodeEntryName is empty");

    // Child entries don't own their bytecode (it lives inside the parent's
    // blob), so the import has to target the root entry instead.
    auto Existing = _Data->CodeByName.find(CodeEntryName);
    if (Existing != _Data->CodeByName.end()) {
        if (Existing->second->ParentEntry != nullptr)
            throw std::runtime_error("Cannot import code into a child code entry: " + CodeEntryName);
        QueueCodeReplace(Existing->second, std::move(GmlCode));
        return;
    }

    auto NewCodeUp = std::make_unique<GMSCode>();
    GMSCode* NewCode = NewCodeUp.get();
    NewCode->NameRef = MakeString(CodeEntryName, nullptr);
    _Data->CodeByName.try_emplace(CodeEntryName, NewCode);
    _Data->Code.push_back(std::move(NewCodeUp));
    QueueCodeReplace(NewCode, std::move(GmlCode));
}

// Lookups are built lazily and reused across all queued operations in one
// Compile() pass; PersistLinkingLookups can keep them alive for follow-up runs.
void CompileGroup::InitializeLinkingLookups() {
    if (_LinkingStringIdLookup.empty()) {
        _LinkingStringIdLookup.reserve(_Data->Strings.size());
        for (std::size_t i = 0; i < _Data->Strings.size(); i++) {
            _LinkingStringIdLookup[_Data->Strings[i]->Content] = static_cast<int>(i);
        }
    }
    if (_LinkingFunctionLookup.empty()) {
        _LinkingFunctionLookup.reserve(_Data->Functions.size());
        for (auto& Up : _Data->Functions) {
            if (Up->NameRef != nullptr)
                _LinkingFunctionLookup[Up->NameRef->Content] = Up.get();
        }
    }
    if (_LinkingScriptLookup.empty()) {
        _LinkingScriptLookup.reserve(_Data->Scripts.size());
        for (auto& Up : _Data->Scripts) {
            if (Up->NameRef != nullptr)
                _LinkingScriptLookup[Up->NameRef->Content].push_back(Up.get());
        }
    }
    // Seed the struct counter from existing ___struct___N names so that any
    // structs we emit pick up beyond the highest-numbered one already on disk.
    if (_LinkingStructCounter == -1) {
        _LinkingStructCounter = 0;
        const std::string Prefix = "___struct___";
        for (auto& Up : _Data->Variables) {
            if (Up->NameRef == nullptr)
                continue;
            const std::string& N = Up->NameRef->Content;
            if (N.size() > Prefix.size() && N.compare(0, Prefix.size(), Prefix) == 0) {
                try {
                    int Id = std::stoi(N.substr(Prefix.size()));
                    if (Id + 1 > _LinkingStructCounter)
                        _LinkingStructCounter = Id + 1;
                } catch (...) {}
            }
        }
    }
}

GMSString* CompileGroup::MakeString(const std::string& Content, int* OutId) {
    InitializeLinkingLookups();
    auto It = _LinkingStringIdLookup.find(Content);
    if (It != _LinkingStringIdLookup.end()) {
        if (OutId != nullptr)
            *OutId = It->second;
        return _Data->Strings[It->second].get();
    }
    auto Up = std::make_unique<GMSString>(Content);
    GMSString* Raw = Up.get();
    int NewId = static_cast<int>(_Data->Strings.size());
    Up->Id = NewId;
    _LinkingStringIdLookup[Content] = NewId;
    _Data->StringByContent.try_emplace(Content, Raw);
    _Data->Strings.push_back(std::move(Up));
    if (OutId != nullptr)
        *OutId = NewId;
    return Raw;
}

void CompileGroup::RegisterLocalVariable(GMSInstruction* Reference, const std::string& Name) {
    auto& List = _LinkingLocalReferences[Name];
    if (List.empty())
        List.reserve(16);
    List.push_back(Reference);

    auto It = _LinkingVariableOrderLookup.find(Name);
    if (It != _LinkingVariableOrderLookup.end()) {
        if (!_LinkingVariableOrder[It->second].second) {
            _LinkingVariableOrder[It->second].second = true;
        }
    } else {
        _LinkingVariableOrderLookup.emplace(Name, static_cast<int>(_LinkingVariableOrder.size()));
        _LinkingVariableOrder.emplace_back(Name, true);
    }
}

void CompileGroup::RegisterNonLocalVariable(const std::string& Name) {
    if (_LinkingVariableOrderLookup.find(Name) == _LinkingVariableOrderLookup.end()) {
        _LinkingVariableOrderLookup.emplace(Name, static_cast<int>(_LinkingVariableOrder.size()));
        _LinkingVariableOrder.emplace_back(Name, false);
    }
}

int CompileGroup::RegisterName(const std::string& Name) {
    auto It = _ParsedNameIds.find(Name);
    if (It != _ParsedNameIds.end())
        return It->second;
    return _ParsedNameIds[Name] = _NextNameId++;
}

int CompileGroup::NextTryVariableID() {
    return _NextTryVariableIndex++;
}

// try/catch desugars to a hidden __yy_breakEx<N> local. To keep recompiled
// scripts byte-stable, walk the existing locals in this script's bytecode
// range and start numbering from the lowest index already in use.
int CompileGroup::SeedTryVariableIndexFromScript(GMSCode* Script) const {
    constexpr std::string_view BreakPrefix = "__yy_breakEx";
    const std::size_t BlobStart = Script->BytecodeAbsoluteAddress;
    const std::size_t BlobEnd = BlobStart + Script->Length;
    int MinIdx = -1;
    for (auto& Up : _Data->Variables) {
        if (Up->InstType != Underanalyzer::IGMInstruction::InstanceType::Local)
            continue;
        if (Up->NameRef == nullptr)
            continue;
        if (Up->FirstAddress < BlobStart || Up->FirstAddress >= BlobEnd)
            continue;
        const std::string& Nm = Up->NameRef->Content;
        if (Nm.size() <= BreakPrefix.size())
            continue;
        if (Nm.compare(0, BreakPrefix.size(), BreakPrefix) != 0)
            continue;
        int Acc = 0;
        bool Ok = true;
        for (std::size_t I = BreakPrefix.size(); I < Nm.size(); I++) {
            char C = Nm[I];
            if (C < '0' || C > '9') {
                Ok = false;
                break;
            }
            Acc = Acc * 10 + (C - '0');
            if (Acc < 0) {
                Ok = false;
                break;
            }
        }
        if (!Ok)
            continue;
        if (MinIdx < 0 || Acc < MinIdx)
            MinIdx = Acc;
    }
    return MinIdx < 0 ? 0 : MinIdx;
}

GMSFunction* CompileGroup::EnsureFunctionDefined(const std::string& FunctionName) {
    InitializeLinkingLookups();
    auto It = _LinkingFunctionLookup.find(FunctionName);
    if (It != _LinkingFunctionLookup.end())
        return It->second;

    auto Up = std::make_unique<GMSFunction>();
    Up->NameRef = MakeString(FunctionName, &Up->NameStringID);
    GMSFunction* Raw = Up.get();
    _LinkingFunctionLookup[FunctionName] = Raw;
    _Data->FunctionByName.try_emplace(FunctionName, Raw);
    _Data->Functions.push_back(std::move(Up));
    return Raw;
}

// Code-entry names follow a fixed prefix convention; this map encodes the
// runtime's classification so the compiler can apply the right preamble.
static Underanalyzer::Compiler::CompileScriptKind GuessScriptKind(const std::string& CodeName,
                                                                  std::optional<std::string>& OutGlobalScriptName) {
    using K = Underanalyzer::Compiler::CompileScriptKind;
    static constexpr const char* GlobalPrefix = "gml_GlobalScript_";
    if (CodeName.rfind(GlobalPrefix, 0) == 0) {
        OutGlobalScriptName = CodeName.substr(std::strlen(GlobalPrefix));
        return K::GlobalScript;
    }
    if (CodeName.rfind("gml_Script", 0) == 0)
        return K::Script;
    if (CodeName.rfind("gml_Object", 0) == 0)
        return K::ObjectEvent;
    if (CodeName.rfind("gml_Room", 0) == 0)
        return K::RoomCreationCode;
    if (CodeName.rfind("Timeline", 0) == 0)
        return K::Timeline;
    return K::Script;
}

// Pattern match for "did this new entry come from the same source as that old
// entry, just with different generated numbers". '%' eats a run of digits and
// '#' eats a run of uppercase hex — these are the only varying parts of the
// runtime-generated suffixes (anon counters and the MurmurHash3 fingerprint).
bool CompileGroup::SimilarCodeEntryNames(const std::string& OriginalName, const std::string& NewNameNoNumbers) {
    std::size_t OriginalPos = 0;
    for (std::size_t i = 0; i < NewNameNoNumbers.size(); i++) {
        if (NewNameNoNumbers[i] == '%') {
            while (OriginalPos < OriginalName.size() &&
                   std::isdigit(static_cast<unsigned char>(OriginalName[OriginalPos]))) {
                OriginalPos++;
            }
            continue;
        }
        if (NewNameNoNumbers[i] == '#') {
            while (OriginalPos < OriginalName.size()) {
                char C = OriginalName[OriginalPos];
                bool IsHexUpper = (C >= '0' && C <= '9') || (C >= 'A' && C <= 'F');
                if (!IsHexUpper)
                    break;
                OriginalPos++;
            }
            continue;
        }
        if (OriginalPos >= OriginalName.size())
            return false;
        if (OriginalName[OriginalPos] != NewNameNoNumbers[i])
            return false;
        OriginalPos++;
    }
    return OriginalPos == OriginalName.size();
}

// Mirror of SimilarCodeEntryNames that also captures the slice of the original
// name that was matched, so we can reuse its exact short name (numbers and all).
std::string CompileGroup::FindOriginalShortName(const std::string& OriginalName,
                                                const std::string& NewShortNameNoNumbers) {
    const std::string Prefix = "gml_Script_";
    std::size_t StartOriginalPos = Prefix.size();
    std::size_t OriginalPos = StartOriginalPos;
    for (std::size_t i = 0; i < NewShortNameNoNumbers.size(); i++) {
        if (NewShortNameNoNumbers[i] == '%') {
            while (OriginalPos < OriginalName.size() &&
                   std::isdigit(static_cast<unsigned char>(OriginalName[OriginalPos]))) {
                OriginalPos++;
            }
            continue;
        }
        if (NewShortNameNoNumbers[i] == '#') {
            while (OriginalPos < OriginalName.size()) {
                char C = OriginalName[OriginalPos];
                bool IsHexUpper = (C >= '0' && C <= '9') || (C >= 'A' && C <= 'F');
                if (!IsHexUpper)
                    break;
                OriginalPos++;
            }
            continue;
        }
        if (OriginalPos < OriginalName.size())
            OriginalPos++;
    }
    return OriginalName.substr(StartOriginalPos, OriginalPos - StartOriginalPos);
}

// Pairs each freshly-compiled sub-function with an existing child code entry
// (by name pattern) whenever possible, so re-importing a script keeps function
// identity stable for save-game/state references. New entries get queued for
// CommitChildEntries to splice in below the parent.
std::vector<CompileGroup::ChildCodeEntryData>
CompileGroup::ResolveFunctionEntries(GMSCode* RootCode, const std::string& RootCodeName,
                                     Underanalyzer::Compiler::CompileScriptKind ScriptKindIn,
                                     const std::vector<Underanalyzer::Compiler::Bytecode::FunctionEntry*>& Entries,
                                     std::unordered_map<std::string, CodeEntryNameGroup>& OutRemainingChildren) {
    using SK = Underanalyzer::Compiler::CompileScriptKind;
    using FK = Underanalyzer::Compiler::Bytecode::FunctionEntryKind;

    // 2023.11 switched anonymous suffix style from "_HASH_N" to "@N@parent";
    // 2024.4 fixed child function names to use '@' separators; 2024.2 added
    // static-binding-aware anonymous naming.
    bool NewNamingProcess = _Data->IsVersionAtLeast(2023, 11);
    bool ChildFunctionNameFix = _Data->IsVersionAtLeast(2024, 4);
    bool StaticAnonymousNames = _Data->IsVersionAtLeast(2024, 2);

    std::vector<std::string> OriginalChildEntryNames;
    OriginalChildEntryNames.reserve(RootCode->ChildEntries.size());
    OutRemainingChildren.clear();
    OutRemainingChildren.reserve(RootCode->ChildEntries.size());
    for (GMSCode* ChildEntry : RootCode->ChildEntries) {
        if (ChildEntry->NameRef == nullptr)
            continue;
        const std::string& CurrentChildName = ChildEntry->NameRef->Content;
        OriginalChildEntryNames.push_back(CurrentChildName);
        OutRemainingChildren[CurrentChildName].RemainingOriginalEntries.push_back(ChildEntry);
    }

    std::vector<ChildCodeEntryData> ChildDataList;
    ChildDataList.reserve(Entries.size());
    std::unordered_set<std::string> UsedChildEntryNames;
    UsedChildEntryNames.reserve(RootCode->ChildEntries.size());

    std::string RootWithoutGlobalScript = RootCodeName;
    {
        const std::string Prefix = "gml_GlobalScript_";
        if (RootCodeName.rfind(Prefix, 0) == 0) {
            RootWithoutGlobalScript = RootCodeName.substr(Prefix.size());
        }
    }

    int AnonCounter = 0;
    for (auto* FE : Entries) {

        std::string ParentFunctionName;
        bool HaveParent = false;
        if (FE->Parent() != nullptr && FE->Parent()->ChildFunctionName().has_value()) {
            ParentFunctionName = *FE->Parent()->ChildFunctionName();
            HaveParent = true;
        }
        if (!HaveParent && ScriptKindIn != SK::GlobalScript) {
            ParentFunctionName = RootCodeName;
            HaveParent = true;
        }

        std::string ShortName;
        std::string ShortNameNoNumbers;
        if (FE->FunctionName().has_value()) {
            ShortName = *FE->FunctionName();
            ShortNameNoNumbers = ShortName;
        } else if (FE->Kind() == FK::StructInstantiation) {
            ShortName = "___struct___" + std::to_string(_LinkingStructCounter);
            ShortNameNoNumbers = "___struct___%";
        } else {

            std::string AnonPrefix = "anon";
            if (StaticAnonymousNames && FE->StaticVariableName().has_value()) {
                AnonPrefix = *FE->StaticVariableName() + "@anon";
            }
            if (!HaveParent) {
                if (NewNamingProcess) {
                    ShortName = AnonPrefix + "@" + std::to_string(AnonCounter++) + "@" + RootWithoutGlobalScript;
                    ShortNameNoNumbers = AnonPrefix + "@%@" + RootWithoutGlobalScript;
                } else {
                    char Buf[16];
                    std::snprintf(Buf, sizeof(Buf), "%08X", static_cast<unsigned>(_CurrentCodeEntryNameHash));
                    ShortName = AnonPrefix + "_" + Buf + "_" + std::to_string(AnonCounter++);
                    ShortNameNoNumbers = AnonPrefix + "_#_%";
                }
            } else {
                if (NewNamingProcess) {
                    ShortName = AnonPrefix + "@" + std::to_string(AnonCounter++);
                    ShortNameNoNumbers = AnonPrefix + "@%";
                } else {
                    ShortName = AnonPrefix + "_" + ParentFunctionName + "_" + std::to_string(AnonCounter++);
                    ShortNameNoNumbers = AnonPrefix + "_" + ParentFunctionName + "_%";
                }
            }
        }

        std::string CodeEntryName;
        std::string CodeEntryNameNoNumbers;
        if (!HaveParent) {
            CodeEntryName = "gml_Script_" + ShortName;
            CodeEntryNameNoNumbers = "gml_Script_" + ShortNameNoNumbers;
        } else if (NewNamingProcess) {
            std::string ParentWithoutGlobalScript = ParentFunctionName;
            const std::string Prefix = "gml_GlobalScript_";
            if (ParentFunctionName.rfind(Prefix, 0) == 0) {
                ParentWithoutGlobalScript = ParentFunctionName.substr(Prefix.size());
            }
            CodeEntryName = "gml_Script_" + ShortName + "@" + ParentWithoutGlobalScript;
            CodeEntryNameNoNumbers = "gml_Script_" + ShortNameNoNumbers + "@" + ParentWithoutGlobalScript;
        } else {
            CodeEntryName = "gml_Script_" + ShortName + "_" + ParentFunctionName;
            CodeEntryNameNoNumbers = "gml_Script_" + ShortNameNoNumbers + "_" + ParentFunctionName;
        }

        // Try to graft onto an existing child entry whose name fits the same
        // pattern; if found, inherit its exact name/short-name and reuse its
        // GMSCode / GMSScript / GMSFunction triple so external refs survive.
        GMSCode* ExistingCodeEntry = nullptr;
        GMSCode* NewCodeEntry = nullptr;
        GMSScript* ExistingScript = nullptr;
        GMSScript* NewScript = nullptr;
        GMSFunction* ExistingFunction = nullptr;
        GMSFunction* NewFunction = nullptr;
        for (const std::string& OriginalEntryName : OriginalChildEntryNames) {
            auto It = OutRemainingChildren.find(OriginalEntryName);
            if (It == OutRemainingChildren.end())
                continue;
            if (!SimilarCodeEntryNames(OriginalEntryName, CodeEntryNameNoNumbers))
                continue;

            CodeEntryName = OriginalEntryName;
            ShortName = FindOriginalShortName(OriginalEntryName, ShortNameNoNumbers);

            CodeEntryNameGroup& OriginalCodeEntries = It->second;
            ExistingCodeEntry = NewCodeEntry = OriginalCodeEntries.RemainingOriginalEntries.front();
            OriginalCodeEntries.RemainingOriginalEntries.pop_front();

            auto SLI = _LinkingScriptLookup.find(OriginalEntryName);
            if (SLI != _LinkingScriptLookup.end() &&
                OriginalCodeEntries.EntriesUsed < static_cast<int>(SLI->second.size())) {
                ExistingScript = NewScript = SLI->second[OriginalCodeEntries.EntriesUsed];
            }
            InitializeLinkingLookups();
            auto FLI = _LinkingFunctionLookup.find(OriginalEntryName);
            if (FLI != _LinkingFunctionLookup.end()) {
                ExistingFunction = NewFunction = FLI->second;
            }

            if (OriginalCodeEntries.RemainingOriginalEntries.empty()) {
                OutRemainingChildren.erase(It);
            } else {
                OriginalCodeEntries.EntriesUsed++;
            }
            break;
        }

        if (ExistingCodeEntry == nullptr) {
            // Two same-name children within one compile (e.g. duplicated nested
            // anon funcs) get disambiguated with an "_N" suffix.
            if (UsedChildEntryNames.count(CodeEntryName) != 0) {
                int SuffixId = 0;
                std::string Base = CodeEntryName;
                do {
                    CodeEntryName = Base + "_" + std::to_string(SuffixId++);
                } while (UsedChildEntryNames.count(CodeEntryName) != 0);
            }
            auto NewCodeUp = std::make_unique<GMSCode>();
            NewCodeEntry = NewCodeUp.get();
            NewCodeEntry->NameRef = MakeString(CodeEntryName, nullptr);
            NewCodeEntry->ParentEntry = RootCode;
            _PendingChildCodeOwnership.push_back(std::move(NewCodeUp));
        }
        if (ExistingScript == nullptr) {
            auto NewScriptUp = std::make_unique<GMSScript>();
            NewScript = NewScriptUp.get();
            NewScript->NameRef = MakeString(CodeEntryName, nullptr);
            NewScript->CodeRef = NewCodeEntry;
            _PendingChildScriptOwnership.push_back(std::move(NewScriptUp));
        }
        if (ExistingFunction == nullptr) {
            NewFunction = EnsureFunctionDefined(CodeEntryName);
        }

        std::string ChildFunctionName = ChildFunctionNameFix
                                            ? ShortName + "@" + (HaveParent ? ParentFunctionName : RootCodeName)
                                            : ShortName + "_" + (HaveParent ? ParentFunctionName : RootCodeName);

        FE->ResolveFunction(NewFunction, ChildFunctionName);

        if (FE->Kind() == FK::StructInstantiation) {
            FE->ResolveStructName(ShortName);
            if (ExistingCodeEntry == nullptr) {
                _LinkingStructCounter++;
            }
        }

        UsedChildEntryNames.insert(CodeEntryName);
        ChildDataList.push_back({
            CodeEntryName,
            FE,
            NewCodeEntry,
            NewScript,
            NewFunction,
            ExistingCodeEntry != nullptr,
            ExistingScript != nullptr,
            ExistingFunction != nullptr,
        });
    }
    return ChildDataList;
}

// Apply the resolution decisions to the live data: prune any old child code
// entries the new compile didn't pair up, then splice the freshly-allocated
// children into place directly after the parent so on-disk ordering matches.
void CompileGroup::CommitChildEntries(GMSCode* RootCode,
                                      std::unordered_map<std::string, CodeEntryNameGroup>& RemainingChildren,
                                      std::vector<ChildCodeEntryData>& ChildData, int OutputLength) {

    for (auto& [Name, Group] : RemainingChildren) {
        while (!Group.RemainingOriginalEntries.empty()) {
            GMSCode* OrphanCode = Group.RemainingOriginalEntries.front();
            Group.RemainingOriginalEntries.pop_front();

            for (auto It = _Data->Code.begin(); It != _Data->Code.end(); ++It) {
                if (It->get() == OrphanCode) {
                    _Data->Code.erase(It);
                    break;
                }
            }

            for (auto It = RootCode->ChildEntries.begin(); It != RootCode->ChildEntries.end(); ++It) {
                if (*It == OrphanCode) {
                    RootCode->ChildEntries.erase(It);
                    break;
                }
            }

            auto CBN = _Data->CodeByName.find(Name);
            if (CBN != _Data->CodeByName.end() && CBN->second == OrphanCode)
                _Data->CodeByName.erase(CBN);

            auto SLI = _LinkingScriptLookup.find(Name);
            if (SLI != _LinkingScriptLookup.end() && Group.EntriesUsed < static_cast<int>(SLI->second.size())) {
                GMSScript* OrphanScript = SLI->second[Group.EntriesUsed];
                SLI->second.erase(SLI->second.begin() + Group.EntriesUsed);
                if (SLI->second.empty())
                    _LinkingScriptLookup.erase(SLI);

                for (auto It = _Data->Scripts.begin(); It != _Data->Scripts.end(); ++It) {
                    if (It->get() == OrphanScript) {
                        _Data->Scripts.erase(It);
                        break;
                    }
                }
                auto SBN = _Data->ScriptByName.find(Name);
                if (SBN != _Data->ScriptByName.end() && SBN->second == OrphanScript)
                    _Data->ScriptByName.erase(SBN);
            }
        }
        // Keep the orphan FUNC entry. Other bytecode in the game may still call this child by name;
        // removing the function from FUNC makes the runtime fail its by-name lookup. The body has
        // already been neutralised by PoolsCommit.
    }

    int ChildIndex = 0;
    auto OwnedCodeIt = _PendingChildCodeOwnership.begin();
    auto OwnedScriptIt = _PendingChildScriptOwnership.begin();
    int RootCodeEntryIndex = -1;
    for (ChildCodeEntryData& Cd : ChildData) {
        Cd.Code->Length = static_cast<std::uint32_t>(OutputLength);
        Cd.Code->Offset = static_cast<std::uint32_t>(Cd.FunctionEntry->BytecodeOffset());
        Cd.Code->ArgumentsCount = static_cast<std::uint16_t>(Cd.FunctionEntry->ArgumentCount());
        Cd.Code->LocalsCount = static_cast<std::uint16_t>(Cd.FunctionEntry->Scope()->LocalCount());

        Cd.Script->IsConstructor = Cd.FunctionEntry->IsConstructor();

        if (!Cd.ExistingCode) {
            auto It = std::find_if(OwnedCodeIt, _PendingChildCodeOwnership.end(),
                                   [&](const std::unique_ptr<GMSCode>& Up) { return Up.get() == Cd.Code; });
            if (It == _PendingChildCodeOwnership.end())
                throw std::runtime_error("Orphan child code missing from ownership list");

            RootCode->ChildEntries.insert(RootCode->ChildEntries.begin() + ChildIndex, Cd.Code);
            if (RootCodeEntryIndex == -1) {
                for (std::size_t i = 0; i < _Data->Code.size(); i++) {
                    if (_Data->Code[i].get() == RootCode) {
                        RootCodeEntryIndex = static_cast<int>(i);
                        break;
                    }
                }
            }
            _Data->Code.insert(_Data->Code.begin() + RootCodeEntryIndex + ChildIndex + 1, std::move(*It));
            _PendingChildCodeOwnership.erase(It);
            OwnedCodeIt = _PendingChildCodeOwnership.begin();
            if (Cd.Code->NameRef != nullptr)
                _Data->CodeByName.try_emplace(Cd.Code->NameRef->Content, Cd.Code);
        }

        if (!Cd.ExistingScript) {
            auto It = std::find_if(OwnedScriptIt, _PendingChildScriptOwnership.end(),
                                   [&](const std::unique_ptr<GMSScript>& Up) { return Up.get() == Cd.Script; });
            if (It == _PendingChildScriptOwnership.end())
                throw std::runtime_error("Orphan child script missing from ownership list");

            _LinkingScriptLookup[Cd.Name].push_back(Cd.Script);
            _Data->Scripts.push_back(std::move(*It));
            _PendingChildScriptOwnership.erase(It);
            OwnedScriptIt = _PendingChildScriptOwnership.begin();
            if (Cd.Script->NameRef != nullptr)
                _Data->ScriptByName.try_emplace(Cd.Script->NameRef->Content, Cd.Script);
        }

        if (!Cd.ExistingFunction) {
            _LinkingFunctionLookup[Cd.Name] = Cd.Function;
        }

        ChildIndex++;
    }
}

CompileResult CompileGroup::Compile() {
    std::vector<CompileError> Errors;

    // Fresh lookups by default — the underlying GMSData may have changed since
    // the last Compile(); callers batching imports flip this to keep the maps.
    if (!PersistLinkingLookups) {
        _LinkingStringIdLookup.clear();
        _LinkingFunctionLookup.clear();
        _LinkingScriptLookup.clear();
    }

    auto& Builder = static_cast<CodeBuilder&>(_GameContext->CodeBuilder());
    Builder.SetCurrentGroup(this);

    // Two-phase compile: first pre-parse every global script so the second
    // pass can resolve forward references to functions they introduce.
    std::vector<std::unique_ptr<Underanalyzer::Compiler::CompileContext>> PreParsedContexts(
        _QueuedCodeReplacements.size());

    for (std::size_t I = 0; I < _QueuedCodeReplacements.size(); I++) {
        auto& Op = _QueuedCodeReplacements[I];
        std::optional<std::string> GlobalScriptName;
        const std::string& CodeName = Op.CodeEntry->NameRef != nullptr ? Op.CodeEntry->NameRef->Content : std::string{};
        auto ScriptKind = GuessScriptKind(CodeName, GlobalScriptName);
        if (ScriptKind != Underanalyzer::Compiler::CompileScriptKind::GlobalScript)
            continue;

        auto Context = std::make_unique<Underanalyzer::Compiler::CompileContext>(Op.GmlCode, ScriptKind,
                                                                                 GlobalScriptName, *_GameContext);
        Builder.SetCurrentArena(&Context->Arena());
        try {
            Context->Parse();
        } catch (const std::exception& E) {
            Errors.emplace_back(Op.CodeEntry, std::string("Pre-parse exception: ") + E.what());
            Builder.SetCurrentArena(nullptr);
            continue;
        }
        if (Context->HasErrors()) {
            for (const auto& Err : Context->Errors()) {
                Errors.emplace_back(Op.CodeEntry, Err->GenerateMessage());
            }
            Builder.SetCurrentArena(nullptr);
            continue;
        }
        PreParsedContexts[I] = std::move(Context);
    }
    Builder.SetCurrentArena(nullptr);

    // Track every global-fn name we (re)bind so that if a later op fails we
    // can roll the bindings back and leave GlobalFunctions in its prior state.
    struct NewlyDefinedGlobalFn {
        std::string Name;
        Underanalyzer::IGMFunction* NewFunction;
        Underanalyzer::IGMFunction* OldFunction;
    };
    std::vector<NewlyDefinedGlobalFn> NewlyDefinedGlobals;
    {
        bool LookupsReady = false;
        auto& Globals = _GameContext->GlobalFunctions();
        for (auto& ContextPtr : PreParsedContexts) {
            if (!ContextPtr)
                continue;
            const auto* Names = ContextPtr->OutputGlobalFunctionNames();
            if (Names == nullptr || Names->empty())
                continue;
            if (!LookupsReady) {
                MainThreadAction([this]() { InitializeLinkingLookups(); });
                LookupsReady = true;
            }
            for (const std::string& FunctionName : *Names) {
                Underanalyzer::IGMFunction* OldFn = nullptr;
                Globals.TryGetFunction(FunctionName, OldFn);
                const std::string EntryName = "gml_Script_" + FunctionName;
                GMSFunction* NewFn = nullptr;
                auto It = _LinkingFunctionLookup.find(EntryName);
                if (It != _LinkingFunctionLookup.end()) {
                    NewFn = It->second;
                } else {
                    NewFn = EnsureFunctionDefined(EntryName);
                }
                Globals.DefineFunction(FunctionName, NewFn);
                NewlyDefinedGlobals.push_back({ FunctionName, NewFn, OldFn });
            }
        }
    }

    for (std::size_t OpIdx = 0; OpIdx < _QueuedCodeReplacements.size(); OpIdx++) {
        auto& Op = _QueuedCodeReplacements[OpIdx];
        std::optional<std::string> GlobalScriptName;
        const std::string& CodeName = Op.CodeEntry->NameRef != nullptr ? Op.CodeEntry->NameRef->Content : std::string{};
        auto ScriptKind = GuessScriptKind(CodeName, GlobalScriptName);

        // Per-script state reset. The name-id allocator restarts at 100000 on
        // pre-2022.2 (copy-on-write arrays) targets so each script gets fresh
        // ids and array-owner hashes stay deterministic.
        _CurrentCodeEntryNameHash = static_cast<int>(MurmurHash32(CodeName));
        _NextTryVariableIndex = SeedTryVariableIndexFromScript(Op.CodeEntry);
        if (_GameContext->UsingArrayCopyOnWrite()) {
            _ParsedNameIds.clear();
            _NextNameId = 100000;
        }
        _LinkingVariableOrder.clear();
        _LinkingVariableOrderLookup.clear();
        _LinkingLocalReferences.clear();

        std::unique_ptr<Underanalyzer::Compiler::CompileContext> FreshContext;
        Underanalyzer::Compiler::CompileContext* ContextPtr = PreParsedContexts[OpIdx].get();
        if (ContextPtr == nullptr) {
            FreshContext = std::make_unique<Underanalyzer::Compiler::CompileContext>(Op.GmlCode, ScriptKind,
                                                                                     GlobalScriptName, *_GameContext);
            ContextPtr = FreshContext.get();
        }
        Underanalyzer::Compiler::CompileContext& Context = *ContextPtr;
        Builder.SetCurrentArena(&Context.Arena());
        try {
            Context.Compile();
        } catch (const std::exception& E) {
            Errors.emplace_back(Op.CodeEntry, std::string("Compile exception: ") + E.what());
            Builder.SetCurrentArena(nullptr);
            continue;
        }

        if (Context.HasErrors()) {
            for (const auto& Err : Context.Errors()) {
                Errors.emplace_back(Op.CodeEntry, Err->GenerateMessage());
            }
            Builder.SetCurrentArena(nullptr);
            continue;
        }

        if (!Errors.empty()) {
            Builder.SetCurrentArena(nullptr);
            continue;
        }

        MainThreadAction([this]() { InitializeLinkingLookups(); });
        std::unordered_map<std::string, CodeEntryNameGroup> RemainingChildren;
        std::vector<ChildCodeEntryData> ChildData;
        try {
            ChildData = ResolveFunctionEntries(Op.CodeEntry, CodeName, ScriptKind, Context.OutputFunctionEntries(),
                                               RemainingChildren);
        } catch (const std::exception& E) {
            Errors.emplace_back(Op.CodeEntry, std::string("Function-entry resolution exception: ") + E.what());
            Builder.SetCurrentArena(nullptr);
            continue;
        }
        try {
            Context.Link();
        } catch (const std::exception& E) {
            Errors.emplace_back(Op.CodeEntry, std::string("Link exception: ") + E.what());
            Builder.SetCurrentArena(nullptr);
            continue;
        }

        // Locals whose FirstAddress falls inside this script's original blob
        // are reused (instead of being orphaned) when the recompile produces a
        // local with the same name and VarID — keeps the VARI table compact.
        std::unordered_set<GMSVariable*> OriginalReferencedLocals;
        {
            const std::size_t BlobStart = Op.CodeEntry->BytecodeAbsoluteAddress;
            const std::size_t BlobEnd = BlobStart + Op.CodeEntry->Length;
            for (auto& Up : _Data->Variables) {
                if (Up->InstType != Underanalyzer::IGMInstruction::InstanceType::Local)
                    continue;
                if (Up->FirstAddress >= BlobStart && Up->FirstAddress < BlobEnd) {
                    OriginalReferencedLocals.insert(Up.get());
                }
            }
        }

        // Walk declared variables in source order, picking out the locals.
        // VarID scheme changed in 2.3: it became the name's string id rather
        // than a monotonically-increasing per-script counter.
        std::size_t LocalsOrderCount = 0;
        std::vector<std::string> LinkedLocalNames;
        for (auto& [Name, IsEverLocal] : _LinkingVariableOrder) {
            if (!IsEverLocal)
                continue;
            int StringId = 0;
            GMSString* NameStr = MakeString(Name, &StringId);
            LocalsOrderCount++;
            LinkedLocalNames.push_back(Name);
            int VarId = _Data->IsVersionAtLeast(2, 3) ? StringId : static_cast<int>(LocalsOrderCount);

            GMSVariable* Variable = nullptr;
            for (GMSVariable* Ref : OriginalReferencedLocals) {
                if (Ref->NameRef == NameStr && Ref->VarID == VarId) {
                    Variable = Ref;
                    break;
                }
            }
            if (Variable == nullptr) {
                auto Up = std::make_unique<GMSVariable>();
                Up->NameRef = NameStr;
                Up->InstType = Underanalyzer::IGMInstruction::InstanceType::Local;
                Up->VarID = VarId;
                Up->NameStringID = StringId;
                Variable = Up.get();
                _Data->Variables.push_back(std::move(Up));
            }

            auto It = _LinkingLocalReferences.find(Name);
            if (It != _LinkingLocalReferences.end()) {
                for (GMSInstruction* I : It->second) {
                    I->ValueVariableRef = Variable;
                }
            }
        }

        // LocalsCount includes a hidden "arguments" slot, so it's always
        // LocalCount() + 1; ArgumentsCount is zero for root entries (only
        // sub-functions accept named arguments).
        Op.CodeEntry->Length = static_cast<std::uint32_t>(Context.OutputLength());
        Op.CodeEntry->LocalsCount = static_cast<std::uint16_t>(1 + Context.OutputRootScope()->LocalCount());
        Op.CodeEntry->ArgumentsCount = 0;

        bool Bc14 = _Data->GeneralInfo.BytecodeVersion <= 14;
        Op.CodeEntry->PendingReplace = true;
        BytecodeSerializer::Serialize(Context.OutputInstructions(), *_Data, Bc14, Op.CodeEntry->PendingBytecode,
                                      Op.CodeEntry->PendingVarRefs, Op.CodeEntry->PendingFuncRefs,
                                      Op.CodeEntry->PendingStringRefs);

        try {
            CommitChildEntries(Op.CodeEntry, RemainingChildren, ChildData, Context.OutputLength());
        } catch (const std::exception& E) {
            Errors.emplace_back(Op.CodeEntry, std::string("Child commit exception: ") + E.what());
            Builder.SetCurrentArena(nullptr);
            continue;
        }

        Op.CodeEntry->PendingChildLocalsCounts.clear();
        Op.CodeEntry->PendingChildArgumentsCounts.clear();
        Op.CodeEntry->PendingChildLocalNames.clear();
        for (const ChildCodeEntryData& Cd : ChildData) {
            Op.CodeEntry->PendingChildLocalsCounts.push_back(
                static_cast<std::uint16_t>(Cd.FunctionEntry->Scope()->LocalCount()));
            Op.CodeEntry->PendingChildArgumentsCounts.push_back(
                static_cast<std::uint16_t>(Cd.FunctionEntry->ArgumentCount()));
            Op.CodeEntry->PendingChildLocalNames[Cd.Name] = Cd.FunctionEntry->Scope()->LocalsOrder();
        }

        // CodeLocals must mirror the SAME order that assigned local VarIDs above
        // (first-reference linking order, ever-local only). Declaration order
        // desynchronizes pre-2.3 CodeLocals indices from VARI VarIDs whenever a
        // local is declared but unreferenced or referenced out of order.
        Op.CodeEntry->PendingLocalNames = std::move(LinkedLocalNames);

        Builder.SetCurrentArena(nullptr);
    }

    Builder.SetCurrentGroup(nullptr);

    _QueuedCodeReplacements.clear();

    // Roll back global-fn rebindings in reverse order so any name we
    // shadowed earlier in this pass is restored to its prior binding.
    if (!Errors.empty() && !NewlyDefinedGlobals.empty()) {
        auto& Globals = _GameContext->GlobalFunctions();
        for (auto It = NewlyDefinedGlobals.rbegin(); It != NewlyDefinedGlobals.rend(); ++It) {
            Globals.UndefineFunction(It->Name, It->NewFunction);
            if (It->OldFunction != nullptr) {
                Globals.DefineFunction(It->Name, It->OldFunction);
            }
        }
    }

    if (!Errors.empty())
        return CompileResult::Failed(std::move(Errors));
    return CompileResult::SuccessfulResult();
}

} // namespace GMSLib
