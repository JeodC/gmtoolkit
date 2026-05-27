
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0
#include "Underanalyzer/Compiler/Nodes/AssetReferenceNode.h"

#include "Underanalyzer/Compiler/Bytecode/BytecodeContext.h"
#include "Underanalyzer/Compiler/CompileContext.h"
#include "Underanalyzer/Compiler/Lexer/Token.h"
#include "Underanalyzer/Compiler/Nodes/NumberNode.h"
#include "Underanalyzer/Compiler/Parser/ParseContext.h"
#include "Underanalyzer/IGameContext.h"

namespace Underanalyzer::Compiler::Nodes {

AssetReferenceNode::AssetReferenceNode(Lexer::TokenAssetReference* Token) : AssetId(Token->AssetId), _Token(Token) {
}

ASTNodeKind AssetReferenceNode::Kind() const {
    return kKind;
}
Lexer::IToken* AssetReferenceNode::NearbyToken() const {
    return _Token;
}

// Runtimes that lack asset-reference support degrade the reference to a raw numeric id;
// the same fallback applies when only room-instance references are unsupported.
IASTNode* AssetReferenceNode::PostProcess(Parser::ParseContext& Context) {
    IGameContext& Game = Context.CompileContextRef().GameContext();
    if (!Game.UsingAssetReferences() || (_Token->IsRoomInstanceAsset && !Game.UsingRoomInstanceReferences())) {
        return Context.Make<NumberNode>(static_cast<double>(AssetId), _Token);
    }
    return this;
}

void AssetReferenceNode::GenerateCode(Bytecode::BytecodeContext& Context) {
    Context.Emit(IGMInstruction::ExtendedOpcode::PushReference, AssetId);
    Context.PushDataType(IGMInstruction::DataType::Variable);
}

} // namespace Underanalyzer::Compiler::Nodes
