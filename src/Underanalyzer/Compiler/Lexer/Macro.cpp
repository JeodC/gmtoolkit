
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Lexer/Macro.h"

#include "Underanalyzer/Compiler/Lexer/LexContext.h"

namespace Underanalyzer::Compiler::Lexer {

Macro::Macro(std::unique_ptr<LexContext> lexContext, std::string name)
    : _name(std::move(name)), _lexContext(std::move(lexContext)) {
}

Macro::~Macro() = default;

} // namespace Underanalyzer::Compiler::Lexer
