// SPDX-License-Identifier: MIT

#include "astcenc.h"
#include "GMSLib/GMSChunks.h"
#include "Toolkit/Codec/Txtr.h"
#include "Toolkit/IO.h"
#include "Toolkit/Platform.h"
#include "Toolkit/Log.h"

#include <algorithm>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utility>
#include <vector>

using Gmtoolkit::find_chunk;
using Gmtoolkit::r_u32;

extern TxtrFormat detect_blob_format(const uint8_t* p, size_t len);
extern size_t scan_blob_len(TxtrFormat fmt, const uint8_t* src, size_t src_left);
extern int decode_blob_to_rgba(TxtrFormat fmt, const uint8_t* blob, size_t blob_len, uint8_t** out_rgba, int* out_w,
                               int* out_h);
extern int build_stub_for_format(TxtrFormat fmt, unsigned int idx, uint8_t** out, size_t* out_len);
extern int encode_atlas_in_format(TxtrFormat fmt, const uint8_t* rgba, int w, int h, uint8_t** out, size_t* out_len);

extern astcenc_context* astc_make_context(int block_x, int block_y, float quality, unsigned threads);
extern int compress_rgba_to_pvr(const uint8_t* rgba, int w, int h, const char* out_path, const struct block_info* blk,
                                astcenc_context* ctx, unsigned threads, size_t max_strip);

struct rect_t {
    int x, y, w, h;
};

struct split_t {
    int x, y, w, h;
    bool invalidated;
    int right() const {
        return x + w;
    }
    int down() const {
        return y + h;
    }
    bool contains(const rect_t& r) const {
        return r.x >= x && r.y >= y && right() >= r.x + r.w && down() >= r.y + r.h;
    }
    bool overlaps(const rect_t& r) const {
        bool x_ov = (r.x >= x && r.x <= right()) || (x >= r.x && x <= r.x + r.w);
        bool y_ov = (r.y >= y && r.y <= down()) || (y >= r.y && y <= r.y + r.h);
        return x_ov && y_ov;
    }
    bool fits(int W, int H) const {
        return w >= W && h >= H;
    }
};

struct atlas_t {
    int w, h;
    std::vector<split_t> splits;
    std::vector<int> tpi_indices;
};

// Guillotine bin-packing: pick the split that wastes least, slice it into up-to-four child rects,
// then drop any child that's contained in another.
static bool atlas_allocate(atlas_t& a, int width, int height, int padding, rect_t* out) {
    int pw = width + 2 * padding;
    int ph = height + 2 * padding;
    int best = -1;
    int best_score = INT_MAX;
    for (size_t i = 0; i < a.splits.size(); i++) {
        if (!a.splits[i].fits(pw, ph))
            continue;
        int score = std::max(a.splits[i].w - pw, a.splits[i].h - ph);
        if (score < best_score) {
            best_score = score;
            best = (int)i;
        }
    }
    if (best < 0)
        return false;

    rect_t r = { a.splits[best].x, a.splits[best].y, pw, ph };
    std::vector<split_t> new_splits;
    for (auto& s : a.splits) {
        if (s.invalidated || !s.overlaps(r))
            continue;
        s.invalidated = true;
        if (r.y - s.y > 0)
            new_splits.push_back({ s.x, s.y, s.w, r.y - s.y, false });
        if (r.x - s.x > 0)
            new_splits.push_back({ s.x, s.y, r.x - s.x, s.h, false });
        if (s.down() - (r.y + r.h) > 0)
            new_splits.push_back({ s.x, r.y + r.h, s.w, s.down() - (r.y + r.h), false });
        if (s.right() - (r.x + r.w) > 0)
            new_splits.push_back({ r.x + r.w, s.y, s.right() - (r.x + r.w), s.h, false });
    }
    a.splits.erase(std::remove_if(a.splits.begin(), a.splits.end(), [](const split_t& s) { return s.invalidated; }),
                   a.splits.end());
    a.splits.insert(a.splits.end(), new_splits.begin(), new_splits.end());

    for (size_t i = 0; i < a.splits.size(); i++) {
        if (a.splits[i].invalidated)
            continue;
        for (size_t j = 0; j < a.splits.size(); j++) {
            if (i == j || a.splits[j].invalidated)
                continue;
            rect_t r2 = { a.splits[j].x, a.splits[j].y, a.splits[j].w, a.splits[j].h };
            if (a.splits[i].contains(r2))
                a.splits[j].invalidated = true;
        }
    }
    a.splits.erase(std::remove_if(a.splits.begin(), a.splits.end(), [](const split_t& s) { return s.invalidated; }),
                   a.splits.end());

    out->x = r.x + padding;
    out->y = r.y + padding;
    out->w = width;
    out->h = height;
    return true;
}

struct tpi_t {
    long file_offset;
    uint16_t source_x, source_y, source_w, source_h;
    uint16_t target_x, target_y, target_w, target_h;
    uint16_t bounding_w, bounding_h;
    int16_t orig_page_idx;

    int new_page_idx;
    rect_t new_rect;
};

struct src_page_t {
    long blob_offset;
    size_t blob_len;
    uint32_t scaled;
    uint32_t generated_mips;
    int w, h;
    TxtrFormat format;
    bool external;
};

static int parse_tpag(const uint8_t* win, size_t win_len, std::vector<tpi_t>& out_tpis) {
    size_t tpag_start, tpag_size;
    if (find_chunk(win, win_len, "TPAG", &tpag_start, &tpag_size) != 0) {
        Gmtoolkit::err("No TPAG chunk.");
        return -1;
    }
    (void)tpag_size;
    const uint8_t* p = win + tpag_start;
    uint32_t count = r_u32(p);
    p += 4;
    out_tpis.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t ptr = r_u32(p);
        p += 4;
        if (ptr + 22 > win_len) {
            Gmtoolkit::err("TPAG entry %u ptr 0x%x out of range", i, ptr);
            return -2;
        }
        const uint8_t* r = win + ptr;
        tpi_t t;
        t.file_offset = (long)ptr;
        t.source_x = r[0] | (r[1] << 8);
        t.source_y = r[2] | (r[3] << 8);
        t.source_w = r[4] | (r[5] << 8);
        t.source_h = r[6] | (r[7] << 8);
        t.target_x = r[8] | (r[9] << 8);
        t.target_y = r[10] | (r[11] << 8);
        t.target_w = r[12] | (r[13] << 8);
        t.target_h = r[14] | (r[15] << 8);
        t.bounding_w = r[16] | (r[17] << 8);
        t.bounding_h = r[18] | (r[19] << 8);
        t.orig_page_idx = (int16_t)(r[20] | (r[21] << 8));
        t.new_page_idx = -1;
        t.new_rect = { 0, 0, 0, 0 };
        out_tpis.push_back(t);
    }
    return 0;
}

static int parse_txtr_pages(const uint8_t* win, size_t win_len, std::vector<src_page_t>& out_pages,
                            size_t* out_entry_size) {
    size_t txtr_start, txtr_size;
    if (find_chunk(win, win_len, "TXTR", &txtr_start, &txtr_size) != 0) {
        Gmtoolkit::err("No TXTR chunk.");
        return -1;
    }
    const uint8_t* base = win + txtr_start;
    uint32_t count = r_u32(base);
    const uint8_t* ptab = base + 4;
    size_t entry_size = Gmtoolkit::detect_txtr_entry_size(base, txtr_size);
    if (out_entry_size)
        *out_entry_size = entry_size;
    size_t ptr_off_in_entry = entry_size - 4;
    out_pages.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t rec_off = r_u32(ptab + i * 4);
        if (rec_off + entry_size > win_len) {
            Gmtoolkit::err("TXTR entry %u ptr out of range", i);
            return -2;
        }
        const uint8_t* rec = win + rec_off;
        src_page_t s;
        s.scaled = r_u32(rec);
        s.generated_mips = (entry_size >= 12) ? r_u32(rec + 4) : 0;
        uint32_t blob_off = r_u32(rec + ptr_off_in_entry);
        s.blob_offset = (long)blob_off;
        s.external = false;
        s.w = 0;
        s.h = 0;
        if (blob_off == 0) {
            s.external = true;
            s.blob_len = 0;
            s.format = TxtrFormat::UNKNOWN;
            out_pages.push_back(s);
            continue;
        }
        size_t left = (txtr_start + txtr_size) - (size_t)blob_off;
        s.format = detect_blob_format(win + blob_off, left);
        if (s.format == TxtrFormat::UNKNOWN) {
            Gmtoolkit::err("TXTR entry %u: unrecognised blob magic @0x%x", i, blob_off);
            return -3;
        }
        s.blob_len = scan_blob_len(s.format, win + blob_off, left);
        if (s.blob_len == 0) {
            Gmtoolkit::err("TXTR entry %u: blob @0x%x not valid", i, blob_off);
            return -3;
        }
        out_pages.push_back(s);
    }
    return 0;
}

static void run_repacker(std::vector<tpi_t>& tpis, const std::vector<src_page_t>& pages, int page_size, int padding,
                         int max_dims, int max_area, std::vector<atlas_t>& out_atlases) {
    std::vector<int> per_page_count(pages.size(), 0);
    for (auto& t : tpis) {
        if (t.orig_page_idx >= 0 && (size_t)t.orig_page_idx < pages.size())
            per_page_count[t.orig_page_idx]++;
    }

    std::vector<int> repack_idx;
    std::vector<int> solo_idx;
    repack_idx.reserve(tpis.size());
    int rej_dims = 0, rej_area = 0, rej_alone = 0, rej_bad_page = 0, rej_zero = 0;
    for (size_t i = 0; i < tpis.size(); i++) {
        const tpi_t& t = tpis[i];
        bool too_big_dim = (t.source_w > max_dims || t.source_h > max_dims);
        bool too_big_area = ((int)t.source_w * (int)t.source_h > max_area);
        bool bad_page = (t.orig_page_idx < 0 || (size_t)t.orig_page_idx >= pages.size());
        bool zero_size = (t.source_w == 0 || t.source_h == 0);
        bool alone = !bad_page && per_page_count[t.orig_page_idx] <= 1;

        if (zero_size) {
            rej_zero++;
            solo_idx.push_back((int)i);
            continue;
        }
        if (bad_page) {
            rej_bad_page++;
            solo_idx.push_back((int)i);
            continue;
        }
        if (too_big_dim) {
            rej_dims++;
            solo_idx.push_back((int)i);
            continue;
        }
        if (too_big_area) {
            rej_area++;
            solo_idx.push_back((int)i);
            continue;
        }
        if (alone) {
            rej_alone++;
            solo_idx.push_back((int)i);
            continue;
        }
        repack_idx.push_back((int)i);
    }
    Gmtoolkit::msg("TPI filter: %zu eligible / %zu total"
                   "  rejected: dims=%d  area=%d  alone-on-page=%d  bad-page=%d  zero=%d\n",
                   repack_idx.size(), tpis.size(), rej_dims, rej_area, rej_alone, rej_bad_page, rej_zero);
    fflush(stdout);

    // Pack largest first to keep small TPIs filling the gaps later.
    std::sort(repack_idx.begin(), repack_idx.end(), [&](int a, int b) {
        int la = std::max(tpis[a].source_w, tpis[a].source_h);
        int lb = std::max(tpis[b].source_w, tpis[b].source_h);
        return la < lb;
    });

    std::vector<int> pending = repack_idx;
    while (!pending.empty()) {
        atlas_t a;
        a.w = page_size;
        a.h = page_size;
        a.splits.push_back({ 0, 0, page_size, page_size, false });

        std::vector<int> leftover;
        for (int idx : pending) {
            rect_t r;
            if (atlas_allocate(a, tpis[idx].source_w, tpis[idx].source_h, padding, &r)) {
                tpis[idx].new_page_idx = (int)out_atlases.size();
                tpis[idx].new_rect = r;
                a.tpi_indices.push_back(idx);
            } else {
                leftover.push_back(idx);
            }
        }
        if (a.tpi_indices.empty()) {
            for (int idx : pending)
                solo_idx.push_back(idx);
            break;
        }
        out_atlases.push_back(std::move(a));
        pending.swap(leftover);
    }

    for (int idx : solo_idx) {
        tpi_t& t = tpis[idx];
        atlas_t a;
        a.w = t.source_w;
        a.h = t.source_h;
        a.tpi_indices.push_back(idx);
        t.new_page_idx = (int)out_atlases.size();
        t.new_rect = { 0, 0, t.source_w, t.source_h };
        out_atlases.push_back(std::move(a));
    }
}

// 2-slot LRU over decoded source pages: most TPIs cluster on 1-2 pages so anything bigger wastes RAM.
struct page_cache_t {
    struct slot {
        int page_idx;
        uint8_t* rgba;
        int w, h;
        uint64_t tick;
    } slots[2];
    uint64_t tick;
};

static void page_cache_init(page_cache_t& c) {
    for (int i = 0; i < 2; i++) {
        c.slots[i].page_idx = -1;
        c.slots[i].rgba = NULL;
        c.slots[i].w = c.slots[i].h = 0;
        c.slots[i].tick = 0;
    }
    c.tick = 0;
}
static void page_cache_free(page_cache_t& c) {
    for (int i = 0; i < 2; i++) {
        free(c.slots[i].rgba);
        c.slots[i].rgba = NULL;
    }
}

static int page_cache_get(page_cache_t& c, int page_idx, const uint8_t* win, const src_page_t& src, uint8_t** out_rgba,
                          int* out_w, int* out_h) {
    c.tick++;
    for (int i = 0; i < 2; i++) {
        if (c.slots[i].page_idx == page_idx && c.slots[i].rgba) {
            c.slots[i].tick = c.tick;
            *out_rgba = c.slots[i].rgba;
            *out_w = c.slots[i].w;
            *out_h = c.slots[i].h;
            return 0;
        }
    }
    uint8_t* rgba = NULL;
    int w = 0, h = 0;
    int r = decode_blob_to_rgba(src.format, win + src.blob_offset, src.blob_len, &rgba, &w, &h);
    if (r != 0) {
        Gmtoolkit::err("decode_blob_to_rgba(src page %d) failed: %d", page_idx, r);
        return -1;
    }
    int victim = (c.slots[0].tick <= c.slots[1].tick) ? 0 : 1;
    free(c.slots[victim].rgba);
    c.slots[victim].page_idx = page_idx;
    c.slots[victim].rgba = rgba;
    c.slots[victim].w = w;
    c.slots[victim].h = h;
    c.slots[victim].tick = c.tick;
    *out_rgba = rgba;
    *out_w = w;
    *out_h = h;
    return 0;
}

static void composite_crop(uint8_t* dst, int dst_w, int dst_h, int dst_x, int dst_y, const uint8_t* src, int src_w,
                           int src_h, int src_x, int src_y, int w, int h) {
    if (src_x < 0 || src_y < 0)
        return;
    if (src_x + w > src_w)
        w = src_w - src_x;
    if (src_y + h > src_h)
        h = src_h - src_y;
    if (dst_x + w > dst_w)
        w = dst_w - dst_x;
    if (dst_y + h > dst_h)
        h = dst_h - dst_y;
    if (w <= 0 || h <= 0)
        return;
    size_t row_bytes = (size_t)w * 4;
    for (int y = 0; y < h; y++) {
        memcpy(dst + (size_t)((dst_y + y) * dst_w + dst_x) * 4, src + (size_t)((src_y + y) * src_w + src_x) * 4,
               row_bytes);
    }
}

static inline void w_u32_le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static inline void w_i16_le(uint8_t* p, int16_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}

// TPAG packs x/y as a single u32 (y<<16 | x), same for w/h; rewrite both plus the new page index.
static void rewrite_tpi_records(uint8_t* win, const std::vector<tpi_t>& tpis) {
    for (const auto& t : tpis) {
        uint8_t* r = win + t.file_offset;
        w_u32_le(r + 0, ((uint32_t)(uint16_t)t.new_rect.y << 16) | (uint16_t)t.new_rect.x);
        w_u32_le(r + 4, ((uint32_t)(uint16_t)t.new_rect.h << 16) | (uint16_t)t.new_rect.w);

        w_i16_le(r + 20, (int16_t)t.new_page_idx);
    }
}

// Build a new TXTR with one entry per repacked atlas, lay blobs out 0x80-aligned, then truncate the
// file and slide AUDO by the delta so its absolute pointers still resolve.
static long rebuild_txtr_and_flush(FILE* f, MappedFile* mf, uint8_t* win, size_t win_len, size_t txtr_start,
                                   size_t txtr_size, size_t entry_size, const std::vector<atlas_t>& atlases,
                                   const std::vector<tpi_t>& tpis, const std::vector<src_page_t>& pages,
                                   std::vector<std::vector<uint8_t>>* inline_blobs) {
    size_t N = atlases.size();
    size_t ptr_off_in_entry = entry_size - 4;

    auto atlas_format = [&](size_t ai) -> TxtrFormat {
        if (atlases[ai].tpi_indices.empty())
            return TxtrFormat::ZOQ;
        int orig_pg = tpis[atlases[ai].tpi_indices[0]].orig_page_idx;
        if (orig_pg < 0 || (size_t)orig_pg >= pages.size())
            return TxtrFormat::ZOQ;
        return pages[orig_pg].format;
    };

    std::vector<std::vector<uint8_t>> stubs(N);
    if (inline_blobs && !inline_blobs->empty()) {
        if (inline_blobs->size() != N) {
            Gmtoolkit::err("inline_blobs size mismatch: %zu vs %zu atlases", inline_blobs->size(), N);
            return 0;
        }
        for (size_t i = 0; i < N; i++)
            stubs[i] = std::move((*inline_blobs)[i]);
    } else {
        for (size_t i = 0; i < N; i++) {
            uint8_t* s = NULL;
            size_t slen = 0;
            if (build_stub_for_format(atlas_format(i), (unsigned)i, &s, &slen) != 0) {
                Gmtoolkit::err("build_stub_for_format %zu failed", i);
                return 0;
            }
            stubs[i].assign(s, s + slen);
            free(s);
        }
    }

    auto inherit_for = [&](size_t ai) -> std::pair<uint32_t, uint32_t> {
        if (atlases[ai].tpi_indices.empty())
            return { 0, 0 };
        int tpi_idx = atlases[ai].tpi_indices[0];
        int orig_pg = tpis[tpi_idx].orig_page_idx;
        if (orig_pg < 0 || (size_t)orig_pg >= pages.size())
            return { 0, 0 };
        return { pages[orig_pg].scaled, pages[orig_pg].generated_mips };
    };

    size_t records_offset = txtr_start + 4 + 4 * N;
    size_t stubs_offset = records_offset + entry_size * N;

    std::vector<size_t> stub_pos(N);
    size_t cur = stubs_offset;
    for (size_t i = 0; i < N; i++) {
        while (cur & 0x7F)
            cur++;
        stub_pos[i] = cur;
        cur += stubs[i].size();
    }
    while ((cur - txtr_start) & 3u)
        cur++;
    size_t new_txtr_size = cur - txtr_start;

    size_t old_tail_start = txtr_start + txtr_size;
    size_t old_tail_len = win_len - old_tail_start;
    long delta = (long)new_txtr_size - (long)txtr_size;
    size_t new_total = txtr_start + new_txtr_size + old_tail_len;

    // If TXTR grew, we can't write in place over the mapped file; stage to a fresh buffer instead.
    uint8_t* out_buf;
    bool owns_out = false;
    if (delta > 0) {
        out_buf = (uint8_t*)malloc(new_total);
        if (!out_buf) {
            Gmtoolkit::err("OOM allocating %zu-byte output buffer", new_total);
            return 0;
        }
        owns_out = true;
        memcpy(out_buf, win, txtr_start);
        if (old_tail_len > 0) {
            memcpy(out_buf + txtr_start + new_txtr_size, win + old_tail_start, old_tail_len);
        }
    } else {
        out_buf = win;
    }

    uint8_t* p = out_buf + txtr_start;
    memset(p, 0, new_txtr_size);
    w_u32_le(p, (uint32_t)N);
    for (size_t i = 0; i < N; i++) {
        w_u32_le(p + 4 + 4 * i, (uint32_t)(records_offset + entry_size * i));
    }

    for (size_t i = 0; i < N; i++) {
        auto [sc, mp] = inherit_for(i);
        size_t roff = records_offset + entry_size * i - txtr_start;
        w_u32_le(p + roff + 0, sc);
        if (entry_size >= 12)
            w_u32_le(p + roff + 4, mp);
        if (entry_size >= 16)
            w_u32_le(p + roff + 8, (uint32_t)stubs[i].size());
        if (entry_size == 28) {
            w_u32_le(p + roff + 12, (uint32_t)atlases[i].w);
            w_u32_le(p + roff + 16, (uint32_t)atlases[i].h);
            w_u32_le(p + roff + 20, 0);
        }
        w_u32_le(p + roff + ptr_off_in_entry, (uint32_t)stub_pos[i]);
    }
    for (size_t i = 0; i < N; i++) {
        memcpy(p + (stub_pos[i] - txtr_start), stubs[i].data(), stubs[i].size());
    }

    w_u32_le(out_buf + txtr_start - 4, (uint32_t)new_txtr_size);

    if (!owns_out && old_tail_len > 0 && delta != 0) {
        memmove(out_buf + txtr_start + new_txtr_size, out_buf + old_tail_start, old_tail_len);
    }

    w_u32_le(out_buf + 4, (uint32_t)(new_total - 8));

    rewind(f);
    if (fwrite(out_buf, 1, new_total, f) != new_total) {
        perror("fwrite");
        if (owns_out)
            free(out_buf);
        return 0;
    }
    fflush(f);
    if (mf) {
        mapped_file_flush(mf);
        mapped_file_close(mf);
    }
    if (portable_truncate(f, (long long)new_total) != 0) {
        perror("ftruncate");
        if (owns_out)
            free(out_buf);
        return 0;
    }
    if (owns_out)
        free(out_buf);

    if (delta != 0) {
        GMSLib::SlideAudoPointers(f, txtr_start + new_txtr_size, (long)delta);
        fflush(f);
    }
    return (long)new_total;
}

int run_repack(const char* data_win, const char* out_dir, const struct block_info* blk, float quality, size_t max_strip,
               unsigned threads, int page_size, int max_dims, int max_area) {
    if (max_dims <= 0)
        max_dims = page_size;
    if (max_area <= 0)
        max_area = page_size * page_size;

    MappedFile mf;
    if (mapped_file_open_rw(data_win, &mf) != 0) {
        perror(data_win);
        return 3;
    }
    long total_size = (long)mf.size;
    uint8_t* win = mf.data;

    FILE* f = nullptr;
    auto cleanup = [&]() {
        mapped_file_flush(&mf);
        mapped_file_close(&mf);
        if (f)
            fclose(f);
    };

    f = fopen(data_win, "r+b");
    if (!f) {
        perror(data_win);
        cleanup();
        return 3;
    }

    std::vector<tpi_t> tpis;
    std::vector<src_page_t> pages;
    size_t entry_size = 16;
    if (parse_tpag(win, (size_t)total_size, tpis) != 0) {
        cleanup();
        return 5;
    }
    if (parse_txtr_pages(win, (size_t)total_size, pages, &entry_size) != 0) {
        cleanup();
        return 5;
    }

    size_t n_external = 0;
    for (const auto& pg : pages)
        if (pg.external)
            n_external++;
    if (n_external > 0) {
        Gmtoolkit::tprint("repack: %zu of %zu TXTR pages are TextureExternal "
                          "(blob lives in sibling .yytex files). Repack would lose "
                          "those references. Use --externalize-textures alone if you "
                          "want to externalize the remaining inline pages.\n",
                          n_external, pages.size());
        cleanup();
        return 5;
    }

    Gmtoolkit::msg("Loaded %zu TPIs from %zu source pages.", tpis.size(), pages.size());
    Gmtoolkit::msg("Filter: max_dims=%d, max_area=%d, page_size=%d", max_dims, max_area, page_size);
    fflush(stdout);

    Gmtoolkit::msg("Bin-packing %zu TPIs into atlases...", tpis.size());
    fflush(stdout);

    std::vector<atlas_t> atlases;
    run_repacker(tpis, pages, page_size, 1, max_dims, max_area, atlases);

    size_t n_eligible = 0, n_solo = 0;
    for (auto& a : atlases)
        (a.tpi_indices.size() > 1 ? n_eligible : n_solo)++;
    Gmtoolkit::msg("Repack plan: %zu new atlases (%zu packed, %zu solo) at <= %dx%d", atlases.size(), n_eligible,
                   n_solo, page_size, page_size);
    Gmtoolkit::msg("Was: %zu source pages", pages.size());
    fflush(stdout);

    page_cache_t cache;
    page_cache_init(cache);

    astcenc_context* astc_ctx = astc_make_context(blk->bx, blk->by, quality, threads);
    if (!astc_ctx) {
        page_cache_free(cache);
        cleanup();
        return 6;
    }

    bool inline_mode = (out_dir == NULL);
    std::vector<std::vector<uint8_t>> inline_blobs;
    if (inline_mode)
        inline_blobs.resize(atlases.size());

    Gmtoolkit::msg("%s %zu texture atlases...", inline_mode ? "Re-encoding" : "Compressing", atlases.size());
    fflush(stdout);
    int last_pct = -1;

    int ret = 0;
    for (size_t ai = 0; ai < atlases.size(); ai++) {
        atlas_t& a = atlases[ai];

        int pct = (int)((ai * 100) / atlases.size());
        if (pct != last_pct) {
            Gmtoolkit::msg("  %3d%%  (atlas %zu/%zu)", pct, ai + 1, atlases.size());
            fflush(stdout);
            last_pct = pct;
        }

        std::sort(a.tpi_indices.begin(), a.tpi_indices.end(),
                  [&](int x, int y) { return tpis[x].orig_page_idx < tpis[y].orig_page_idx; });

        size_t atlas_bytes = (size_t)a.w * (size_t)a.h * 4;
        uint8_t* atlas_rgba = (uint8_t*)calloc(1, atlas_bytes);
        if (!atlas_rgba) {
            Gmtoolkit::err("OOM allocating atlas %zu (%dx%d)", ai, a.w, a.h);
            ret = 6;
            break;
        }

        for (int tpi_idx : a.tpi_indices) {
            const tpi_t& t = tpis[tpi_idx];
            if (t.orig_page_idx < 0 || (size_t)t.orig_page_idx >= pages.size())
                continue;

            uint8_t* src_rgba = NULL;
            int src_w = 0, src_h = 0;
            if (page_cache_get(cache, t.orig_page_idx, win, pages[t.orig_page_idx], &src_rgba, &src_w, &src_h) != 0) {
                Gmtoolkit::err("Atlas %zu: failed source page %d", ai, t.orig_page_idx);
                free(atlas_rgba);
                ret = 7;
                goto pass2_done;
            }
            composite_crop(atlas_rgba, a.w, a.h, t.new_rect.x, t.new_rect.y, src_rgba, src_w, src_h, t.source_x,
                           t.source_y, t.source_w, t.source_h);
        }

        if (inline_mode) {
            TxtrFormat fmt = TxtrFormat::ZOQ;
            if (!a.tpi_indices.empty()) {
                int orig_pg = tpis[a.tpi_indices[0]].orig_page_idx;
                if (orig_pg >= 0 && (size_t)orig_pg < pages.size())
                    fmt = pages[orig_pg].format;
            }
            uint8_t* enc = NULL;
            size_t enc_len = 0;
            if (encode_atlas_in_format(fmt, atlas_rgba, a.w, a.h, &enc, &enc_len) != 0) {
                Gmtoolkit::err("Atlas %zu inline encode failed", ai);
                free(atlas_rgba);
                ret = 8;
                goto pass2_done;
            }
            inline_blobs[ai].assign(enc, enc + enc_len);
            free(enc);
        } else {
            char pvr_path[1024];
            snprintf(pvr_path, sizeof(pvr_path), "%s/%zu.pvr", out_dir, ai);
            if (compress_rgba_to_pvr(atlas_rgba, a.w, a.h, pvr_path, blk, astc_ctx, threads, max_strip) != 0) {
                Gmtoolkit::err("Atlas %zu compress/write failed", ai);
                free(atlas_rgba);
                ret = 8;
                goto pass2_done;
            }
        }
        free(atlas_rgba);
    }
    Gmtoolkit::msg("  100%%  (atlas %zu/%zu)", atlases.size(), atlases.size());
    fflush(stdout);

pass2_done:
    astcenc_context_free(astc_ctx);
    page_cache_free(cache);
    if (ret != 0) {
        cleanup();
        return ret;
    }

    rewrite_tpi_records(win, tpis);

    size_t txtr_start = 0, txtr_size = 0;
    if (find_chunk(win, (size_t)total_size, "TXTR", &txtr_start, &txtr_size) != 0) {
        Gmtoolkit::err("TXTR not found during rewrite");
        cleanup();
        return 9;
    }
    long new_total = rebuild_txtr_and_flush(f, &mf, win, (size_t)total_size, txtr_start, txtr_size, entry_size, atlases,
                                            tpis, pages, inline_mode ? &inline_blobs : nullptr);
    fclose(f);
    f = nullptr;
    if (new_total == 0) {
        Gmtoolkit::err("TXTR rebuild failed");
        return 10;
    }

    Gmtoolkit::msg("Updated %s: %ld -> %ld (%.1f MB saved)", data_win, total_size, new_total,
                   (total_size - new_total) / 1048576.0);
    return 0;
}
