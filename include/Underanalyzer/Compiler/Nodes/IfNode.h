
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Nodes {

class IfNode final : public IASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::If;

    IfNode(Lexer::IToken* NearbyTokenIn, IASTNode* ConditionIn, IASTNode* TrueStatementIn, IASTNode* FalseStatementIn)
        : Condition(ConditionIn), TrueStatement(TrueStatementIn), FalseStatement(FalseStatementIn),
          _NearbyToken(NearbyTokenIn) {
    }

    IASTNode* Condition;
    IASTNode* TrueStatement;
    IASTNode* FalseStatement;

    static IfNode* Parse(Parser::ParseContext& Context);

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
        if (FalseStatement)
            return { Condition, TrueStatement, FalseStatement };
        return { Condition, TrueStatement };
    }

  private:
    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes
