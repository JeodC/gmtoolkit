
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Lexer/LexContext.h"

#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/IBuiltins.h"
#include "Underanalyzer/Compiler/ICodeBuilder.h"
#include "Underanalyzer/Compiler/Lexer/Identifiers.h"
#include "Underanalyzer/Compiler/Lexer/Macro.h"
#include "Underanalyzer/Compiler/Lexer/Numbers.h"
#include "Underanalyzer/Compiler/Lexer/Strings.h"
#include "Underanalyzer/Compiler/Lexer/Symbols.h"
#include "Underanalyzer/Compiler/Lexer/Tags.h"
#include "Underanalyzer/Compiler/Lexer/Whitespace.h"
#include "Underanalyzer/IGameContext.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace Underanalyzer::Compiler::Lexer {

LexContext::LexContext(CompileContext& context, std::string text) : _CompileContext(context), _Text(std::move(text)) {
    _Tokens.reserve(128);
}

LexContext::LexContext(CompileContext& context, std::string text, std::string macroName)
    : _CompileContext(context), _Text(std::move(text)), _MacroName(std::move(macroName)) {
    _Tokens.reserve(128);
}

FunctionScope& LexContext::CurrentScope() {
    throw std::logic_error("LexContext has no CurrentScope");
}
void LexContext::SetCurrentScope(FunctionScope&) {
    throw std::logic_error("LexContext has no CurrentScope");
}
FunctionScope& LexContext::RootScope() {
    throw std::logic_error("LexContext has no RootScope");
}
void LexContext::SetRootScope(FunctionScope&) {
    throw std::logic_error("LexContext has no RootScope");
}

NodeArena& LexContext::Arena() {
    return _CompileContext.Arena();
}

void LexContext::Tokenize() {
    std::string_view Text = _Text;
    int Pos = 0;
    bool NewStrings = _CompileContext.GameContext().UsingGMS2OrLater();

    while (Pos < static_cast<int>(Text.size())) {
        Pos = Whitespace::Skip(Text, Pos);
        if (Pos >= static_cast<int>(Text.size()))
            break;

        char CurrChar = Text[Pos];
        char NextChar = (Pos + 1 < static_cast<int>(Text.size())) ? Text[Pos + 1] : '\0';

        // Two-char lookahead is enough to classify every token start in GML.
        if (CurrChar == '#' && NextChar != '\0') {
            Pos = Tags::Parse(*this, Pos);
        } else if ((CurrChar >= 'a' && CurrChar <= 'z') || (CurrChar >= 'A' && CurrChar <= 'Z') || CurrChar == '_') {
            Pos = Identifiers::Parse(*this, Pos);
        } else if (CurrChar == '$' || (CurrChar == '0' && NextChar == 'x')) {
            Pos = Numbers::ParseHex(*this, Pos, CurrChar == '$');
        } else if (std::isdigit(static_cast<unsigned char>(CurrChar)) ||
                   (CurrChar == '.' && std::isdigit(static_cast<unsigned char>(NextChar)))) {
            Pos = Numbers::ParseDecimal(*this, Pos);
        } else if (CurrChar == '@' && (NextChar == '"' || NextChar == '\'') && NewStrings) {
            Pos = Strings::ParseVerbatim(*this, Pos, '@');
        } else if (CurrChar == '"') {
            // GMS2 introduced escape sequences in "..." strings; legacy GML treats them as verbatim.
            Pos = NewStrings ? Strings::ParseRegular(*this, Pos) : Strings::ParseVerbatim(*this, Pos, '"');
        } else if (CurrChar == '\'' && !NewStrings) {
            Pos = Strings::ParseVerbatim(*this, Pos, '\'');
        } else if (CurrChar == '@') {
            Pos = Identifiers::ParseInternal(*this, Pos);
        } else {
            bool Success = false;
            Pos = Symbols::Parse(*this, Pos, CurrChar, NextChar, Success);
            if (!Success) {
                _CompileContext.PushError("Unrecognized token", *this, Pos);
            }
        }
    }
}

// Second lexer pass: expand macros, then reclassify identifiers as functions/assets/
// booleans/constants/variables based on the surrounding context and game lookups.
void LexContext::PostProcessTokens() {
    std::vector<int> MacroExpansionEnd;
    MacroExpansionEnd.reserve(4);

    for (int i = 0; i < static_cast<int>(_Tokens.size()); i++) {
        // Hard ceiling on nested expansions catches infinite macro recursion.
        if (MacroExpansionEnd.size() >= 128) {
            _CompileContext.PushError("Macro expansion limit exceeded", *this, _Tokens[i]->TextPosition());
            return;
        }
        while (!MacroExpansionEnd.empty() && i >= MacroExpansionEnd.back()) {
            MacroExpansionEnd.pop_back();
        }

        if (TokenIdentifier* Identifier = As<TokenIdentifier>(_Tokens[i])) {
            const std::string& Text = Identifier->Text;

            auto MacroIt = _CompileContext.Macros().find(Text);
            if (MacroIt != _CompileContext.Macros().end()) {
                auto& MacroTokens = MacroIt->second->LexContextRef().Tokens();

                _Tokens.erase(_Tokens.begin() + i);

                std::vector<IToken*> Inserted;
                Inserted.reserve(MacroTokens.size());
                for (IToken* T : MacroTokens) {
                    switch (T->Tag()) {
                        case TokenKind::Separator: {
                            auto* P = static_cast<TokenSeparator*>(T);
                            Inserted.push_back(Arena().New<TokenSeparator>(P->Context(), P->TextPosition(), P->Kind));
                            break;
                        }
                        case TokenKind::Operator: {
                            auto* P = static_cast<TokenOperator*>(T);
                            Inserted.push_back(Arena().New<TokenOperator>(P->Context(), P->TextPosition(), P->Kind));
                            break;
                        }
                        case TokenKind::Keyword: {
                            auto* P = static_cast<TokenKeyword*>(T);
                            Inserted.push_back(Arena().New<TokenKeyword>(P->Context(), P->TextPosition(), P->Kind));
                            break;
                        }
                        case TokenKind::Identifier: {
                            auto* P = static_cast<TokenIdentifier*>(T);
                            Inserted.push_back(Arena().New<TokenIdentifier>(P->Context(), P->TextPosition(), P->Text));
                            break;
                        }
                        case TokenKind::Number: {
                            auto* P = static_cast<TokenNumber*>(T);
                            Inserted.push_back(Arena().New<TokenNumber>(P->Context(), P->TextPosition(), P->Text,
                                                                        P->Value, P->IsConstant));
                            break;
                        }
                        case TokenKind::Int64: {
                            auto* P = static_cast<TokenInt64*>(T);
                            Inserted.push_back(
                                Arena().New<TokenInt64>(P->Context(), P->TextPosition(), P->Text, P->Value));
                            break;
                        }
                        case TokenKind::Boolean: {
                            auto* P = static_cast<TokenBoolean*>(T);
                            Inserted.push_back(Arena().New<TokenBoolean>(P->Context(), P->TextPosition(), P->Value));
                            break;
                        }
                        case TokenKind::String: {
                            auto* P = static_cast<TokenString*>(T);
                            Inserted.push_back(
                                Arena().New<TokenString>(P->Context(), P->TextPosition(), P->Text, P->Value));
                            break;
                        }
                        case TokenKind::Function: {
                            auto* P = static_cast<TokenFunction*>(T);
                            Inserted.push_back(Arena().New<TokenFunction>(P->Context(), P->TextPosition(), P->Text,
                                                                          P->BuiltinFunction));
                            break;
                        }
                        case TokenKind::AssetReference: {
                            auto* P = static_cast<TokenAssetReference*>(T);
                            Inserted.push_back(Arena().New<TokenAssetReference>(
                                P->Context(), P->TextPosition(), P->Text, P->AssetId, P->IsRoomInstanceAsset));
                            break;
                        }
                        case TokenKind::Variable: {
                            auto* P = static_cast<TokenVariable*>(T);
                            Inserted.push_back(Arena().New<TokenVariable>(P->Context(), P->TextPosition(), P->Text,
                                                                          P->BuiltinVariable));
                            break;
                        }
                    }
                }
                int InsertedCount = static_cast<int>(Inserted.size());
                _Tokens.insert(_Tokens.begin() + i, Inserted.begin(), Inserted.end());

                if (InsertedCount > 0) {
                    // Rewind so the expanded tokens get reclassified, and shift any outer
                    // expansion boundaries to account for the net insertion.
                    i--;
                    for (auto& End : MacroExpansionEnd)
                        End += InsertedCount - 1;
                    MacroExpansionEnd.push_back(i + InsertedCount + 1);
                }
                continue;
            }

            // An identifier followed by '(' is a function call, even if its name isn't
            // a known builtin. The builtin pointer may be null, resolved later.
            if (i + 1 < static_cast<int>(_Tokens.size())) {
                if (TokenSeparator* Sep = As<TokenSeparator>(_Tokens[i + 1]);
                    Sep != nullptr && Sep->Kind == SeparatorKind::GroupOpen) {
                    IBuiltinFunction* BuiltinFunction =
                        _CompileContext.GameContext().Builtins().LookupBuiltinFunction(Text);
                    _Tokens[i] = Arena().New<TokenFunction>(Identifier->Context(), Identifier->TextPosition(),
                                                            Identifier->Text, BuiltinFunction);
                    continue;
                }
            }

            int AssetId = 0;
            bool IsAsset = _CompileContext.GameContext().GetAssetId(Text, AssetId);
            bool IsRoomInstanceAsset = false;
            if (!IsAsset) {
                IsRoomInstanceAsset = _CompileContext.GameContext().GetRoomInstanceId(Text, AssetId);
            }
            int Unused = 0;
            // Script names take precedence over asset names that happen to collide.
            if ((IsAsset || IsRoomInstanceAsset) && !_CompileContext.GameContext().GetScriptId(Text, Unused)) {
                _Tokens[i] = Arena().New<TokenAssetReference>(Identifier->Context(), Identifier->TextPosition(),
                                                              Identifier->Text, AssetId, IsRoomInstanceAsset);
                continue;
            }

            if (Text == "true" || Text == "false") {
                _Tokens[i] =
                    Arena().New<TokenBoolean>(Identifier->Context(), Identifier->TextPosition(), Text == "true");
                continue;
            }

            double ConstantDouble = 0.0;
            if (_CompileContext.GameContext().Builtins().LookupConstantDouble(Text, ConstantDouble)) {
                _Tokens[i] = Arena().New<TokenNumber>(Identifier->Context(), Identifier->TextPosition(), Text,
                                                      ConstantDouble, true);
                continue;
            }

            IBuiltinVariable* BuiltinVariable = _CompileContext.GameContext().Builtins().LookupBuiltinVariable(Text);
            _CompileContext.GameContext().CodeBuilder().OnParseNameIdentifier(Text);
            _Tokens[i] = Arena().New<TokenVariable>(Identifier->Context(), Identifier->TextPosition(), Identifier->Text,
                                                    BuiltinVariable);
        }
    }
}

// Lazily index newline offsets the first time a caller asks for line/column;
// subsequent lookups are a single binary search over the cached table.
std::pair<int, int> LexContext::GetLineAndColumnFromPos(int textPosition) {
    if (!_LineIndices) {
        _LineIndices = std::make_unique<std::vector<int>>();
        _LineIndices->reserve(128);
        for (int i = 0; i < static_cast<int>(_Text.size()); i++) {
            if (_Text[i] == '\n')
                _LineIndices->push_back(i);
        }
    }

    auto It = std::lower_bound(_LineIndices->begin(), _LineIndices->end(), textPosition);
    if (It != _LineIndices->end() && *It == textPosition) {
        int Idx = static_cast<int>(It - _LineIndices->begin());
        return { Idx + 1, 1 };
    }
    int Idx = static_cast<int>(It - _LineIndices->begin()) - 1;
    int PrevNewlinePos = (Idx == -1) ? -1 : (*_LineIndices)[Idx];
    int Column = textPosition - PrevNewlinePos - 1;
    return { Idx + 2, Column };
}

} // namespace Underanalyzer::Compiler::Lexer
