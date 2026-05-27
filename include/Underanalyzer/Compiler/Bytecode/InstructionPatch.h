
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/VMData.h"

#include <memory>
#include <string>
#include <vector>

namespace Underanalyzer::Compiler {
class FunctionScope;
class IBuiltinFunction;
class ISubCompileContext;
} // namespace Underanalyzer::Compiler

namespace Underanalyzer::Compiler::Bytecode {

class FunctionEntry;

struct IInstructionPatch {
    IGMInstruction* Instruction = nullptr;
    virtual ~IInstructionPatch() = default;
};

struct VariablePatch : IInstructionPatch {
    std::string Name;
    IGMInstruction::InstanceType InstanceType;
    IGMInstruction::VariableType VariableType = IGMInstruction::VariableType::Normal;
    bool IsBuiltin = false;
    IGMInstruction::InstanceType InstructionInstanceType;
    bool KeepInstanceType = false;

    VariablePatch(std::string name, IGMInstruction::InstanceType instanceType,
                  IGMInstruction::VariableType variableType = IGMInstruction::VariableType::Normal,
                  bool isBuiltin = false)
        : Name(std::move(name)), InstanceType(instanceType), VariableType(variableType), IsBuiltin(isBuiltin),
          InstructionInstanceType(instanceType) {
    }
};

struct StructVariablePatch : IInstructionPatch {
    FunctionEntry* FunctionEntryRef;
    IGMInstruction::InstanceType InstanceType;
    IGMInstruction::VariableType VariableType = IGMInstruction::VariableType::Normal;
    IGMInstruction::InstanceType InstructionInstanceType;

    StructVariablePatch(FunctionEntry* functionEntry, IGMInstruction::InstanceType instanceType,
                        IGMInstruction::VariableType variableType = IGMInstruction::VariableType::Normal)
        : FunctionEntryRef(functionEntry), InstanceType(instanceType), VariableType(variableType),
          InstructionInstanceType(instanceType) {
    }
};

struct FunctionPatch : IInstructionPatch {
    FunctionScope* Scope;
    std::string Name;
    IBuiltinFunction* BuiltinFunction = nullptr;

    FunctionPatch(FunctionScope* scope, std::string name, IBuiltinFunction* builtinFunction = nullptr)
        : Scope(scope), Name(std::move(name)), BuiltinFunction(builtinFunction) {
    }

    static FunctionPatch FromBuiltin(ISubCompileContext& context, const std::string& builtinName);
};

struct LocalFunctionPatch : IInstructionPatch {
    FunctionEntry* FunctionEntryRef = nullptr;
    FunctionScope* FunctionScopeRef = nullptr;
    std::string FunctionName;

    LocalFunctionPatch(FunctionEntry* functionEntry, FunctionScope* functionScope = nullptr,
                       std::string functionName = {})
        : FunctionEntryRef(functionEntry), FunctionScopeRef(functionScope), FunctionName(std::move(functionName)) {
    }
};

struct StringPatch : IInstructionPatch {
    std::string Content;
    explicit StringPatch(std::string content) : Content(std::move(content)) {
    }
};

// Each emit() call that references a symbol enqueues a patch here; PatchInstructions
// resolves them in a later pass, after all forward references have been declared.
struct InstructionPatches {
    std::vector<VariablePatch> VariablePatches;
    std::vector<FunctionPatch> FunctionPatches;
    std::vector<LocalFunctionPatch> LocalFunctionPatches;
    std::vector<StructVariablePatch> StructVariablePatches;
    std::vector<StringPatch> StringPatches;

    static InstructionPatches Create() {
        InstructionPatches p;
        p.VariablePatches.reserve(32);
        p.FunctionPatches.reserve(32);
        p.LocalFunctionPatches.reserve(4);
        p.StructVariablePatches.reserve(4);
        p.StringPatches.reserve(16);
        return p;
    }
};

} // namespace Underanalyzer::Compiler::Bytecode
