
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

#include <string>

namespace Underanalyzer::Compiler {
class IBuiltinFunction;
}
namespace Underanalyzer::Compiler::Lexer {
class TokenFunction;
}

namespace Underanalyzer::Compiler::Nodes {

class SimpleFunctionCallNode final : public IMaybeStatementASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::SimpleFunctionCall;

    SimpleFunctionCallNode(Parser::ParseContext& Context, Lexer::TokenFunction* Token);
    SimpleFunctionCallNode(std::string FunctionNameIn, Lexer::IToken* NearbyTokenIn, std::vector<IASTNode*> ArgumentsIn,
                           IBuiltinFunction* BuiltinFunctionIn = nullptr)
        : FunctionName(std::move(FunctionNameIn)), BuiltinFunction(BuiltinFunctionIn),
          Arguments(std::move(ArgumentsIn)), _NearbyToken(NearbyTokenIn) {
    }

    std::string FunctionName;
    IBuiltinFunction* BuiltinFunction;
    std::vector<IASTNode*> Arguments;

    static SimpleFunctionCallNode* ParseArrayLiteral(Parser::ParseContext& Context);

    ASTNodeKind Kind() const override {
        return kKind;
    }
    Lexer::IToken* NearbyToken() const override {
        return _NearbyToken;
    }
    IASTNode* PostProcess(Parser::ParseContext& Context) override;
    void PostProcessChildrenOnly(Parser::ParseContext& Context);
    IASTNode* Duplicate(Parser::ParseContext& Context) override;
    void GenerateCode(Bytecode::BytecodeContext& Context) override;
    void GenerateDirectCode(Bytecode::BytecodeContext& Context);
    std::vector<IASTNode*> EnumerateChildren() override {
        return Arguments;
    }

    bool IsStatement() const override {
        return _IsStatement;
    }
    void SetIsStatement(bool v) override {
        _IsStatement = v;
    }

  private:
    void GenerateArguments(Bytecode::BytecodeContext& Context);
    IASTNode* OptimizeOrd(Parser::ParseContext& Context);
    IASTNode* OptimizeChr(Parser::ParseContext& Context);
    IASTNode* OptimizeInt64(Parser::ParseContext& Context);
    IASTNode* OptimizeReal(Parser::ParseContext& Context);
    IASTNode* OptimizeString(Parser::ParseContext& Context);

    Lexer::IToken* _NearbyToken;
    bool _IsStatement = false;
};

} // namespace Underanalyzer::Compiler::Nodes
