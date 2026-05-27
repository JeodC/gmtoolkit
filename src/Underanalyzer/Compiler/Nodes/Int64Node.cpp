
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/Int64Node.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"

namespace Underanalyzer::Compiler::Nodes {

Int64Node::Int64Node(Lexer::TokenInt64* Token) : Value(Token->Value), _NearbyToken(Token) {
}

void Int64Node::GenerateCode(Bytecode::BytecodeContext& Context) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;
    Context.Emit(Op::Push, Value, DT::Int64);
    Context.PushDataType(DT::Int64);
}

} // namespace Underanalyzer::Compiler::Nodes
