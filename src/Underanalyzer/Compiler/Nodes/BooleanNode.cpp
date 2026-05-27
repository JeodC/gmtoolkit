
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/BooleanNode.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/IGameContext.h"

namespace Underanalyzer::Compiler::Nodes {

BooleanNode::BooleanNode(Lexer::TokenBoolean* Token) : Value(Token->Value), _NearbyToken(Token) {
}

void BooleanNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;
    Context.Emit(Op::PushImmediate, static_cast<int16_t>(Value ? 1 : 0), DT::Int16);
    Context.PushDataType(Context.CompileContextRef().GameContext().UsingTypedBooleans() ? DT::Boolean : DT::Int32);
}

} // namespace Underanalyzer::Compiler::Nodes
