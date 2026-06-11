
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/ISubCompileContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/NodeArena.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Underanalyzer::Compiler {
class CompileContext;
class FunctionScope;
} // namespace Underanalyzer::Compiler
namespace Underanalyzer::Compiler::Nodes {
class IASTNode;
class EnumDeclaration;
} // namespace Underanalyzer::Compiler::Nodes

namespace Underanalyzer::Compiler::Parser {

class TryStatementContext;

class ParseContext final : public ISubCompileContext {
  public:
    ParseContext(CompileContext& context, std::vector<Lexer::IToken*>& tokens);

    CompileContext& CompileContextRef() override {
        return _compileContext;
    }
    FunctionScope& CurrentScope() override {
        return *_currentScope;
    }
    void SetCurrentScope(FunctionScope& scope) override {
        _currentScope = &scope;
    }
    FunctionScope& RootScope() override {
        return *_rootScope;
    }
    void SetRootScope(FunctionScope& scope) override {
        _rootScope = &scope;
    }

    NodeArena& Arena() {
        return _arena;
    }
    template <class T, class... Args> T* Make(Args&&... args) {
        return _arena.New<T>(std::forward<Args>(args)...);
    }

    std::vector<Lexer::IToken*>& Tokens() {
        return _tokens;
    }
    Nodes::IASTNode* Root() const {
        return _root;
    }
    void SetRoot(Nodes::IASTNode* root) {
        _root = root;
    }

    std::unordered_map<std::string, Nodes::EnumDeclaration*>& ParseEnums() {
        return _parseEnums;
    }

    // Declaration order of parse enums. C# iterates a Dictionary, which yields
    // insertion order in practice; enum value resolution is order-sensitive
    // when enums reference each other, so the resolve loops walk this instead
    // of the unordered map.
    std::vector<Nodes::EnumDeclaration*>& ParseEnumOrder() {
        return _parseEnumOrder;
    }

    std::unordered_set<std::string>* ReleaseGlobalFunctionsPtr() {
        auto* p = _parseGlobalFunctions;
        _parseGlobalFunctions = nullptr;
        return p;
    }
    std::unordered_set<std::string>* ParseGlobalFunctionsPtr() const {
        return _parseGlobalFunctions;
    }

    int Position() const {
        return _position;
    }
    void SetPosition(int v) {
        _position = v;
    }
    bool EndOfCode() const {
        return _position >= static_cast<int>(_tokens.size());
    }

    int ExitReturnBreakContinueCount() const {
        return _exitReturnBreakContinueCount;
    }
    void SetExitReturnBreakContinueCount(int v) {
        _exitReturnBreakContinueCount = v;
    }
    int ThrowCount() const {
        return _throwCount;
    }
    void SetThrowCount(int v) {
        _throwCount = v;
    }
    int TryStatementProcessIndex() const {
        return _tryStatementProcessIndex;
    }
    void SetTryStatementProcessIndex(int v) {
        _tryStatementProcessIndex = v;
    }

    TryStatementContext* TryStatementCtx() const {
        return _tryStatementContext;
    }
    void SetTryStatementCtx(TryStatementContext* c) {
        _tryStatementContext = c;
    }

    bool ProcessingFinally() const {
        return _processingFinally;
    }
    void SetProcessingFinally(bool v) {
        _processingFinally = v;
    }
    bool ProcessingSwitch() const {
        return _processingSwitch;
    }
    void SetProcessingSwitch(bool v) {
        _processingSwitch = v;
    }

    void Parse();
    void PostProcessTree();

    void SkipSemicolons();

    Lexer::TokenSeparator* EnsureToken(Lexer::SeparatorKind kind);
    Lexer::IToken* EnsureToken(Lexer::SeparatorKind separatorKind, Lexer::KeywordKind keywordKind);
    Lexer::TokenKeyword* EnsureToken(Lexer::KeywordKind kind);

    bool IsCurrentToken(Lexer::SeparatorKind kind);
    bool IsCurrentToken(Lexer::OperatorKind kind);
    bool IsCurrentToken(Lexer::KeywordKind kind);
    bool IsCurrentToken(Lexer::SeparatorKind separatorKind, Lexer::KeywordKind keywordKind);

  private:
    CompileContext& _compileContext;
    NodeArena& _arena;
    std::vector<Lexer::IToken*>& _tokens;

    Nodes::IASTNode* _root = nullptr;
    FunctionScope* _currentScope = nullptr;
    FunctionScope* _rootScope = nullptr;

    std::unordered_map<std::string, Nodes::EnumDeclaration*> _parseEnums;
    std::vector<Nodes::EnumDeclaration*> _parseEnumOrder;
    std::unordered_set<std::string>* _parseGlobalFunctions = nullptr;

    int _position = 0;
    int _exitReturnBreakContinueCount = 0;
    int _throwCount = 0;
    int _tryStatementProcessIndex = 0;
    TryStatementContext* _tryStatementContext = nullptr;
    bool _processingFinally = false;
    bool _processingSwitch = false;
};

} // namespace Underanalyzer::Compiler::Parser
