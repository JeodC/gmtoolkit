
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Bytecode/ControlFlowContext.h"

#include "Underanalyzer/Compiler/Bytecode/BranchPatch.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/IGameContext.h"

namespace Underanalyzer::Compiler::Bytecode {

void LoopContext::UseBreak(BytecodeContext& context, IGMInstruction* instruction) {
    _breakPatch->AddInstruction(context, instruction);
}

void LoopContext::UseContinue(BytecodeContext& context, IGMInstruction* instruction) {
    _continuePatch->AddInstruction(context, instruction);
}

void WithLoopContext::GenerateCleanupCode(BytecodeContext& context) {
    context.EmitPopWithExit();
}

// Drop the loop counter sitting on the stack. Older bytecode left it implicit;
// GMLv2 requires an explicit PopDelete on early exits to keep the stack balanced.
void RepeatLoopContext::GenerateCleanupCode(BytecodeContext& context) {
    if (context.CompileContextRef().GameContext().UsingGMLv2()) {
        context.Emit(IGMInstruction::Opcode::PopDelete, IGMInstruction::DataType::Int32);
    }
}

void SwitchContext::GenerateCleanupCode(BytecodeContext& context) {
    context.Emit(IGMInstruction::Opcode::PopDelete, _expressionType);
}

void SwitchContext::UseBreak(BytecodeContext& context, IGMInstruction* instruction) {
    _breakPatch->AddInstruction(context, instruction);
}

void SwitchContext::UseContinue(BytecodeContext& context, IGMInstruction* instruction) {
    _continuePatch->AddInstruction(context, instruction);
}

} // namespace Underanalyzer::Compiler::Bytecode
