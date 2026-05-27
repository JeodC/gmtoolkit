
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Nodes {

class UnaryNode final : public IASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::Unary;

    enum class UnaryKind { BooleanNot, BitwiseNegate, Positive, Negative };

    UnaryNode(Lexer::IToken* NearbyTokenIn, UnaryKind KindIn, IASTNode* ExpressionIn)
        : Expression(ExpressionIn), KindValue(KindIn), _NearbyToken(NearbyTokenIn) {
    }

    IASTNode* Expression;
    UnaryKind KindValue;

    static UnaryNode* Parse(Parser::ParseContext& Context, Lexer::IToken* Token, UnaryKind KindIn);

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
        return { Expression };
    }

  private:
    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes
