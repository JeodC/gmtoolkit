
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

#include <optional>
#include <string>

namespace Underanalyzer::Compiler::Lexer {
class TokenNumber;
}

namespace Underanalyzer::Compiler::Nodes {

class NumberNode final : public IConstantASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::Number;

    NumberNode(Lexer::TokenNumber* Token, const std::string* ConstantNameIn);
    NumberNode(double ValueIn, Lexer::IToken* NearbyTokenIn) : Value(ValueIn), _NearbyToken(NearbyTokenIn) {
    }

    double Value;
    std::optional<std::string> ConstantName;

    ASTNodeKind Kind() const override {
        return kKind;
    }
    Lexer::IToken* NearbyToken() const override {
        return _NearbyToken;
    }
    IASTNode* PostProcess(Parser::ParseContext& Context) override;
    IASTNode* Duplicate(Parser::ParseContext&) override {
        return this;
    }
    void GenerateCode(Bytecode::BytecodeContext& Context) override;
    std::vector<IASTNode*> EnumerateChildren() override {
        return {};
    }

    static void GenerateCode(Bytecode::BytecodeContext& Context, double ValueIn);

  private:
    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes
