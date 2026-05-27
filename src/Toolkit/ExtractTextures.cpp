// SPDX-License-Identifier: MIT

#include "Toolkit/ExtractTextures.h"

#include "astcenc.h"
#include "Toolkit/Codec/Txtr.h"
#include "Toolkit/IO.h"
#include "Toolkit/Log.h"
#include "Toolkit/Platform.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern astcenc_context* astc_make_context(int block_x, int block_y, float quality, unsigned threads);
extern int compress_rgba_to_pvr(const uint8_t* rgba, int w, int h, const char* out_path, const struct block_info* blk,
                                astcenc_context* ctx, unsigned threads, size_t max_strip);
extern int detect_blob_format_int(const uint8_t* p, size_t len);
extern size_t scan_blob_len(TxtrFormat fmt, const uint8_t* src, size_t src_left);
extern int decode_blob_to_rgba(TxtrFormat fmt, const uint8_t* blob, size_t blob_len, uint8_t** rgba, int* w, int* h);
extern int build_stub_for_format(TxtrFormat fmt, unsigned int idx, uint8_t** out_blob, size_t* out_blob_len);
extern long compact_txtr(FILE* f, size_t txtr_start, size_t txtr_size, uint8_t** stubs, size_t* stub_lens,
                         unsigned int count, size_t entry_size);
extern TxtrFormat detect_blob_format(const uint8_t* p, size_t len);

namespace Ops {

namespace {

bool rgba_contains_any_color(const uint8_t* rgba, int w, int h, const std::vector<uint32_t>& colors) {
    if (colors.empty() || !rgba || w <= 0 || h <= 0)
        return false;
    size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; i++) {
        const uint8_t* p = rgba + i * 4;
        if (p[3] == 0)
            continue;
        uint32_t rgb = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
        for (uint32_t c : colors) {
            if (rgb == c)
                return true;
        }
    }
    return false;
}

} // namespace

int extract_textures(const char* data_win, const char* out_dir, const block_info* blk, float quality,
                     const ExtractOptions& opt) {
    MappedFile mf;
    if (mapped_file_open(data_win, &mf) != 0) {
        perror(data_win);
        return 3;
    }
    long total_size = (long)mf.size;
    uint8_t* win = mf.data;

    FILE* f = fopen(data_win, "r+b");
    if (!f) {
        perror(data_win);
        mapped_file_close(&mf);
        return 3;
    }

    uint32_t count = 0;
    uint8_t** stubs = nullptr;
    size_t* stub_lens = nullptr;
    astcenc_context* astc_ctx = nullptr;
    auto cleanup = [&]() {
        if (astc_ctx)
            astcenc_context_free(astc_ctx);
        if (stubs)
            for (uint32_t j = 0; j < count; j++)
                free(stubs[j]);
        free(stubs);
        free(stub_lens);
        mapped_file_close(&mf);
        fclose(f);
    };

    size_t txtr_start, txtr_size;
    if (Gmtoolkit::find_chunk(win, (size_t)total_size, "TXTR", &txtr_start, &txtr_size) != 0) {
        Gmtoolkit::err("no TXTR chunk in %s", data_win);
        cleanup();
        return 5;
    }
    size_t txtr_end = txtr_start + txtr_size;

    const uint8_t* base = win + txtr_start;
    count = Gmtoolkit::r_u32(base);
    const uint8_t* ptab = base + 4;
    size_t entry_size = Gmtoolkit::detect_txtr_entry_size(base, txtr_size);
    size_t ptr_off_in_entry = entry_size - 4;
    Gmtoolkit::msg("TXTR chunk: offset=0x%zx  size=%zu (%.1f MB), count=%u, entry_size=%zu", txtr_start, txtr_size,
                   txtr_size / 1048576.0, count, entry_size);
    fflush(stdout);

    stubs = (uint8_t**)calloc(count, sizeof(uint8_t*));
    stub_lens = (size_t*)calloc(count, sizeof(size_t));
    if (!stubs || !stub_lens) {
        Gmtoolkit::err("OOM");
        cleanup();
        return 4;
    }

    std::vector<bool> keep_set(count, false);
    for (unsigned int idx : opt.keep_pages) {
        if (idx < count)
            keep_set[idx] = true;
    }
    unsigned int n_kept = 0;

    astc_ctx = astc_make_context(blk->bx, blk->by, quality, (unsigned)opt.threads);
    if (!astc_ctx) {
        cleanup();
        return 4;
    }

    char pvr_path[1024];
    unsigned int n_internal = 0;
    unsigned int n_external = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t rec_off = Gmtoolkit::r_u32(ptab + i * 4);
        if (rec_off + entry_size > (size_t)total_size) {
            Gmtoolkit::err("entry %u pointer out of range", i);
            cleanup();
            return 5;
        }
        const uint8_t* rec = win + rec_off;
        uint32_t blob_off = Gmtoolkit::r_u32(rec + ptr_off_in_entry);
        if (blob_off == 0) {
            n_external++;
            continue;
        }
        if (blob_off >= txtr_end) {
            Gmtoolkit::warn("entry %u: blob_off 0x%x outside TXTR", i, blob_off);
            continue;
        }

        TxtrFormat fmt = detect_blob_format(win + blob_off, txtr_end - blob_off);
        if (fmt == TxtrFormat::UNKNOWN) {
            Gmtoolkit::warn("entry %u: unrecognised blob magic @0x%x", i, blob_off);
            continue;
        }
        size_t blob_len = scan_blob_len(fmt, win + blob_off, txtr_end - blob_off);
        if (blob_len == 0) {
            Gmtoolkit::warn("entry %u: scan_blob_len returned 0", i);
            continue;
        }

        uint8_t* rgba = nullptr;
        int tex_w = 0, tex_h = 0;
        if (decode_blob_to_rgba(fmt, win + blob_off, blob_len, &rgba, &tex_w, &tex_h) != 0) {
            continue;
        }
        const char* fmt_name = fmt == TxtrFormat::ZOQ    ? "2zoq"
                               : fmt == TxtrFormat::QOIF ? "fioq"
                               : fmt == TxtrFormat::PNG  ? "png"
                                                         : "?";

        bool keep_inline = keep_set[i];
        if (!keep_inline && !opt.keep_colors.empty()) {
            keep_inline = rgba_contains_any_color(rgba, tex_w, tex_h, opt.keep_colors);
        }
        // The inline copy keeps anything the engine needs to read at runtime (palette lookup, etc).
        if (keep_inline) {
            stubs[i] = (uint8_t*)malloc(blob_len);
            if (!stubs[i]) {
                Gmtoolkit::err("OOM staging inline blob for entry %u", i);
                free(rgba);
                cleanup();
                return 4;
            }
            memcpy(stubs[i], win + blob_off, blob_len);
            stub_lens[i] = blob_len;
            free(rgba);
            n_kept++;
            n_internal++;
            Gmtoolkit::msg("Texture %u (%s %dx%d): kept inline (palette/explicit).", i, fmt_name, tex_w, tex_h);
            fflush(stdout);
            continue;
        }

        Gmtoolkit::msg("Texture %u (%s %dx%d): compressing...", i, fmt_name, tex_w, tex_h);
        fflush(stdout);

        snprintf(pvr_path, sizeof(pvr_path), "%s/%u.pvr", out_dir, i);
        int r = compress_rgba_to_pvr(rgba, tex_w, tex_h, pvr_path, blk, astc_ctx, (unsigned)opt.threads, opt.max_strip);
        free(rgba);
        if (r != 0) {
            cleanup();
            return 6;
        }

        // Replace the inline blob with a tiny same-format stub so chunk walkers still see valid bytes.
        if (build_stub_for_format(fmt, i, &stubs[i], &stub_lens[i]) != 0) {
            Gmtoolkit::err("build_stub_for_format %u failed", i);
            cleanup();
            return 7;
        }
        n_internal++;
    }
    astcenc_context_free(astc_ctx);
    astc_ctx = nullptr;
    mapped_file_close(&mf);
    mf.data = nullptr;
    Gmtoolkit::msg("Found %u TXTR textures (%u internal, %u external, %u kept inline).", count, n_internal, n_external,
                   n_kept);
    fflush(stdout);

    long new_size = compact_txtr(f, txtr_start, txtr_size, stubs, stub_lens, count, entry_size);
    cleanup();
    if (new_size == 0) {
        return 8;
    }

    Gmtoolkit::msg("Updated %s: %ld -> %ld (%.1f MB saved)", data_win, total_size, new_size,
                   (total_size - new_size) / 1048576.0);
    return 0;
}

} // namespace Ops
