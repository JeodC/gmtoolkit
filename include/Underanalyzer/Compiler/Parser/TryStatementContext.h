
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <string>

namespace Underanalyzer::Compiler::Parser {

class TryStatementContext {
  public:
    TryStatementContext(std::string breakVariableName, std::string continueVariableName, bool hasFinally)
        : _breakVariableName(std::move(breakVariableName)), _continueVariableName(std::move(continueVariableName)),
          _hasFinally(hasFinally) {
    }

    const std::string& BreakVariableName() const {
        return _breakVariableName;
    }
    const std::string& ContinueVariableName() const {
        return _continueVariableName;
    }
    bool HasFinally() const {
        return _hasFinally;
    }

    bool HasBreakContinueVariable() const {
        return _hasBreakContinueVariable;
    }
    void SetHasBreakContinueVariable(bool v) {
        _hasBreakContinueVariable = v;
    }
    bool ShouldGenerateBreakContinueCode() const {
        return _shouldGenerateBreakContinueCode;
    }
    void SetShouldGenerateBreakContinueCode(bool v) {
        _shouldGenerateBreakContinueCode = v;
    }
    bool ThrowFinallyGeneration() const {
        return _throwFinallyGeneration;
    }
    void SetThrowFinallyGeneration(bool v) {
        _throwFinallyGeneration = v;
    }

  private:
    std::string _breakVariableName;
    std::string _continueVariableName;
    bool _hasFinally;
    bool _hasBreakContinueVariable = false;
    bool _shouldGenerateBreakContinueCode = true;
    bool _throwFinallyGeneration = false;
};

} // namespace Underanalyzer::Compiler::Parser
