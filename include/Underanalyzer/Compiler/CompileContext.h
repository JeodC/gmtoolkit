
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"
#include "Underanalyzer/Compiler/NodeArena.h"
#include "Underanalyzer/GMEnum.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Underanalyzer {
class IGameContext;
class IGMInstruction;
} // namespace Underanalyzer
namespace Underanalyzer::Compiler::Lexer {
class Macro;
class IToken;
class LexContext;
} // namespace Underanalyzer::Compiler::Lexer
namespace Underanalyzer::Compiler::Errors {
class ICompileError;
}
namespace Underanalyzer::Compiler::Nodes {
class IASTNode;
}
namespace Underanalyzer::Compiler::Bytecode {
class FunctionEntry;
}

namespace Underanalyzer::Compiler {

class FunctionScope;

enum class CompileScriptKind { Script, GlobalScript, RoomCreationCode, ObjectEvent, Timeline };

class CompileContext {
  public:
    CompileContext(std::string code, CompileScriptKind scriptKind, std::optional<std::string> globalScriptName,
                   IGameContext& gameContext);
    ~CompileContext();

    const std::string& Code() const {
        return _code;
    }
    CompileScriptKind ScriptKind() const {
        return _scriptKind;
    }
    const std::optional<std::string>& GlobalScriptName() const {
        return _globalScriptName;
    }
    IGameContext& GameContext() {
        return *_gameContext;
    }

    NodeArena& Arena() {
        return _arena;
    }

    std::unordered_map<std::string, std::shared_ptr<Lexer::Macro>>& Macros() {
        return _macros;
    }
    std::unordered_map<std::string, std::shared_ptr<GMEnum>>& Enums() {
        return _enums;
    }

    const std::vector<std::unique_ptr<Errors::ICompileError>>& Errors() const {
        return _errors;
    }
    bool HasErrors() const {
        return !_errors.empty();
    }

    const std::vector<IGMInstruction*>& OutputInstructions() const {
        return _outputInstructions;
    }
    FunctionScope* OutputRootScope() const {
        return _outputRootScope;
    }
    const std::unordered_set<std::string>* OutputGlobalFunctionNames() const {
        return _parseGlobalFunctions;
    }
    const std::vector<Bytecode::FunctionEntry*>& OutputFunctionEntries() const {
        return _outputFunctionEntries;
    }
    int OutputLength() const {
        return _outputLength;
    }

    void Parse();
    void Compile(int initialPosition = 0);
    void Link();

    void PushError(std::string message, Lexer::LexContext& lexContext, int textPosition);
    void PushError(std::string message, Lexer::IToken* nearbyToken = nullptr);

  private:
    std::string _code;
    CompileScriptKind _scriptKind;
    std::optional<std::string> _globalScriptName;
    IGameContext* _gameContext;
    std::unordered_map<std::string, std::shared_ptr<Lexer::Macro>> _macros;
    std::unordered_map<std::string, std::shared_ptr<GMEnum>> _enums;
    std::vector<std::unique_ptr<Errors::ICompileError>> _errors;

    NodeArena _arena;

    std::unique_ptr<Lexer::LexContext> _rootLexContext;

    std::vector<IGMInstruction*> _outputInstructions;
    FunctionScope* _outputRootScope = nullptr;
    std::vector<Bytecode::FunctionEntry*> _outputFunctionEntries;
    int _outputLength = 0;

    bool _startedParse = false;
    Nodes::IASTNode* _parseRootNode = nullptr;
    FunctionScope* _parseRootScope = nullptr;
    std::unordered_set<std::string>* _parseGlobalFunctions = nullptr;
    bool _startedCompile = false;
    bool _compileSucceeded = false;
    Bytecode::InstructionPatches _compilePatches;
    bool _startedLink = false;
};

} // namespace Underanalyzer::Compiler
