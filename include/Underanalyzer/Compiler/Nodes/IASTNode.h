
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "Underanalyzer/VMData.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Underanalyzer::Compiler {
class IBuiltinVariable;
namespace Lexer {
class IToken;
}
namespace Parser {
class ParseContext;
}
namespace Bytecode {
class BytecodeContext;
}
} // namespace Underanalyzer::Compiler

namespace Underanalyzer::Compiler::Nodes {

enum class ASTNodeKind : uint8_t {
    Accessor,
    AssetReference,
    Assign,
    BinaryChain,
    Block,
    Boolean,
    Break,
    Conditional,
    Continue,
    DotVariable,
    DoUntilLoop,
    Empty,
    Exit,
    ForLoop,
    FunctionCall,
    FunctionDecl,
    If,
    Int64,
    LocalVarDecl,
    NewObject,
    NullishCoalesce,
    Number,
    Postfix,
    Prefix,
    RepeatLoop,
    Return,
    SimpleFunctionCall,
    SimpleVariable,
    String,
    SwitchCase,
    Switch,
    Throw,
    TryCatch,
    Unary,
    WhileLoop,
    WithLoop
};

class IASTNode {
  public:
    virtual ~IASTNode() = default;
    virtual ASTNodeKind Kind() const = 0;
    virtual Lexer::IToken* NearbyToken() const = 0;
    virtual IASTNode* PostProcess(Parser::ParseContext& context) = 0;
    virtual IASTNode* Duplicate(Parser::ParseContext& context) = 0;
    virtual void GenerateCode(Bytecode::BytecodeContext& context) = 0;
    virtual std::vector<IASTNode*> EnumerateChildren() = 0;
};

class IConstantASTNode : public virtual IASTNode {};

class IAssignableASTNode : public virtual IASTNode {
  public:
    virtual void GenerateAssignCode(Bytecode::BytecodeContext& context) = 0;
    virtual void GenerateCompoundAssignCode(Bytecode::BytecodeContext& context, IASTNode* expression,
                                            IGMInstruction::Opcode operationOpcode) = 0;
    virtual void GeneratePrePostAssignCode(Bytecode::BytecodeContext& context, bool isIncrement, bool isPre,
                                           bool isStatement) = 0;
};

class IMaybeStatementASTNode : public virtual IASTNode {
  public:
    virtual bool IsStatement() const = 0;
    virtual void SetIsStatement(bool value) = 0;
};

class IVariableASTNode : public virtual IASTNode {
  public:
    virtual const std::string& VariableName() const = 0;
    virtual IBuiltinVariable* BuiltinVariable() const = 0;
};

// Cheap node-kind dispatch: short-circuit on the integer tag before paying for RTTI.
// T must expose a static kKind member matching its ASTNodeKind enumerator.
template <class T> inline T* As(IASTNode* n) {
    return (n && n->Kind() == T::kKind) ? dynamic_cast<T*>(n) : nullptr;
}

} // namespace Underanalyzer::Compiler::Nodes
