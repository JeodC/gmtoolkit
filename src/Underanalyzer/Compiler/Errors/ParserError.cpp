
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Errors/ParserError.h"

#include "Underanalyzer/Compiler/Lexer/LexContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"

namespace Underanalyzer::Compiler::Errors {

ParserError::ParserError(std::string baseMessage, Lexer::IToken* nearbyToken)
    : _baseMessage(std::move(baseMessage)), _nearbyToken(nearbyToken) {
}

std::string ParserError::GenerateMessage() const {
    if (_nearbyToken != nullptr) {
        auto [line, column] = _nearbyToken->Context().GetLineAndColumnFromPos(_nearbyToken->TextPosition());
        if (_nearbyToken->Context().MacroName().has_value()) {
            return _baseMessage + " around line " + std::to_string(line) + ", column " + std::to_string(column) +
                   " of macro \"" + *_nearbyToken->Context().MacroName() + "\"";
        }
        return _baseMessage + " around line " + std::to_string(line) + ", column " + std::to_string(column);
    }
    return _baseMessage;
}

} // namespace Underanalyzer::Compiler::Errors
