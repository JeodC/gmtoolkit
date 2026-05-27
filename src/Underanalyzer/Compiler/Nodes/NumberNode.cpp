
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/SimpleFunctionCallNode.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/IGameContext.h"
#include "Underanalyzer/VMConstants.h"

#include <cstdint>

namespace Underanalyzer::Compiler::Nodes {

NumberNode::NumberNode(Lexer::TokenNumber* Token, const std::string* ConstantNameIn)
    : Value(Token->Value), _NearbyToken(Token) {
    if (ConstantNameIn != nullptr)
        ConstantName = *ConstantNameIn;
}

// Constants 'self', 'other' (and 'global' on supported runtimes) parse as numbers but
// in GMLv2 stand in for the corresponding @@self@@/@@other@@/@@global@@ calls.
IASTNode* NumberNode::PostProcess(Parser::ParseContext& Context) {
    if (!Context.CompileContextRef().GameContext().UsingGMLv2())
        return this;
    if (!ConstantName.has_value())
        return this;
    const std::string& Name = *ConstantName;
    if (Name == "self") {
        return Context.Make<SimpleFunctionCallNode>(std::string(VMConstants::SelfFunction), (Lexer::IToken*)nullptr,
                                                    std::vector<IASTNode*>{});
    }
    if (Name == "other") {
        return Context.Make<SimpleFunctionCallNode>(std::string(VMConstants::OtherFunction), (Lexer::IToken*)nullptr,
                                                    std::vector<IASTNode*>{});
    }
    if (Name == "global") {
        if (Context.CompileContextRef().GameContext().UsingGlobalConstantFunction()) {
            return Context.Make<SimpleFunctionCallNode>(std::string(VMConstants::GlobalFunction),
                                                        (Lexer::IToken*)nullptr, std::vector<IASTNode*>{});
        }
    }
    return this;
}

void NumberNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    GenerateCode(Context, Value);
}

// Pick the narrowest push opcode that still round-trips the value: Int16 immediate,
// Int32 push, Int64 push, or Double push. Falls through when fractional bits exist.
void NumberNode::GenerateCode(Bytecode::BytecodeContext& Context, double ValueIn) {
    using Op = IGMInstruction::Opcode;
    using DT = IGMInstruction::DataType;
    if (static_cast<double>(static_cast<int64_t>(ValueIn)) == ValueIn) {
        int64_t IntegerValue = static_cast<int64_t>(ValueIn);
        if (IntegerValue <= INT32_MAX && IntegerValue >= INT32_MIN) {
            if (IntegerValue <= INT16_MAX && IntegerValue >= INT16_MIN) {
                Context.Emit(Op::PushImmediate, static_cast<int16_t>(IntegerValue), DT::Int16);
                Context.PushDataType(DT::Int32);
            } else {
                Context.Emit(Op::Push, static_cast<int32_t>(IntegerValue), DT::Int32);
                Context.PushDataType(DT::Int32);
            }
        } else {
            Context.Emit(Op::Push, IntegerValue, DT::Int64);
            Context.PushDataType(DT::Int64);
        }
    } else {
        Context.Emit(Op::Push, ValueIn, DT::Double);
        Context.PushDataType(DT::Double);
    }
}

} // namespace Underanalyzer::Compiler::Nodes
