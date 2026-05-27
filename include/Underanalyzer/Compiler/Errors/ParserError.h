
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Errors/ICompileError.h"

namespace Underanalyzer::Compiler::Lexer {
class IToken;
}

namespace Underanalyzer::Compiler::Errors {

class ParserError final : public ICompileError {
  public:
    ParserError(std::string baseMessage, Lexer::IToken* nearbyToken = nullptr);

    const std::string& BaseMessage() const override {
        return _baseMessage;
    }
    std::string GenerateMessage() const override;

  private:
    std::string _baseMessage;
    Lexer::IToken* _nearbyToken;
};

} // namespace Underanalyzer::Compiler::Errors
