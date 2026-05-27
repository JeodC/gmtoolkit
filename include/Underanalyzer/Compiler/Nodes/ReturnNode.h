
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Nodes {

class ReturnNode final : public IASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::Return;

    ReturnNode(Lexer::IToken* NearbyTokenIn, IASTNode* ReturnValueIn)
        : ReturnValue(ReturnValueIn), _NearbyToken(NearbyTokenIn) {
    }

    IASTNode* ReturnValue;

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
        return { ReturnValue };
    }

  private:
    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes
