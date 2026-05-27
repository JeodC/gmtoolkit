// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ShaderPatch {
    std::string shader;
    std::string stage;
    std::string find;
    std::string replace;
};

int apply_shader_patches(uint8_t* win, size_t win_size, const std::vector<ShaderPatch>& patches);
int patch_shaders_in_file(const char* path, const std::vector<ShaderPatch>& patches);
