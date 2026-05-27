
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Nodes {

class WhileLoopNode final : public IASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::WhileLoop;

    WhileLoopNode(Lexer::IToken* NearbyTokenIn, IASTNode* ConditionIn, IASTNode* BodyIn)
        : Condition(ConditionIn), Body(BodyIn), _NearbyToken(NearbyTokenIn) {
    }

    IASTNode* Condition;
    IASTNode* Body;

    static WhileLoopNode* Parse(Parser::ParseContext& Context);

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
        return { Condition, Body };
    }

  private:
    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes
