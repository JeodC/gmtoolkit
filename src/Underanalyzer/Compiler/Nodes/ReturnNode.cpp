
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/ReturnNode.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/Nodes/AssignNode.h"
#include "Underanalyzer/Compiler/Nodes/BlockNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/Compiler/Parser/TryStatementContext.h"
#include "Underanalyzer/VMConstants.h"

namespace Underanalyzer::Compiler::Nodes {

IASTNode* ReturnNode::PostProcess(Parser::ParseContext& Context) {
    ReturnValue = ReturnValue->PostProcess(Context);

    if (Context.ProcessingFinally()) {
        Context.CompileContextRef().PushError("Cannot use return inside of finally block", _NearbyToken);
    }
    Parser::TryStatementContext* TryCtx = Context.TryStatementCtx();
    // A 'return expr' inside a try with finally is rewritten to: stash the value in a
    // local, run each nested finally block (inner-to-outer), then return the stashed
    // value. That way finally runs before the function exits.
    if (TryCtx != nullptr && TryCtx->HasFinally()) {
        std::vector<IASTNode*>& FinallyNodes = Context.CurrentScope().TryFinallyNodes();
        BlockNode* NewBlock = BlockNode::CreateEmpty(Context, _NearbyToken, 2 + static_cast<int>(FinallyNodes.size()));

        Context.CurrentScope().DeclareLocal(std::string(VMConstants::TryCopyVariable));
        SimpleVariableNode* Variable = Context.Make<SimpleVariableNode>(
            std::string(VMConstants::TryCopyVariable), (IBuiltinVariable*)nullptr, IGMInstruction::InstanceType::Local);

        NewBlock->Children().push_back(Context.Make<AssignNode>(AssignNode::AssignKind::Normal, Variable, ReturnValue));
        for (int i = static_cast<int>(FinallyNodes.size()) - 1; i >= 0; i--) {
            NewBlock->Children().push_back(FinallyNodes[i]->Duplicate(Context));
        }
        NewBlock->Children().push_back(Context.Make<ReturnNode>(_NearbyToken, Variable));
        return NewBlock;
    }
    return this;
}

IASTNode* ReturnNode::Duplicate(Parser::ParseContext& Context) {
    return Context.Make<ReturnNode>(_NearbyToken, ReturnValue->Duplicate(Context));
}

void ReturnNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;
    using IT = IGMInstruction::InstanceType;

    ReturnValue->GenerateCode(Context);
    Context.ConvertDataType(DT::Variable);

    // When cleanup is required (open with/repeat/switch or try-unhook), stash the
    // return value in a local first so cleanup ops can mutate the stack freely,
    // then reload it just before the Return instruction.
    if (Context.DoAnyControlFlowRequireCleanup() || Context.FunctionCallBeforeExit() != nullptr) {
        Bytecode::VariablePatch TempVariable(std::string(VMConstants::TempReturnVariable), IT::Local);
        Context.Emit(Op::Pop, TempVariable, DT::Variable, DT::Variable);
        Context.GenerateControlFlowCleanup();
        if (Context.FunctionCallBeforeExit() != nullptr) {
            Context.EmitCall(Bytecode::FunctionPatch::FromBuiltin(Context, *Context.FunctionCallBeforeExit()), 0);
        }
        Context.Emit(Op::Push, TempVariable, DT::Variable);
    }

    Context.Emit(Op::Return, DT::Variable);
}

} // namespace Underanalyzer::Compiler::Nodes
