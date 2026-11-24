// SPDX-License-Identifier: MIT

#pragma once

#include "Toolkit/Version.h"

#include <cstdint>
#include <string>
#include <vector>

struct block_info;

namespace Ops {

struct ExtractOptions {
    std::vector<unsigned int> keep_pages;
    std::vector<uint32_t> keep_colors;
    int threads;
    size_t max_strip;
    Gmtoolkit::Version version;
};

int extract_textures(const char* data_win, const char* out_dir, const block_info* blk, float quality,
                     const ExtractOptions& opt);

} // namespace Ops
