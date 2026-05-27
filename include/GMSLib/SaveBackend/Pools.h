// SPDX-License-Identifier: MIT

#pragma once

#include "Toolkit/Buffer.h"
#include "Toolkit/Version.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace GMSLib {
class GMSData;
}

namespace GMSLib::SaveBackend {

struct Pools {
    Gmtoolkit::Buffer buf;

    struct ChunkLoc {
        size_t payload_off;
        size_t size;
    };
    std::unordered_map<std::string, ChunkLoc> chunks;
    Gmtoolkit::Version version;
    std::vector<uint32_t> strg_ptr_table;
    std::unordered_map<std::string, uint32_t> strg_index;

    struct VariEntry {
        std::string name;
        int32_t inst_type;
        int32_t var_id;
        uint32_t occurrences;
        uint32_t first_addr;
    };
    std::vector<VariEntry> vari_entries;
    std::unordered_map<uint64_t, uint32_t> vari_index;

    struct FuncEntry {
        std::string name;
        uint32_t occurrences;
        uint32_t first_addr;
    };
    std::vector<FuncEntry> func_entries;
    std::unordered_map<std::string, uint32_t> func_index;

    struct CodeLocal {
        std::string name;
        struct Var {
            std::string name;
            uint32_t index;
        };
        std::vector<Var> vars;
    };
    std::vector<CodeLocal> code_locals;
    std::unordered_map<std::string, uint32_t> code_locals_index;
    bool has_code_locals = false;
    size_t code_locals_off = 0;
    std::unordered_map<std::string, int32_t> asset_objt;
    std::unordered_map<std::string, int32_t> asset_sprt;
    std::unordered_map<std::string, int32_t> asset_sond;
    std::unordered_map<std::string, int32_t> asset_room;
    std::unordered_map<std::string, int32_t> asset_bgnd;
    std::unordered_map<std::string, int32_t> asset_font;
    std::unordered_map<std::string, int32_t> asset_path;
    bool assets_loaded = false;
    int32_t asset_lookup(std::string_view name);
    std::vector<std::string> pending_strings;
    std::vector<std::pair<std::string, int32_t>> pending_vars;
    std::vector<std::string> pending_funcs;

    struct CodePatch {
        std::string entry_name;
        std::vector<uint8_t> bytecode;
        uint16_t locals_count = 0;
        uint16_t args_count = 0;

        struct VarRef {
            size_t byte_offset;
            std::string name;
            int32_t inst_type;
            uint8_t var_type;
        };

        struct FuncRef {
            size_t byte_offset;
            std::string name;
        };
        std::vector<VarRef> var_refs;
        std::vector<FuncRef> func_refs;
        std::vector<std::string> local_names;

        struct ChildEntry {
            std::string name;
            size_t body_offset = 0;
            size_t body_length = 0;
            uint16_t args_count = 0;
            uint16_t locals_count = 0;
            std::vector<std::string> local_names;
            bool is_wrapper_sub = false;
        };
        std::vector<ChildEntry> children;
    };
    std::vector<CodePatch> pending_code;

    int add_code_patch(CodePatch p);

    struct ScptInsert {
        std::string name;
        // code_id resolved at commit time by matching name against the file's
        // CODE table; the high bit is set for constructor scripts.
        bool is_constructor = false;
    };
    std::vector<ScptInsert> pending_scpt_inserts;

    struct Occurrence {
        uint32_t operand_offset;
        uint8_t var_type;
    };
    std::vector<std::vector<Occurrence>> var_occurrences;
    std::vector<std::vector<Occurrence>> func_occurrences;

    int open(const char* path);

    int adopt_from_gmsdata(GMSLib::GMSData& data);
    void return_to_gmsdata(GMSLib::GMSData& data);

    void detect_format_versions();
    void detect_extn_2022_6();
    void detect_extn_2023_4();
    void detect_sond_2024_6();
    void detect_sprt_2024_6();
    void detect_objt_2022_5();
    void detect_room_2024_2();
    void detect_txtr_2022_3();
    void detect_txtr_2022_5();
    void detect_func_2024_8();
    void detect_font_2022_2();
    void detect_font_2023_6();
    void detect_font_2024_11();
    void detect_font_2024_14();
    void detect_psem_2023_x();
    void detect_tgin_2022_9();
    void detect_tgin_2023_1();
    void detect_bgnd_2024_14_1();
    void detect_agrp_2024_14();
    void detect_room_2022_1();

    int32_t find_string(std::string_view s);
    int32_t find_variable(std::string_view name, int32_t inst_type);
    int32_t find_function(std::string_view name);
    int32_t intern_string(std::string_view s);
    int32_t add_variable(std::string_view name, int32_t inst_type);
    int32_t add_function(std::string_view name);
    int32_t add_local_for_patch(std::string_view name);
    int commit(const char* out_path);
};

} // namespace GMSLib::SaveBackend
