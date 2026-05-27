
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/ExitNode.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Nodes/BlockNode.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/Compiler/Parser/TryStatementContext.h"

namespace Underanalyzer::Compiler::Nodes {

IASTNode* ExitNode::PostProcess(Parser::ParseContext& Context) {
    if (Context.ProcessingFinally()) {
        Context.CompileContextRef().PushError("Cannot use exit inside of finally block", _NearbyToken);
    }
    Parser::TryStatementContext* TryCtx = Context.TryStatementCtx();
    // Same shape as ReturnNode: each pending finally is duplicated ahead of the exit
    // so all enclosing finally blocks run before the function actually returns.
    if (TryCtx != nullptr && TryCtx->HasFinally()) {
        std::vector<IASTNode*>& FinallyNodes = Context.CurrentScope().TryFinallyNodes();
        BlockNode* NewBlock = BlockNode::CreateEmpty(Context, _NearbyToken, 1 + static_cast<int>(FinallyNodes.size()));
        for (int i = static_cast<int>(FinallyNodes.size()) - 1; i >= 0; i--) {
            NewBlock->Children().push_back(FinallyNodes[i]->Duplicate(Context));
        }
        NewBlock->Children().push_back(this);
        return NewBlock;
    }
    return this;
}

void ExitNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;
    if (Context.FunctionCallBeforeExit() != nullptr) {
        Context.EmitCall(Bytecode::FunctionPatch::FromBuiltin(Context, *Context.FunctionCallBeforeExit()), 0);
    }
    Context.GenerateControlFlowCleanup();
    Context.Emit(Op::Exit, DT::Int32);
}

} // namespace Underanalyzer::Compiler::Nodes
