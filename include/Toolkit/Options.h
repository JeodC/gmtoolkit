// SPDX-License-Identifier: MIT

#pragma once

#include "Toolkit/PatchShaders.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Options {
    int bytecode_target = 0;
    std::string externalize_dir;
    bool repack = false;
    uint32_t set_flags = 0;
    uint32_t clear_flags = 0;
    bool compress_audio = false;

    std::string block_name = "4x4";
    std::string quality = "medium";
    long threads = 0;
    size_t max_strip = 25000000;

    int page_size = 1024;
    int max_dims = 0;
    int max_area = 0;

    // SIZE_MAX is the sentinel for "auto" — CompressAudio walks the AUDO entry
    // size distribution and picks a threshold that covers most of the byte
    // volume while skipping the short-clip regime where 64kbps Vorbis is
    // brittle. Any positive integer is an explicit override.
    size_t audio_min_size = SIZE_MAX;
    int audio_bitrate = 0;
    bool audio_downmix = false;
    int audio_resample = 0;
    bool audio_recompress = false;

    bool verbose = false;
    bool skip_audiogroups = false;

    std::vector<unsigned int> keep_pages;
    std::vector<uint32_t> keep_colors;

    std::vector<ShaderPatch> shader_patches;

    struct CodePatchSpec {
        std::string entry_name;
        std::string gml_path;
    };
    std::vector<CodePatchSpec> code_patches;
    std::string config_dir;
    std::string game;
};

int load_config_json(const char* path, Options& opt);
