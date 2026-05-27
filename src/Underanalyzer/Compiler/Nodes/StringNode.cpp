
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/StringNode.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"

namespace Underanalyzer::Compiler::Nodes {

StringNode::StringNode(Lexer::TokenString* Token) : Value(Token->Value), _NearbyToken(Token) {
}

void StringNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;
    Context.Emit(Op::Push, Bytecode::StringPatch(Value), DT::String);
    Context.PushDataType(DT::String);
}

} // namespace Underanalyzer::Compiler::Nodes
