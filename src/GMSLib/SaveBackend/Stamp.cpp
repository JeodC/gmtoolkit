// SPDX-License-Identifier: MIT

#include "GMSLib/SaveBackend/Stamp.h"

#include "GMSLib/GMSData.h"
#include "GMSLib/GMSIO.h"
#include "GMSLib/Models/GMSString.h"
#include "Toolkit/IO.h"
#include "Toolkit/Log.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace GMSLib::SaveBackend {

namespace {

// FNV-1a 32-bit; we only need a short stable hash, not cryptographic strength.
uint32_t fnv1a32(const uint8_t* p, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x01000193u;
    }
    return h;
}

constexpr const char* TOOL_VERSION = "1.0";

} // namespace

std::string compute_config_hash(const char* config_path) {
    if (!config_path || !*config_path)
        return "noconfig";
    std::vector<uint8_t> data;
    if (Gmtoolkit::slurp(config_path, data) != 0)
        return "noconfig";
    uint32_t h = fnv1a32(data.data(), data.size());
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", h);
    return std::string(buf);
}

std::string make_sentinel(const std::string& config_hash) {
    std::string s = GMTK_SENTINEL_PREFIX;
    s += TOOL_VERSION;
    s += ":";
    s += config_hash;
    s += "__";
    return s;
}

int find_sentinel(const char* data_file_path, std::string* out) {
    if (!data_file_path)
        return -1;
    std::vector<uint8_t> buf;
    if (Gmtoolkit::slurp(data_file_path, buf) != 0)
        return -1;
    size_t strg_off, strg_size;
    if (Gmtoolkit::find_chunk(buf.data(), buf.size(), "STRG", &strg_off, &strg_size) != 0)
        return -1;
    if (strg_size < 4)
        return -1;
    uint32_t count = Gmtoolkit::r_u32(buf.data() + strg_off);
    if (count == 0 || 4 + (size_t)count * 4 > strg_size)
        return -1;
    // STRG ptr_table values on disk point at the 4-byte length prefix; the
    // string data lives at ptr+4. (In-memory Pools::strg_ptr_table stores
    // the data position instead; don't confuse the two conventions.)
    const size_t prefix_len = std::strlen(GMTK_SENTINEL_PREFIX);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t ptr = Gmtoolkit::r_u32(buf.data() + strg_off + 4 + (size_t)i * 4);
        if (ptr == 0 || (size_t)ptr + 4 > buf.size())
            continue;
        uint32_t slen = Gmtoolkit::r_u32(buf.data() + ptr);
        if (slen < prefix_len || (size_t)ptr + 4 + slen > buf.size())
            continue;
        if (std::memcmp(buf.data() + ptr + 4, GMTK_SENTINEL_PREFIX, prefix_len) == 0) {
            if (out)
                out->assign((const char*)(buf.data() + ptr + 4), slen);
            return 0;
        }
    }
    return -1;
}

int stamp_file(const char* data_file_path, const std::string& sentinel) {
    // Idempotency: if the exact sentinel already lives in STRG, don't pay for
    // another Load+Save just to re-append a duplicate copy.
    std::string existing;
    if (find_sentinel(data_file_path, &existing) == 0 && existing == sentinel)
        return 0;

    GMSLib::GMSData Data;
    if (GMSLib::LoadFromFile(data_file_path, Data) != 0) {
        Gmtoolkit::err("stamp: failed to load %s", data_file_path);
        return -1;
    }
    auto S = std::make_unique<GMSLib::GMSString>(sentinel);
    S->Id = (int32_t)Data.Strings.size();
    // SourcePayloadOffset stays -1: SaveToFile treats Strings past OriginalStringCount
    // as pending and emits them through the STRG-append path in Pools::commit.
    Data.StringByContent.emplace(S->Content, S.get());
    Data.Strings.push_back(std::move(S));

    if (GMSLib::SaveToFile(data_file_path, Data) != 0) {
        Gmtoolkit::err("stamp: failed to save %s", data_file_path);
        return -1;
    }
    return 0;
}

} // namespace GMSLib::SaveBackend
