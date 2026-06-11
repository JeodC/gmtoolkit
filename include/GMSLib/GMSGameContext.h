// Source: github.com/UnderminersTeam/UndertaleModTool @ c4e5c2c3
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "GMSLib/Compiler/BuiltinList.h"
#include "GMSLib/Compiler/CodeBuilder.h"
#include "GMSLib/GlobalFunctions.h"
#include "Underanalyzer/GameSpecificRegistry.h"
#include "Underanalyzer/IGameContext.h"
#include "Underanalyzer/IGlobalFunctions.h"

#include <string>

namespace GMSLib {

class GMSData;

class GMSGameContext final : public Underanalyzer::IGameContext {
  public:
    explicit GMSGameContext(GMSData& DataIn);

    GMSData& Data() {
        return *_Data;
    }

    bool UsingGMS2OrLater() const override;
    bool UsingGMLv2() const override;
    bool UsingStringRealOptimizations() const override;
    bool UsingTypedBooleans() const override;
    bool UsingNullishOperator() const override;
    bool UsingAssetReferences() const override;
    bool UsingRoomInstanceReferences() const override;
    bool UsingFunctionScriptReferences() const override;
    bool UsingNewFunctionResolution() const override;
    bool Bytecode14OrLower() const override;
    bool UsingLogicalShortCircuit() const override;
    bool UsingLongCompoundBitwise() const override;
    bool UsingExtraRepeatInstruction() const override;
    bool UsingFinallyBeforeThrow() const override;
    bool UsingConstructorSetStatic() const override;
    bool UsingArrayCopyOnWrite() const override;
    bool UsingNewArrayOwners() const override;
    bool UsingReentrantStatic() const override;
    bool UsingNewFunctionVariables() const override;
    bool UsingSelfToBuiltin() const override;
    bool UsingGlobalConstantFunction() const override;
    bool UsingObjectFunctionForesight() const override;
    bool UsingBetterTryBreakContinue() const override;
    bool UsingBuiltinDefaultArguments() const override;
    bool UsingOptimizedFunctionDeclarations() const override;
    bool UsingNewChainedFunctionArgumentOrder() const override;

    Underanalyzer::IGlobalFunctions& GlobalFunctions() override {
        return _Globals;
    }
    Underanalyzer::GameSpecificRegistry& GameSpecificRegistryRef() override {
        return _GameSpecificRegistry;
    }
    Underanalyzer::Compiler::IBuiltins& Builtins() override {
        return _Builtins;
    }
    Underanalyzer::Compiler::ICodeBuilder& CodeBuilder() override {
        return _CodeBuilder;
    }

    bool GetAssetName(Underanalyzer::AssetType AssetType, int AssetIndex, std::string& OutName) override;
    bool GetAssetId(const std::string& AssetName, int& AssetId) override;
    bool GetAssetType(const std::string& AssetName, Underanalyzer::AssetType& OutType) override;
    bool GetRoomInstanceId(const std::string& RoomInstanceName, int& AssetId) override;
    bool GetScriptId(const std::string& ScriptName, int& AssetId) override;
    bool GetScriptIdByFunctionName(const std::string& FunctionName, int& AssetId) override;

  private:
    GMSData* _Data;
    GMSLib::GlobalFunctions _Globals;
    BuiltinList _Builtins;
    GMSLib::CodeBuilder _CodeBuilder;
    Underanalyzer::GameSpecificRegistry _GameSpecificRegistry;
    void _EnsureAssetsLoaded();
    bool _AssetsLoaded = false;
    // ShortCircuit / ArrayCopyOnWrite are per-game compiler options, not version
    // features. UTMT detects them by scanning the existing bytecode at load
    // (and.b.b / or.b.b => short-circuit off; setowner => copy-on-write on);
    // mirror that here, lazily, so injected code matches the game's own codegen.
    void _EnsureCodeFlagsScanned() const;
    mutable bool _CodeFlagsScanned = false;
    mutable bool _ShortCircuit = true;
    mutable bool _ArrayCopyOnWrite = false;
    std::unordered_map<std::string, int> _AssetObj;
    std::unordered_map<std::string, int> _AssetSpr;
    std::unordered_map<std::string, int> _AssetSnd;
    std::unordered_map<std::string, int> _AssetRoom;
    std::unordered_map<std::string, int> _AssetBgnd;
    std::unordered_map<std::string, int> _AssetPath;
    std::unordered_map<std::string, int> _AssetFont;
    std::unordered_map<std::string, int> _AssetTmln;
    std::unordered_map<std::string, int> _AssetShdr;
    std::unordered_map<std::string, int> _AssetSeqn;
    std::unordered_map<std::string, int> _AssetAcrv;
    std::unordered_map<std::string, int> _AssetPsem;
};

} // namespace GMSLib
