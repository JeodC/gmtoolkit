
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Errors/ICompileError.h"

namespace Underanalyzer::Compiler::Lexer {
class LexContext;
}

namespace Underanalyzer::Compiler::Errors {

class LexerError final : public ICompileError {
  public:
    LexerError(std::string baseMessage, Lexer::LexContext& context, int textPosition);

    const std::string& BaseMessage() const override {
        return _baseMessage;
    }
    std::string GenerateMessage() const override;

  private:
    std::string _baseMessage;
    Lexer::LexContext* _lexContext;
    int _textPosition;
};

} // namespace Underanalyzer::Compiler::Errors
