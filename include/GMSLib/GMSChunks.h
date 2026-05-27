// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace GMSLib {

using ChunkTag = std::string;

struct ChunkLocation {
    std::size_t PayloadOffset = 0;
    std::size_t PayloadSize = 0;
};

int IndexChunks(const std::uint8_t* Buffer, std::size_t BufferSize,
                std::unordered_map<ChunkTag, ChunkLocation>& OutChunks);
void SlideAudoPointers(std::FILE* F, std::size_t TxtrEndInNewFile, long Delta);

} // namespace GMSLib
