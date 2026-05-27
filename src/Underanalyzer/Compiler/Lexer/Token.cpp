
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Lexer/Token.h"

#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/IBuiltins.h"
#include "Underanalyzer/Compiler/Lexer/LexContext.h"
#include "Underanalyzer/IGameContext.h"

#include <stdexcept>

namespace Underanalyzer::Compiler::Lexer {

std::string TokenSeparator::KindToString(SeparatorKind kind) {
    switch (kind) {
        case SeparatorKind::BlockOpen:
            return "{";
        case SeparatorKind::BlockClose:
            return "}";
        case SeparatorKind::GroupOpen:
            return "(";
        case SeparatorKind::GroupClose:
            return ")";
        case SeparatorKind::Dot:
            return ".";
        case SeparatorKind::Comma:
            return ",";
        case SeparatorKind::Semicolon:
            return ";";
        case SeparatorKind::Colon:
            return ":";
        case SeparatorKind::ArrayOpen:
            return "[";
        case SeparatorKind::ArrayOpenList:
            return "[|";
        case SeparatorKind::ArrayOpenMap:
            return "[?";
        case SeparatorKind::ArrayOpenGrid:
            return "[#";
        case SeparatorKind::ArrayOpenDirect:
            return "[@";
        case SeparatorKind::ArrayOpenStruct:
            return "[$";
        case SeparatorKind::ArrayClose:
            return "]";
    }
    throw std::runtime_error("Unknown separator kind");
}

std::string TokenOperator::KindToString(OperatorKind kind) {
    switch (kind) {
        case OperatorKind::Assign:
            return "=";
        case OperatorKind::Assign2:
            return ":=";
        case OperatorKind::CompareEqual:
            return "==";
        case OperatorKind::CompareNotEqual:
            return "!=";
        case OperatorKind::CompareNotEqual2:
            return "<>";
        case OperatorKind::CompareGreater:
            return ">";
        case OperatorKind::CompareGreaterEqual:
            return ">=";
        case OperatorKind::CompareLesser:
            return "<";
        case OperatorKind::CompareLesserEqual:
            return "<=";
        case OperatorKind::Plus:
            return "+";
        case OperatorKind::Minus:
            return "-";
        case OperatorKind::Times:
            return "*";
        case OperatorKind::Divide:
            return "/";
        case OperatorKind::Mod:
            return "%";
        case OperatorKind::Not:
            return "!";
        case OperatorKind::Conditional:
            return "?";
        case OperatorKind::NullishCoalesce:
            return "??";
        case OperatorKind::LogicalAnd:
            return "&&";
        case OperatorKind::LogicalOr:
            return "||";
        case OperatorKind::LogicalXor:
            return "^^";
        case OperatorKind::BitwiseAnd:
            return "&";
        case OperatorKind::BitwiseOr:
            return "|";
        case OperatorKind::BitwiseXor:
            return "^";
        case OperatorKind::BitwiseNegate:
            return "~";
        case OperatorKind::BitwiseShiftLeft:
            return "<<";
        case OperatorKind::BitwiseShiftRight:
            return ">>";
        case OperatorKind::Increment:
            return "++";
        case OperatorKind::Decrement:
            return "--";
        case OperatorKind::CompoundPlus:
            return "+=";
        case OperatorKind::CompoundMinus:
            return "-=";
        case OperatorKind::CompoundTimes:
            return "*=";
        case OperatorKind::CompoundDivide:
            return "/=";
        case OperatorKind::CompoundMod:
            return "%=";
        case OperatorKind::CompoundNullishCoalesce:
            return "?\?=";
        case OperatorKind::CompoundBitwiseAnd:
            return "&=";
        case OperatorKind::CompoundBitwiseOr:
            return "|=";
        case OperatorKind::CompoundBitwiseXor:
            return "^=";
    }
    throw std::runtime_error("Unknown operator kind");
}

std::string TokenKeyword::KindToString(KeywordKind kind) {
    switch (kind) {
        case KeywordKind::If:
            return "if";
        case KeywordKind::Then:
            return "then";
        case KeywordKind::Else:
            return "else";
        case KeywordKind::Switch:
            return "switch";
        case KeywordKind::Case:
            return "case";
        case KeywordKind::Default:
            return "default";
        case KeywordKind::Begin:
            return "begin";
        case KeywordKind::End:
            return "end";
        case KeywordKind::Break:
            return "break";
        case KeywordKind::Continue:
            return "continue";
        case KeywordKind::Exit:
            return "exit";
        case KeywordKind::Return:
            return "return";
        case KeywordKind::While:
            return "while";
        case KeywordKind::For:
            return "for";
        case KeywordKind::Repeat:
            return "repeat";
        case KeywordKind::Do:
            return "do";
        case KeywordKind::Until:
            return "until";
        case KeywordKind::With:
            return "with";
        case KeywordKind::Var:
            return "var";
        case KeywordKind::Globalvar:
            return "globalvar";
        case KeywordKind::Not:
            return "not";
        case KeywordKind::Div:
            return "div";
        case KeywordKind::Mod:
            return "mod";
        case KeywordKind::And:
            return "and";
        case KeywordKind::Or:
            return "or";
        case KeywordKind::Xor:
            return "xor";
        case KeywordKind::Enum:
            return "enum";
        case KeywordKind::Try:
            return "try";
        case KeywordKind::Catch:
            return "catch";
        case KeywordKind::Finally:
            return "finally";
        case KeywordKind::Throw:
            return "throw";
        case KeywordKind::New:
            return "new";
        case KeywordKind::Delete:
            return "delete";
        case KeywordKind::Function:
            return "function";
        case KeywordKind::Static:
            return "static";
        case KeywordKind::None:
            break;
    }
    throw std::runtime_error("Unknown keyword kind");
}

TokenVariable::TokenVariable(const TokenAssetReference& assetReference)
    : Text(assetReference.Text),
      BuiltinVariable(assetReference.Context().CompileContextRef().GameContext().Builtins().LookupBuiltinVariable(
          assetReference.Text)),
      _context(assetReference.Context()), _textPosition(assetReference.TextPosition()) {
}

TokenVariable::TokenVariable(const TokenNumber& number)
    : Text(number.Text),
      BuiltinVariable(number.Context().CompileContextRef().GameContext().Builtins().LookupBuiltinVariable(number.Text)),
      _context(number.Context()), _textPosition(number.TextPosition()) {
}

TokenVariable::TokenVariable(const TokenString& str)
    : Text(str.Value),
      BuiltinVariable(str.Context().CompileContextRef().GameContext().Builtins().LookupBuiltinVariable(str.Value)),
      _context(str.Context()), _textPosition(str.TextPosition()) {
}

TokenVariable::TokenVariable(const TokenKeyword& keyword)
    : Text(TokenKeyword::KindToString(keyword.Kind)),
      BuiltinVariable(keyword.Context().CompileContextRef().GameContext().Builtins().LookupBuiltinVariable(
          TokenKeyword::KindToString(keyword.Kind))),
      _context(keyword.Context()), _textPosition(keyword.TextPosition()) {
}

} // namespace Underanalyzer::Compiler::Lexer
