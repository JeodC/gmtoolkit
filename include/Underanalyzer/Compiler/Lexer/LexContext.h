
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/ISubCompileContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/NodeArena.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Underanalyzer::Compiler {
class CompileContext;
class FunctionScope;
} // namespace Underanalyzer::Compiler

namespace Underanalyzer::Compiler::Lexer {

class LexContext final : public ISubCompileContext {
  public:
    LexContext(CompileContext& context, std::string text);
    LexContext(CompileContext& context, std::string text, std::string macroName);

    CompileContext& CompileContextRef() override {
        return _CompileContext;
    }
    FunctionScope& CurrentScope() override;
    void SetCurrentScope(FunctionScope&) override;
    FunctionScope& RootScope() override;
    void SetRootScope(FunctionScope&) override;

    NodeArena& Arena();
    template <class T, class... Args> T* Make(Args&&... Args2) {
        return Arena().New<T>(std::forward<Args>(Args2)...);
    }

    const std::string& Text() const {
        return _Text;
    }
    const std::optional<std::string>& MacroName() const {
        return _MacroName;
    }
    std::vector<IToken*>& Tokens() {
        return _Tokens;
    }

    void Tokenize();
    void PostProcessTokens();

    std::pair<int, int> GetLineAndColumnFromPos(int textPosition);

  private:
    CompileContext& _CompileContext;
    std::string _Text;
    std::optional<std::string> _MacroName;
    std::vector<IToken*> _Tokens;
    std::unique_ptr<std::vector<int>> _LineIndices;
};

} // namespace Underanalyzer::Compiler::Lexer
