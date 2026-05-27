// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace GMSLib::Compiler {

bool lookup_constant(std::string_view name, double* out_value);

struct BuiltinVar {
    bool is_global;
    bool is_automatic_array;
};

bool lookup_builtin_var(std::string_view name, BuiltinVar* out_info);
bool lookup_builtin_func(std::string_view name);

} // namespace GMSLib::Compiler
