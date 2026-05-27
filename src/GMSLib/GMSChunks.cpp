// SPDX-License-Identifier: GPL-3.0-or-later

#include "GMSLib/GMSChunks.h"

#include "Toolkit/IO.h"

#include <cstdlib>
#include <cstring>

namespace GMSLib {

// IFF-style container: "FORM" + size + flat list of (tag, size, payload)
// chunks. We index by tag and trust the size field to walk to the next one.
int IndexChunks(const std::uint8_t* Buffer, std::size_t BufferSize,
                std::unordered_map<ChunkTag, ChunkLocation>& OutChunks) {
    if (BufferSize < 8)
        return -1;
    if (std::memcmp(Buffer, "FORM", 4) != 0)
        return -1;
    std::uint32_t FormSize = Gmtoolkit::r_u32(Buffer + 4);
    std::size_t End = static_cast<std::size_t>(8) + FormSize;
    if (End > BufferSize)
        return -1;

    std::size_t Pos = 8;
    while (Pos + 8 <= End) {
        const std::uint8_t* Hdr = Buffer + Pos;
        ChunkTag Tag(reinterpret_cast<const char*>(Hdr), 4);
        std::uint32_t ChunkSize = Gmtoolkit::r_u32(Hdr + 4);
        std::size_t PayloadStart = Pos + 8;
        if (PayloadStart + ChunkSize > End)
            return -1;
        OutChunks[Tag] = ChunkLocation{ PayloadStart, ChunkSize };
        Pos = PayloadStart + ChunkSize;
    }
    return 0;
}

// AUDO entries are referenced by absolute file offsets, so when a chunk
// upstream of it grows/shrinks (typically TXTR) every pointer in AUDO has to
// be bumped by Delta. Skip the work if AUDO sits before the resized region.
void SlideAudoPointers(std::FILE* F, std::size_t TxtrEndInNewFile, long Delta) {
    if (Delta == 0)
        return;
    std::fseek(F, 8, SEEK_SET);
    for (;;) {
        long ChunkHdrPos = std::ftell(F);
        std::uint8_t Hdr[8];
        if (std::fread(Hdr, 1, 8, F) != 8)
            break;
        std::uint32_t CSize = Gmtoolkit::r_u32(Hdr + 4);
        if (std::memcmp(Hdr, "AUDO", 4) == 0 && static_cast<std::size_t>(ChunkHdrPos) >= TxtrEndInNewFile) {
            long PayloadStart = ChunkHdrPos + 8;
            std::uint32_t AudoCount = 0;
            if (std::fread(&AudoCount, 4, 1, F) != 1)
                break;
            if (AudoCount == 0)
                break;
            std::uint32_t* AudoPtrs = static_cast<std::uint32_t*>(std::malloc(AudoCount * 4u));
            if (AudoPtrs == nullptr)
                break;
            if (std::fread(AudoPtrs, 4, AudoCount, F) != AudoCount) {
                std::free(AudoPtrs);
                break;
            }
            for (std::uint32_t i = 0; i < AudoCount; i++) {
                AudoPtrs[i] = static_cast<std::uint32_t>(static_cast<std::int64_t>(AudoPtrs[i]) + Delta);
            }
            std::fseek(F, PayloadStart + 4, SEEK_SET);
            std::fwrite(AudoPtrs, 4, AudoCount, F);
            std::free(AudoPtrs);
            break;
        }
        if (std::fseek(F, ChunkHdrPos + 8 + static_cast<long>(CSize), SEEK_SET) != 0)
            break;
    }
}

} // namespace GMSLib
