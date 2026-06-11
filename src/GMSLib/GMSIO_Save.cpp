
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GMSLib/GMSData.h"
#include "GMSLib/GMSIO.h"
#include "GMSLib/Models/GMSCode.h"
#include "GMSLib/Models/GMSFunction.h"
#include "GMSLib/Models/GMSString.h"
#include "GMSLib/Models/GMSVariable.h"
#include "GMSLib/SaveBackend/Pools.h"
#include "Toolkit/IO.h"
#include "Toolkit/Log.h"

#include <cstdint>
#include <string>
#include <unordered_set>

namespace GMSLib {

namespace {

// The variable-reference word packs the VariableType into the top 5 bits of
// the high byte; the bottom 3 bits are part of the name-string-id field.
inline std::uint8_t RecoverVariableTypeByte(const std::vector<std::uint8_t>& Bytecode, std::size_t ByteOffset) {
    if (ByteOffset + 4 > Bytecode.size())
        return 0;
    std::uint8_t HiByte = Bytecode[ByteOffset + 3];
    return static_cast<std::uint8_t>(HiByte & 0xF8u);
}

} // namespace

int SaveToFile(const std::string& Path, GMSData& Data) {
    // Fast path: if nothing was appended or recompiled the original buffer is
    // still byte-identical to what should land on disk.
    bool AnyPending = Data.Strings.size() > Data.OriginalStringCount ||
                      Data.Variables.size() > Data.OriginalVariableCount ||
                      Data.Functions.size() > Data.OriginalFunctionCount;
    if (!AnyPending) {
        for (const auto& Up : Data.Code) {
            if (Up->PendingReplace) {
                AnyPending = true;
                break;
            }
        }
    }
    if (!AnyPending) {
        return Gmtoolkit::spew(Path.c_str(), Data.Buffer.data(), Data.Buffer.size());
    }

    GMSLib::SaveBackend::Pools Pools;
    if (Pools.adopt_from_gmsdata(Data) != 0)
        return -1;

    // Newly-introduced locals need to be registered with the save backend so
    // VARI/FUNC get matching entries; dedupe by name across this save pass.
    using IT = Underanalyzer::IGMInstruction::InstanceType;
    std::unordered_set<std::string> LocalsThisRun;
    for (std::size_t i = Data.OriginalVariableCount; i < Data.Variables.size(); i++) {
        const GMSVariable* V = Data.Variables[i].get();
        if (V == nullptr || V->NameRef == nullptr)
            continue;
        if (V->InstType == IT::Local) {
            if (LocalsThisRun.insert(V->NameRef->Content).second) {
                Pools.add_local_for_patch(V->NameRef->Content);
            }
        }
    }

    for (auto& Up : Data.Code) {
        GMSCode* Code = Up.get();
        if (!Code->PendingReplace)
            continue;
        if (Code->NameRef == nullptr)
            continue;
        Pools.intern_string(Code->NameRef->Content);

        GMSLib::SaveBackend::Pools::CodePatch P;
        P.entry_name = Code->NameRef->Content;
        P.bytecode = Code->PendingBytecode;
        P.locals_count = Code->LocalsCount;
        // Top bit of ArgumentsCount is a runtime "uses-arguments" flag in the
        // chunk; mask it off so the patch carries just the count.
        P.args_count = static_cast<std::uint16_t>(Code->ArgumentsCount & 0x7FFF);

        P.var_refs.reserve(Code->PendingVarRefs.size());
        for (const auto& Slot : Code->PendingVarRefs) {
            if (Slot.Target == nullptr || Slot.Target->NameRef == nullptr)
                continue;
            GMSLib::SaveBackend::Pools::CodePatch::VarRef VR;
            VR.byte_offset = Slot.ByteOffset;
            VR.name = Slot.Target->NameRef->Content;
            VR.inst_type = static_cast<std::int32_t>(Slot.Target->InstType);
            VR.var_type = RecoverVariableTypeByte(Code->PendingBytecode, Slot.ByteOffset);
            VR.target = static_cast<const void*>(Slot.Target);
            P.var_refs.push_back(std::move(VR));
        }

        P.func_refs.reserve(Code->PendingFuncRefs.size());
        for (const auto& Slot : Code->PendingFuncRefs) {
            if (Slot.Target == nullptr || Slot.Target->NameRef == nullptr)
                continue;
            GMSLib::SaveBackend::Pools::CodePatch::FuncRef FR;
            FR.byte_offset = Slot.ByteOffset;
            FR.name = Slot.Target->NameRef->Content;
            P.func_refs.push_back(std::move(FR));
        }

        P.children.reserve(Code->ChildEntries.size());
        for (GMSCode* ChildEntry : Code->ChildEntries) {
            if (ChildEntry->NameRef == nullptr)
                continue;
            GMSLib::SaveBackend::Pools::CodePatch::ChildEntry CE;
            CE.name = ChildEntry->NameRef->Content;
            CE.body_offset = ChildEntry->Offset;
            // Child body runs from its Offset to the end of the parent blob;
            // the next sibling's Offset (if any) cuts it shorter at write time.
            CE.body_length = Code->PendingBytecode.size() > ChildEntry->Offset
                                 ? Code->PendingBytecode.size() - ChildEntry->Offset
                                 : 0;
            CE.args_count = ChildEntry->ArgumentsCount & 0x7FFF;
            CE.locals_count = ChildEntry->LocalsCount;
            CE.is_wrapper_sub = false;
            auto LnIt = Code->PendingChildLocalNames.find(CE.name);
            if (LnIt != Code->PendingChildLocalNames.end()) {
                CE.local_names = LnIt->second;
            }
            P.children.push_back(std::move(CE));
        }

        P.local_names = Code->PendingLocalNames;

        Pools.add_code_patch(std::move(P));
    }

    // Detect new scripts by string-ID: anything whose name STRG-id is past the original count
    // was interned this run; PoolsCommit resolves the file-level CODE index by name.
    for (std::size_t i = 0; i < Data.Scripts.size(); i++) {
        const GMSScript* S = Data.Scripts[i].get();
        if (S == nullptr || S->NameRef == nullptr || S->CodeRef == nullptr)
            continue;
        if (static_cast<std::size_t>(S->NameRef->Id) < Data.OriginalStringCount)
            continue;
        Pools.intern_string(S->NameRef->Content);
        GMSLib::SaveBackend::Pools::ScptInsert SI;
        SI.name = S->NameRef->Content;
        SI.is_constructor = S->IsConstructor;
        Pools.pending_scpt_inserts.push_back(std::move(SI));
    }

    // Either branch returns Pools' adopted buffer to Data so a failed save
    // still leaves the in-memory state coherent for retry or inspection.
    if (Pools.commit(Path.c_str()) != 0) {
        Pools.return_to_gmsdata(Data);
        return -1;
    }

    Pools.return_to_gmsdata(Data);
    return 0;
}

} // namespace GMSLib
