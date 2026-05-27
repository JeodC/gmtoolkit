
// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GMSLib/Compiler/CodeBuilder.h"

#include "GMSLib/Compiler/BuiltinList.h"
#include "GMSLib/Compiler/CompileGroup.h"
#include "GMSLib/GlobalFunctions.h"
#include "GMSLib/GMSData.h"
#include "GMSLib/GMSGameContext.h"
#include "GMSLib/Models/GMSFunction.h"
#include "GMSLib/Models/GMSInstruction.h"
#include "GMSLib/Models/GMSString.h"
#include "GMSLib/Models/GMSVariable.h"
#include "Underanalyzer/Compiler/Bytecode/FunctionEntry.h"
#include "Underanalyzer/Compiler/CompilerException.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/IBuiltins.h"
#include "Underanalyzer/Compiler/NodeArena.h"

#include <cstdint>
#include <stdexcept>

namespace GMSLib {

using IT = Underanalyzer::IGMInstruction::InstanceType;
using Op = Underanalyzer::IGMInstruction::Opcode;
using ExtOp = Underanalyzer::IGMInstruction::ExtendedOpcode;
using DT = Underanalyzer::IGMInstruction::DataType;
using VT = Underanalyzer::IGMInstruction::VariableType;

// Build a (name, instance-type) -> variable map up front so PatchInstruction
// can reuse existing VARI entries instead of duplicating them.
CodeBuilder::CodeBuilder(GMSGameContext& GameContext, GMSData& Data, GlobalFunctions& Globals)
    : _GameContext(&GameContext), _Data(&Data), _Globals(&Globals) {
    _VariableLookup.reserve(_Data->Variables.size());
    for (auto& Up : _Data->Variables) {
        if (Up->NameRef != nullptr) {
            _VariableLookup.try_emplace(std::make_pair(Up->NameRef->Content, static_cast<std::int16_t>(Up->InstType)),
                                        Up.get());
        }
    }
}

// Final normalization before a variable goes into the VARI table: builtins are
// always stored as Self, and bytecode 14 had no instance-type field at all.
static Underanalyzer::IGMInstruction::InstanceType _CalculateInstType(Underanalyzer::IGMInstruction::InstanceType Inst,
                                                                      bool IsBuiltin, bool Bytecode14) {
    using IT = Underanalyzer::IGMInstruction::InstanceType;
    if (Inst == IT::Local) {
        throw Underanalyzer::Compiler::CompilerException(
            "CodeBuilder: Local variables must go through DefineLocal path");
    }
    if (IsBuiltin)
        Inst = IT::Self;
    if (Bytecode14)
        Inst = static_cast<IT>(0);
    return Inst;
}

// Bytecode 14 only had a single Push opcode; the specialized variants
// introduced in 15+ collapse back to plain Push when emitting for an older
// target so disassembly/runtimes that predate them still load.
Op CodeBuilder::MapOpcode(Op Opcode) const {
    if (_Data->GeneralInfo.BytecodeVersion > 14)
        return Opcode;
    switch (Opcode) {
        case Op::PushBuiltin:
        case Op::PushLocal:
        case Op::PushGlobal:
        case Op::PushImmediate:
            return Op::Push;
        default:
            return Opcode;
    }
}

Underanalyzer::IGMInstruction* CodeBuilder::CreateInstruction(int, Op Opcode) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = MapOpcode(Opcode);
    return I;
}

Underanalyzer::IGMInstruction* CodeBuilder::CreateInstruction(int, Op Opcode, DT DataType) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = MapOpcode(Opcode);
    I->Type1Value = DataType;
    return I;
}

Underanalyzer::IGMInstruction* CodeBuilder::CreateInstruction(int, Op Opcode, DT DataType1, DT DataType2) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = MapOpcode(Opcode);
    I->Type1Value = DataType1;
    I->Type2Value = DataType2;
    return I;
}

Underanalyzer::IGMInstruction* CodeBuilder::CreateInstruction(int, Op Opcode, std::int16_t Value, DT DataType1,
                                                              DT DataType2) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = MapOpcode(Opcode);
    I->Type1Value = DataType1;
    I->Type2Value = DataType2;
    I->ValueShortVal = Value;
    return I;
}

Underanalyzer::IGMInstruction* CodeBuilder::CreateInstruction(int, Op Opcode, std::int32_t Value, DT DataType1,
                                                              DT DataType2) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = MapOpcode(Opcode);
    I->Type1Value = DataType1;
    I->Type2Value = DataType2;
    I->ValueIntVal = Value;
    return I;
}

Underanalyzer::IGMInstruction* CodeBuilder::CreateInstruction(int, Op Opcode, std::int64_t Value, DT DataType1,
                                                              DT DataType2) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = MapOpcode(Opcode);
    I->Type1Value = DataType1;
    I->Type2Value = DataType2;
    I->ValueLongVal = Value;
    return I;
}

Underanalyzer::IGMInstruction* CodeBuilder::CreateInstruction(int, Op Opcode, double Value, DT DataType1,
                                                              DT DataType2) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = MapOpcode(Opcode);
    I->Type1Value = DataType1;
    I->Type2Value = DataType2;
    I->ValueDoubleVal = Value;
    return I;
}

Underanalyzer::IGMInstruction*
CodeBuilder::CreateInstruction(int, Op Opcode, Underanalyzer::IGMInstruction::ComparisonType ComparisonType,
                               DT DataType1, DT DataType2) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = MapOpcode(Opcode);
    I->ComparisonKindValue = ComparisonType;
    I->Type1Value = DataType1;
    I->Type2Value = DataType2;
    return I;
}

Underanalyzer::IGMInstruction* CodeBuilder::CreateInstruction(int, ExtOp ExtendedOpcode) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = Op::Extended;
    I->ExtKindValue = ExtendedOpcode;
    I->Type1Value = DT::Int16;
    return I;
}

Underanalyzer::IGMInstruction* CodeBuilder::CreateInstruction(int, ExtOp ExtendedOpcode, int Value) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = Op::Extended;
    I->ExtKindValue = ExtendedOpcode;
    I->Type1Value = DT::Int32;
    I->ValueIntVal = Value;
    return I;
}

Underanalyzer::IGMInstruction* CodeBuilder::CreateDuplicateInstruction(int, DT DataType, std::uint8_t DuplicationSize) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = Op::Duplicate;
    I->Type1Value = DataType;
    I->DuplicationSizeVal = DuplicationSize;
    return I;
}

// DupSwap reuses the Duplicate opcode but smuggles the second size + a 0x80
// marker through the ComparisonKind field so the serializer can tell them apart.
Underanalyzer::IGMInstruction* CodeBuilder::CreateDupSwapInstruction(int, DT DataType, std::uint8_t DuplicationSize,
                                                                     std::uint8_t DuplicationSize2) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = Op::Duplicate;
    I->Type1Value = DataType;
    I->DuplicationSizeVal = DuplicationSize;
    I->DuplicationSize2Val = DuplicationSize2;
    I->ComparisonKindValue =
        static_cast<Underanalyzer::IGMInstruction::ComparisonType>(((DuplicationSize2 << 3) | 0x80));
    return I;
}

Underanalyzer::IGMInstruction* CodeBuilder::CreatePopSwapInstruction(int, std::uint8_t SwapSize) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = Op::Pop;
    I->Type1Value = DT::Int16;
    I->Type2Value = DT::Variable;
    I->PopSwapSizeVal = SwapSize;
    return I;
}

// A "with"-block early-exit is a PopWithContext flagged with the sentinel
// offset; the serializer turns this back into a 0xF00000 in the goto word.
Underanalyzer::IGMInstruction* CodeBuilder::CreateWithExitInstruction(int) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = Op::PopWithContext;
    I->PopWithContextExitVal = true;
    I->BranchOffsetVal = 0xF00000;
    return I;
}

Underanalyzer::IGMInstruction* CodeBuilder::CreateCallInstruction(int, int ArgumentCount) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = Op::Call;
    I->Type1Value = DT::Int32;
    I->ArgumentCountVal = static_cast<std::uint16_t>(ArgumentCount);
    return I;
}

// CallVariable encodes argc into the low byte of the SingleType slot rather
// than the dedicated ArgumentCount word, so route it through DuplicationSize.
Underanalyzer::IGMInstruction* CodeBuilder::CreateCallVariableInstruction(int, int ArgumentCount) {
    auto* I = _CurrentArena->New<GMSInstruction>();
    I->KindValue = Op::CallVariable;
    I->Type1Value = DT::Variable;
    I->DuplicationSizeVal = static_cast<std::uint8_t>(ArgumentCount);
    return I;
}

void CodeBuilder::PatchInstruction(Underanalyzer::IGMInstruction* Instruction, const std::string& VariableName,
                                   IT VariableInstanceType, IT InstructionInstanceType, VT VariableType, bool IsBuiltin,
                                   bool KeepInstanceType) {
    auto* GI = static_cast<GMSInstruction*>(Instruction);

    // Indexed/array access (anything beyond Normal/Instance) always lands on
    // Self in the VARI table; the original instance type lives on the instr.
    if (!KeepInstanceType && (static_cast<std::int16_t>(VariableInstanceType) >= 0 || _Data->IsVersionAtLeast(2, 3)) &&
        VariableType != VT::Normal && VariableType != VT::Instance) {
        VariableInstanceType = IT::Self;
        InstructionInstanceType = IT::Self;
    }

    // "argument" is a pseudo-instance — store it under Builtin in VARI so all
    // arguments share one entry, but it isn't actually a builtin variable.
    if (VariableInstanceType == IT::Argument) {
        VariableInstanceType = IT::Builtin;
        IsBuiltin = false;
    }

    // Anything outside the canonical set of storage classes degrades to Self
    // for VARI keying (Other/Stacktop/etc. only matter on the instruction).
    if (VariableInstanceType != IT::Self && VariableInstanceType != IT::Local && VariableInstanceType != IT::Builtin &&
        VariableInstanceType != IT::Global && VariableInstanceType != IT::Static) {
        VariableInstanceType = IT::Self;
    }

    if (VariableInstanceType == IT::Local) {
        // Locals are deferred: CompileGroup assigns VarIDs in declaration order
        // at link time so the layout matches the runtime's expectations.
        _CurrentGroup->RegisterLocalVariable(GI, VariableName);
    } else {
        _CurrentGroup->RegisterNonLocalVariable(VariableName);

        const bool Bytecode14 = _Data->GeneralInfo.BytecodeVersion <= 14;
        VariableInstanceType = _CalculateInstType(VariableInstanceType, IsBuiltin, Bytecode14);

        GMSString* NameString = _CurrentGroup->MakeString(VariableName, nullptr);
        const auto Key = std::make_pair(VariableName, static_cast<std::int16_t>(VariableInstanceType));
        auto It = _VariableLookup.find(Key);
        GMSVariable* Variable = nullptr;
        if (It != _VariableLookup.end()) {
            Variable = It->second;
        } else {
            // VarID convention: bytecode 14 leaves it zero, builtins flag with
            // the Builtin enum value, everything else mirrors the name's STRG id.
            auto Up = std::make_unique<GMSVariable>();
            Up->NameRef = NameString;
            Up->NameStringID = NameString != nullptr ? NameString->Id : 0;
            Up->InstType = VariableInstanceType;
            if (Bytecode14) {
                Up->VarID = 0;
            } else if (IsBuiltin) {
                Up->VarID = static_cast<std::int32_t>(IT::Builtin);
            } else {
                Up->VarID = Up->NameStringID;
            }
            Variable = Up.get();
            _VariableLookup[Key] = Variable;
            _Data->VariableByName.try_emplace(VariableName, Variable);
            _Data->Variables.push_back(std::move(Up));
        }
        GI->ValueVariableRef = Variable;
    }

    GI->ReferenceVarTypeValue = VariableType;
    if (VariableType == VT::Normal || VariableType == VT::Instance) {
        GI->InstTypeValue = InstructionInstanceType;
    }
}

void CodeBuilder::PatchInstruction(Underanalyzer::IGMInstruction* Instruction,
                                   Underanalyzer::Compiler::FunctionScope& Scope, const std::string& FunctionName,
                                   Underanalyzer::Compiler::IBuiltinFunction* BuiltinFunction) {
    auto* GI = static_cast<GMSInstruction*>(Instruction);

    // Resolution order matches the compiler's lexical scoping: local function
    // declaration > builtin > global function (incl. user-defined) > script.
    Underanalyzer::IGMFunction* Function = nullptr;

    Underanalyzer::Compiler::Bytecode::FunctionEntry* Entry = nullptr;
    if (Scope.TryGetDeclaredFunction(*_GameContext, FunctionName, Entry)) {
        Function = Entry->Function();
        if (Function == nullptr)
            throw Underanalyzer::Compiler::CompilerException("Function not resolved for function entry");
    } else if (BuiltinFunction != nullptr || _GameContext->Builtins().LookupBuiltinFunction(FunctionName) != nullptr) {
        Function = _CurrentGroup->EnsureFunctionDefined(FunctionName);
    } else if (_Globals->TryGetFunction(FunctionName, Function)) {

    } else {
        auto It = _Data->ScriptByName.find(FunctionName);
        if (It != _Data->ScriptByName.end()) {
            Function = _CurrentGroup->EnsureFunctionDefined(FunctionName);
        } else {
            throw Underanalyzer::Compiler::CompilerException("Failed to look up function \"" + FunctionName + "\"");
        }
    }

    GI->ValueFunctionRef = static_cast<GMSFunction*>(Function);
}

void CodeBuilder::PatchInstruction(Underanalyzer::IGMInstruction* Instruction,
                                   Underanalyzer::Compiler::Bytecode::FunctionEntry& FunctionEntry) {
    auto* GI = static_cast<GMSInstruction*>(Instruction);
    Underanalyzer::IGMFunction* Function = FunctionEntry.Function();
    if (Function == nullptr)
        throw Underanalyzer::Compiler::CompilerException("Function not resolved for function entry");
    GI->ValueFunctionRef = static_cast<GMSFunction*>(Function);
}

void CodeBuilder::PatchInstruction(Underanalyzer::IGMInstruction* Instruction, const std::string& StringContent) {
    auto* GI = static_cast<GMSInstruction*>(Instruction);
    int StringId = 0;
    GMSString* Str = _CurrentGroup->MakeString(StringContent, &StringId);
    GI->ValueStringRef = Str;
    GI->ValueIntVal = StringId;
}

// Reused by both push.i (carries the literal integer) and goto-family
// instructions (the value is a byte offset, divided by 4 to get word units).
void CodeBuilder::PatchInstruction(Underanalyzer::IGMInstruction* Instruction, int Value) {
    auto* GI = static_cast<GMSInstruction*>(Instruction);
    if (GI->KindValue == Op::Push) {
        GI->ValueIntVal = Value;
    } else {
        GI->BranchOffsetVal = Value / 4;
    }
}

bool CodeBuilder::IsGlobalFunctionName(const std::string& Name) {
    return _Globals->FunctionNameExists(Name);
}

int CodeBuilder::GenerateTryVariableID(int) {
    return _CurrentGroup->NextTryVariableID();
}

// Hash that GameMaker uses to give each array allocation a stable, per-script
// owner id. 2.3.2 added the function-index seed so nested arrays don't collide.
std::int64_t CodeBuilder::GenerateArrayOwnerID(const std::string* VariableName, std::int64_t FunctionIndex,
                                               bool IsDot) {
    std::int64_t ID = _Data->IsVersionAtLeast(2, 3, 2) ? (FunctionIndex << 16) : 0;
    if (IsDot)
        ID += ID;
    if (VariableName != nullptr) {
        ID += _CurrentGroup->RegisterName(*VariableName);
    }
    ID += _CurrentGroup->CurrentCodeEntryNameHash();
    constexpr std::int64_t Limit = 2147483647;
    std::int64_t Mod = ID % Limit;
    if (Mod < 0)
        Mod = -Mod;
    return Mod;
}

void CodeBuilder::OnParseNameIdentifier(const std::string&) {
}

} // namespace GMSLib
