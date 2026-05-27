
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"

#include "Underanalyzer/Compiler/Bytecode/ControlFlowContext.h"
#include "Underanalyzer/Compiler/Bytecode/FunctionEntry.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/CompilerException.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/IBuiltins.h"
#include "Underanalyzer/Compiler/ICodeBuilder.h"
#include "Underanalyzer/Compiler/Nodes/IASTNode.h"
#include "Underanalyzer/IGameContext.h"

#include <stdexcept>

namespace Underanalyzer::Compiler::Bytecode {

using Op = IGMInstruction::Opcode;
using ExtOp = IGMInstruction::ExtendedOpcode;
using DT = IGMInstruction::DataType;
using IT = IGMInstruction::InstanceType;

BytecodeContext::BytecodeContext(CompileContext& context, Nodes::IASTNode* rootNode, FunctionScope& rootScope,
                                 const std::unordered_set<std::string>*)
    : _compileContext(context), _arena(context.Arena()), _rootNode(rootNode), _currentScope(&rootScope),
      _rootScope(&rootScope), _patches(InstructionPatches::Create()) {
    _gameContext = &context.GameContext();
    _codeBuilder = &_gameContext->CodeBuilder();
}

void BytecodeContext::GenerateCode(int initialPosition) {
    _position = initialPosition;
    _canGenerateArrayOwners = _gameContext->UsingArrayCopyOnWrite();
    _rootNode->GenerateCode(*this);

#ifndef NDEBUG
    if (!_dataTypeStack.empty()) {
        throw std::runtime_error("Data type stack not cleared by end of code generation");
    }
#endif
}

// Second pass: resolves the symbolic references collected during emit into concrete
// variable/function/string targets on the actual instructions, in deterministic order.
void BytecodeContext::PatchInstructions(CompileContext& context, InstructionPatches& patches) {
    ICodeBuilder& codeBuilder = context.GameContext().CodeBuilder();

    for (VariablePatch& variablePatch : patches.VariablePatches) {
        codeBuilder.PatchInstruction(variablePatch.Instruction, variablePatch.Name, variablePatch.InstanceType,
                                     variablePatch.InstructionInstanceType, variablePatch.VariableType,
                                     variablePatch.IsBuiltin, variablePatch.KeepInstanceType);
    }
    for (FunctionPatch& functionPatch : patches.FunctionPatches) {
        codeBuilder.PatchInstruction(functionPatch.Instruction, *functionPatch.Scope, functionPatch.Name,
                                     functionPatch.BuiltinFunction);
    }
    for (LocalFunctionPatch& functionPatch : patches.LocalFunctionPatches) {
        FunctionEntry* entry = nullptr;
        if (functionPatch.FunctionEntryRef != nullptr) {
            entry = functionPatch.FunctionEntryRef;
        } else if (functionPatch.FunctionScopeRef != nullptr &&
                   functionPatch.FunctionScopeRef->TryGetDeclaredFunction(context.GameContext(),
                                                                          functionPatch.FunctionName, entry)) {
        } else {
            throw CompilerException("Failed to resolve local function with name \"" + functionPatch.FunctionName +
                                    "\"");
        }
        codeBuilder.PatchInstruction(functionPatch.Instruction, *entry);
    }
    for (StructVariablePatch& variablePatch : patches.StructVariablePatches) {
        if (!variablePatch.FunctionEntryRef->StructName().has_value()) {
            throw std::logic_error("Struct name not resolved on function entry");
        }
        codeBuilder.PatchInstruction(variablePatch.Instruction, *variablePatch.FunctionEntryRef->StructName(),
                                     variablePatch.InstanceType, variablePatch.InstructionInstanceType,
                                     variablePatch.VariableType, false, true);
    }
    for (StringPatch& stringPatch : patches.StringPatches) {
        codeBuilder.PatchInstruction(stringPatch.Instruction, stringPatch.Content);
    }
}

IGMInstruction* BytecodeContext::Emit(Op opcode) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, opcode);
    _instructions.push_back(instr);
    _position += 4;
    return instr;
}

IGMInstruction* BytecodeContext::Emit(Op opcode, DT dataType) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, opcode, dataType);
    _instructions.push_back(instr);
    _position += 4;
    return instr;
}

IGMInstruction* BytecodeContext::Emit(Op opcode, DT dataType1, DT dataType2) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, opcode, dataType1, dataType2);
    _instructions.push_back(instr);
    _position += 4;
    return instr;
}

IGMInstruction* BytecodeContext::Emit(Op opcode, int16_t value, DT dataType1, DT dataType2) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, opcode, value, dataType1, dataType2);
    _instructions.push_back(instr);
    _position += 4;
    return instr;
}

IGMInstruction* BytecodeContext::Emit(Op opcode, int32_t value, DT dataType1, DT dataType2) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, opcode, value, dataType1, dataType2);
    _instructions.push_back(instr);
    _position += 8;
    return instr;
}

IGMInstruction* BytecodeContext::Emit(Op opcode, int64_t value, DT dataType1, DT dataType2) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, opcode, value, dataType1, dataType2);
    _instructions.push_back(instr);
    _position += 12;
    return instr;
}

IGMInstruction* BytecodeContext::Emit(Op opcode, double value, DT dataType1, DT dataType2) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, opcode, value, dataType1, dataType2);
    _instructions.push_back(instr);
    _position += 12;
    return instr;
}

IGMInstruction* BytecodeContext::Emit(Op opcode, IGMInstruction::ComparisonType comparisonType, DT dataType1,
                                      DT dataType2) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, opcode, comparisonType, dataType1, dataType2);
    _instructions.push_back(instr);
    _position += 4;
    return instr;
}

IGMInstruction* BytecodeContext::Emit(ExtOp extendedOpcode) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, extendedOpcode);
    _instructions.push_back(instr);
    _position += 4;
    return instr;
}

IGMInstruction* BytecodeContext::Emit(ExtOp extendedOpcode, LocalFunctionPatch function) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, extendedOpcode, 0);
    _instructions.push_back(instr);
    _position += 8;
    function.Instruction = instr;
    _patches.LocalFunctionPatches.push_back(std::move(function));
    return instr;
}

IGMInstruction* BytecodeContext::Emit(ExtOp extendedOpcode, FunctionPatch function) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, extendedOpcode, 0);
    _instructions.push_back(instr);
    _position += 8;
    function.Instruction = instr;
    _patches.FunctionPatches.push_back(std::move(function));
    return instr;
}

IGMInstruction* BytecodeContext::Emit(ExtOp extendedOpcode, int extendedValue) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, extendedOpcode, extendedValue);
    _instructions.push_back(instr);
    _position += 8;
    return instr;
}

IGMInstruction* BytecodeContext::EmitDuplicate(DT dataType, uint8_t duplicationSize) {
    IGMInstruction* instr = _codeBuilder->CreateDuplicateInstruction(_position, dataType, duplicationSize);
    _instructions.push_back(instr);
    _position += 4;
    return instr;
}

IGMInstruction* BytecodeContext::EmitDupSwap(DT dataType, uint8_t duplicationSize, uint8_t duplicationSize2) {
    IGMInstruction* instr =
        _codeBuilder->CreateDupSwapInstruction(_position, dataType, duplicationSize, duplicationSize2);
    _instructions.push_back(instr);
    _position += 4;
    return instr;
}

IGMInstruction* BytecodeContext::EmitPopSwap(uint8_t swapSize) {
    IGMInstruction* instr = _codeBuilder->CreatePopSwapInstruction(_position, swapSize);
    _instructions.push_back(instr);
    _position += 4;
    return instr;
}

IGMInstruction* BytecodeContext::EmitPopWithExit() {
    IGMInstruction* instr = _codeBuilder->CreateWithExitInstruction(_position);
    _instructions.push_back(instr);
    _position += 4;
    return instr;
}

IGMInstruction* BytecodeContext::Emit(Op opcode, VariablePatch variable, DT dataType1, DT dataType2) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, opcode, dataType1, dataType2);
    _instructions.push_back(instr);
    _position += 8;
    variable.Instruction = instr;
    if (variable.InstanceType == IT::Local) {
        _currentScope->DeclareLocal(variable.Name);
    }
    _patches.VariablePatches.push_back(std::move(variable));
    return instr;
}

IGMInstruction* BytecodeContext::Emit(Op opcode, StructVariablePatch variable, DT dataType1, DT dataType2) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, opcode, dataType1, dataType2);
    _instructions.push_back(instr);
    _position += 8;
    variable.Instruction = instr;
    _patches.StructVariablePatches.push_back(std::move(variable));
    return instr;
}

IGMInstruction* BytecodeContext::EmitPushFunction(FunctionPatch function) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, Op::Push, DT::Int32);
    _instructions.push_back(instr);
    _position += 8;
    function.Instruction = instr;
    _patches.FunctionPatches.push_back(std::move(function));
    return instr;
}

IGMInstruction* BytecodeContext::EmitPushFunction(LocalFunctionPatch function) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, Op::Push, DT::Int32);
    _instructions.push_back(instr);
    _position += 8;
    function.Instruction = instr;
    _patches.LocalFunctionPatches.push_back(std::move(function));
    return instr;
}

IGMInstruction* BytecodeContext::EmitCall(FunctionPatch function, int argumentCount) {
    IGMInstruction* instr = _codeBuilder->CreateCallInstruction(_position, argumentCount);
    _instructions.push_back(instr);
    _position += 8;
    function.Instruction = instr;
    _patches.FunctionPatches.push_back(std::move(function));
    return instr;
}

IGMInstruction* BytecodeContext::EmitCallVariable(int argumentCount) {
    IGMInstruction* instr = _codeBuilder->CreateCallVariableInstruction(_position, argumentCount);
    _instructions.push_back(instr);
    _position += 4;
    return instr;
}

IGMInstruction* BytecodeContext::Emit(Op opcode, StringPatch stringPatch, DT dataType1, DT dataType2) {
    IGMInstruction* instr = _codeBuilder->CreateInstruction(_position, opcode, dataType1, dataType2);
    _instructions.push_back(instr);
    _position += 8;
    stringPatch.Instruction = instr;
    _patches.StringPatches.push_back(std::move(stringPatch));
    return instr;
}

void BytecodeContext::PatchBranch(IGMInstruction* instruction, int branchOffset) {
    _codeBuilder->PatchInstruction(instruction, branchOffset);
}

void BytecodeContext::PatchPush(IGMInstruction* instruction, int value) {
    _codeBuilder->PatchInstruction(instruction, value);
}

void BytecodeContext::PushDataType(DT dataType) {
    _dataTypeStack.push_back(dataType);
}
DT BytecodeContext::PeekDataType() {
    return _dataTypeStack.back();
}
DT BytecodeContext::PopDataType() {
    DT t = _dataTypeStack.back();
    _dataTypeStack.pop_back();
    return t;
}

bool BytecodeContext::ConvertDataType(DT destDataType) {
    DT srcDataType = _dataTypeStack.back();
    _dataTypeStack.pop_back();
    if (srcDataType != destDataType) {
        Emit(Op::Convert, srcDataType, destDataType);
        return true;
    }
    return false;
}

// Coerce the top-of-stack value into an instance identifier. GMLv2 keeps a Variable on
// the stack and tags it with the StackTop sentinel; older flavors collapse to plain Int32.
BytecodeContext::InstanceConversionType BytecodeContext::ConvertToInstanceId() {
    DT dataType = PopDataType();
    if (dataType != DT::Int32) {
        if (dataType == DT::Variable && _compileContext.GameContext().UsingGMLv2()) {
            Emit(Op::PushImmediate, static_cast<int16_t>(IT::StackTop), DT::Int16);
            return InstanceConversionType::StacktopId;
        }
        Emit(Op::Convert, dataType, DT::Int32);
        return InstanceConversionType::Int32;
    }
    return InstanceConversionType::None;
}

bool BytecodeContext::DoAnyControlFlowRequireCleanup() {
    for (auto* ctx : _currentScope->ControlFlowContexts()) {
        if (ctx->RequiresCleanup())
            return true;
    }
    return false;
}

void BytecodeContext::GenerateControlFlowCleanup() {
    for (auto* ctx : _currentScope->ControlFlowContexts()) {
        if (ctx->RequiresCleanup())
            ctx->GenerateCleanupCode(*this);
    }
}

bool BytecodeContext::IsGlobalFunctionName(const std::string& name) {
    if (_gameContext->Builtins().LookupBuiltinFunction(name) != nullptr)
        return true;
    int unused;
    if (_gameContext->GetScriptId(name, unused))
        return true;
    return _codeBuilder->IsGlobalFunctionName(name);
}

void BytecodeContext::PushControlFlowContext(IControlFlowContext* context) {
    _currentScope->ControlFlowContexts().push_back(context);
}

void BytecodeContext::PopControlFlowContext() {
    _currentScope->ControlFlowContexts().pop_back();
}

bool BytecodeContext::AnyControlFlowContexts() {
    return !_currentScope->ControlFlowContexts().empty();
}

bool BytecodeContext::AnyLoopContexts() {
    for (auto* ctx : _currentScope->ControlFlowContexts()) {
        if (ctx->IsLoop())
            return true;
    }
    return false;
}

IControlFlowContext* BytecodeContext::GetTopControlFlowContext() {
    return _currentScope->ControlFlowContexts().back();
}

// While emitting a function header, lookups need to hit the parent scope so the
// function doesn't accidentally resolve to itself.
bool BytecodeContext::IsFunctionDeclaredInCurrentScope(const std::string& name) {
    FunctionScope* scope = _currentScope->GeneratingFunctionDeclHeader() ? _currentScope->Parent() : _currentScope;
    switch (_compileContext.ScriptKind()) {
        case CompileScriptKind::GlobalScript:
        case CompileScriptKind::RoomCreationCode:
            return scope->IsFunctionDeclared(_compileContext.GameContext(), name);
        case CompileScriptKind::ObjectEvent:
            // Newer runtimes hoist object-scope function decls; older ones only see prior decls.
            if (_compileContext.GameContext().UsingObjectFunctionForesight()) {
                return scope->IsFunctionDeclared(_compileContext.GameContext(), name);
            }
            {
                FunctionEntry* entry = nullptr;
                return scope->TryGetDeclaredFunction(_compileContext.GameContext(), name, entry);
            }
        default: {
            FunctionEntry* entry = nullptr;
            return scope->TryGetDeclaredFunction(_compileContext.GameContext(), name, entry);
        }
    }
}

int64_t BytecodeContext::GenerateArrayOwnerID(const std::string* variableName, int64_t functionId, bool isDot) {
    return _codeBuilder->GenerateArrayOwnerID(variableName, functionId, isDot);
}

} // namespace Underanalyzer::Compiler::Bytecode
