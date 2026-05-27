
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/CompileContext.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Errors/LexerError.h"
#include "Underanalyzer/Compiler/Errors/ParserError.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Lexer/LexContext.h"
#include "Underanalyzer/Compiler/Nodes/IASTNode.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"

#include <stdexcept>

namespace Underanalyzer::Compiler {

CompileContext::CompileContext(std::string code, CompileScriptKind scriptKind,
                               std::optional<std::string> globalScriptName, IGameContext& gameContext)
    : _code(std::move(code)), _scriptKind(scriptKind), _globalScriptName(std::move(globalScriptName)),
      _gameContext(&gameContext) {
}

CompileContext::~CompileContext() = default;

void CompileContext::Parse() {
    if (_startedParse)
        throw std::logic_error("Code parse already started");
    _startedParse = true;

    _rootLexContext = std::make_unique<Lexer::LexContext>(*this, _code);
    _rootLexContext->Tokenize();
    if (HasErrors())
        return;

    _rootLexContext->PostProcessTokens();
    if (HasErrors())
        return;

    Parser::ParseContext parseContext(*this, _rootLexContext->Tokens());
    parseContext.Parse();
    if (HasErrors())
        return;

    parseContext.PostProcessTree();
    if (HasErrors())
        return;

    _parseRootNode = parseContext.Root();
    _parseRootScope = &parseContext.RootScope();
    _parseGlobalFunctions = parseContext.ReleaseGlobalFunctionsPtr();
}

void CompileContext::Compile(int initialPosition) {
    if (_startedCompile)
        throw std::logic_error("Code compile already started");
    _startedCompile = true;

    if (!_startedParse)
        Parse();
    if (_parseRootNode == nullptr || _parseRootScope == nullptr)
        return;

    Bytecode::BytecodeContext bytecodeContext(*this, _parseRootNode, *_parseRootScope, _parseGlobalFunctions);
    bytecodeContext.GenerateCode(initialPosition);
    if (HasErrors())
        return;

    _outputInstructions = bytecodeContext.Instructions();
    _outputFunctionEntries = std::move(bytecodeContext.FunctionEntries());
    _outputRootScope = _parseRootScope;
    _outputLength = bytecodeContext.Position();
    _compilePatches = std::move(bytecodeContext.Patches());

    _parseRootNode = nullptr;
    _parseRootScope = nullptr;

    _parseGlobalFunctions = nullptr;
}

void CompileContext::Link() {
    if (_startedLink)
        throw std::logic_error("Code link already started");
    _startedLink = true;
    if (!_startedCompile || _outputInstructions.empty()) {
        throw std::logic_error("Code compile was not completed; linking may not occur");
    }
    Bytecode::BytecodeContext::PatchInstructions(*this, _compilePatches);
    _compilePatches = Bytecode::InstructionPatches{};
}

void CompileContext::PushError(std::string message, Lexer::LexContext& lexContext, int textPosition) {
    _errors.push_back(std::make_unique<Errors::LexerError>(std::move(message), lexContext, textPosition));
}

void CompileContext::PushError(std::string message, Lexer::IToken* nearbyToken) {
    _errors.push_back(std::make_unique<Errors::ParserError>(std::move(message), nearbyToken));
}

} // namespace Underanalyzer::Compiler
