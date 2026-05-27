
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/BreakNode.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/ControlFlowContext.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Nodes/AssignNode.h"
#include "Underanalyzer/Compiler/Nodes/BlockNode.h"
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/Compiler/Parser/TryStatementContext.h"
#include "Underanalyzer/IGameContext.h"

namespace Underanalyzer::Compiler::Nodes {

IASTNode* BreakNode::PostProcess(Parser::ParseContext& Context) {
    if (Context.ProcessingFinally()) {
        Context.CompileContextRef().PushError("Cannot use break inside of finally block", _NearbyToken);
    }
    Parser::TryStatementContext* TryCtx = Context.TryStatementCtx();
    if (TryCtx != nullptr && !Context.ProcessingSwitch()) {
        if (!Context.CompileContextRef().GameContext().UsingBetterTryBreakContinue() ||
            TryCtx->ShouldGenerateBreakContinueCode()) {
            Context.CurrentScope().DeclareLocal(TryCtx->BreakVariableName());
            BlockNode* Block = BlockNode::CreateEmpty(Context, _NearbyToken, 2);
            Block->Children().push_back(Context.Make<AssignNode>(
                AssignNode::AssignKind::Normal,
                Context.Make<SimpleVariableNode>(TryCtx->BreakVariableName(), (IBuiltinVariable*)nullptr,
                                                 IGMInstruction::InstanceType::Local),
                Context.Make<NumberNode>(1.0, _NearbyToken)));
            Block->Children().push_back(this);
            TryCtx->SetHasBreakContinueVariable(true);
            return Block;
        }
    }
    return this;
}

void BreakNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    if (!Context.AnyControlFlowContexts()) {
        Context.CompileContextRef().PushError("Break used outside of any loop or switch statement", _NearbyToken);
        return;
    }
    Context.GetTopControlFlowContext()->UseBreak(Context, Context.Emit(IGMInstruction::Opcode::Branch));
}

} // namespace Underanalyzer::Compiler::Nodes
