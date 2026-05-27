
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Nodes {

class ForLoopNode final : public IASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::ForLoop;

    ForLoopNode(Lexer::IToken* NearbyTokenIn, IASTNode* InitializerIn, IASTNode* ConditionIn, IASTNode* IncrementorIn,
                IASTNode* BodyIn)
        : Initializer(InitializerIn), Condition(ConditionIn), Incrementor(IncrementorIn), Body(BodyIn),
          _NearbyToken(NearbyTokenIn) {
    }

    IASTNode* Initializer;
    IASTNode* Condition;
    IASTNode* Incrementor;
    IASTNode* Body;

    static ForLoopNode* Parse(Parser::ParseContext& Context);

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
        return { Initializer, Condition, Body, Incrementor };
    }

  private:
    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes
