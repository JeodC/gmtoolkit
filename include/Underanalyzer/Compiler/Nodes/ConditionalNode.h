
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Nodes {

class ConditionalNode final : public IASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::Conditional;

    ConditionalNode(Lexer::IToken* NearbyTokenIn, IASTNode* ConditionIn, IASTNode* TrueExpressionIn,
                    IASTNode* FalseExpressionIn)
        : Condition(ConditionIn), TrueExpression(TrueExpressionIn), FalseExpression(FalseExpressionIn),
          _NearbyToken(NearbyTokenIn) {
    }

    IASTNode* Condition;
    IASTNode* TrueExpression;
    IASTNode* FalseExpression;

    ASTNodeKind Kind() const override {
        return kKind;
    }
    Lexer::IToken* NearbyToken() const override {
        return _NearbyToken;
    }
    IASTNode* PostProcess(Parser::ParseContext& Context) override;
    IASTNode* Duplicate(Parser::ParseContext& Context) override;
    void GenerateCode(Bytecode::BytecodeContext& Context) override;
    std::vector<IASTNode*> EnumerateChildren() override {
        return { Condition, TrueExpression, FalseExpression };
    }

  private:
    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes
