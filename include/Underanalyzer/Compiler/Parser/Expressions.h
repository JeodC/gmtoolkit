
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

namespace Underanalyzer::Compiler::Nodes {
class IASTNode;
}

namespace Underanalyzer::Compiler::Parser {

class ParseContext;

class Expressions {
  public:
    static Nodes::IASTNode* ParseExpression(ParseContext& context);
    static Nodes::IASTNode* ParseConditional(ParseContext& context);
    static Nodes::IASTNode* ParseNullishExpression(ParseContext& context);
    static Nodes::IASTNode* ParseOrExpression(ParseContext& context);
    static Nodes::IASTNode* ParseAndExpression(ParseContext& context);
    static Nodes::IASTNode* ParseXorExpression(ParseContext& context);
    static Nodes::IASTNode* ParseCompareExpression(ParseContext& context);
    static Nodes::IASTNode* ParseBitwiseExpression(ParseContext& context);
    static Nodes::IASTNode* ParseBitwiseShiftExpression(ParseContext& context);
    static Nodes::IASTNode* ParseAddSubtractExpression(ParseContext& context);
    static Nodes::IASTNode* ParseMultiplyDivideExpression(ParseContext& context);
    static Nodes::IASTNode* ParseChainExpression(ParseContext& context, bool stopAtFunctionCall = false);
    static Nodes::IASTNode* ParseLeftmostExpression(ParseContext& context);
};

} // namespace Underanalyzer::Compiler::Parser
