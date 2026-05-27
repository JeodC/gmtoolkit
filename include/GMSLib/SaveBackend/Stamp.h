// SPDX-License-Identifier: MIT

#pragma once

#include <string>

namespace GMSLib::SaveBackend {

// Marker prefix used by gmtoolkit to recognise its own previously-patched files.
constexpr const char* GMTK_SENTINEL_PREFIX = "__gmtk:";

// Compute a stable 32-bit hex hash of a config file's contents (FNV-1a).
// Returns "noconfig" when path is null/empty/unreadable; the literal stays
// stable across runs so a no-config patch is still detectable by --check.
std::string compute_config_hash(const char* config_path);

// Build the full sentinel string: "__gmtk:<tool_version>:<config_hash>__"
std::string make_sentinel(const std::string& config_hash);

// Scan STRG for a string starting with GMTK_SENTINEL_PREFIX.
// Returns 0 and fills *out on hit, -1 on miss / read failure.
int find_sentinel(const char* data_file_path, std::string* out);

// Append a new STRG entry containing `sentinel` and rewrite the file.
// Goes through GMSData::Load + Save so all the version-specific downstream
// chunk fixups in the commit path apply automatically.
int stamp_file(const char* data_file_path, const std::string& sentinel);

} // namespace GMSLib::SaveBackend
