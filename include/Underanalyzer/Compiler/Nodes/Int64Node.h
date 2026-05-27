
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

#include <cstdint>

namespace Underanalyzer::Compiler::Lexer {
class TokenInt64;
}

namespace Underanalyzer::Compiler::Nodes {

class Int64Node final : public IConstantASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::Int64;

    explicit Int64Node(Lexer::TokenInt64* Token);
    Int64Node(int64_t ValueIn, Lexer::IToken* NearbyTokenIn) : Value(ValueIn), _NearbyToken(NearbyTokenIn) {
    }

    int64_t Value;

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
