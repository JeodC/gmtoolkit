
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Lexer/Tags.h"

#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Lexer/ContiguousTextReader.h"
#include "Underanalyzer/Compiler/Lexer/LexContext.h"
#include "Underanalyzer/Compiler/Lexer/Macro.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <string>

namespace Underanalyzer::Compiler::Lexer {

int Tags::Parse(LexContext& context, int startPosition) {
    std::string_view text = context.Text();

    std::string_view directiveType;
    int pos = ContiguousTextReader::ReadWhileIdentifier(text, startPosition + 1, directiveType);

    if (directiveType == "macro") {
        // Nested macros are not allowed: the outer LexContext has a MacroName already.
        if (context.MacroName().has_value()) {
            context.CompileContextRef().PushError("Invalid #macro syntax found", context, startPosition);
            return pos;
        }

        std::string_view macroName;
        pos = ContiguousTextReader::ReadUntilWhitespace(text, pos + 1, macroName);

        pos++;
        std::string macroContent;
        macroContent.reserve(64);
        while (pos < static_cast<int>(text.size()) && text[pos] != '\n') {
            char curr = text[pos];
            // A trailing backslash continues the macro to the next line; otherwise
            // the backslash is just a literal character and we rewind to keep it.
            if (curr == '\\' && pos + 1 < static_cast<int>(text.size())) {
                int backslashPos = pos++;
                do {
                    curr = text[pos++];
                } while (pos < static_cast<int>(text.size()) && std::isspace(static_cast<unsigned char>(curr)) &&
                         curr != '\n');

                if (curr == '\n') {
                    continue;
                }
                pos = backslashPos + 1;
                macroContent.push_back('\\');
                continue;
            }
            macroContent.push_back(curr);
            pos++;
        }

        std::string macroNameStr(macroName);
        auto newLexContext =
            std::make_unique<LexContext>(context.CompileContextRef(), std::move(macroContent), macroNameStr);
        LexContext* rawPtr = newLexContext.get();
        auto newMacro = std::make_shared<Macro>(std::move(newLexContext), macroNameStr);

        auto [it, inserted] = context.CompileContextRef().Macros().try_emplace(macroNameStr, newMacro);
        if (!inserted) {
            context.CompileContextRef().PushError("Duplicate macro \"" + macroNameStr + "\" found", context,
                                                  startPosition);
        } else {
            rawPtr->Tokenize();
        }
        return pos;
    }

    if (directiveType == "region" || directiveType == "endregion") {
        while (pos < static_cast<int>(text.size()) && text[pos] != '\n')
            pos++;
        return pos;
    }

    // #RRGGBB or #AARRGGBB color literals: reorder bytes so the parsed integer
    // matches GML's BGR(A) packing rather than the source-order ARGB hex string.
    if (directiveType.size() == 6 || directiveType.size() == 8) {
        bool valid = true;
        for (char hex : directiveType) {
            if ((hex < '0' || hex > '9') && (hex < 'A' || hex > 'F') && (hex < 'a' || hex > 'f')) {
                valid = false;
                break;
            }
        }
        if (valid) {
            char converted[8];
            converted[0] = directiveType[4];
            converted[1] = directiveType[5];
            converted[2] = directiveType[2];
            converted[3] = directiveType[3];
            converted[4] = directiveType[0];
            converted[5] = directiveType[1];
            if (directiveType.size() == 8) {
                converted[6] = directiveType[6];
                converted[7] = directiveType[7];
            }
            int64_t value = 0;
            auto result = std::from_chars(converted, converted + directiveType.size(), value, 16);
            if (result.ec == std::errc{} && result.ptr == converted + directiveType.size()) {
                std::string display = "#" + std::string(directiveType);
                if (value >= INT32_MIN && value <= INT32_MAX) {
                    context.Tokens().push_back(context.Arena().New<TokenNumber>(
                        context, startPosition, std::move(display), static_cast<double>(value)));
                } else {
                    context.Tokens().push_back(
                        context.Arena().New<TokenInt64>(context, startPosition, std::move(display), value));
                }
                return pos;
            }
        }
    }

    context.CompileContextRef().PushError("Unrecognized tag or directive \"" + std::string(directiveType) + "\"",
                                          context, startPosition);
    return pos;
}

} // namespace Underanalyzer::Compiler::Lexer
