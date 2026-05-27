
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"

#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/IBuiltins.h"
#include "Underanalyzer/Compiler/ISubCompileContext.h"
#include "Underanalyzer/IGameContext.h"

namespace Underanalyzer::Compiler::Bytecode {

FunctionPatch FunctionPatch::FromBuiltin(ISubCompileContext& context, const std::string& builtinName) {
    return FunctionPatch(&context.CurrentScope(), builtinName,
                         context.CompileContextRef().GameContext().Builtins().LookupBuiltinFunction(builtinName));
}

} // namespace Underanalyzer::Compiler::Bytecode
