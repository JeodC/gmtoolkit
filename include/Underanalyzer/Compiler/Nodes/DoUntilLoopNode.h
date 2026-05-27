
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Nodes {

class DoUntilLoopNode final : public IASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::DoUntilLoop;

    DoUntilLoopNode(Lexer::IToken* NearbyTokenIn, IASTNode* BodyIn, IASTNode* ConditionIn)
        : Body(BodyIn), Condition(ConditionIn), _NearbyToken(NearbyTokenIn) {
    }

    IASTNode* Body;
    IASTNode* Condition;

    static DoUntilLoopNode* Parse(Parser::ParseContext& Context);

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
        return { Body, Condition };
    }

  private:
    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes
