
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/LocalVarDeclNode.h"

#include "Underanalyzer/Compiler/Bytecode/ArrayOwners.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;

LocalVarDeclNode* LocalVarDeclNode::Parse(ParseContext& Context) {
    TokenKeyword* TokenKw = Context.EnsureToken(KeywordKind::Var);
    if (TokenKw == nullptr)
        return nullptr;

    std::vector<std::string> DeclaredLocals;
    std::vector<IASTNode*> AssignedValues;
    DeclaredLocals.reserve(8);
    AssignedValues.reserve(8);

    while (!Context.EndOfCode()) {
        TokenVariable* TokenVar = As<TokenVariable>(Context.Tokens()[Context.Position()]);
        if (TokenVar == nullptr)
            break;
        Context.SetPosition(Context.Position() + 1);

        DeclaredLocals.push_back(TokenVar->Text);

        if (TokenVar->BuiltinVariable != nullptr) {
            Context.CompileContextRef().PushError("Declaring local variable over builtin '" + TokenVar->Text + "'",
                                                  TokenVar);
        }
        Context.CurrentScope().DeclareLocal(TokenVar->Text);

        if (Context.IsCurrentToken(OperatorKind::Assign) || Context.IsCurrentToken(OperatorKind::Assign2)) {
            Context.SetPosition(Context.Position() + 1);
            IASTNode* Value = Parser::Expressions::ParseExpression(Context);
            if (Value != nullptr) {
                AssignedValues.push_back(Value);
            } else {
                AssignedValues.push_back(nullptr);
                break;
            }
        } else {
            AssignedValues.push_back(nullptr);
        }

        if (!Context.IsCurrentToken(SeparatorKind::Comma))
            break;
        Context.SetPosition(Context.Position() + 1);
    }

    return Context.Make<LocalVarDeclNode>(TokenKw, std::move(DeclaredLocals), std::move(AssignedValues));
}

IASTNode* LocalVarDeclNode::PostProcess(ParseContext& Context) {
    for (auto& Val : AssignedValues)
        if (Val != nullptr)
            Val = Val->PostProcess(Context);
    return this;
}

IASTNode* LocalVarDeclNode::Duplicate(ParseContext& Context) {
    std::vector<IASTNode*> NewValues(AssignedValues);
    for (auto& Val : NewValues)
        if (Val != nullptr)
            Val = Val->Duplicate(Context);
    return Context.Make<LocalVarDeclNode>(_NearbyToken, std::vector<std::string>(DeclaredLocals), std::move(NewValues));
}

void LocalVarDeclNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;
    using IT = IGMInstruction::InstanceType;

    for (size_t i = 0; i < AssignedValues.size(); i++) {
        IASTNode* Expr = AssignedValues[i];
        if (Expr == nullptr)
            continue;

        bool CanGen = Context.CanGenerateArrayOwners();
        if (CanGen) {
            if (Bytecode::ArrayOwners::ContainsNewArrayLiteral(Expr)) {
                Context.SetCanGenerateArrayOwners(false);
                SimpleVariableNode* Target =
                    Context.Make<SimpleVariableNode>(DeclaredLocals[i], (IBuiltinVariable*)nullptr);
                Bytecode::ArrayOwners::GenerateSetArrayOwner(Context, Target);
            }
        }

        Expr->GenerateCode(Context);
        Bytecode::VariablePatch Patch(DeclaredLocals[i], IT::Local);
        Context.Emit(Op::Pop, Patch, DT::Variable, Context.PopDataType());

        Context.SetCanGenerateArrayOwners(CanGen);
    }
}

std::vector<IASTNode*> LocalVarDeclNode::EnumerateChildren() {
    std::vector<IASTNode*> Out;
    Out.reserve(AssignedValues.size());
    for (IASTNode* V : AssignedValues)
        if (V != nullptr)
            Out.push_back(V);
    return Out;
}

} // namespace Underanalyzer::Compiler::Nodes
