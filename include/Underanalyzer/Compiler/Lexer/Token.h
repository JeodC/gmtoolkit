
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <cstdint>
#include <string>

namespace Underanalyzer::Compiler {
class IBuiltinFunction;
class IBuiltinVariable;
} // namespace Underanalyzer::Compiler

namespace Underanalyzer::Compiler::Lexer {

class LexContext;

enum class TokenKind : uint8_t {
    Separator,
    Operator,
    Keyword,
    Identifier,
    Number,
    Int64,
    Boolean,
    String,
    Function,
    Variable,
    AssetReference
};

class IToken {
  public:
    virtual ~IToken() = default;
    virtual TokenKind Tag() const = 0;
    virtual LexContext& Context() const = 0;
    virtual int TextPosition() const = 0;
};

enum class SeparatorKind {
    BlockOpen,
    BlockClose,
    GroupOpen,
    GroupClose,
    Dot,
    Comma,
    Semicolon,
    Colon,
    ArrayOpen,
    ArrayOpenList,
    ArrayOpenMap,
    ArrayOpenGrid,
    ArrayOpenDirect,
    ArrayOpenStruct,
    ArrayClose
};

class TokenSeparator final : public IToken {
  public:
    TokenSeparator(LexContext& context, int textPosition, SeparatorKind kind)
        : Kind(kind), _context(context), _textPosition(textPosition) {
    }
    TokenKind Tag() const override {
        return TokenKind::Separator;
    }
    LexContext& Context() const override {
        return _context;
    }
    int TextPosition() const override {
        return _textPosition;
    }
    SeparatorKind Kind;
    static std::string KindToString(SeparatorKind kind);

  private:
    LexContext& _context;
    int _textPosition;
};

enum class OperatorKind {
    Assign,
    Assign2,
    CompareEqual,
    CompareNotEqual,
    CompareNotEqual2,
    CompareGreater,
    CompareGreaterEqual,
    CompareLesser,
    CompareLesserEqual,
    Plus,
    Minus,
    Times,
    Divide,
    Mod,
    Not,
    Conditional,
    NullishCoalesce,
    LogicalAnd,
    LogicalOr,
    LogicalXor,
    BitwiseAnd,
    BitwiseOr,
    BitwiseXor,
    BitwiseNegate,
    BitwiseShiftLeft,
    BitwiseShiftRight,
    Increment,
    Decrement,
    CompoundPlus,
    CompoundMinus,
    CompoundTimes,
    CompoundDivide,
    CompoundMod,
    CompoundNullishCoalesce,
    CompoundBitwiseAnd,
    CompoundBitwiseOr,
    CompoundBitwiseXor
};

class TokenOperator final : public IToken {
  public:
    TokenOperator(LexContext& context, int textPosition, OperatorKind kind)
        : Kind(kind), _context(context), _textPosition(textPosition) {
    }
    TokenKind Tag() const override {
        return TokenKind::Operator;
    }
    LexContext& Context() const override {
        return _context;
    }
    int TextPosition() const override {
        return _textPosition;
    }
    OperatorKind Kind;
    static std::string KindToString(OperatorKind kind);

  private:
    LexContext& _context;
    int _textPosition;
};

enum class KeywordKind {
    None,
    If,
    Then,
    Else,
    Switch,
    Case,
    Default,
    Begin,
    End,
    Break,
    Continue,
    Exit,
    Return,
    While,
    For,
    Repeat,
    Do,
    Until,
    With,
    Var,
    Globalvar,
    Not,
    Div,
    Mod,
    And,
    Or,
    Xor,
    Enum,
    Try,
    Catch,
    Finally,
    Throw,
    New,
    Delete,
    Function,
    Static
};

class TokenKeyword final : public IToken {
  public:
    TokenKeyword(LexContext& context, int textPosition, KeywordKind kind)
        : Kind(kind), _context(context), _textPosition(textPosition) {
    }
    TokenKind Tag() const override {
        return TokenKind::Keyword;
    }
    LexContext& Context() const override {
        return _context;
    }
    int TextPosition() const override {
        return _textPosition;
    }
    KeywordKind Kind;
    static std::string KindToString(KeywordKind kind);

  private:
    LexContext& _context;
    int _textPosition;
};

class TokenIdentifier final : public IToken {
  public:
    TokenIdentifier(LexContext& context, int textPosition, std::string text)
        : Text(std::move(text)), _context(context), _textPosition(textPosition) {
    }
    TokenKind Tag() const override {
        return TokenKind::Identifier;
    }
    LexContext& Context() const override {
        return _context;
    }
    int TextPosition() const override {
        return _textPosition;
    }
    std::string Text;

  private:
    LexContext& _context;
    int _textPosition;
};

class TokenNumber final : public IToken {
  public:
    TokenNumber(LexContext& context, int textPosition, std::string text, double value, bool isConstant = false)
        : Text(std::move(text)), Value(value), IsConstant(isConstant), _context(context), _textPosition(textPosition) {
    }
    TokenKind Tag() const override {
        return TokenKind::Number;
    }
    LexContext& Context() const override {
        return _context;
    }
    int TextPosition() const override {
        return _textPosition;
    }
    std::string Text;
    double Value;
    bool IsConstant;

  private:
    LexContext& _context;
    int _textPosition;
};

class TokenInt64 final : public IToken {
  public:
    TokenInt64(LexContext& context, int textPosition, std::string text, int64_t value)
        : Text(std::move(text)), Value(value), _context(context), _textPosition(textPosition) {
    }
    TokenKind Tag() const override {
        return TokenKind::Int64;
    }
    LexContext& Context() const override {
        return _context;
    }
    int TextPosition() const override {
        return _textPosition;
    }
    std::string Text;
    int64_t Value;

  private:
    LexContext& _context;
    int _textPosition;
};

class TokenBoolean final : public IToken {
  public:
    TokenBoolean(LexContext& context, int textPosition, bool value)
        : Value(value), _context(context), _textPosition(textPosition) {
    }
    TokenKind Tag() const override {
        return TokenKind::Boolean;
    }
    LexContext& Context() const override {
        return _context;
    }
    int TextPosition() const override {
        return _textPosition;
    }
    bool Value;

  private:
    LexContext& _context;
    int _textPosition;
};

class TokenString final : public IToken {
  public:
    TokenString(LexContext& context, int textPosition, std::string text, std::string value)
        : Text(std::move(text)), Value(std::move(value)), _context(context), _textPosition(textPosition) {
    }
    TokenKind Tag() const override {
        return TokenKind::String;
    }
    LexContext& Context() const override {
        return _context;
    }
    int TextPosition() const override {
        return _textPosition;
    }
    std::string Text;
    std::string Value;

  private:
    LexContext& _context;
    int _textPosition;
};

class TokenFunction final : public IToken {
  public:
    TokenFunction(LexContext& context, int textPosition, std::string text, IBuiltinFunction* builtinFunction)
        : Text(std::move(text)), BuiltinFunction(builtinFunction), _context(context), _textPosition(textPosition) {
    }
    TokenKind Tag() const override {
        return TokenKind::Function;
    }
    LexContext& Context() const override {
        return _context;
    }
    int TextPosition() const override {
        return _textPosition;
    }
    std::string Text;
    IBuiltinFunction* BuiltinFunction;

  private:
    LexContext& _context;
    int _textPosition;
};

class TokenAssetReference final : public IToken {
  public:
    TokenAssetReference(LexContext& context, int textPosition, std::string text, int assetId, bool isRoomInstanceAsset)
        : Text(std::move(text)), AssetId(assetId), IsRoomInstanceAsset(isRoomInstanceAsset), _context(context),
          _textPosition(textPosition) {
    }
    TokenKind Tag() const override {
        return TokenKind::AssetReference;
    }
    LexContext& Context() const override {
        return _context;
    }
    int TextPosition() const override {
        return _textPosition;
    }
    std::string Text;
    int AssetId;
    bool IsRoomInstanceAsset;

  private:
    LexContext& _context;
    int _textPosition;
};

class TokenVariable final : public IToken {
  public:
    TokenVariable(LexContext& context, int textPosition, std::string text, IBuiltinVariable* builtinVariable)
        : Text(std::move(text)), BuiltinVariable(builtinVariable), _context(context), _textPosition(textPosition) {
    }
    explicit TokenVariable(const TokenAssetReference& assetReference);
    explicit TokenVariable(const TokenNumber& number);
    explicit TokenVariable(const TokenString& str);
    explicit TokenVariable(const TokenKeyword& keyword);

    TokenKind Tag() const override {
        return TokenKind::Variable;
    }
    LexContext& Context() const override {
        return _context;
    }
    int TextPosition() const override {
        return _textPosition;
    }
    std::string Text;
    IBuiltinVariable* BuiltinVariable;

  private:
    LexContext& _context;
    int _textPosition;
};

template <class T> inline T* As(IToken* t);
template <> inline TokenSeparator* As<TokenSeparator>(IToken* t) {
    return (t && t->Tag() == TokenKind::Separator) ? static_cast<TokenSeparator*>(t) : nullptr;
}
template <> inline TokenOperator* As<TokenOperator>(IToken* t) {
    return (t && t->Tag() == TokenKind::Operator) ? static_cast<TokenOperator*>(t) : nullptr;
}
template <> inline TokenKeyword* As<TokenKeyword>(IToken* t) {
    return (t && t->Tag() == TokenKind::Keyword) ? static_cast<TokenKeyword*>(t) : nullptr;
}
template <> inline TokenIdentifier* As<TokenIdentifier>(IToken* t) {
    return (t && t->Tag() == TokenKind::Identifier) ? static_cast<TokenIdentifier*>(t) : nullptr;
}
template <> inline TokenNumber* As<TokenNumber>(IToken* t) {
    return (t && t->Tag() == TokenKind::Number) ? static_cast<TokenNumber*>(t) : nullptr;
}
template <> inline TokenInt64* As<TokenInt64>(IToken* t) {
    return (t && t->Tag() == TokenKind::Int64) ? static_cast<TokenInt64*>(t) : nullptr;
}
template <> inline TokenBoolean* As<TokenBoolean>(IToken* t) {
    return (t && t->Tag() == TokenKind::Boolean) ? static_cast<TokenBoolean*>(t) : nullptr;
}
template <> inline TokenString* As<TokenString>(IToken* t) {
    return (t && t->Tag() == TokenKind::String) ? static_cast<TokenString*>(t) : nullptr;
}
template <> inline TokenFunction* As<TokenFunction>(IToken* t) {
    return (t && t->Tag() == TokenKind::Function) ? static_cast<TokenFunction*>(t) : nullptr;
}
template <> inline TokenVariable* As<TokenVariable>(IToken* t) {
    return (t && t->Tag() == TokenKind::Variable) ? static_cast<TokenVariable*>(t) : nullptr;
}
template <> inline TokenAssetReference* As<TokenAssetReference>(IToken* t) {
    return (t && t->Tag() == TokenKind::AssetReference) ? static_cast<TokenAssetReference*>(t) : nullptr;
}

} // namespace Underanalyzer::Compiler::Lexer
