// SPDX-License-Identifier: MIT

#include "astcenc.h"
#include "Toolkit/Codec/Txtr.h"
#include "Toolkit/Platform.h"
#include "Toolkit/Log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <vector>

extern int parse_2zoq(const uint8_t* blob, size_t blob_len, uint8_t** qoif_out, size_t* qoif_len_out);
extern int decode_yyg_qoif(const uint8_t* qoif, size_t qoif_len, uint8_t** out_rgba, int* out_w, int* out_h);

#pragma pack(push, 1)
typedef struct {
    uint32_t version;
    uint32_t flags;
    uint64_t pixel_format;
    uint32_t colour_space;
    uint32_t channel_type;
    uint32_t height;
    uint32_t width;
    uint32_t depth;
    uint32_t num_surfaces;
    uint32_t num_faces;
    uint32_t mip_count;
    uint32_t metadata_size;
} pvr3_header_t;
#pragma pack(pop)

extern const struct block_info BLOCKS[] = {
    { "4x4", 4, 4, 0x1B },
    { "5x5", 5, 5, 0x1D },
    { "6x6", 6, 6, 0x1F },
    { NULL, 0, 0, 0 },
};

// Split a tall atlas into block-aligned horizontal strips so the encoder's peak working set stays bounded.
static int compute_strip_heights(int w, int h, int block_dim, size_t max_px, int** heights_out, int* n_out) {
    int pad_h = ((h + block_dim - 1) / block_dim) * block_dim;
    if (max_px == 0 || (size_t)w * (size_t)h <= max_px) {
        int* r = (int*)malloc(sizeof(int));
        if (!r)
            return -1;
        r[0] = pad_h;
        *heights_out = r;
        *n_out = 1;
        return 0;
    }
    int max_strip = (int)(max_px / (size_t)w);
    max_strip = (max_strip / block_dim) * block_dim;
    if (max_strip <= 0)
        return -2;

    int n = 0, rem = h;
    while (rem > max_strip) {
        n++;
        rem -= max_strip;
    }
    if (rem > 0)
        n++;

    int* r = (int*)malloc((size_t)n * sizeof(int));
    if (!r)
        return -1;
    int i = 0;
    rem = h;
    while (rem > max_strip) {
        r[i++] = max_strip;
        rem -= max_strip;
    }
    if (rem > 0)
        r[i++] = ((rem + block_dim - 1) / block_dim) * block_dim;
    *heights_out = r;
    *n_out = n;
    return 0;
}

struct astc_thread_arg {
    astcenc_context* ctx;
    astcenc_image* image;
    const astcenc_swizzle* swizzle;
    uint8_t* payload;
    size_t payload_len;
    unsigned int thread_id;
    astcenc_error err;
};

static void* astc_thread_fn(void* p) {
    struct astc_thread_arg* a = (struct astc_thread_arg*)p;
    a->err = astcenc_compress_image(a->ctx, a->image, a->swizzle, a->payload, a->payload_len, a->thread_id);
    return NULL;
}

astcenc_context* astc_make_context(int block_x, int block_y, float quality, unsigned threads) {
    astcenc_config config;
    astcenc_error err =
        astcenc_config_init(ASTCENC_PRF_LDR, (unsigned)block_x, (unsigned)block_y, 1, quality, 0, &config);
    if (err != ASTCENC_SUCCESS) {
        Gmtoolkit::err("astcenc_config_init failed: %d", err);
        return NULL;
    }
    astcenc_context* ctx = NULL;
    err = astcenc_context_alloc(&config, threads, &ctx);
    if (err != ASTCENC_SUCCESS) {
        Gmtoolkit::err("astcenc_context_alloc failed: %d", err);
        return NULL;
    }
    return ctx;
}

static int astc_compress_strip(const uint8_t* src_rgba, int full_w, int full_h, int y_start, int strip_h, int block_x,
                               int block_y, astcenc_context* ctx, unsigned threads, uint8_t* payload_out,
                               size_t payload_cap, size_t* payload_written) {
    size_t stride = (size_t)full_w * 4;
    uint8_t* strip_buf = (uint8_t*)malloc((size_t)strip_h * stride);
    if (!strip_buf)
        return -1;
    // Repeat the last row to fill the block-aligned padding rather than leaving uninitialised pixels.
    for (int y = 0; y < strip_h; y++) {
        int src_y = y_start + y;
        if (src_y >= full_h)
            src_y = full_h - 1;
        memcpy(strip_buf + (size_t)y * stride, src_rgba + (size_t)src_y * stride, stride);
    }

    void* slice_ptrs[1] = { strip_buf };
    astcenc_image image;
    image.dim_x = (unsigned)full_w;
    image.dim_y = (unsigned)strip_h;
    image.dim_z = 1;
    image.data_type = ASTCENC_TYPE_U8;
    image.data = slice_ptrs;

    astcenc_swizzle sw = { ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };

    int bx = (full_w + block_x - 1) / block_x;
    int by = (strip_h + block_y - 1) / block_y;
    size_t needed = (size_t)bx * (size_t)by * 16;
    if (needed > payload_cap) {
        free(strip_buf);
        return -4;
    }

    // astcenc requires reset between images even when the same context is reused.
    astcenc_compress_reset(ctx);

    int ret = 0;
    astcenc_error err;
    if (threads <= 1) {
        err = astcenc_compress_image(ctx, &image, &sw, payload_out, needed, 0);
        if (err != ASTCENC_SUCCESS)
            ret = -5;
    } else {
        std::vector<astc_thread_arg> args(threads);
        std::vector<std::thread> tids;
        tids.reserve(threads);
        for (unsigned i = 0; i < threads; i++) {
            args[i].ctx = ctx;
            args[i].image = &image;
            args[i].swizzle = &sw;
            args[i].payload = payload_out;
            args[i].payload_len = needed;
            args[i].thread_id = i;
            args[i].err = ASTCENC_SUCCESS;
            try {
                tids.emplace_back(astc_thread_fn, &args[i]);
            } catch (...) {
                ret = -6;
                break;
            }
        }
        for (auto& t : tids)
            t.join();
        if (ret == 0) {
            for (auto& a : args) {
                if (a.err != ASTCENC_SUCCESS) {
                    ret = -5;
                    break;
                }
            }
        }
    }

    free(strip_buf);
    if (ret == 0 && payload_written)
        *payload_written = needed;
    return ret;
}

static int write_pvr3(const char* path, const uint8_t* payload, size_t payload_len, int width, int height,
                      uint64_t pvr_code) {
    pvr3_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = 0x03525650;
    h.pixel_format = pvr_code;
    h.height = (uint32_t)height;
    h.width = (uint32_t)width;
    h.depth = 1;
    h.num_surfaces = 1;
    h.num_faces = 1;
    h.mip_count = 1;

    FILE* f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return -1;
    }
    if (fwrite(&h, 1, sizeof(h), f) != sizeof(h)) {
        perror("hdr");
        fclose(f);
        return -1;
    }
    if (fwrite(payload, 1, payload_len, f) != payload_len) {
        perror("payload");
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0) {
        perror("fclose");
        return -1;
    }
    return 0;
}

int compress_rgba_to_pvr(const uint8_t* rgba, int w, int h, const char* out_path, const struct block_info* blk,
                         astcenc_context* ctx, unsigned threads, size_t max_strip) {
    int* strip_h_arr = NULL;
    int n_strips = 0;
    int r = compute_strip_heights(w, h, blk->by, max_strip, &strip_h_arr, &n_strips);
    if (r != 0) {
        Gmtoolkit::err("compute_strip_heights failed: %d", r);
        return -1;
    }

    int blocks_x = (w + blk->bx - 1) / blk->bx;
    size_t total_payload = 0;
    for (int i = 0; i < n_strips; i++) {
        int blocks_y = (strip_h_arr[i] + blk->by - 1) / blk->by;
        total_payload += (size_t)blocks_x * (size_t)blocks_y * 16;
    }
    uint8_t* payload = (uint8_t*)malloc(total_payload);
    if (!payload) {
        Gmtoolkit::err("malloc payload %zu failed", total_payload);
        free(strip_h_arr);
        return -1;
    }

    size_t pos = 0;
    int y_start = 0;
    for (int i = 0; i < n_strips; i++) {
        int sh = strip_h_arr[i];
        size_t written = 0;
        r = astc_compress_strip(rgba, w, h, y_start, sh, blk->bx, blk->by, ctx, threads, payload + pos,
                                total_payload - pos, &written);
        if (r != 0) {
            Gmtoolkit::err("astc_compress_strip %d/%d failed: %d", i + 1, n_strips, r);
            free(payload);
            free(strip_h_arr);
            return -1;
        }
        pos += written;
        y_start += sh;
    }
    free(strip_h_arr);

    r = write_pvr3(out_path, payload, pos, w, h, blk->pvr_code);
    free(payload);
    return r;
}

int compress_blob_to_pvr(const uint8_t* blob, size_t blob_len, const char* out_path, const struct block_info* blk,
                         astcenc_context* ctx, unsigned threads, size_t max_strip) {
    uint8_t* qoif = NULL;
    size_t qoif_len = 0;
    int r = parse_2zoq(blob, blob_len, &qoif, &qoif_len);
    if (r != 0) {
        Gmtoolkit::err("parse_2zoq failed: %d", r);
        return -1;
    }

    uint8_t* rgba = NULL;
    int w = 0, h = 0;
    r = decode_yyg_qoif(qoif, qoif_len, &rgba, &w, &h);
    free(qoif);
    if (r != 0) {
        Gmtoolkit::err("decode_yyg_qoif failed: %d", r);
        return -1;
    }

    r = compress_rgba_to_pvr(rgba, w, h, out_path, blk, ctx, threads, max_strip);
    free(rgba);
    return r;
}
