
// Source: github.com/UnderminersTeam/Underanalyzer @ 4be8217
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <string>

namespace Underanalyzer {

class IGlobalFunctions;
class GameSpecificRegistry;
namespace Compiler {
class IBuiltins;
class ICodeBuilder;
} // namespace Compiler

enum class AssetType {
    Object,
    Sprite,
    Sound,
    Room,
    Background,
    Path,
    Script,
    Font,
    Timeline,
    Shader,
    Sequence,
    AnimCurve,
    ParticleSystem,
    RoomInstance
};

class IGameContext {
  public:
    virtual ~IGameContext() = default;

    virtual bool UsingGMS2OrLater() const = 0;
    virtual bool UsingGMLv2() const = 0;
    virtual bool UsingStringRealOptimizations() const = 0;
    virtual bool UsingTypedBooleans() const = 0;
    virtual bool UsingNullishOperator() const = 0;
    virtual bool UsingAssetReferences() const = 0;
    virtual bool UsingRoomInstanceReferences() const = 0;
    virtual bool UsingFunctionScriptReferences() const = 0;
    virtual bool UsingNewFunctionResolution() const = 0;
    virtual bool Bytecode14OrLower() const = 0;
    virtual bool UsingLogicalShortCircuit() const = 0;
    virtual bool UsingLongCompoundBitwise() const = 0;
    virtual bool UsingExtraRepeatInstruction() const = 0;
    virtual bool UsingFinallyBeforeThrow() const = 0;
    virtual bool UsingConstructorSetStatic() const = 0;
    virtual bool UsingArrayCopyOnWrite() const = 0;
    virtual bool UsingNewArrayOwners() const = 0;
    virtual bool UsingReentrantStatic() const = 0;
    virtual bool UsingNewFunctionVariables() const = 0;
    virtual bool UsingSelfToBuiltin() const = 0;
    virtual bool UsingGlobalConstantFunction() const = 0;
    virtual bool UsingObjectFunctionForesight() const = 0;
    virtual bool UsingBetterTryBreakContinue() const = 0;
    virtual bool UsingBuiltinDefaultArguments() const = 0;
    virtual bool UsingOptimizedFunctionDeclarations() const = 0;
    virtual bool UsingNewChainedFunctionArgumentOrder() const = 0;

    virtual IGlobalFunctions& GlobalFunctions() = 0;
    virtual GameSpecificRegistry& GameSpecificRegistryRef() = 0;
    virtual Compiler::IBuiltins& Builtins() = 0;
    virtual Compiler::ICodeBuilder& CodeBuilder() = 0;

    virtual bool GetAssetName(AssetType assetType, int assetIndex, std::string& outName) = 0;
    virtual bool GetAssetId(const std::string& assetName, int& assetId) = 0;
    virtual bool GetRoomInstanceId(const std::string& roomInstanceName, int& assetId) = 0;
    virtual bool GetScriptId(const std::string& scriptName, int& assetId) = 0;
    virtual bool GetScriptIdByFunctionName(const std::string& functionName, int& assetId) = 0;
};

} // namespace Underanalyzer
