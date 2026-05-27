
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/NewObjectNode.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/Functions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/IGameContext.h"
#include "Underanalyzer/VMConstants.h"

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;

NewObjectNode* NewObjectNode::Parse(ParseContext& Context) {
    TokenKeyword* TokenKw = Context.EnsureToken(KeywordKind::New);
    if (TokenKw == nullptr)
        return nullptr;

    IASTNode* Expr;
    if (!Context.EndOfCode()) {
        if (TokenFunction* TokenFn = As<TokenFunction>(Context.Tokens()[Context.Position()])) {
            Expr = Context.Make<SimpleVariableNode>(TokenFn->Text, (IBuiltinVariable*)nullptr);
            Context.SetPosition(Context.Position() + 1);
        } else {
            Expr = Parser::Expressions::ParseChainExpression(Context, true);
            if (Expr == nullptr)
                return nullptr;
        }
    } else {
        Expr = Parser::Expressions::ParseChainExpression(Context, true);
        if (Expr == nullptr)
            return nullptr;
    }

    // 65534 leaves room for the constructor reference appended later (+1 in EmitCall).
    std::vector<IASTNode*> Args = Parser::Functions::ParseCallArguments(Context, 65534);
    return Context.Make<NewObjectNode>(TokenKw, Expr, std::move(Args));
}

IASTNode* NewObjectNode::PostProcess(ParseContext& Context) {
    Expression = Expression->PostProcess(Context);
    for (auto& Arg : Arguments)
        Arg = Arg->PostProcess(Context);
    return this;
}

IASTNode* NewObjectNode::Duplicate(ParseContext& Context) {
    std::vector<IASTNode*> NewArgs(Arguments);
    for (auto& A : NewArgs)
        A = A->Duplicate(Context);
    NewObjectNode* N = Context.Make<NewObjectNode>(_NearbyToken, Expression->Duplicate(Context), std::move(NewArgs));
    N->SetIsStatement(_IsStatement);
    return N;
}

void NewObjectNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using ExtOp = IGMInstruction::ExtendedOpcode;
    using DT = IGMInstruction::DataType;

    for (int i = static_cast<int>(Arguments.size()) - 1; i >= 0; i--) {
        Arguments[i]->GenerateCode(Context);
        Context.ConvertDataType(DT::Variable);
    }

    if (auto* SimpleVar = As<SimpleVariableNode>(Expression)) {
        FunctionScope* ResolveScope = Context.CurrentScope().GeneratingFunctionDeclHeader()
                                          ? Context.CurrentScope().Parent()
                                          : &Context.CurrentScope();

        const std::string& FunctionName = SimpleVar->VariableName();
        bool IsGlobalFunction = Context.IsGlobalFunctionName(FunctionName) ||
                                (Context.CompileContextRef().ScriptKind() == CompileScriptKind::GlobalScript &&
                                 Context.RootScope().IsFunctionDeclaredImmediately(FunctionName));
        if (IsGlobalFunction || Context.IsFunctionDeclaredInCurrentScope(FunctionName)) {
            IGameContext& Game = Context.CompileContextRef().GameContext();
            int Unused = 0;
            if (Game.UsingFunctionScriptReferences() && !Context.CurrentScope().GeneratingDotVariableCall() &&
                ((Game.GetScriptId(FunctionName, Unused) && !Game.GetScriptIdByFunctionName(FunctionName, Unused)) ||
                 (Game.UsingNewFunctionResolution() && !IsGlobalFunction &&
                  !ResolveScope->IsFunctionDeclaredImmediately(FunctionName)))) {
                Context.Emit(ExtOp::PushReference, Bytecode::FunctionPatch(ResolveScope, FunctionName));
            } else {
                Context.EmitPushFunction(Bytecode::FunctionPatch(ResolveScope, FunctionName));
                Context.Emit(Op::Convert, DT::Int32, DT::Variable);
            }
        } else {

            if (!SimpleVar->CollapsedFromDot())
                SimpleVar->SetIsFunctionCall(true);
            SimpleVar->GenerateCode(Context);
            Context.ConvertDataType(DT::Variable);
        }
    } else {
        Expression->GenerateCode(Context);
        Context.ConvertDataType(DT::Variable);
    }

    Context.EmitCall(Bytecode::FunctionPatch::FromBuiltin(Context, std::string(VMConstants::NewObjectFunction)),
                     static_cast<int>(Arguments.size()) + 1);

    if (_IsStatement) {
        Context.Emit(Op::PopDelete, DT::Variable);
    } else {
        Context.PushDataType(DT::Variable);
    }
}

std::vector<IASTNode*> NewObjectNode::EnumerateChildren() {
    std::vector<IASTNode*> Out;
    Out.reserve(1 + Arguments.size());
    Out.push_back(Expression);
    for (IASTNode* A : Arguments)
        Out.push_back(A);
    return Out;
}

} // namespace Underanalyzer::Compiler::Nodes
