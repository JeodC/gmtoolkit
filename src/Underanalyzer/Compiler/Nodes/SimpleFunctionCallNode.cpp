
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/SimpleFunctionCallNode.h"

#include "Underanalyzer/Compiler/Bytecode/ArrayOwners.h"
#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/Bytecode/InstructionPatch.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/FunctionScope.h"
#include "Underanalyzer/Compiler/IBuiltins.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/FunctionCallNode.h"
#include "Underanalyzer/Compiler/Nodes/Int64Node.h"
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"
#include "Underanalyzer/Compiler/Nodes/SimpleVariableNode.h"
#include "Underanalyzer/Compiler/Nodes/StringNode.h"
#include "Underanalyzer/Compiler/Parser/Expressions.h"
#include "Underanalyzer/Compiler/Parser/Functions.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/IGameContext.h"
#include "Underanalyzer/VMConstants.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace Underanalyzer::Compiler::Nodes {

using namespace Lexer;
using Parser::ParseContext;
using Op = IGMInstruction::Opcode;
using ExtOp = IGMInstruction::ExtendedOpcode;
using DT = IGMInstruction::DataType;
using IT = IGMInstruction::InstanceType;

// Direct-name calls (unlike FunctionCallNode's expression-resolved calls) can carry up
// to 65535 arguments because the count is encoded in a 16-bit field of the instruction.
SimpleFunctionCallNode::SimpleFunctionCallNode(ParseContext& Context, TokenFunction* Token)
    : FunctionName(Token->Text), BuiltinFunction(Token->BuiltinFunction),
      Arguments(Parser::Functions::ParseCallArguments(Context, 65535)), _NearbyToken(Token) {
}

SimpleFunctionCallNode* SimpleFunctionCallNode::ParseArrayLiteral(ParseContext& Context) {
    std::vector<IASTNode*> Args;
    Args.reserve(16);
    auto* Result = Context.Make<SimpleFunctionCallNode>(
        std::string(VMConstants::NewArrayFunction), (Lexer::IToken*)nullptr, std::vector<IASTNode*>{},
        Context.CompileContextRef().GameContext().Builtins().LookupBuiltinFunction(
            std::string(VMConstants::NewArrayFunction)));

    while (!Context.EndOfCode() && !Context.IsCurrentToken(SeparatorKind::ArrayClose)) {
        if (IASTNode* Expr = Parser::Expressions::ParseExpression(Context)) {
            Result->Arguments.push_back(Expr);
        } else {
            break;
        }
        if (Context.EndOfCode())
            break;
        if (Context.IsCurrentToken(SeparatorKind::Comma)) {
            Context.SetPosition(Context.Position() + 1);
            continue;
        }
        if (!Context.IsCurrentToken(SeparatorKind::ArrayClose)) {
            IToken* Cur = Context.Tokens()[Context.Position()];
            Context.CompileContextRef().PushError(
                "Expected '" + TokenSeparator::KindToString(SeparatorKind::Comma) + "' or '" +
                    TokenSeparator::KindToString(SeparatorKind::ArrayClose) + "', got token",
                Cur);
            break;
        }
    }
    Context.EnsureToken(SeparatorKind::ArrayClose);
    return Result;
}

IASTNode* SimpleFunctionCallNode::PostProcess(ParseContext& Context) {
    for (auto& A : Arguments)
        A = A->PostProcess(Context);

    if (FunctionName == "ord")
        return OptimizeOrd(Context);
    if (FunctionName == "chr")
        return OptimizeChr(Context);
    if (FunctionName == "int64")
        return OptimizeInt64(Context);
    if (FunctionName == "real")
        return OptimizeReal(Context);
    if (FunctionName == "string")
        return OptimizeString(Context);
    return this;
}

void SimpleFunctionCallNode::PostProcessChildrenOnly(ParseContext& Context) {
    for (auto& A : Arguments)
        A = A->PostProcess(Context);
}

IASTNode* SimpleFunctionCallNode::Duplicate(ParseContext& Context) {
    std::vector<IASTNode*> NewArgs(Arguments);
    for (auto& A : NewArgs)
        A = A->Duplicate(Context);
    auto* N = Context.Make<SimpleFunctionCallNode>(FunctionName, _NearbyToken, std::move(NewArgs), BuiltinFunction);
    N->SetIsStatement(_IsStatement);
    return N;
}

// Fold ord("...") to a NumberNode at compile time by decoding the first UTF-8 code
// point from the literal; runtime ord() does the same work, just avoided here.
IASTNode* SimpleFunctionCallNode::OptimizeOrd(ParseContext& Context) {
    if (Arguments.size() != 1)
        return this;
    auto* Str = As<StringNode>(Arguments[0]);
    if (Str == nullptr)
        return this;

    int Value;
    if (Str->Value.empty()) {
        Value = 0;
    } else {
        const unsigned char* Bytes = reinterpret_cast<const unsigned char*>(Str->Value.data());
        Value = Bytes[0];
        if ((Value & 0x80) != 0) {
            if ((Value & 0xF8) != 0xF0) {
                if ((Value & 0x20) == 0) {
                    Value = ((Value & 0x1F) << 6) | (Bytes[1] & 0x3F);
                } else {
                    Value = ((Value & 0xF) << 12) | ((Bytes[1] & 0x3F) << 6) | (Bytes[2] & 0x3F);
                }
            } else {
                Value =
                    ((Value & 0x7) << 18) | ((Bytes[1] & 0x3F) << 12) | ((Bytes[2] & 0x3F) << 6) | (Bytes[3] & 0x3F);
            }
        }
    }
    return Context.Make<NumberNode>(static_cast<double>(Value), _NearbyToken);
}

static void AppendUtf32(std::string& Out, int Codepoint) {
    if (Codepoint < 0 || Codepoint > 0x10FFFF)
        throw std::out_of_range("bad codepoint");
    if (Codepoint < 0x80) {
        Out.push_back(static_cast<char>(Codepoint));
    } else if (Codepoint < 0x800) {
        Out.push_back(static_cast<char>(0xC0 | (Codepoint >> 6)));
        Out.push_back(static_cast<char>(0x80 | (Codepoint & 0x3F)));
    } else if (Codepoint < 0x10000) {
        Out.push_back(static_cast<char>(0xE0 | (Codepoint >> 12)));
        Out.push_back(static_cast<char>(0x80 | ((Codepoint >> 6) & 0x3F)));
        Out.push_back(static_cast<char>(0x80 | (Codepoint & 0x3F)));
    } else {
        Out.push_back(static_cast<char>(0xF0 | (Codepoint >> 18)));
        Out.push_back(static_cast<char>(0x80 | ((Codepoint >> 12) & 0x3F)));
        Out.push_back(static_cast<char>(0x80 | ((Codepoint >> 6) & 0x3F)));
        Out.push_back(static_cast<char>(0x80 | (Codepoint & 0x3F)));
    }
}

IASTNode* SimpleFunctionCallNode::OptimizeChr(ParseContext& Context) {
    if (Arguments.size() != 1)
        return this;
    IConstantASTNode* Constant = dynamic_cast<IConstantASTNode*>(Arguments[0]);
    if (Constant == nullptr)
        return this;

    int Value;
    if (auto* N = As<NumberNode>(Arguments[0]))
        Value = static_cast<int>(std::max<int64_t>(0, static_cast<int64_t>(N->Value)));
    else if (auto* I = As<Int64Node>(Arguments[0]))
        Value = static_cast<int>(std::max<int64_t>(0, I->Value));
    else
        return this;

    std::string Str;
    try {
        AppendUtf32(Str, Value);
    } catch (const std::out_of_range&) { Str = "X"; }
    return Context.Make<StringNode>(std::move(Str), _NearbyToken);
}

IASTNode* SimpleFunctionCallNode::OptimizeInt64(ParseContext& Context) {
    if (Arguments.size() != 1)
        return this;
    IConstantASTNode* Constant = dynamic_cast<IConstantASTNode*>(Arguments[0]);
    if (Constant == nullptr)
        return this;
    int64_t Value;
    if (auto* N = As<NumberNode>(Arguments[0]))
        Value = static_cast<int64_t>(N->Value);
    else if (auto* I = As<Int64Node>(Arguments[0]))
        Value = I->Value;
    else
        return this;
    return Context.Make<Int64Node>(Value, _NearbyToken);
}

IASTNode* SimpleFunctionCallNode::OptimizeReal(ParseContext& Context) {
    if (Arguments.size() != 1)
        return this;
    IConstantASTNode* Constant = dynamic_cast<IConstantASTNode*>(Arguments[0]);
    if (Constant == nullptr)
        return this;
    if (!Context.CompileContextRef().GameContext().UsingStringRealOptimizations())
        return this;

    double Value;
    if (auto* N = As<NumberNode>(Arguments[0]))
        Value = N->Value;
    else if (auto* I = As<Int64Node>(Arguments[0]))
        Value = static_cast<double>(I->Value);
    else if (auto* S = As<StringNode>(Arguments[0])) {
        std::string Str = S->Value;
        char* End = nullptr;
        Value = std::strtod(Str.c_str(), &End);
        if (End == Str.c_str() + Str.size()) {
        } else {
            std::string Trimmed;
            Trimmed.reserve(Str.size());
            for (char C : Str)
                if (C != '_')
                    Trimmed.push_back(C);
            size_t L = Trimmed.find_first_not_of(" \t\r\n");
            size_t R = Trimmed.find_last_not_of(" \t\r\n");
            if (L == std::string::npos) {
                Context.CompileContextRef().PushError("Failed to convert \"" + S->Value + "\" to real number",
                                                      _NearbyToken);
                return this;
            }
            Trimmed = Trimmed.substr(L, R - L + 1);

            auto StartsWithCI = [](const std::string& Hay, const char* Needle) {
                size_t N = std::strlen(Needle);
                if (Hay.size() < N)
                    return false;
                for (size_t i = 0; i < N; i++)
                    if (std::tolower(static_cast<unsigned char>(Hay[i])) != Needle[i])
                        return false;
                return true;
            };

            if (StartsWithCI(Trimmed, "0x")) {
                int64_t Hex = 0;
                std::string HexPart = Trimmed.substr(2);
                char* HexEnd = nullptr;
                Hex = std::strtoll(HexPart.c_str(), &HexEnd, 16);
                if (HexEnd == HexPart.c_str() + HexPart.size()) {
                    Value = static_cast<double>(Hex);
                } else {
                    Context.CompileContextRef().PushError("Failed to convert \"" + S->Value + "\" to real number",
                                                          _NearbyToken);
                    return this;
                }
            } else if (StartsWithCI(Trimmed, "0b")) {
                int64_t Binary = 0;
                for (size_t i = 2; i < Trimmed.size(); i++) {
                    char C = Trimmed[i];
                    if (C == '0') {
                        Binary <<= 1;
                    } else if (C == '1') {
                        Binary = (Binary << 1) | 1;
                    } else {
                        Context.CompileContextRef().PushError("Failed to convert \"" + S->Value + "\" to real number",
                                                              _NearbyToken);
                        return this;
                    }
                }
                Value = static_cast<double>(Binary);
            } else {
                Context.CompileContextRef().PushError("Failed to convert \"" + S->Value + "\" to real number",
                                                      _NearbyToken);
                return this;
            }
        }
    } else
        return this;

    return Context.Make<NumberNode>(Value, _NearbyToken);
}

IASTNode* SimpleFunctionCallNode::OptimizeString(ParseContext& Context) {

    if (Arguments.size() != 1)
        return this;
    StringNode* Str = As<StringNode>(Arguments[0]);
    if (Str == nullptr)
        return this;
    if (!Context.CompileContextRef().GameContext().UsingStringRealOptimizations())
        return this;
    return Str;
}

void SimpleFunctionCallNode::GenerateArguments(Bytecode::BytecodeContext& Context) {
    for (int i = static_cast<int>(Arguments.size()) - 1; i >= 0; i--) {
        Arguments[i]->GenerateCode(Context);
        Context.ConvertDataType(DT::Variable);
    }
}

void SimpleFunctionCallNode::GenerateDirectCode(Bytecode::BytecodeContext& Context) {
    if (Context.CanGenerateArrayOwners()) {
        if (Bytecode::ArrayOwners::IsArraySetFunction(this)) {
            Bytecode::ArrayOwners::GenerateSetArrayOwner(Context, this);
        }
    }
    GenerateArguments(Context);

    FunctionScope* Scope = Context.CurrentScope().GeneratingFunctionDeclHeader() ? Context.CurrentScope().Parent()
                                                                                 : &Context.CurrentScope();
    Bytecode::FunctionPatch Patch(Scope, FunctionName, BuiltinFunction);
    Context.EmitCall(Patch, static_cast<int>(Arguments.size()));
    Context.PushDataType(DT::Variable);

    if (_IsStatement)
        Context.Emit(Op::PopDelete, Context.PopDataType());
}

void SimpleFunctionCallNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    IGameContext& Game = Context.CompileContextRef().GameContext();
    bool IsGlobalFunction = Context.IsGlobalFunctionName(FunctionName) ||
                            (Context.CompileContextRef().ScriptKind() == CompileScriptKind::GlobalScript &&
                             Context.RootScope().IsFunctionDeclaredImmediately(FunctionName));

    if (IsGlobalFunction || Context.IsFunctionDeclaredInCurrentScope(FunctionName)) {
        // Names declared in an outer scope under the new resolution rules need an
        // indirect call (PushReference + CallVariable); same-scope names go direct.
        if (Game.UsingNewFunctionResolution() && !IsGlobalFunction &&
            !Context.CurrentScope().IsFunctionDeclaredImmediately(FunctionName)) {
            GenerateArguments(Context);
            Context.EmitCall(Bytecode::FunctionPatch::FromBuiltin(Context, std::string(VMConstants::SelfFunction)), 0);
            Context.Emit(ExtOp::PushReference,
                         Bytecode::LocalFunctionPatch(nullptr, &Context.CurrentScope(), FunctionName));
            Context.EmitCallVariable(static_cast<int>(Arguments.size()));
            Context.PushDataType(DT::Variable);
            if (_IsStatement)
                Context.Emit(Op::PopDelete, Context.PopDataType());
        } else {
            GenerateDirectCode(Context);
        }
    } else {
        if (!Game.UsingGMLv2()) {
            Context.CompileContextRef().PushError("Failed to find function \"" + FunctionName + "\"", _NearbyToken);
        }

        SimpleVariableNode StackVarNode(FunctionName, Game.Builtins().LookupBuiltinVariable(FunctionName));
        IAssignableASTNode* Assignable = StackVarNode.ResolveStandaloneType(Context);

        if (Assignable == &StackVarNode) {
            GenerateArguments(Context);

            std::string_view FunctionToCall;
            switch (StackVarNode.ExplicitInstanceType()) {
                case IT::Other:
                    FunctionToCall = VMConstants::OtherFunction;
                    break;
                case IT::Global:
                    FunctionToCall = VMConstants::GlobalFunction;
                    break;
                default:
                    FunctionToCall = VMConstants::SelfFunction;
                    break;
            }
            Context.EmitCall(Bytecode::FunctionPatch::FromBuiltin(Context, std::string(FunctionToCall)), 0);

            StackVarNode.SetIsFunctionCall(true);
            StackVarNode.GenerateCode(Context);
            Context.PopDataType();

            Context.EmitCallVariable(static_cast<int>(Arguments.size()));
            Context.PushDataType(DT::Variable);
            if (_IsStatement)
                Context.Emit(Op::PopDelete, Context.PopDataType());
        } else {
            std::vector<IASTNode*> ArgsCopy(Arguments);
            auto* Wrapper = Context.Make<FunctionCallNode>(_NearbyToken, Assignable, std::move(ArgsCopy));
            Wrapper->SetIsStatement(_IsStatement);
            Wrapper->GenerateCode(Context);
        }
    }
}

} // namespace Underanalyzer::Compiler::Nodes
