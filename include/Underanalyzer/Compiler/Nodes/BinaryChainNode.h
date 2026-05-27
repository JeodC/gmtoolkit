
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Nodes {

class BinaryChainNode final : public IASTNode {
  public:
    static constexpr ASTNodeKind kKind = ASTNodeKind::BinaryChain;

    enum class BinaryOperation {
        Add,
        Subtract,
        Multiply,
        Divide,
        GMLDivRemainder,
        GMLModulo,
        LogicalAnd,
        LogicalOr,
        LogicalXor,
        BitwiseAnd,
        BitwiseOr,
        BitwiseXor,
        BitwiseShiftLeft,
        BitwiseShiftRight,
        CompareEqual,
        CompareNotEqual,
        CompareGreater,
        CompareGreaterEqual,
        CompareLesser,
        CompareLesserEqual
    };

    BinaryChainNode(Lexer::IToken* NearbyTokenIn, std::vector<IASTNode*> ArgumentsIn,
                    std::vector<BinaryOperation> OperationsIn)
        : Arguments(std::move(ArgumentsIn)), Operations(std::move(OperationsIn)), _NearbyToken(NearbyTokenIn) {
    }

    std::vector<IASTNode*> Arguments;
    std::vector<BinaryOperation> Operations;

    static BinaryOperation OperationKindFromToken(Lexer::IToken* Token);

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
        return Arguments;
    }

  private:
    void GenerateShortCircuitCode(Bytecode::BytecodeContext& Context);

    Lexer::IToken* _NearbyToken;
};

} // namespace Underanalyzer::Compiler::Nodes
