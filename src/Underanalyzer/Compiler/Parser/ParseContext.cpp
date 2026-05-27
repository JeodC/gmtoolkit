
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Parser/ParseContext.h"

#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Nodes/BlockNode.h"
#include "Underanalyzer/Compiler/Nodes/EnumDeclaration.h"
#include "Underanalyzer/Compiler/Nodes/IASTNode.h"

namespace Underanalyzer::Compiler::Parser {

using namespace Lexer;

ParseContext::ParseContext(CompileContext& context, std::vector<Lexer::IToken*>& tokens)
    : _compileContext(context), _arena(context.Arena()), _tokens(tokens) {

    _rootScope = Make<FunctionScope>(nullptr, false);
    _currentScope = _rootScope;
    if (context.ScriptKind() == CompileScriptKind::GlobalScript) {

        _parseGlobalFunctions = Make<std::unordered_set<std::string>>();
        _parseGlobalFunctions->reserve(8);
    }
}

void ParseContext::Parse() {
    _root = Nodes::BlockNode::ParseRoot(*this);
}

// Run after Parse(): folds enum values (they may forward-reference each other)
// and lets each node rewrite/replace itself as a final tree-shaping pass.
void ParseContext::PostProcessTree() {
    Nodes::EnumDeclaration::ResolveValues(*this);
    if (_root != nullptr) {
        _root = _root->PostProcess(*this);
    }
}

void ParseContext::SkipSemicolons() {
    while (!EndOfCode()) {
        auto* sep = As<TokenSeparator>(_tokens[_position]);
        if (sep == nullptr || sep->Kind != SeparatorKind::Semicolon)
            break;
        _position++;
    }
}

TokenSeparator* ParseContext::EnsureToken(SeparatorKind kind) {
    if (EndOfCode()) {
        _compileContext.PushError("Unexpected end of code (expected '" + TokenSeparator::KindToString(kind) + "')");
        return nullptr;
    }
    IToken* token = _tokens[_position++];
    auto* separator = As<TokenSeparator>(token);
    if (separator == nullptr || separator->Kind != kind) {
        _compileContext.PushError("Expected '" + TokenSeparator::KindToString(kind) + "', got '...'", token);
        return nullptr;
    }
    return separator;
}

IToken* ParseContext::EnsureToken(SeparatorKind separatorKind, KeywordKind keywordKind) {
    if (EndOfCode()) {
        _compileContext.PushError("Unexpected end of code (expected '" + TokenSeparator::KindToString(separatorKind) +
                                  "' or '" + TokenKeyword::KindToString(keywordKind) + "')");
        return nullptr;
    }
    IToken* token = _tokens[_position++];
    auto* sep = As<TokenSeparator>(token);
    auto* kw = As<TokenKeyword>(token);
    if ((sep == nullptr || sep->Kind != separatorKind) && (kw == nullptr || kw->Kind != keywordKind)) {
        _compileContext.PushError("Expected '" + TokenSeparator::KindToString(separatorKind) + "' or '" +
                                      TokenKeyword::KindToString(keywordKind) + "', got '...'",
                                  token);
        return nullptr;
    }
    return token;
}

TokenKeyword* ParseContext::EnsureToken(KeywordKind kind) {
    if (EndOfCode()) {
        _compileContext.PushError("Unexpected end of code (expected '" + TokenKeyword::KindToString(kind) + "')");
        return nullptr;
    }
    IToken* token = _tokens[_position++];
    auto* kw = As<TokenKeyword>(token);
    if (kw == nullptr || kw->Kind != kind) {
        _compileContext.PushError("Expected '" + TokenKeyword::KindToString(kind) + "', got '...'", token);
        return nullptr;
    }
    return kw;
}

bool ParseContext::IsCurrentToken(SeparatorKind kind) {
    if (EndOfCode())
        return false;
    auto* sep = As<TokenSeparator>(_tokens[_position]);
    return sep != nullptr && sep->Kind == kind;
}

bool ParseContext::IsCurrentToken(OperatorKind kind) {
    if (EndOfCode())
        return false;
    auto* op = As<TokenOperator>(_tokens[_position]);
    return op != nullptr && op->Kind == kind;
}

bool ParseContext::IsCurrentToken(KeywordKind kind) {
    if (EndOfCode())
        return false;
    auto* kw = As<TokenKeyword>(_tokens[_position]);
    return kw != nullptr && kw->Kind == kind;
}

bool ParseContext::IsCurrentToken(SeparatorKind separatorKind, KeywordKind keywordKind) {
    if (EndOfCode())
        return false;
    auto* sep = As<TokenSeparator>(_tokens[_position]);
    if (sep != nullptr && sep->Kind == separatorKind)
        return true;
    auto* kw = As<TokenKeyword>(_tokens[_position]);
    return kw != nullptr && kw->Kind == keywordKind;
}

} // namespace Underanalyzer::Compiler::Parser
