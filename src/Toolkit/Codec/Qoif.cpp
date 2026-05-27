// SPDX-License-Identifier: MIT

#include "Toolkit/IO.h"
#include "Toolkit/Log.h"
#include "Toolkit/Platform.h"

#include <bzlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int sx(unsigned v, int bits) {
    int mask = (1 << bits) - 1;
    int x = (int)(v & (unsigned)mask);
    if (x & (1 << (bits - 1)))
        x -= (1 << bits);
    return x;
}

static inline unsigned yyg_hash(uint32_t p) {
    return (p ^ (p >> 8) ^ (p >> 16) ^ (p >> 24)) & 0x3F;
}

// YYG's variant of QOI: 'fioq' magic, RGBA with delta/index/run/literal ops similar to upstream QOI.
int decode_yyg_qoif(const uint8_t* qoif, size_t qoif_len, uint8_t** out_rgba, int* out_w, int* out_h) {
    if (qoif_len < 12)
        return -1;
    if (memcmp(qoif, "fioq", 4) != 0)
        return -2;

    int w = qoif[4] | (qoif[5] << 8);
    int h = qoif[6] | (qoif[7] << 8);
    if (w <= 0 || h <= 0)
        return -3;

    size_t total_px = (size_t)w * (size_t)h;
    uint8_t* rgba = (uint8_t*)malloc(total_px * 4);
    if (!rgba)
        return -4;

    const uint8_t* p = qoif + 12;
    const uint8_t* end = qoif + qoif_len;

    uint8_t pixel[4] = { 0, 0, 0, 0xFF };
    uint8_t index[64][4];
    memset(index, 0, sizeof(index));

    size_t out_pos = 0;
    size_t n_px = 0;

#define NEED(n)              \
    do {                     \
        if (end - p < (n)) { \
            free(rgba);      \
            return -5;       \
        }                    \
    } while (0)

    while (n_px < total_px && p < end) {
        uint8_t b1 = *p++;
        int run = 0;
        int update_cache = 1;

        if (b1 < 0x40) {
            memcpy(pixel, index[b1], 4);
            update_cache = 0;
        } else if (b1 < 0x60) {
            run = b1 & 0x1F;
            update_cache = 0;
        } else if (b1 < 0x80) {
            NEED(1);
            uint8_t b2 = *p++;
            run = (((b1 & 0x1F) << 8) | b2) + 32;
            update_cache = 0;
        } else if (b1 < 0xC0) {
            pixel[0] = (uint8_t)(pixel[0] + sx((b1 >> 4) & 0x3, 2));
            pixel[1] = (uint8_t)(pixel[1] + sx((b1 >> 2) & 0x3, 2));
            pixel[2] = (uint8_t)(pixel[2] + sx(b1 & 0x3, 2));
        } else if (b1 < 0xE0) {
            NEED(1);
            uint8_t b2 = *p++;
            pixel[0] = (uint8_t)(pixel[0] + sx(b1 & 0x1F, 5));
            pixel[1] = (uint8_t)(pixel[1] + sx((b2 >> 4) & 0xF, 4));
            pixel[2] = (uint8_t)(pixel[2] + sx(b2 & 0xF, 4));
        } else if (b1 < 0xF0) {
            NEED(2);
            uint8_t b2 = *p++;
            uint8_t b3 = *p++;
            int dR = sx(((b1 & 0xF) << 1) | ((b2 >> 7) & 1), 5);
            int dGs = sx(b2 & 0x7F, 7);
            int dG = dGs >> 2;
            int dBs = sx(((b2 & 0x3) << 8) | b3, 10);
            int dB = dBs >> 5;
            int dA = sx(b3 & 0x1F, 5);
            pixel[0] = (uint8_t)(pixel[0] + dR);
            pixel[1] = (uint8_t)(pixel[1] + dG);
            pixel[2] = (uint8_t)(pixel[2] + dB);
            pixel[3] = (uint8_t)(pixel[3] + dA);
        } else {
            int mask = b1 & 0x0F;
            if (mask & 0x8) {
                NEED(1);
                pixel[0] = *p++;
            }
            if (mask & 0x4) {
                NEED(1);
                pixel[1] = *p++;
            }
            if (mask & 0x2) {
                NEED(1);
                pixel[2] = *p++;
            }
            if (mask & 0x1) {
                NEED(1);
                pixel[3] = *p++;
            }
        }

        if (update_cache) {
            uint32_t pix32 = Gmtoolkit::r_u32(pixel);
            memcpy(index[yyg_hash(pix32)], pixel, 4);
        }

        memcpy(rgba + out_pos, pixel, 4);
        out_pos += 4;
        n_px++;

        for (int i = 0; i < run && n_px < total_px; i++) {
            memcpy(rgba + out_pos, pixel, 4);
            out_pos += 4;
            n_px++;
        }
    }

#undef NEED

    if (n_px != total_px) {
        Gmtoolkit::warn("decoded %zu/%zu pixels", n_px, total_px);
    }

    *out_rgba = rgba;
    *out_w = w;
    *out_h = h;
    return 0;
}

static size_t detect_2zoq_bz_offset(const uint8_t* blob, size_t blob_len) {
    auto is_bz_magic = [](const uint8_t* p) { return p[0] == 'B' && p[1] == 'Z' && p[2] == 'h'; };
    if (blob_len >= 8 + 3 && is_bz_magic(blob + 8))
        return 8;
    if (blob_len >= 12 + 3 && is_bz_magic(blob + 12))
        return 12;
    return 0;
}

int parse_2zoq(const uint8_t* blob, size_t blob_len, uint8_t** qoif_out, size_t* qoif_len_out) {
    if (blob_len < 12)
        return -1;
    if (memcmp(blob, "2zoq", 4) != 0)
        return -2;

    size_t bz_off = detect_2zoq_bz_offset(blob, blob_len);
    if (bz_off == 0)
        return -2;

    size_t decomp_cap = (bz_off == 12) ? (size_t)Gmtoolkit::r_u32(blob + 8) : (1u << 20);
    uint8_t* qoif = (uint8_t*)malloc(decomp_cap);
    if (!qoif)
        return -3;

    const uint8_t* in = blob + bz_off;
    size_t in_left = blob_len - bz_off;
    size_t produced = 0;

    // Some atlases are stored as a chain of BZ2 streams concatenated back-to-back; decode each in turn.
    while (in_left > 0) {
        bz_stream s;
        memset(&s, 0, sizeof(s));
        if (BZ2_bzDecompressInit(&s, 0, 0) != BZ_OK) {
            free(qoif);
            return -4;
        }
        s.next_in = (char*)in;
        s.avail_in = (unsigned int)in_left;
        s.next_out = (char*)(qoif + produced);
        s.avail_out = (unsigned int)(decomp_cap - produced);

        int r;
        for (;;) {
            r = BZ2_bzDecompress(&s);
            if (r != BZ_OK || s.avail_out > 0)
                break;
            size_t new_cap = decomp_cap * 2;
            uint8_t* grown = (uint8_t*)realloc(qoif, new_cap);
            if (!grown) {
                BZ2_bzDecompressEnd(&s);
                free(qoif);
                return -3;
            }
            qoif = grown;
            size_t produced_now = (decomp_cap - produced) - s.avail_out;
            produced += produced_now;
            s.next_out = (char*)(qoif + produced);
            s.avail_out = (unsigned int)(new_cap - produced);
            decomp_cap = new_cap;
        }

        size_t consumed = in_left - s.avail_in;
        size_t produced_now = (decomp_cap - produced) - s.avail_out;
        BZ2_bzDecompressEnd(&s);

        produced += produced_now;
        in += consumed;
        in_left -= consumed;

        if (r == BZ_STREAM_END) {

            if (in_left == 0 || !(in[0] == 'B' && in[1] == 'Z' && in[2] == 'h'))
                break;
            continue;
        }
        if (r != BZ_OK || produced_now == 0) {
            free(qoif);
            return -6;
        }
    }

    *qoif_out = qoif;
    *qoif_len_out = produced;
    return 0;
}

int encode_yyg_qoif(const uint8_t* rgba, int w, int h, uint8_t** out, size_t* out_len) {
    if (w <= 0 || h <= 0 || w > 0xFFFF || h > 0xFFFF)
        return -1;
    size_t total_px = (size_t)w * (size_t)h;
    size_t cap = 12 + total_px * 5 + 16;
    uint8_t* buf = (uint8_t*)malloc(cap);
    if (!buf)
        return -2;
    size_t pos = 0;

    memcpy(buf + pos, "fioq", 4);
    pos += 4;
    buf[pos++] = (uint8_t)(w & 0xFF);
    buf[pos++] = (uint8_t)((w >> 8) & 0xFF);
    buf[pos++] = (uint8_t)(h & 0xFF);
    buf[pos++] = (uint8_t)((h >> 8) & 0xFF);
    size_t payload_len_off = pos;
    buf[pos++] = 0;
    buf[pos++] = 0;
    buf[pos++] = 0;
    buf[pos++] = 0;

    uint8_t pr = 0, pg = 0, pb = 0, pa = 0xFF;
    uint8_t cache[64][4];
    memset(cache, 0, sizeof(cache));

    uint32_t run = 0;

    auto flush_run = [&](void) {
        while (run > 0) {
            if (run <= 32) {
                buf[pos++] = (uint8_t)(0x40 | (run - 1));
                run = 0;
            } else {
                uint32_t r = run - 33;
                if (r > 0x1FFF)
                    r = 0x1FFF;
                buf[pos++] = (uint8_t)(0x60 | ((r >> 8) & 0x1F));
                buf[pos++] = (uint8_t)(r & 0xFF);
                run -= (r + 33);
            }
        }
    };

    for (size_t i = 0; i < total_px; i++) {
        uint8_t r = rgba[i * 4 + 0];
        uint8_t g = rgba[i * 4 + 1];
        uint8_t b = rgba[i * 4 + 2];
        uint8_t a = rgba[i * 4 + 3];

        if (r == pr && g == pg && b == pb && a == pa) {
            run++;
            if (run == 32 + 0x1FFF + 1)
                flush_run();
            continue;
        }
        flush_run();

        uint32_t px32 = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
        unsigned h6 = yyg_hash(px32);
        if (cache[h6][0] == r && cache[h6][1] == g && cache[h6][2] == b && cache[h6][3] == a) {
            buf[pos++] = (uint8_t)h6;
            pr = r;
            pg = g;
            pb = b;
            pa = a;
            continue;
        }

        int dR = (int)r - (int)pr;
        int dG = (int)g - (int)pg;
        int dB = (int)b - (int)pb;
        int dA = (int)a - (int)pa;

        bool wrote = false;

        if (dA == 0 && dR >= -2 && dR <= 1 && dG >= -2 && dG <= 1 && dB >= -2 && dB <= 1) {
            uint8_t op = (uint8_t)(0x80 | ((dR & 0x3) << 4) | ((dG & 0x3) << 2) | (dB & 0x3));
            buf[pos++] = op;
            wrote = true;
        } else if (dA == 0 && dR >= -16 && dR <= 15 && dG >= -8 && dG <= 7 && dB >= -8 && dB <= 7) {
            buf[pos++] = (uint8_t)(0xC0 | (dR & 0x1F));
            buf[pos++] = (uint8_t)(((dG & 0xF) << 4) | (dB & 0xF));
            wrote = true;
        } else if (dR >= -16 && dR <= 15 && dG >= -16 && dG <= 15 && dB >= -16 && dB <= 15 && dA >= -16 && dA <= 15) {
            int dA_bits = dA & 0x1F;
            int dBs = ((dB << 5) | dA_bits) & 0x3FF;
            int dBs_top2 = (dBs >> 8) & 0x3;
            int dGs = (((dG << 2) | dBs_top2)) & 0x7F;
            int dR_bits = dR & 0x1F;

            uint8_t b1 = (uint8_t)(0xE0 | ((dR_bits >> 1) & 0xF));
            uint8_t b2 = (uint8_t)(((dR_bits & 0x1) << 7) | (dGs & 0x7F));
            uint8_t b3 = (uint8_t)(dBs & 0xFF);
            buf[pos++] = b1;
            buf[pos++] = b2;
            buf[pos++] = b3;
            wrote = true;
        }

        if (!wrote) {
            uint8_t mask = 0;
            uint8_t bytes[4];
            int n = 0;
            if (r != pr) {
                mask |= 0x8;
                bytes[n++] = r;
            }
            if (g != pg) {
                mask |= 0x4;
                bytes[n++] = g;
            }
            if (b != pb) {
                mask |= 0x2;
                bytes[n++] = b;
            }
            if (a != pa) {
                mask |= 0x1;
                bytes[n++] = a;
            }
            buf[pos++] = (uint8_t)(0xF0 | mask);
            for (int k = 0; k < n; k++)
                buf[pos++] = bytes[k];
        }

        cache[h6][0] = r;
        cache[h6][1] = g;
        cache[h6][2] = b;
        cache[h6][3] = a;
        pr = r;
        pg = g;
        pb = b;
        pa = a;
    }
    flush_run();

    uint32_t payload_len = (uint32_t)(pos - 12);
    buf[payload_len_off + 0] = (uint8_t)(payload_len & 0xFF);
    buf[payload_len_off + 1] = (uint8_t)((payload_len >> 8) & 0xFF);
    buf[payload_len_off + 2] = (uint8_t)((payload_len >> 16) & 0xFF);
    buf[payload_len_off + 3] = (uint8_t)((payload_len >> 24) & 0xFF);

    *out = buf;
    *out_len = pos;
    return 0;
}

int wrap_2zoq(const uint8_t* qoif, size_t qoif_len, int w, int h, uint8_t** out, size_t* out_len) {
    if (qoif_len > 0xFFFFFFFFu)
        return -1;
    unsigned int bz_cap = (unsigned int)(qoif_len + (qoif_len / 100) + 600);
    uint8_t* bz_buf = (uint8_t*)malloc(bz_cap);
    if (!bz_buf)
        return -2;
    int r = BZ2_bzBuffToBuffCompress((char*)bz_buf, &bz_cap, (char*)qoif, (unsigned int)qoif_len, 9, 0, 30);
    if (r != BZ_OK) {
        free(bz_buf);
        return -3;
    }

    size_t total = 12 + bz_cap;
    uint8_t* blob = (uint8_t*)malloc(total);
    if (!blob) {
        free(bz_buf);
        return -4;
    }
    memcpy(blob, "2zoq", 4);
    blob[4] = (uint8_t)(w & 0xFF);
    blob[5] = (uint8_t)((w >> 8) & 0xFF);
    blob[6] = (uint8_t)(h & 0xFF);
    blob[7] = (uint8_t)((h >> 8) & 0xFF);
    blob[8] = (uint8_t)(qoif_len & 0xFF);
    blob[9] = (uint8_t)((qoif_len >> 8) & 0xFF);
    blob[10] = (uint8_t)((qoif_len >> 16) & 0xFF);
    blob[11] = (uint8_t)((qoif_len >> 24) & 0xFF);
    memcpy(blob + 12, bz_buf, bz_cap);
    free(bz_buf);

    *out = blob;
    *out_len = total;
    return 0;
}

// Like decode, but skip over the actual pixel writes -- only need to know where the stream ends.
size_t scan_yyg_qoif_blob_len(const uint8_t* qoif, size_t qoif_len) {
    if (qoif_len < 12)
        return 0;
    if (memcmp(qoif, "fioq", 4) != 0)
        return 0;

    int w = qoif[4] | (qoif[5] << 8);
    int h = qoif[6] | (qoif[7] << 8);
    if (w <= 0 || h <= 0)
        return 0;
    size_t total_px = (size_t)w * (size_t)h;

    const uint8_t* p = qoif + 12;
    const uint8_t* end = qoif + qoif_len;
    size_t n_px = 0;

#define NEED_(n)           \
    do {                   \
        if (end - p < (n)) \
            return 0;      \
    } while (0)

    while (n_px < total_px && p < end) {
        uint8_t b1 = *p++;
        int run = 0;

        if (b1 < 0x40) {

        } else if (b1 < 0x60) {
            run = b1 & 0x1F;
        } else if (b1 < 0x80) {
            NEED_(1);
            p++;
            run = (((b1 & 0x1F) << 8) | p[-1]) + 32;
        } else if (b1 < 0xC0) {

        } else if (b1 < 0xE0) {
            NEED_(1);
            p++;
        } else if (b1 < 0xF0) {
            NEED_(2);
            p += 2;
        } else {
            int mask = b1 & 0x0F;
            if (mask & 0x8) {
                NEED_(1);
                p++;
            }
            if (mask & 0x4) {
                NEED_(1);
                p++;
            }
            if (mask & 0x2) {
                NEED_(1);
                p++;
            }
            if (mask & 0x1) {
                NEED_(1);
                p++;
            }
        }
        n_px++;
        n_px += (size_t)run;
        if (n_px > total_px)
            n_px = total_px;
    }

#undef NEED_

    if (n_px != total_px)
        return 0;
    return (size_t)(p - qoif);
}

// 2x1 placeholder with the index baked into the pixel data so the entry remains a valid 'fioq' blob.
int build_fioq_stub(unsigned int idx, uint8_t** out, size_t* out_len) {
    const size_t total = 30;
    const uint32_t payload_len = (uint32_t)(total - 12);
    uint8_t* buf = (uint8_t*)malloc(total);
    if (!buf)
        return -1;
    size_t q = 0;
    memcpy(buf + q, "fioq", 4);
    q += 4;
    buf[q++] = 2;
    buf[q++] = 0;
    buf[q++] = 1;
    buf[q++] = 0;
    buf[q++] = (uint8_t)(payload_len & 0xFF);
    buf[q++] = (uint8_t)((payload_len >> 8) & 0xFF);
    buf[q++] = (uint8_t)((payload_len >> 16) & 0xFF);
    buf[q++] = (uint8_t)((payload_len >> 24) & 0xFF);
    buf[q++] = 0xFF;
    buf[q++] = 0xDE;
    buf[q++] = 0xAD;
    buf[q++] = 0xBE;
    buf[q++] = 0xFF;
    buf[q++] = 0xFF;
    buf[q++] = (uint8_t)(idx & 0xFF);
    buf[q++] = (uint8_t)((idx >> 8) & 0xFF);
    buf[q++] = (uint8_t)((idx >> 16) & 0xFF);
    buf[q++] = 0xFF;
    memset(buf + q, 0, 7);
    q += 7;
    buf[q++] = 0x01;
    *out = buf;
    *out_len = q;
    return 0;
}

size_t scan_2zoq_blob_len(const uint8_t* src, size_t src_left) {
    if (src_left < 12 || memcmp(src, "2zoq", 4) != 0)
        return 0;

    size_t bz_off = detect_2zoq_bz_offset(src, src_left);
    if (bz_off == 0)
        return 0;

    const uint8_t* in = src + bz_off;
    size_t in_left = src_left - bz_off;
    uint8_t scratch[16384];

    while (in_left > 0) {
        if (!(in[0] == 'B' && in[1] == 'Z' && in[2] == 'h'))
            break;
        bz_stream s;
        memset(&s, 0, sizeof(s));
        if (BZ2_bzDecompressInit(&s, 0, 0) != BZ_OK)
            return 0;
        s.next_in = (char*)in;
        s.avail_in = (unsigned int)in_left;
        s.next_out = (char*)scratch;
        s.avail_out = sizeof(scratch);

        int r;
        for (;;) {
            r = BZ2_bzDecompress(&s);
            if (r == BZ_STREAM_END || r != BZ_OK)
                break;
            s.next_out = (char*)scratch;
            s.avail_out = sizeof(scratch);
        }
        size_t consumed = in_left - s.avail_in;
        BZ2_bzDecompressEnd(&s);

        in += consumed;
        in_left -= consumed;

        if (r != BZ_STREAM_END)
            return 0;
    }
    return (size_t)(in - src);
}

int build_2zoq_stub(unsigned int idx, uint8_t** out, size_t* out_len) {
    uint8_t qoif[64];
    size_t q = 0;
    memcpy(qoif + q, "fioq", 4);
    q += 4;
    qoif[q++] = 2;
    qoif[q++] = 0;
    qoif[q++] = 1;
    qoif[q++] = 0;
    qoif[q++] = 0x04;
    qoif[q++] = 0;
    qoif[q++] = 0;
    qoif[q++] = 0;
    qoif[q++] = 0xFF;
    qoif[q++] = 0xDE;
    qoif[q++] = 0xAD;
    qoif[q++] = 0xBE;
    qoif[q++] = 0xFF;
    qoif[q++] = 0xFF;
    qoif[q++] = (uint8_t)(idx & 0xFF);
    qoif[q++] = (uint8_t)((idx >> 8) & 0xFF);
    qoif[q++] = (uint8_t)((idx >> 16) & 0xFF);
    qoif[q++] = 0xFF;
    memset(qoif + q, 0, 7);
    q += 7;
    qoif[q++] = 0x01;

    unsigned int bz_cap = (unsigned int)(q + (q / 100) + 600);
    uint8_t* bz_buf = (uint8_t*)malloc(bz_cap);
    if (!bz_buf)
        return -1;
    int r = BZ2_bzBuffToBuffCompress((char*)bz_buf, &bz_cap, (char*)qoif, (unsigned int)q, 9, 0, 30);
    if (r != BZ_OK) {
        free(bz_buf);
        return -2;
    }

    size_t total = 12 + bz_cap;
    uint8_t* stub = (uint8_t*)malloc(total);
    if (!stub) {
        free(bz_buf);
        return -1;
    }
    memcpy(stub, "2zoq", 4);
    stub[4] = 2;
    stub[5] = 0;
    stub[6] = 1;
    stub[7] = 0;
    stub[8] = (uint8_t)(q & 0xFF);
    stub[9] = (uint8_t)((q >> 8) & 0xFF);
    stub[10] = (uint8_t)((q >> 16) & 0xFF);
    stub[11] = (uint8_t)((q >> 24) & 0xFF);
    memcpy(stub + 12, bz_buf, bz_cap);
    free(bz_buf);

    *out = stub;
    *out_len = total;
    return 0;
}
