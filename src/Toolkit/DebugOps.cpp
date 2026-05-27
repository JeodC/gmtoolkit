// SPDX-License-Identifier: MIT

#include "Toolkit/DebugOps.h"

#include "GMSLib/GMSData.h"
#include "GMSLib/GMSIO.h"
#include "GMSLib/Models/GMSFunction.h"
#include "GMSLib/Models/GMSString.h"
#include "GMSLib/Models/GMSVariable.h"
#include "Toolkit/IO.h"
#include "Toolkit/Log.h"
#include "Toolkit/Version.h"
#include "Underanalyzer/VMData.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace Cli {

int dispatch_info(const char* data_path) {
    GMSLib::GMSData Data;
    if (GMSLib::LoadFromFile(data_path, Data) != 0) {
        Gmtoolkit::err("--info: load failed");
        return 1;
    }
    const auto& V = Data.GeneralInfo.Version;
    bool IsYYC = (Data.Chunks.find("CODE") == Data.Chunks.end() || Data.Chunks["CODE"].PayloadSize == 0);
    Gmtoolkit::tprint("data file         %s\n", data_path);
    Gmtoolkit::tprint("GEN8.FileName:    %s\n",
                      Data.GeneralInfo.Filename != nullptr ? Data.GeneralInfo.Filename->Content.c_str() : "<unset>");
    Gmtoolkit::tprint("GameID:           %u\n", Data.GeneralInfo.GameID);
    Gmtoolkit::tprint("Bytecode version: %u\n", (unsigned)Data.GeneralInfo.BytecodeVersion);
    Gmtoolkit::tprint("YYC:              %s\n", IsYYC ? "yes" : "no");
    Gmtoolkit::tprint("GEN8 declared:    %u.%u.%u.%u\n", V.gen8_major, V.gen8_minor, V.gen8_release, V.gen8_build);
    bool Bumped =
        !(V.major == V.gen8_major && V.minor == V.gen8_minor && V.release == V.gen8_release && V.build == V.gen8_build);
    Gmtoolkit::tprint("Detected:         %u.%u.%u.%u%s\n", V.major, V.minor, V.release, V.build,
                      Bumped ? "  (bumped by chunk heuristics)" : "");
    Gmtoolkit::tprint("Branch:           %s\n", V.branch == Gmtoolkit::BranchType::Pre2022_0   ? "Pre2022_0"
                                                : V.branch == Gmtoolkit::BranchType::LTS2022_0 ? "LTS2022_0"
                                                                                               : "Post2022_0");
    Gmtoolkit::tprint("Feature flags:\n");
    Gmtoolkit::tprint("  using_gms2_3:               %s\n", V.using_gms2_3() ? "yes" : "no");
    Gmtoolkit::tprint("  using_self_to_builtin:      %s\n", V.using_self_to_builtin() ? "yes" : "no");
    Gmtoolkit::tprint("  using_extended_sound_info:  %s\n", V.using_extended_sound_info() ? "yes" : "no");
    Gmtoolkit::tprint("  using_extended_particles:   %s\n", V.using_extended_particles() ? "yes" : "no");
    return 0;
}

// Smoke test for STRG/VARI/FUNC append paths: insert dummies, commit, reload, confirm survival.
int dispatch_pool_test(const char* data_path, const char* sample, const char* out_path) {
    GMSLib::GMSData Data;
    if (GMSLib::LoadFromFile(data_path, Data) != 0) {
        Gmtoolkit::err("pool-test: load failed");
        return 1;
    }
    Gmtoolkit::tprint("STRG: %zu entries\n", Data.Strings.size());
    Gmtoolkit::tprint("VARI: %zu entries\n", Data.Variables.size());
    Gmtoolkit::tprint("FUNC: %zu entries\n", Data.Functions.size());

    auto SampleIt = Data.StringByContent.find(sample);
    std::int32_t SampleIdx =
        SampleIt != Data.StringByContent.end() && SampleIt->second != nullptr ? SampleIt->second->Id : -1;
    Gmtoolkit::tprint("find_string(\"%s\") = %d\n", sample, SampleIdx);

    auto GlobalIt = Data.VariableByName.find("global");
    Gmtoolkit::tprint("find_variable(\"global\", *) = %s\n",
                      GlobalIt != Data.VariableByName.end() ? "present" : "missing");
    auto DrawIt = Data.FunctionByName.find("draw_self");
    Gmtoolkit::tprint("find_function(\"draw_self\") = %s\n",
                      DrawIt != Data.FunctionByName.end() ? "present" : "missing");

    const std::string NewStr = "utmt_lite_m2_test_string";
    const std::string NewVar = "utmt_lite_m2_test_var";
    const std::string NewFn = "utmt_lite_m2_test_fn";

    auto NewStrUp = std::make_unique<GMSLib::GMSString>(NewStr);
    NewStrUp->Id = static_cast<std::int32_t>(Data.Strings.size());
    auto* NewStrRaw = NewStrUp.get();
    Data.StringByContent.try_emplace(NewStr, NewStrRaw);
    Data.Strings.push_back(std::move(NewStrUp));

    auto NewVarUp = std::make_unique<GMSLib::GMSVariable>();
    NewVarUp->NameRef = NewStrRaw;
    NewVarUp->InstType = Underanalyzer::IGMInstruction::InstanceType::Global;
    NewVarUp->VarID = -1;
    NewVarUp->NameStringID = NewStrRaw->Id;
    auto* NewVarRaw = NewVarUp.get();
    Data.VariableByName.try_emplace(NewVar, NewVarRaw);
    Data.Variables.push_back(std::move(NewVarUp));

    auto FnStrUp = std::make_unique<GMSLib::GMSString>(NewFn);
    FnStrUp->Id = static_cast<std::int32_t>(Data.Strings.size());
    auto* FnStrRaw = FnStrUp.get();
    Data.StringByContent.try_emplace(NewFn, FnStrRaw);
    Data.Strings.push_back(std::move(FnStrUp));

    auto NewFnUp = std::make_unique<GMSLib::GMSFunction>();
    NewFnUp->NameRef = FnStrRaw;
    NewFnUp->NameStringID = FnStrRaw->Id;
    auto* NewFnRaw = NewFnUp.get();
    Data.FunctionByName.try_emplace(NewFn, NewFnRaw);
    Data.Functions.push_back(std::move(NewFnUp));

    Gmtoolkit::tprint("Appended: STRG[%d]=%s, VARI[%zu]=%s, FUNC[%zu]=%s\n", NewStrRaw->Id, NewStr.c_str(),
                      Data.Variables.size() - 1, NewVar.c_str(), Data.Functions.size() - 1, NewFn.c_str());

    if (GMSLib::SaveToFile(out_path, Data) != 0) {
        Gmtoolkit::err("pool-test: save failed");
        return 2;
    }
    Gmtoolkit::tprint("Committed to %s\n", out_path);

    GMSLib::GMSData Reload;
    if (GMSLib::LoadFromFile(out_path, Reload) != 0) {
        Gmtoolkit::err("pool-test: reload failed");
        return 3;
    }
    Gmtoolkit::tprint("Reloaded: STRG=%zu VARI=%zu FUNC=%zu\n", Reload.Strings.size(), Reload.Variables.size(),
                      Reload.Functions.size());

    auto* SV = Reload.StringByContent.count(NewStr) ? Reload.StringByContent.at(NewStr) : nullptr;
    auto* VV = Reload.VariableByName.count(NewVar) ? Reload.VariableByName.at(NewVar) : nullptr;
    auto* FV = Reload.FunctionByName.count(NewFn) ? Reload.FunctionByName.at(NewFn) : nullptr;
    Gmtoolkit::tprint("  string=%d var=%d fn=%d\n", SV != nullptr ? SV->Id : -1, VV != nullptr ? VV->VarID : -1,
                      FV != nullptr ? FV->NameStringID : -1);

    auto* ReloadSample = Reload.StringByContent.count(sample) ? Reload.StringByContent.at(sample) : nullptr;
    std::int32_t OldCheck = ReloadSample != nullptr ? ReloadSample->Id : -1;
    Gmtoolkit::tprint("  existing string \"%s\" idx=%d (was %d)\n", sample, OldCheck, SampleIdx);

    return (SV != nullptr && VV != nullptr && FV != nullptr && OldCheck == SampleIdx) ? 0 : 4;
}

} // namespace Cli
