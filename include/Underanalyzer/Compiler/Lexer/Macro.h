
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <memory>
#include <string>

namespace Underanalyzer::Compiler::Lexer {

class LexContext;

class Macro {
  public:
    Macro(std::unique_ptr<LexContext> lexContext, std::string name);
    ~Macro();

    const std::string& Name() const {
        return _name;
    }
    LexContext& LexContextRef() {
        return *_lexContext;
    }

  private:
    std::string _name;
    std::unique_ptr<LexContext> _lexContext;
};

} // namespace Underanalyzer::Compiler::Lexer
