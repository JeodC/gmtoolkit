
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Lexer/Symbols.h"

#include "Underanalyzer/Compiler/Lexer/LexContext.h"

namespace Underanalyzer::Compiler::Lexer {

int Symbols::Parse(LexContext& context, int startPosition, char currChar, char nextChar, bool& success) {
    success = true;
    auto& tokens = context.Tokens();
    switch (currChar) {
        case '{':
            tokens.push_back(context.Arena().New<TokenSeparator>(context, startPosition, SeparatorKind::BlockOpen));
            return startPosition + 1;
        case '}':
            tokens.push_back(context.Arena().New<TokenSeparator>(context, startPosition, SeparatorKind::BlockClose));
            return startPosition + 1;
        case '(':
            tokens.push_back(context.Arena().New<TokenSeparator>(context, startPosition, SeparatorKind::GroupOpen));
            return startPosition + 1;
        case ')':
            tokens.push_back(context.Arena().New<TokenSeparator>(context, startPosition, SeparatorKind::GroupClose));
            return startPosition + 1;
        case ',':
            tokens.push_back(context.Arena().New<TokenSeparator>(context, startPosition, SeparatorKind::Comma));
            return startPosition + 1;
        case '.':
            tokens.push_back(context.Arena().New<TokenSeparator>(context, startPosition, SeparatorKind::Dot));
            return startPosition + 1;
        case ':':
            if (nextChar == '=') {
                tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::Assign2));
                return startPosition + 2;
            }
            tokens.push_back(context.Arena().New<TokenSeparator>(context, startPosition, SeparatorKind::Colon));
            return startPosition + 1;
        case ';':
            tokens.push_back(context.Arena().New<TokenSeparator>(context, startPosition, SeparatorKind::Semicolon));
            return startPosition + 1;
        case '[':
            // GML's accessor sigils: '|' list, '?' map, '#' grid, '@' direct, '$' struct.
            switch (nextChar) {
                case '|':
                    tokens.push_back(
                        context.Arena().New<TokenSeparator>(context, startPosition, SeparatorKind::ArrayOpenList));
                    return startPosition + 2;
                case '?':
                    tokens.push_back(
                        context.Arena().New<TokenSeparator>(context, startPosition, SeparatorKind::ArrayOpenMap));
                    return startPosition + 2;
                case '#':
                    tokens.push_back(
                        context.Arena().New<TokenSeparator>(context, startPosition, SeparatorKind::ArrayOpenGrid));
                    return startPosition + 2;
                case '@':
                    tokens.push_back(
                        context.Arena().New<TokenSeparator>(context, startPosition, SeparatorKind::ArrayOpenDirect));
                    return startPosition + 2;
                case '$':
                    tokens.push_back(
                        context.Arena().New<TokenSeparator>(context, startPosition, SeparatorKind::ArrayOpenStruct));
                    return startPosition + 2;
            }
            tokens.push_back(context.Arena().New<TokenSeparator>(context, startPosition, SeparatorKind::ArrayOpen));
            return startPosition + 1;
        case ']':
            tokens.push_back(context.Arena().New<TokenSeparator>(context, startPosition, SeparatorKind::ArrayClose));
            return startPosition + 1;

        case '=':
            if (nextChar == '=') {
                tokens.push_back(
                    context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::CompareEqual));
                return startPosition + 2;
            }
            tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::Assign));
            return startPosition + 1;
        case '+':
            if (nextChar == '+') {
                tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::Increment));
                return startPosition + 2;
            }
            if (nextChar == '=') {
                tokens.push_back(
                    context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::CompoundPlus));
                return startPosition + 2;
            }
            tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::Plus));
            return startPosition + 1;
        case '-':
            if (nextChar == '-') {
                tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::Decrement));
                return startPosition + 2;
            }
            if (nextChar == '=') {
                tokens.push_back(
                    context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::CompoundMinus));
                return startPosition + 2;
            }
            tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::Minus));
            return startPosition + 1;
        case '*':
            if (nextChar == '=') {
                tokens.push_back(
                    context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::CompoundTimes));
                return startPosition + 2;
            }
            tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::Times));
            return startPosition + 1;
        case '/':
            if (nextChar == '=') {
                tokens.push_back(
                    context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::CompoundDivide));
                return startPosition + 2;
            }
            tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::Divide));
            return startPosition + 1;
        case '!':
            if (nextChar == '=') {
                tokens.push_back(
                    context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::CompareNotEqual));
                return startPosition + 2;
            }
            tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::Not));
            return startPosition + 1;
        case '<':
            switch (nextChar) {
                case '=':
                    tokens.push_back(
                        context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::CompareLesserEqual));
                    return startPosition + 2;
                case '<':
                    tokens.push_back(
                        context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::BitwiseShiftLeft));
                    return startPosition + 2;
                case '>':
                    tokens.push_back(
                        context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::CompareNotEqual2));
                    return startPosition + 2;
            }
            tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::CompareLesser));
            return startPosition + 1;
        case '>':
            switch (nextChar) {
                case '=':
                    tokens.push_back(
                        context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::CompareGreaterEqual));
                    return startPosition + 2;
                case '>':
                    tokens.push_back(
                        context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::BitwiseShiftRight));
                    return startPosition + 2;
            }
            tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::CompareGreater));
            return startPosition + 1;
        case '?':
            if (nextChar == '?') {
                // Three-char lookahead for "??=" since the other two-char path already advanced.
                if (startPosition + 2 < static_cast<int>(context.Text().size()) &&
                    context.Text()[startPosition + 2] == '=') {
                    tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition,
                                                                        OperatorKind::CompoundNullishCoalesce));
                    return startPosition + 3;
                }
                tokens.push_back(
                    context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::NullishCoalesce));
                return startPosition + 2;
            }
            tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::Conditional));
            return startPosition + 1;
        case '%':
            if (nextChar == '=') {
                tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::CompoundMod));
                return startPosition + 2;
            }
            tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::Mod));
            return startPosition + 1;
        case '&':
            if (nextChar == '&') {
                tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::LogicalAnd));
                return startPosition + 2;
            }
            if (nextChar == '=') {
                tokens.push_back(
                    context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::CompoundBitwiseAnd));
                return startPosition + 2;
            }
            tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::BitwiseAnd));
            return startPosition + 1;
        case '|':
            if (nextChar == '|') {
                tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::LogicalOr));
                return startPosition + 2;
            }
            if (nextChar == '=') {
                tokens.push_back(
                    context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::CompoundBitwiseOr));
                return startPosition + 2;
            }
            tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::BitwiseOr));
            return startPosition + 1;
        case '^':
            if (nextChar == '^') {
                tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::LogicalXor));
                return startPosition + 2;
            }
            if (nextChar == '=') {
                tokens.push_back(
                    context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::CompoundBitwiseXor));
                return startPosition + 2;
            }
            tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::BitwiseXor));
            return startPosition + 1;
        case '~':
            tokens.push_back(context.Arena().New<TokenOperator>(context, startPosition, OperatorKind::BitwiseNegate));
            return startPosition + 1;
    }
    success = false;
    return startPosition + 1;
}

} // namespace Underanalyzer::Compiler::Lexer
