
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/IBuiltins.h"
#include "Underanalyzer/Compiler/ICodeBuilder.h"
#include "Underanalyzer/Compiler/ISubCompileContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/AccessorNode.h"
#include "Underanalyzer/Compiler/Nodes/AssignNode.h"
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/IGameContext.h"
#include "Underanalyzer/VMConstants.h"

namespace Underanalyzer::Compiler::Nodes {

using Parser::ParseContext;
using IT = IGMInstruction::InstanceType;
using Op = IGMInstruction::Opcode;
using ExtOp = IGMInstruction::ExtendedOpcode;
using DT = IGMInstruction::DataType;
using VT = IGMInstruction::VariableType;

const std::unordered_set<std::string>& SimpleVariableNode::BuiltinArgumentVariables() {
    static const std::unordered_set<std::string> Set = { "argument0",  "argument1",  "argument2",  "argument3",
                                                         "argument4",  "argument5",  "argument6",  "argument7",
                                                         "argument8",  "argument9",  "argument10", "argument11",
                                                         "argument12", "argument13", "argument14", "argument15" };
    return Set;
}

SimpleVariableNode::SimpleVariableNode(Lexer::TokenVariable* Token)
    : _VariableName(Token->Text), _BuiltinVariable(Token->BuiltinVariable), _NearbyToken(Token) {
}

IASTNode* SimpleVariableNode::PostProcess(ParseContext& Context) {
    return ResolveStandaloneType(Context);
}

IASTNode* SimpleVariableNode::Duplicate(ParseContext& Context) {
    SimpleVariableNode* N = Context.Make<SimpleVariableNode>(_VariableName, _BuiltinVariable);
    N->_ExplicitInstanceType = _ExplicitInstanceType;
    N->_HasExplicitInstanceType = _HasExplicitInstanceType;
    N->_IsFunctionCall = _IsFunctionCall;
    N->_CollapsedFromDot = _CollapsedFromDot;
    N->_LeftmostSideOfDot = _LeftmostSideOfDot;
    N->_NearbyToken = _NearbyToken;
    return N;
}

Bytecode::VariablePatch SimpleVariableNode::CreateVariablePatch(Bytecode::BytecodeContext& Context) const {
    Bytecode::VariablePatch Patch(_VariableName, _ExplicitInstanceType,
                                  _RoomInstanceVariable ? VT::Instance : VT::Normal, _BuiltinVariable != nullptr);

    if (_ExplicitInstanceType == IT::Self && !_StructVariable) {

        if (_IsFunctionCall || (!_CollapsedFromDot && Context.CompileContextRef().GameContext().UsingSelfToBuiltin())) {
            Patch.InstructionInstanceType = IT::Builtin;
        }
    }
    return Patch;
}

void SimpleVariableNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    IGameContext& Game = Context.CompileContextRef().GameContext();
    bool IsGlobalFunction = Context.IsGlobalFunctionName(_VariableName);
    bool IsLocalGlobalFunction = Context.CompileContextRef().ScriptKind() == CompileScriptKind::GlobalScript &&
                                 Context.RootScope().IsFunctionDeclaredImmediately(_VariableName);

    if (_ExplicitInstanceType == IT::Self && !_CollapsedFromDot &&
        (IsGlobalFunction || IsLocalGlobalFunction || Context.IsFunctionDeclaredInCurrentScope(_VariableName))) {

        FunctionScope* ResolveScope = Context.CurrentScope().GeneratingFunctionDeclHeader()
                                          ? Context.CurrentScope().Parent()
                                          : &Context.CurrentScope();

        if (!_LeftmostSideOfDot && Game.UsingFunctionScriptReferences()) {
            int Unused = 0;
            if (IsLocalGlobalFunction) {
                if (!Context.CurrentScope().GeneratingDotVariableCall()) {
                    Context.Emit(ExtOp::PushReference,
                                 Bytecode::LocalFunctionPatch(nullptr, &Context.RootScope(), _VariableName));
                    Context.PushDataType(DT::Variable);
                } else {
                    Context.EmitPushFunction(
                        Bytecode::LocalFunctionPatch(nullptr, &Context.RootScope(), _VariableName));
                    Context.PushDataType(DT::Int32);
                }
            } else if (Game.UsingNewFunctionResolution() && ResolveScope->IsFunctionDeclared(Game, _VariableName)) {
                if (!Context.CurrentScope().GeneratingDotVariableCall()) {
                    Context.Emit(ExtOp::PushReference,
                                 Bytecode::LocalFunctionPatch(nullptr, ResolveScope, _VariableName));
                    Context.PushDataType(DT::Variable);
                } else {
                    Context.EmitPushFunction(Bytecode::FunctionPatch(ResolveScope, _VariableName, nullptr));
                    Context.PushDataType(DT::Int32);
                }
            } else if (Game.GetScriptIdByFunctionName(_VariableName, Unused) ||
                       Game.GetScriptId(_VariableName, Unused)) {
                if (!Context.CurrentScope().GeneratingDotVariableCall() || Game.UsingNewFunctionResolution()) {
                    Context.Emit(ExtOp::PushReference, Bytecode::FunctionPatch(ResolveScope, _VariableName));
                    Context.PushDataType(DT::Variable);
                } else {
                    Context.EmitPushFunction(Bytecode::FunctionPatch(ResolveScope, _VariableName, nullptr));
                    Context.PushDataType(DT::Int32);
                }
            } else {
                Context.EmitPushFunction(Bytecode::FunctionPatch(ResolveScope, _VariableName,
                                                                 Game.Builtins().LookupBuiltinFunction(_VariableName)));
                Context.PushDataType(DT::Int32);
            }
        } else if (Game.UsingGMLv2()) {
            Context.EmitPushFunction(Bytecode::FunctionPatch(ResolveScope, _VariableName,
                                                             Game.Builtins().LookupBuiltinFunction(_VariableName)));
            Context.PushDataType(DT::Int32);
        } else {
            int ScriptId = 0;
            if (Game.GetScriptId(_VariableName, ScriptId)) {
                NumberNode::GenerateCode(Context, static_cast<double>(ScriptId));
            } else {
                Context.CompileContextRef().PushError(
                    "Failed to find script with name \"" + _VariableName +
                        "\" (note: cannot use built-in functions directly in this GameMaker version)",
                    _NearbyToken);
                Context.PushDataType(DT::Int32);
            }
        }

        if (Game.UsingGMLv2() && _LeftmostSideOfDot) {
            Context.ConvertDataType(DT::Variable);
            Context.EmitCall(Bytecode::FunctionPatch::FromBuiltin(Context, std::string(VMConstants::StaticGetFunction)),
                             1);
            Context.PushDataType(DT::Variable);
        }
        return;
    }

    Op opcode;
    switch (_ExplicitInstanceType) {
        case IT::Local:
            opcode = Op::PushLocal;
            break;
        case IT::Global:
            opcode = Op::PushGlobal;
            break;
        case IT::Builtin:
            opcode = Op::PushBuiltin;
            break;
        case IT::Argument:
            opcode = Op::Push;
            break;
        default:
            opcode = (_BuiltinVariable == nullptr || !_BuiltinVariable->IsGlobal()) ? Op::Push : Op::PushBuiltin;
            break;
    }

    Context.Emit(opcode, CreateVariablePatch(Context), DT::Variable);
    Context.PushDataType(DT::Variable);
}

void SimpleVariableNode::GenerateAssignCode(Bytecode::BytecodeContext& Context) {
    Context.Emit(Op::Pop, CreateVariablePatch(Context), DT::Variable, Context.PopDataType());
}

void SimpleVariableNode::GenerateCompoundAssignCode(Bytecode::BytecodeContext& Context, IASTNode* Expression,
                                                    Op OperationOpcode) {
    Bytecode::VariablePatch Patch = CreateVariablePatch(Context);
    Context.Emit(Op::Push, Patch, DT::Variable);
    Expression->GenerateCode(Context);
    AssignNode::PerformCompoundOperation(Context, OperationOpcode);
    Context.Emit(Op::Pop, Patch, DT::Variable, DT::Variable);
}

void SimpleVariableNode::GeneratePrePostAssignCode(Bytecode::BytecodeContext& Context, bool IsIncrement, bool IsPre,
                                                   bool IsStatement) {
    Bytecode::VariablePatch Patch = CreateVariablePatch(Context);
    Context.Emit(Op::Push, Patch, DT::Variable);

    if (!IsStatement && !IsPre) {
        Context.EmitDuplicate(DT::Variable, 0);
        Context.PushDataType(DT::Variable);
    }

    Context.Emit(Op::Push, static_cast<int16_t>(1), DT::Int16);
    Context.Emit(IsIncrement ? Op::Add : Op::Subtract, DT::Int32, DT::Variable);

    if (!IsStatement && IsPre) {
        Context.EmitDuplicate(DT::Variable, 0);
        Context.PushDataType(DT::Variable);
    }

    Context.Emit(Op::Pop, Patch, DT::Variable, DT::Variable);
}

// Resolves the implicit instance type for an unqualified name. Priority order is
// locals, then static, then arguments, then builtins, finally falling back to Self.
IAssignableASTNode* SimpleVariableNode::ResolveStandaloneType(ISubCompileContext& Context) {
    if (_HasExplicitInstanceType)
        return this;

    if (Context.CurrentScope().IsLocalDeclared(_VariableName)) {
        SetExplicitInstanceType(IT::Local);
        return this;
    }

    if (Context.CompileContextRef().GameContext().UsingGMLv2()) {
        if (Context.CurrentScope().IsStaticDeclared(_VariableName)) {
            SetExplicitInstanceType(IT::Static);
            return this;
        }
        int ArgumentIndex = 0;
        if (Context.CurrentScope().TryGetArgumentIndex(_VariableName, ArgumentIndex)) {
            return CreateArgumentVariable(Context, _NearbyToken, ArgumentIndex);
        }
        if (BuiltinArgumentVariables().count(_VariableName) > 0) {
            SetExplicitInstanceType(Context.CompileContextRef().GameContext().UsingSelfToBuiltin() &&
                                            &Context.CurrentScope() == &Context.RootScope()
                                        ? IT::Argument
                                        : IT::Builtin);
            return this;
        }
        if (_VariableName == "argument") {
            SetExplicitInstanceType(IT::Argument);
            return this;
        }
        if (_BuiltinVariable != nullptr) {
            SetExplicitInstanceType(_BuiltinVariable->IsGlobal() ? IT::Builtin : IT::Self);
            return this;
        }
    }

    SetExplicitInstanceType(IT::Self);
    return this;
}

SimpleVariableNode* SimpleVariableNode::CreateUndefined(ParseContext& Context) {
    return Context.Make<SimpleVariableNode>(
        std::string("undefined"),
        Context.CompileContextRef().GameContext().Builtins().LookupBuiltinVariable("undefined"));
}

IAssignableASTNode* SimpleVariableNode::CreateArgumentVariable(ISubCompileContext& Context,
                                                               Lexer::IToken* NearbyTokenIn, int ArgumentIndex,
                                                               bool UseBuiltinInstanceType) {
    // Reached from both parse (ParseContext) and codegen (BytecodeContext —
    // e.g. SimpleFunctionCallNode calling a named argument), so only the
    // ISubCompileContext surface may be used here; downcasting to ParseContext
    // was UB at codegen time. Both contexts share CompileContext's arena.
    NodeArena& Arena = Context.CompileContextRef().Arena();

    // Arguments 0-15 have dedicated 'argumentN' builtins; beyond that, fall back to
    // the variadic 'argument' array indexed by position.
    if (ArgumentIndex < 16) {
        std::string ArgName = "argument" + std::to_string(ArgumentIndex);
        SimpleVariableNode* ArgVar = Arena.New<SimpleVariableNode>(
            ArgName, Context.CompileContextRef().GameContext().Builtins().LookupBuiltinVariable(ArgName));
        ArgVar->SetExplicitInstanceType(UseBuiltinInstanceType ? IT::Builtin : IT::Argument);
        return ArgVar;
    }
    const std::string ArgName = "argument";
    SimpleVariableNode* ArgVar = Arena.New<SimpleVariableNode>(
        ArgName, Context.CompileContextRef().GameContext().Builtins().LookupBuiltinVariable(ArgName));

    ArgVar->SetExplicitInstanceType(IT::Argument);
    NumberNode* ArgNumber = Arena.New<NumberNode>(static_cast<double>(ArgumentIndex), NearbyTokenIn);
    AccessorNode* AccessorArg =
        Arena.New<AccessorNode>(NearbyTokenIn, ArgVar, AccessorNode::AccessorKind::Array, ArgNumber);
    return AccessorArg;
}

} // namespace Underanalyzer::Compiler::Nodes
