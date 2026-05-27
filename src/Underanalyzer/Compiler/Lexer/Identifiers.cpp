
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Lexer/Identifiers.h"

#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Lexer/ContiguousTextReader.h"
#include "Underanalyzer/Compiler/Lexer/LexContext.h"
#include "Underanalyzer/IGameContext.h"

namespace Underanalyzer::Compiler::Lexer {

int Identifiers::Parse(LexContext& context, int startPosition) {
    std::string_view identifier;
    int pos = ContiguousTextReader::ReadWhileIdentifier(context.Text(), startPosition, identifier);

    bool isGMLv2 = context.CompileContextRef().GameContext().UsingGMLv2();
    KeywordKind keywordKind = KeywordKind::None;
    if (identifier == "if")
        keywordKind = KeywordKind::If;
    else if (identifier == "then")
        keywordKind = KeywordKind::Then;
    else if (identifier == "else")
        keywordKind = KeywordKind::Else;
    else if (identifier == "switch")
        keywordKind = KeywordKind::Switch;
    else if (identifier == "case")
        keywordKind = KeywordKind::Case;
    else if (identifier == "default")
        keywordKind = KeywordKind::Default;
    else if (identifier == "begin")
        keywordKind = KeywordKind::Begin;
    else if (identifier == "end")
        keywordKind = KeywordKind::End;
    else if (identifier == "break")
        keywordKind = KeywordKind::Break;
    else if (identifier == "continue")
        keywordKind = KeywordKind::Continue;
    else if (identifier == "exit")
        keywordKind = KeywordKind::Exit;
    else if (identifier == "return")
        keywordKind = KeywordKind::Return;
    else if (identifier == "while")
        keywordKind = KeywordKind::While;
    else if (identifier == "for")
        keywordKind = KeywordKind::For;
    else if (identifier == "repeat")
        keywordKind = KeywordKind::Repeat;
    else if (identifier == "do")
        keywordKind = KeywordKind::Do;
    else if (identifier == "until")
        keywordKind = KeywordKind::Until;
    else if (identifier == "with")
        keywordKind = KeywordKind::With;
    else if (identifier == "var")
        keywordKind = KeywordKind::Var;
    else if (identifier == "globalvar")
        keywordKind = KeywordKind::Globalvar;
    else if (identifier == "not")
        keywordKind = KeywordKind::Not;
    else if (identifier == "div")
        keywordKind = KeywordKind::Div;
    else if (identifier == "mod")
        keywordKind = KeywordKind::Mod;
    else if (identifier == "and")
        keywordKind = KeywordKind::And;
    else if (identifier == "or")
        keywordKind = KeywordKind::Or;
    else if (identifier == "xor")
        keywordKind = KeywordKind::Xor;
    else if (identifier == "enum")
        keywordKind = KeywordKind::Enum;
    // GMLv2 introduced try/catch/finally/throw/new/delete/function/static as keywords;
    // older GameMaker versions parse these names as ordinary identifiers.
    else if (isGMLv2 && identifier == "try")
        keywordKind = KeywordKind::Try;
    else if (isGMLv2 && identifier == "catch")
        keywordKind = KeywordKind::Catch;
    else if (isGMLv2 && identifier == "finally")
        keywordKind = KeywordKind::Finally;
    else if (isGMLv2 && identifier == "throw")
        keywordKind = KeywordKind::Throw;
    else if (isGMLv2 && identifier == "new")
        keywordKind = KeywordKind::New;
    else if (isGMLv2 && identifier == "delete")
        keywordKind = KeywordKind::Delete;
    else if (isGMLv2 && identifier == "function")
        keywordKind = KeywordKind::Function;
    else if (isGMLv2 && identifier == "static")
        keywordKind = KeywordKind::Static;

    if (keywordKind != KeywordKind::None) {
        context.Tokens().push_back(context.Arena().New<TokenKeyword>(context, startPosition, keywordKind));
    } else {
        context.Tokens().push_back(
            context.Arena().New<TokenIdentifier>(context, startPosition, std::string(identifier)));
    }
    return pos;
}

int Identifiers::ParseInternal(LexContext& context, int startPosition) {
    std::string_view identifier;
    bool anyNormalChars = false;
    int pos =
        ContiguousTextReader::ReadWhileInternalIdentifier(context.Text(), startPosition, identifier, anyNormalChars);

    if (!anyNormalChars) {
        context.CompileContextRef().PushError("Unrecognized token", context, pos);
    } else {
        context.Tokens().push_back(
            context.Arena().New<TokenIdentifier>(context, startPosition, std::string(identifier)));
    }
    return pos;
}

} // namespace Underanalyzer::Compiler::Lexer
