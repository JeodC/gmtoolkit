// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace GMSLib::SaveBackend {

struct CodeEntry {
    std::string name;
    uint32_t entry_offset;
    uint32_t length;
    uint16_t locals_count;
    uint16_t args_count;
    bool weird_local_flag;
    uint32_t bytecode_offset;
    uint32_t self_offset;
};

struct Reference {
    std::string name;
    uint32_t first_address;
    uint32_t occurrence_count;
    int32_t name_string_id;
};

struct AddressLabel {
    uint32_t address;
    uint32_t ref_index;
};

bool find_code_chunk(const uint8_t* win, size_t win_size, size_t* out_start, size_t* out_size);
bool find_strg_chunk(const uint8_t* win, size_t win_size, size_t* out_start, size_t* out_size);
bool find_vari_chunk(const uint8_t* win, size_t win_size, size_t* out_start, size_t* out_size);
bool find_func_chunk(const uint8_t* win, size_t win_size, size_t* out_start, size_t* out_size);
std::string read_strg_string(const uint8_t* win, size_t win_size, uint32_t data_ptr);
bool parse_code_entries(const uint8_t* win, size_t win_size, size_t code_start, size_t code_size,
                        std::vector<CodeEntry>* out_entries, uint8_t bytecode_version = 17, bool using_gms_2_3 = true);
bool parse_references(const uint8_t* win, size_t win_size, size_t chunk_start, size_t chunk_size, bool is_vari,
                      std::vector<Reference>* out_refs, uint8_t bytecode_version = 17);
void build_address_labels(const uint8_t* win, size_t win_size, const std::vector<Reference>& refs,
                          std::vector<AddressLabel>* out);
const AddressLabel* find_label(const std::vector<AddressLabel>& labels, uint32_t address);

} // namespace GMSLib::SaveBackend
