
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/FunctionDeclNode.h"

#include "Underanalyzer/Compiler/Bytecode/BranchPatch.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/FunctionEntry.h"
#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/AccessorNode.h"
#include "Underanalyzer/Compiler/Nodes/AssignNode.h"
#include "Underanalyzer/Compiler/Nodes/BinaryChainNode.h"
#include "Underanalyzer/Compiler/Nodes/BlockNode.h"
#include "Underanalyzer/Compiler/Nodes/IfNode.h"
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleFunctionCallNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/IGameContext.h"
#include "Underanalyzer/VMConstants.h"

#include <algorithm>

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;
using Op = IGMInstruction::Opcode;
using ExtOp = IGMInstruction::ExtendedOpcode;
using DT = IGMInstruction::DataType;
using IT = IGMInstruction::InstanceType;
using VT = IGMInstruction::VariableType;

// Default values are desugared at parse time into "if argument[i] == undefined then argument[i] = expr",
// which is what the runtime executes; there is no dedicated default-arg opcode.
static IfNode* GenerateDefaultCheckAndAssign(ParseContext& Context, int ArgumentIndex, IASTNode* Value) {
    SimpleVariableNode* Undefined = SimpleVariableNode::CreateUndefined(Context);
    bool UseBuiltinInstanceType = Context.CompileContextRef().GameContext().UsingBuiltinDefaultArguments();

    IAssignableASTNode* ArgVar = SimpleVariableNode::CreateArgumentVariable(Context, Value->NearbyToken(),
                                                                            ArgumentIndex, UseBuiltinInstanceType);
    std::vector<IASTNode*> Args = { ArgVar, Undefined };
    std::vector<BinaryChainNode::BinaryOperation> Ops = { BinaryChainNode::BinaryOperation::CompareEqual };
    auto* Condition = Context.Make<BinaryChainNode>(Value->NearbyToken(), std::move(Args), std::move(Ops));

    IAssignableASTNode* ArgVar2 = SimpleVariableNode::CreateArgumentVariable(Context, Value->NearbyToken(),
                                                                             ArgumentIndex, UseBuiltinInstanceType);
    auto* Assign = Context.Make<AssignNode>(AssignNode::AssignKind::Normal, ArgVar2, Value);

    return Context.Make<IfNode>(Value->NearbyToken(), (IASTNode*)Condition, (IASTNode*)Assign, (IASTNode*)nullptr);
}

// Non-constant struct field values are hoisted out as positional arguments to the
// struct's @@NewGMLObject@@ call, then referenced from the body via argument[N].
static AccessorNode* HoistStructValue(ParseContext& Context, std::vector<IASTNode*>& ArgsOut, IASTNode* ToHoist) {
    auto* ArgumentVar = Context.Make<SimpleVariableNode>(std::string("argument"), (IBuiltinVariable*)nullptr);
    ArgumentVar->SetExplicitInstanceType(IT::Argument);
    auto* Acc = Context.Make<AccessorNode>(
        ToHoist->NearbyToken(), (IASTNode*)ArgumentVar, AccessorNode::AccessorKind::Array,
        (IASTNode*)Context.Make<NumberNode>(static_cast<double>(ArgsOut.size() - 1), ToHoist->NearbyToken()));
    ArgsOut.push_back(ToHoist);
    return Acc;
}

FunctionDeclNode* FunctionDeclNode::Parse(ParseContext& Context, TokenKeyword* TokenKw) {
    std::optional<std::string> FunctionNameIn;
    if (!Context.EndOfCode()) {
        if (TokenFunction* TF = As<TokenFunction>(Context.Tokens()[Context.Position()])) {
            FunctionNameIn = TF->Text;
            Context.SetPosition(Context.Position() + 1);
        }
    }

    if (Context.EnsureToken(SeparatorKind::GroupOpen) == nullptr)
        return nullptr;

    FunctionScope* OldScope = &Context.CurrentScope();
    FunctionScope* NewScope = Context.Make<FunctionScope>(OldScope, true);
    Context.SetCurrentScope(*NewScope);

    std::vector<std::string> ArgumentNamesIn;
    ArgumentNamesIn.reserve(16);
    BlockNode* DefaultValueBlockIn = nullptr;

    while (!Context.EndOfCode()) {
        TokenVariable* TV = As<TokenVariable>(Context.Tokens()[Context.Position()]);
        if (TV == nullptr)
            break;

        const std::string& ArgName = TV->Text;
        if (std::find(ArgumentNamesIn.begin(), ArgumentNamesIn.end(), ArgName) != ArgumentNamesIn.end()) {
            Context.CompileContextRef().PushError("Duplicate argument name '" + ArgName + "'", TV);
        }
        ArgumentNamesIn.push_back(ArgName);
        Context.SetPosition(Context.Position() + 1);

        if (Context.IsCurrentToken(OperatorKind::Assign) || Context.IsCurrentToken(OperatorKind::Assign2)) {
            Context.SetPosition(Context.Position() + 1);
            IASTNode* Expr = Parser::Expressions::ParseExpression(Context);
            if (Expr != nullptr) {
                if (DefaultValueBlockIn == nullptr)
                    DefaultValueBlockIn = BlockNode::CreateEmpty(Context, TokenKw, 16);
                DefaultValueBlockIn->Children().push_back(
                    GenerateDefaultCheckAndAssign(Context, static_cast<int>(ArgumentNamesIn.size()) - 1, Expr));
            } else {
                break;
            }
        }

        if (Context.EndOfCode())
            break;
        if (Context.IsCurrentToken(SeparatorKind::Comma)) {
            Context.SetPosition(Context.Position() + 1);
            continue;
        }
        if (!Context.IsCurrentToken(SeparatorKind::GroupClose)) {
            IToken* Cur = Context.Tokens()[Context.Position()];
            Context.CompileContextRef().PushError(
                "Expected '" + TokenSeparator::KindToString(SeparatorKind::Comma) + "' or '" +
                    TokenSeparator::KindToString(SeparatorKind::GroupClose) + "', got token",
                Cur);
            break;
        }
    }

    if (Context.EnsureToken(SeparatorKind::GroupClose) == nullptr)
        return nullptr;

    NewScope->DeclareArguments(ArgumentNamesIn);

    SimpleFunctionCallNode* InheritanceCallIn = nullptr;
    if (Context.IsCurrentToken(SeparatorKind::Colon)) {
        Context.SetPosition(Context.Position() + 1);
        if (!Context.EndOfCode()) {
            if (TokenFunction* TF = As<TokenFunction>(Context.Tokens()[Context.Position()])) {
                Context.SetPosition(Context.Position() + 1);
                InheritanceCallIn = Context.Make<SimpleFunctionCallNode>(Context, TF);
            } else {
                return nullptr;
            }
        }
    }

    bool IsConstructor = false;
    while (!Context.EndOfCode()) {
        TokenVariable* Attr = As<TokenVariable>(Context.Tokens()[Context.Position()]);
        if (Attr == nullptr)
            break;
        if (Attr->Text == "constructor")
            IsConstructor = true;
        else
            Context.CompileContextRef().PushError("Unknown function attribute '" + Attr->Text + "'", Attr);
        Context.SetPosition(Context.Position() + 1);
        if (Context.IsCurrentToken(SeparatorKind::Comma))
            Context.SetPosition(Context.Position() + 1);
    }

    if (InheritanceCallIn != nullptr && !IsConstructor) {
        Context.CompileContextRef().PushError("Only constructor functions can inherit",
                                              InheritanceCallIn->NearbyToken());
    }

    BlockNode* BodyIn = BlockNode::ParseRegular(Context);
    Context.SetCurrentScope(*OldScope);

    return Context.Make<FunctionDeclNode>(NewScope, TokenKw, std::move(FunctionNameIn), std::move(ArgumentNamesIn),
                                          DefaultValueBlockIn, BodyIn, InheritanceCallIn, false, IsConstructor);
}

SimpleFunctionCallNode* FunctionDeclNode::ParseStruct(ParseContext& Context, IToken* TokenOpen) {
    // Fast path for "{}" empty struct literal when the runtime supports the optimized form.
    if (Context.CompileContextRef().GameContext().UsingOptimizedFunctionDeclarations() && !Context.EndOfCode() &&
        Context.IsCurrentToken(SeparatorKind::BlockClose, KeywordKind::End)) {
        Context.SetPosition(Context.Position() + 1);
        return Context.Make<SimpleFunctionCallNode>(std::string(VMConstants::NewObjectFunction), TokenOpen,
                                                    std::vector<IASTNode*>{}, (IBuiltinFunction*)nullptr);
    }

    FunctionScope* OldScope = &Context.CurrentScope();
    FunctionScope* NewScope = Context.Make<FunctionScope>(OldScope, true);
    Context.SetCurrentScope(*NewScope);

    BlockNode* Block = BlockNode::CreateEmpty(Context, TokenOpen, 8);
    auto* Decl =
        Context.Make<FunctionDeclNode>(NewScope, TokenOpen, std::optional<std::string>{}, std::vector<std::string>{},
                                       (BlockNode*)nullptr, Block, (SimpleFunctionCallNode*)nullptr, true, true);
    std::vector<IASTNode*> Args;
    Args.reserve(9);
    Args.push_back(Decl);

    while (!Context.EndOfCode() && !Context.IsCurrentToken(SeparatorKind::BlockClose, KeywordKind::End)) {
        IToken* Current = Context.Tokens()[Context.Position()];
        TokenVariable* Variable = As<TokenVariable>(Current);
        if (Variable == nullptr) {
            if (auto* AR = As<TokenAssetReference>(Current)) {
                Variable = Context.Make<TokenVariable>(*AR);
            } else if (auto* N = As<TokenNumber>(Current); N != nullptr && N->IsConstant) {
                Variable = Context.Make<TokenVariable>(*N);
            } else if (auto* S = As<TokenString>(Current)) {
                Variable = Context.Make<TokenVariable>(*S);
            } else if (auto* K = As<TokenKeyword>(Current)) {
                Variable = Context.Make<TokenVariable>(*K);
            } else {
                break;
            }
        }
        Context.SetPosition(Context.Position() + 1);

        IASTNode* Value;
        if (Context.IsCurrentToken(SeparatorKind::Comma) ||
            Context.IsCurrentToken(SeparatorKind::BlockClose, KeywordKind::End)) {
            Value = Context.Make<SimpleVariableNode>(Variable);
        } else {
            Context.EnsureToken(SeparatorKind::Colon);
            IASTNode* Expr = Parser::Expressions::ParseExpression(Context);
            if (Expr == nullptr)
                break;
            Value = Expr;
        }

        bool IsConstant = dynamic_cast<IConstantASTNode*>(Value) != nullptr || As<FunctionDeclNode>(Value) != nullptr;
        if (!IsConstant) {
            if (auto* Func = As<SimpleFunctionCallNode>(Value);
                Func != nullptr && (Func->FunctionName == VMConstants::NewArrayFunction ||
                                    Func->FunctionName == VMConstants::NewObjectFunction)) {
                for (size_t i = 0; i < Func->Arguments.size(); i++) {
                    IASTNode* Elem = Func->Arguments[i];
                    bool ElemIsConst =
                        dynamic_cast<IConstantASTNode*>(Elem) != nullptr || As<FunctionDeclNode>(Elem) != nullptr;
                    if (!ElemIsConst) {
                        Func->Arguments[i] = HoistStructValue(Context, Args, Elem);
                    }
                }
            } else {
                Value = HoistStructValue(Context, Args, Value);
            }
        }

        SimpleVariableNode* Destination = Context.Make<SimpleVariableNode>(Variable);
        Destination->SetStructVariable(true);
        Block->Children().push_back(Context.Make<AssignNode>(AssignNode::AssignKind::Normal, Destination, Value));

        if (Context.IsCurrentToken(SeparatorKind::Comma)) {
            Context.SetPosition(Context.Position() + 1);
        } else if (!Context.IsCurrentToken(SeparatorKind::BlockClose, KeywordKind::End)) {
            break;
        }
    }

    Context.EnsureToken(SeparatorKind::BlockClose, KeywordKind::End);
    Context.SetCurrentScope(*OldScope);
    return Context.Make<SimpleFunctionCallNode>(std::string(VMConstants::NewObjectFunction), TokenOpen, std::move(Args),
                                                (IBuiltinFunction*)nullptr);
}

IASTNode* FunctionDeclNode::PostProcess(ParseContext& Context) {
    if (&Context.CurrentScope() == &Context.RootScope() && Context.ParseGlobalFunctionsPtr() != nullptr &&
        FunctionName.has_value()) {
        if (!Context.ParseGlobalFunctionsPtr()->insert(*FunctionName).second) {
            Context.CompileContextRef().PushError("Global function \"" + *FunctionName + "\" declared more than once",
                                                  _NearbyToken);
        }
    }

    FunctionScope* OldScope = &Context.CurrentScope();
    Context.SetCurrentScope(*Scope);

    if (FunctionName.has_value()) {
        if (!OldScope->TryDeclareFunction(*FunctionName)) {
            Context.CompileContextRef().PushError("Function name \"" + *FunctionName + "\" already declared in scope",
                                                  _NearbyToken);
        }
    }

    if (DefaultValueBlock != nullptr)
        DefaultValueBlock->PostProcessChildrenOnly(Context);
    if (InheritanceCall != nullptr)
        InheritanceCall->PostProcessChildrenOnly(Context);
    if (Scope->StaticInitializerBlock() != nullptr)
        Scope->StaticInitializerBlock()->PostProcessChildrenOnly(Context);
    Body->PostProcessChildrenOnly(Context);

    Context.SetCurrentScope(*OldScope);
    return this;
}

IASTNode* FunctionDeclNode::Duplicate(ParseContext& Context) {
    auto* N = Context.Make<FunctionDeclNode>(
        Scope, _NearbyToken, FunctionName, std::vector<std::string>(ArgumentNames),
        DefaultValueBlock ? dynamic_cast<BlockNode*>(DefaultValueBlock->Duplicate(Context)) : nullptr,
        dynamic_cast<BlockNode*>(Body->Duplicate(Context)),
        InheritanceCall ? dynamic_cast<SimpleFunctionCallNode*>(InheritanceCall->Duplicate(Context)) : nullptr,
        IsStruct, IsConstructor);
    N->SetIsStatement(_IsStatement);
    return N;
}

void FunctionDeclNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    FunctionScope* OldScope = &Context.CurrentScope();
    Context.SetCurrentScope(*Scope);

    if (Context.CompileContextRef().GameContext().UsingArrayCopyOnWrite()) {
        Context.SetLastFunctionID(Context.LastFunctionID() + 1);
        Scope->SetArrayOwnerID(Context.LastFunctionID());
    }

    // Functions are inlined into the surrounding bytecode but must not execute when
    // the declaration is hit; an unconditional branch jumps past the body.
    Bytecode::SingleForwardBranchPatch SkipFunction(Context, Context.Emit(Op::Branch));

    Bytecode::FunctionEntry* ParentEntry = Context.CurrentFunctionEntry();
    auto* Entry = Context.Make<Bytecode::FunctionEntry>(
        ParentEntry, Scope, Context.Position(), static_cast<int>(ArgumentNames.size()), IsConstructor, FunctionName,
        OldScope == &Context.RootScope(),
        (OldScope->StaticVariableName() != nullptr ? std::optional<std::string>(*OldScope->StaticVariableName())
                                                   : std::nullopt),
        IsStruct ? Bytecode::FunctionEntryKind::StructInstantiation : Bytecode::FunctionEntryKind::FunctionDeclaration);
    Context.SetCurrentFunctionEntry(Entry);
    Context.FunctionEntries().push_back(Entry);

    if (FunctionName.has_value()) {
        OldScope->AssignFunctionEntry(*FunctionName, Entry);
    }

    Scope->SetGeneratingFunctionDeclHeader(true);

    if (DefaultValueBlock != nullptr)
        DefaultValueBlock->GenerateCode(Context);

    if (InheritanceCall != nullptr) {
        InheritanceCall->GenerateCode(Context);
        Context.PopDataType();

        Context.EmitPushFunction(
            Bytecode::FunctionPatch(OldScope, InheritanceCall->FunctionName, InheritanceCall->BuiltinFunction));
        Context.Emit(Op::Convert, DT::Int32, DT::Variable);
        Context.EmitCall(Bytecode::FunctionPatch::FromBuiltin(Context, std::string(VMConstants::CopyStaticFunction)),
                         1);
    }

    Scope->SetGeneratingFunctionDeclHeader(false);

    if (IsConstructor && Context.CompileContextRef().GameContext().UsingConstructorSetStatic()) {
        Context.EmitCall(Bytecode::FunctionPatch::FromBuiltin(Context, std::string(VMConstants::SetStaticFunction)), 0);
    }

    if (BlockNode* StaticBlock = Scope->StaticInitializerBlock()) {
        // Guard the static block so it runs once. The non-reentrant runtime sets the flag
        // before running the body to prevent recursion; the reentrant one sets it after.
        Context.Emit(ExtOp::HasStaticInitialized);
        Bytecode::SingleForwardBranchPatch SkipStatic(Context, Context.Emit(Op::BranchTrue));

        bool AllowReentrantStatic = Context.CompileContextRef().GameContext().UsingReentrantStatic();
        if (!AllowReentrantStatic)
            Context.Emit(ExtOp::SetStaticInitialized);

        StaticBlock->GenerateStaticCode(Context);

        SkipStatic.Patch(Context);

        if (AllowReentrantStatic)
            Context.Emit(ExtOp::SetStaticInitialized);
    }

    Body->GenerateCode(Context);
    Context.Emit(Op::Exit, DT::Int32);

    Scope->ControlFlowContexts().clear();

    Context.SetCurrentFunctionEntry(ParentEntry);
    Context.SetCurrentScope(*OldScope);

    SkipFunction.Patch(Context);

    Context.EmitPushFunction(Bytecode::LocalFunctionPatch(Entry));
    Context.Emit(Op::Convert, DT::Int32, DT::Variable);
    if (IsConstructor) {
        Context.EmitCall(Bytecode::FunctionPatch::FromBuiltin(Context, std::string(VMConstants::NullObjectFunction)),
                         0);
    } else {
        Context.Emit(Op::PushImmediate, static_cast<int16_t>(OldScope->GeneratingStaticBlock() ? IT::Static : IT::Self),
                     DT::Int16);
        Context.Emit(Op::Convert, DT::Int32, DT::Variable);
    }
    Context.EmitCall(Bytecode::FunctionPatch::FromBuiltin(Context, std::string(VMConstants::MethodFunction)), 2);

    if (FunctionName.has_value()) {
        Context.EmitDuplicate(DT::Variable, 0);
        if (Context.CompileContextRef().GameContext().UsingOptimizedFunctionDeclarations()) {
            Context.Emit(Op::Pop, Bytecode::VariablePatch(*FunctionName, IT::Self, VT::Normal), DT::Variable,
                         DT::Variable);
        } else {
            bool NewVars = Context.CompileContextRef().GameContext().UsingNewFunctionVariables();
            bool MatchesGlobalScriptName = Context.CompileContextRef().GlobalScriptName().has_value() &&
                                           *FunctionName == *Context.CompileContextRef().GlobalScriptName();
            if (NewVars || MatchesGlobalScriptName) {
                Context.Emit(Op::PushImmediate, static_cast<int16_t>(IT::Self), DT::Int16);
            } else {
                Context.Emit(Op::PushImmediate, static_cast<int16_t>(IT::Builtin), DT::Int16);
            }
            Context.Emit(Op::Pop, Bytecode::VariablePatch(*FunctionName, IT::Self, VT::StackTop), DT::Variable,
                         DT::Variable);
        }
    } else if (IsStruct) {
        Context.EmitDuplicate(DT::Variable, 0);
        if (Context.CompileContextRef().GameContext().UsingOptimizedFunctionDeclarations()) {
            Context.Emit(Op::Pop, Bytecode::StructVariablePatch(Entry, IT::Global, VT::Normal), DT::Variable,
                         DT::Variable);
        } else if (Context.CompileContextRef().GameContext().UsingNewFunctionVariables()) {
            Context.Emit(Op::PushImmediate, static_cast<int16_t>(IT::Global), DT::Int16);
            Context.Emit(Op::Pop, Bytecode::StructVariablePatch(Entry, IT::Global, VT::StackTop), DT::Variable,
                         DT::Variable);
        } else {
            Context.Emit(Op::PushImmediate, static_cast<int16_t>(IT::Static), DT::Int16);
            Context.Emit(Op::Pop, Bytecode::StructVariablePatch(Entry, IT::Static, VT::StackTop), DT::Variable,
                         DT::Variable);
        }
    }

    if (_IsStatement) {
        Context.Emit(Op::PopDelete, DT::Variable);
    } else {
        Context.PushDataType(DT::Variable);
    }
}

std::vector<IASTNode*> FunctionDeclNode::EnumerateChildren() {
    std::vector<IASTNode*> Out;
    Out.reserve(3);
    if (DefaultValueBlock != nullptr)
        Out.push_back(DefaultValueBlock);
    Out.push_back(Body);
    if (InheritanceCall != nullptr)
        Out.push_back(InheritanceCall);
    return Out;
}

} // namespace Underanalyzer::Compiler::Nodes
