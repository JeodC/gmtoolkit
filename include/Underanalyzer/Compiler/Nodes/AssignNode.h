
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Nodes {

class AssignNode final : public IASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::Assign;

    enum class AssignKind {
        Normal,
        CompoundPlus,
        CompoundMinus,
        CompoundTimes,
        CompoundDivide,
        CompoundMod,
        CompoundBitwiseAnd,
        CompoundBitwiseOr,
        CompoundBitwiseXor,
        CompoundNullishCoalesce
    };

    AssignNode(AssignKind KindIn, IAssignableASTNode* DestinationIn, IASTNode* ExpressionIn)
        : Destination(DestinationIn), Expression(ExpressionIn), KindValue(KindIn) {
    }

    IAssignableASTNode* Destination;
    IASTNode* Expression;
    AssignKind KindValue;

    ASTNodeKind Kind() const override {
        return kKind;
    }
    Lexer::IToken* NearbyToken() const override;
    IASTNode* PostProcess(Parser::ParseContext& Context) override;
    IASTNode* Duplicate(Parser::ParseContext& Context) override;
    void GenerateCode(Bytecode::BytecodeContext& Context) override;
    std::vector<IASTNode*> EnumerateChildren() override {
        return { Destination, Expression };
    }

    static void PerformCompoundOperation(Bytecode::BytecodeContext& Context, IGMInstruction::Opcode OperationOpcode);
};

} // namespace Underanalyzer::Compiler::Nodes
