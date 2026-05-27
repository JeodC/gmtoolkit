
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Bytecode/BranchPatch.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"

namespace Underanalyzer::Compiler::Bytecode {

// Record the branch's start address now (Position is post-emit; back off by its size)
// and resolve to a delta against the final target when Patch is called later.
void MultiForwardBranchPatch::AddInstruction(BytecodeContext& context, IGMInstruction* instruction) {
    _instructions.emplace_back(instruction, context.Position() - IGMInstruction::GetSize(*instruction));
}

void MultiForwardBranchPatch::Patch(BytecodeContext& context) {
    int destAddress = context.Position();
    for (auto& [instruction, address] : _instructions) {
        context.PatchBranch(instruction, destAddress - address);
    }
}

MultiBackwardBranchPatch::MultiBackwardBranchPatch(BytecodeContext& context) : _destAddress(context.Position()) {
}

void MultiBackwardBranchPatch::AddInstruction(BytecodeContext& context, IGMInstruction* instruction) {
    context.PatchBranch(instruction, _destAddress - (context.Position() - IGMInstruction::GetSize(*instruction)));
}

MultiBackwardBranchPatchTracked::MultiBackwardBranchPatchTracked(BytecodeContext& context)
    : _destAddress(context.Position()) {
}

void MultiBackwardBranchPatchTracked::AddInstruction(BytecodeContext& context, IGMInstruction* instruction) {
    _numberUsed++;
    context.PatchBranch(instruction, _destAddress - (context.Position() - IGMInstruction::GetSize(*instruction)));
}

SingleForwardBranchPatch::SingleForwardBranchPatch(BytecodeContext& context, IGMInstruction* instruction)
    : _instruction(instruction), _startAddress(context.Position() - IGMInstruction::GetSize(*instruction)) {
}

void SingleForwardBranchPatch::Patch(BytecodeContext& context) {
    context.PatchBranch(_instruction, context.Position() - _startAddress);
}

} // namespace Underanalyzer::Compiler::Bytecode
