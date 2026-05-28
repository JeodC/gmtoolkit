// SPDX-License-Identifier: MIT
#include "Toolkit/PatchShaders.h"

#include "Toolkit/IO.h"
#include "Toolkit/Platform.h"
#include "Toolkit/Log.h"

#include <cstdint>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <vector>

using Gmtoolkit::find_chunk;
using Gmtoolkit::r_u32;

// Byte offset of each stage's source pointer within a SHDR entry.
static int stage_offset(const std::string& stage) {
    if (stage == "glsl_es_vertex")
        return 8;
    if (stage == "glsl_es_fragment")
        return 12;
    if (stage == "glsl_vertex")
        return 16;
    if (stage == "glsl_fragment")
        return 20;
    if (stage == "hlsl9_vertex")
        return 24;
    if (stage == "hlsl9_fragment")
        return 28;
    return -1;
}

static uint32_t strg_string_length(const uint8_t* buf, size_t buf_len, uint32_t ptr) {
    if (ptr < 4 || (size_t)ptr > buf_len)
        return 0;
    return r_u32(buf + ptr - 4);
}

// In-place substitution: replacement must be <= pattern, gap is padded with spaces (GLSL no-op).
static int apply_one_patch(uint8_t* buf, uint32_t data_ptr, uint32_t data_len, const std::string& find,
                           const std::string& replace, const char* shader, const char* stage) {
    if (find.empty())
        return 0;
    if (replace.size() > find.size()) {
        Gmtoolkit::err("shader patch %s:%s: replacement (%zu bytes) longer than "
                       "pattern (%zu bytes); not yet supported -- pad your `find` "
                       "or shorten your `replace`.\n",
                       shader, stage, replace.size(), find.size());
        return -1;
    }
    int hits = 0;
    char* data = (char*)(buf + data_ptr);
    size_t pos = 0;
    while (pos + find.size() <= data_len) {
        if (memcmp(data + pos, find.data(), find.size()) == 0) {
            memcpy(data + pos, replace.data(), replace.size());
            size_t pad = find.size() - replace.size();
            if (pad)
                memset(data + pos + replace.size(), ' ', pad);
            hits++;
            pos += find.size();
        } else {
            pos++;
        }
    }
    return hits;
}

int apply_shader_patches(uint8_t* win, size_t win_size, const std::vector<ShaderPatch>& patches) {
    if (patches.empty())
        return 0;

    size_t shdr_start, shdr_size;
    if (find_chunk(win, win_size, "SHDR", &shdr_start, &shdr_size) != 0) {
        Gmtoolkit::err("shader patches: SHDR chunk not found");
        return -1;
    }
    size_t strg_start, strg_size;
    if (find_chunk(win, win_size, "STRG", &strg_start, &strg_size) != 0) {
        Gmtoolkit::err("shader patches: STRG chunk not found");
        return -1;
    }

    const uint8_t* shdr = win + shdr_start;
    uint32_t shader_count = r_u32(shdr);

    // Aggregate stats per wildcard patch so we can emit a single summary line
    // instead of one per (shader, stage) hit — wildcards typically touch dozens
    // of shaders for engine-wide adjustments like mediump->highp.
    std::vector<int> wildcard_hits(patches.size(), 0);
    std::vector<int> wildcard_shaders(patches.size(), 0);

    for (uint32_t i = 0; i < shader_count; i++) {
        uint32_t ent = r_u32(shdr + 4 + i * 4);
        if (ent + 32 > win_size)
            continue;
        uint32_t name_ptr = r_u32(win + ent);
        if (name_ptr < 4 || name_ptr >= win_size)
            continue;
        uint32_t name_len = strg_string_length(win, win_size, name_ptr);
        if (name_ptr + name_len > win_size)
            continue;
        std::string name((const char*)(win + name_ptr), name_len);

        for (size_t pi = 0; pi < patches.size(); pi++) {
            const ShaderPatch& pp = patches[pi];
            bool wildcard = (pp.shader == "*");
            if (!wildcard && pp.shader != name)
                continue;
            int soff = stage_offset(pp.stage);
            if (soff < 0) {
                Gmtoolkit::err("shader patch %s: unknown stage `%s' "
                               "(use glsl_es_fragment / glsl_es_vertex / glsl_fragment "
                               "/ glsl_vertex / hlsl9_fragment / hlsl9_vertex)",
                               pp.shader.c_str(), pp.stage.c_str());
                return -1;
            }
            if (ent + (uint32_t)soff + 4 > win_size)
                continue;
            uint32_t src_ptr = r_u32(win + ent + soff);
            if (src_ptr == 0) {
                // Wildcards skip absent stages silently; a named patch still
                // gets the diagnostic so the user knows their config targets
                // a stage that doesn't exist on that shader.
                if (!wildcard)
                    Gmtoolkit::err("shader patch %s:%s: stage not present (null ptr)",
                                   pp.shader.c_str(), pp.stage.c_str());
                continue;
            }
            if (src_ptr < 4 || src_ptr >= win_size) {
                Gmtoolkit::err("shader patch %s:%s: source ptr 0x%x out of bounds", pp.shader.c_str(), pp.stage.c_str(),
                               src_ptr);
                return -1;
            }
            uint32_t src_len = strg_string_length(win, win_size, src_ptr);
            if ((size_t)src_ptr + src_len > win_size) {
                Gmtoolkit::err("shader patch %s:%s: source length overflows file", pp.shader.c_str(), pp.stage.c_str());
                return -1;
            }
            int hits = apply_one_patch(win, src_ptr, src_len, pp.find, pp.replace, pp.shader.c_str(), pp.stage.c_str());
            if (hits < 0)
                return -1;
            if (wildcard) {
                wildcard_hits[pi] += hits;
                if (hits > 0)
                    wildcard_shaders[pi]++;
                continue;
            }
            if (hits == 0) {
                Gmtoolkit::err("shader patch %s:%s: pattern not found "
                               "(`%s' -- 0 replacements)",
                               pp.shader.c_str(), pp.stage.c_str(), pp.find.c_str());
            } else {
                Gmtoolkit::msg("  shader patch %s:%s: %d replacement%s", pp.shader.c_str(), pp.stage.c_str(), hits,
                               hits == 1 ? "" : "s");
            }
        }
    }
    for (size_t pi = 0; pi < patches.size(); pi++) {
        if (patches[pi].shader != "*")
            continue;
        Gmtoolkit::msg("  shader patch *:%s: %d replacement%s across %d shader%s",
                       patches[pi].stage.c_str(), wildcard_hits[pi], wildcard_hits[pi] == 1 ? "" : "s",
                       wildcard_shaders[pi], wildcard_shaders[pi] == 1 ? "" : "s");
    }
    return 0;
}

int patch_shaders_in_file(const char* path, const std::vector<ShaderPatch>& patches) {
    if (patches.empty())
        return 0;

    FILE* f = fopen(path, "r+b");
    if (!f) {
        perror(path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) {
        fclose(f);
        return -1;
    }
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }

    int rc = apply_shader_patches(buf, (size_t)sz, patches);
    if (rc == 0) {
        rewind(f);
        if (fwrite(buf, 1, (size_t)sz, f) != (size_t)sz) {
            perror(path);
            rc = -1;
        }
    }
    free(buf);
    fclose(f);
    return rc;
}
