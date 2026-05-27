
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

#include <string>

namespace Underanalyzer::Compiler::Lexer {
class TokenString;
}

namespace Underanalyzer::Compiler::Nodes {

class StringNode final : public IConstantASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::String;

    explicit StringNode(Lexer::TokenString* Token);
    StringNode(std::string ValueIn, Lexer::IToken* NearbyTokenIn)
        : Value(std::move(ValueIn)), _NearbyToken(NearbyTokenIn) {
    }

    std::string Value;

    ASTNodeKind Kind() const override {
        return kKind;
    }
    Lexer::IToken* NearbyToken() const override {
        return _NearbyToken;
    }
    IASTNode* PostProcess(Parser::ParseContext&) override {
        return this;
    }
    IASTNode* Duplicate(Parser::ParseContext&) override {
        return this;
    }
    void GenerateCode(Bytecode::BytecodeContext& Context) override;
    std::vector<IASTNode*> EnumerateChildren() override {
        return {};
    }

  private:
    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes
