
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Errors/LexerError.h"

#include "Underanalyzer/Compiler/Lexer/LexContext.h"

namespace Underanalyzer::Compiler::Errors {

LexerError::LexerError(std::string baseMessage, Lexer::LexContext& context, int textPosition)
    : _baseMessage(std::move(baseMessage)), _lexContext(&context), _textPosition(textPosition) {
}

std::string LexerError::GenerateMessage() const {
    auto [line, column] = _lexContext->GetLineAndColumnFromPos(_textPosition);
    if (_lexContext->MacroName().has_value()) {
        return _baseMessage + " on line " + std::to_string(line) + ", column " + std::to_string(column) +
               " of macro \"" + *_lexContext->MacroName() + "\"";
    }
    return _baseMessage + " on line " + std::to_string(line) + ", column " + std::to_string(column);
}

} // namespace Underanalyzer::Compiler::Errors
