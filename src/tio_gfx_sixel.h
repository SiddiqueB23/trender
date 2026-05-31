#ifndef TIO_GFX_SIXEL_H
#define TIO_GFX_SIXEL_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Allocator overrides ─────────────────────────────────────────────────── */
#ifndef TIO_GFX_MALLOC
#define TIO_GFX_MALLOC malloc
#endif
#ifndef TIO_GFX_FREE
#define TIO_GFX_FREE free
#endif

/* ── Shared pixel formats (guarded so multiple tio_gfx_* headers coexist) ── */
#ifndef TIO_GFX_PIXEL_FMT_DEFINED
#define TIO_GFX_PIXEL_FMT_DEFINED
typedef enum {
    TIO_GFX_FMT_RGBA8  = 0,
    TIO_GFX_FMT_RGB565 = 1,
    TIO_GFX_FMT_BGRA8  = 2,
} tio_gfx_pixel_fmt;
#endif

/* ── Generate parts bitmask (guarded) ───────────────────────────────────── */
#ifndef TIO_GFX_PARTS_DEFINED
#define TIO_GFX_PARTS_DEFINED
#define TIO_GFX_HEADER   (1 << 0)
#define TIO_GFX_PAYLOAD  (1 << 1)
#define TIO_GFX_FOOTER   (1 << 2)
#define TIO_GFX_FULL     (TIO_GFX_HEADER | TIO_GFX_PAYLOAD | TIO_GFX_FOOTER)
#endif

/* ── Sixel-specific types ────────────────────────────────────────────────── */
typedef enum {
    TIO_GFX_SIXEL_PALETTE_216 = 0,  /* 6x6x6 RGB cube, 16x16 Bayer dithering */
} tio_gfx_sixel_palette_mode;

typedef struct {
    int width, height;
    int scale_x, scale_y;
    tio_gfx_sixel_palette_mode palette;
    /* Pixel offset of this ctx within the full image.
     * Used to align the 16x16 Bayer dither tile correctly when the ctx
     * covers a strip rather than the full frame. Default 0,0. */
    int dither_offset_x, dither_offset_y;
} tio_gfx_sixel_params;

#define TIO_GFX_SIXEL_DEFAULT_PARAMS(w, h) \
    ((tio_gfx_sixel_params){ .width=(w), .height=(h), \
                              .scale_x=1, .scale_y=1, \
                              .palette=TIO_GFX_SIXEL_PALETTE_216, \
                              .dither_offset_x=0, .dither_offset_y=0 })

typedef struct {
    /* public — read after generate */
    char*   data;
    size_t  data_size;
    size_t  data_cap;
    int     width, height;

    /* stats */
    double  total_generate_ms;

    /* private */
    tio_gfx_sixel_params  _params;
    unsigned char*         _index_data;         /* width*6 bytes, one band scratch */
    int*                   _scratch_painted;    /* width ints                     */
    uint64_t*              _scratch_transposed; /* width uint64s                  */
    int                    _owns_scratch;       /* 1 = this ctx frees on destroy  */
} tio_gfx_sixel_ctx;

/* ── Declarations ────────────────────────────────────────────────────────── */
void tio_gfx_sixel_init(tio_gfx_sixel_ctx* ctx, tio_gfx_sixel_params params);
void tio_gfx_sixel_destroy(tio_gfx_sixel_ctx* ctx);
void tio_gfx_sixel_init_shared(tio_gfx_sixel_ctx* ctxs, int n,
                                tio_gfx_sixel_params params);
void tio_gfx_sixel_destroy_shared(tio_gfx_sixel_ctx* ctxs, int n);
void tio_gfx_sixel_set_params(tio_gfx_sixel_ctx* ctx,
                               tio_gfx_sixel_params params);
void tio_gfx_sixel_generate(tio_gfx_sixel_ctx* ctx,
                             const void* pixels,
                             tio_gfx_pixel_fmt fmt,
                             int parts);
void tio_gfx_sixel_reset_stats(tio_gfx_sixel_ctx* ctx);
void tio_gfx_sixel_print_stats(const tio_gfx_sixel_ctx* ctx);

/* ═══════════════════════════════════════════════════════════════════════════
   IMPLEMENTATION — define TIO_GFX_SIXEL_IMPLEMENTATION in exactly one TU
   ═══════════════════════════════════════════════════════════════════════════ */
#ifdef TIO_GFX_SIXEL_IMPLEMENTATION

#include "bayer_patterns.h"
#include "timer.h"
#ifdef TIO_GFX_USE_AVX2
#include <immintrin.h>
#endif

/* ── clamp helper ────────────────────────────────────────────────────────── */
static inline int tio_gfx__clamp_int(int x, int xmin, int xmax) {
    return x < xmin ? xmin : x > xmax ? xmax : x;
}

/* ── Integer-to-ASCII ────────────────────────────────────────────────────── */
static char* tio_gfx__fast_itoa(char* buffer, int value) {
    if (value == 0) { *buffer++ = '0'; return buffer; }
    char temp[12]; char* p = temp;
    int is_negative = (value < 0);
    if (is_negative) value = -value;
    while (value > 0) { *p++ = (char)('0' + value % 10); value /= 10; }
    if (is_negative) *p++ = '-';
    while (p > temp) *buffer++ = *--p;
    return buffer;
}

static char* tio_gfx__fast_itoa_lt1000(char* buffer, int num) {
    const int hundreds = (num / 100);
    *buffer = (char)(0x30 + hundreds);
    buffer += (hundreds != 0);
    num -= hundreds * 100;
    const int tens = (num / 10);
    *buffer = (char)(0x30 + tens);
    buffer += (hundreds + tens != 0);
    num -= tens * 10;
    *buffer++ = (char)(0x30 + num);
    return buffer;
}

static char* tio_gfx__fast_itoa_lt10000(char* buffer, int num) {
    const int thousands = (num / 1000);
    *buffer = (char)(0x30 + thousands);
    buffer += (thousands != 0);
    num -= thousands * 1000;
    const int hundreds = (num / 100);
    *buffer = (char)(0x30 + hundreds);
    buffer += (thousands != 0 || hundreds != 0);
    num -= hundreds * 100;
    const int tens = (num / 10);
    *buffer = (char)(0x30 + tens);
    buffer += (thousands != 0 || hundreds != 0 || tens != 0);
    num -= tens * 10;
    *buffer++ = (char)(0x30 + num);
    return buffer;
}

/* ── RGB565 → RGB888 ─────────────────────────────────────────────────────── */
static void tio_gfx__rgb565_to_rgb888(uint16_t src,
                                       unsigned char* r,
                                       unsigned char* g,
                                       unsigned char* b) {
    *r = (unsigned char)(((src >> 11) & 0x1F) * 255 / 31);
    *g = (unsigned char)(((src >>  5) & 0x3F) * 255 / 63);
    *b = (unsigned char)((src         & 0x1F) * 255 / 31);
}

/* ── Sixel escape emitters ───────────────────────────────────────────────── */
static inline char* tio_gfx__sx_header(char* buffer) {
    memcpy(buffer, "\x1bP9;1;;q", 8);
    return buffer + 8;
}
static inline char* tio_gfx__sx_terminator(char* buffer) {
    buffer[0] = '\x1b'; buffer[1] = '\\';
    return buffer + 2;
}
static inline char* tio_gfx__sx_modify_color(char* buffer,
                                               int color_number,
                                               int r, int g, int b) {
    *buffer++ = '#';
    buffer = tio_gfx__fast_itoa(buffer, color_number);
    *buffer++ = ';'; *buffer++ = '2'; *buffer++ = ';';
    buffer = tio_gfx__fast_itoa(buffer, r); *buffer++ = ';';
    buffer = tio_gfx__fast_itoa(buffer, g); *buffer++ = ';';
    buffer = tio_gfx__fast_itoa(buffer, b);
    return buffer;
}
static inline char* tio_gfx__sx_set_color(char* buffer, int color_number) {
    *buffer++ = '#';
    return tio_gfx__fast_itoa_lt1000(buffer, color_number);
}
static inline char* tio_gfx__sx_char_repeated(char* buffer, int mask, int count) {
    char character = (char)(0x3F + mask);
    if (count > 3) {
        *buffer++ = '!';
        buffer = tio_gfx__fast_itoa_lt10000(buffer, count);
        *buffer++ = character;
        return buffer;
    }
    *(buffer + 0) = character;
    *(buffer + 1) = character;
    *(buffer + 2) = character;
    return buffer + count;
}
static inline char* tio_gfx__sx_newline(char* buffer) { *buffer++ = '-'; return buffer; }
static inline char* tio_gfx__sx_cr(char* buffer)      { *buffer++ = '$'; return buffer; }

/* ── Capacity ────────────────────────────────────────────────────────────── */
/* 216-color palette needs up to 216*18 = ~3888 bytes; use 4096 as safe reserve */
static size_t tio_gfx__sixel_buf_cap(int w, int h, int scale_y) {
    return (size_t)(w * h * 5 * scale_y) + 4096;
}

/* ── Scratch alloc/free ──────────────────────────────────────────────────── */
static void tio_gfx__sixel_alloc_scratch(tio_gfx_sixel_ctx* ctx,
                                          const tio_gfx_sixel_params* p) {
    ctx->_index_data         = (unsigned char*)TIO_GFX_MALLOC(
                                    (size_t)(p->width * 6)); /* one band max */
    ctx->_scratch_painted    = (int*)TIO_GFX_MALLOC(
                                    (size_t)p->width * sizeof(int));
    ctx->_scratch_transposed = (uint64_t*)TIO_GFX_MALLOC(
                                    (size_t)p->width * sizeof(uint64_t));
}

static void tio_gfx__sixel_free_scratch(tio_gfx_sixel_ctx* ctx) {
    TIO_GFX_FREE(ctx->_index_data);
    TIO_GFX_FREE(ctx->_scratch_painted);
    TIO_GFX_FREE(ctx->_scratch_transposed);
    ctx->_index_data         = NULL;
    ctx->_scratch_painted    = NULL;
    ctx->_scratch_transposed = NULL;
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */
void tio_gfx_sixel_init(tio_gfx_sixel_ctx* ctx, tio_gfx_sixel_params params) {
    ctx->_params           = params;
    ctx->width             = params.width;
    ctx->height            = params.height;
    ctx->data_cap          = tio_gfx__sixel_buf_cap(params.width, params.height,
                                                     params.scale_y);
    ctx->data              = (char*)TIO_GFX_MALLOC(ctx->data_cap);
    ctx->data_size         = 0;
    ctx->total_generate_ms = 0.0;
    ctx->_owns_scratch     = 1;
    tio_gfx__sixel_alloc_scratch(ctx, &params);
}

void tio_gfx_sixel_destroy(tio_gfx_sixel_ctx* ctx) {
    TIO_GFX_FREE(ctx->data);
    ctx->data      = NULL;
    ctx->data_cap  = 0;
    ctx->data_size = 0;
    if (ctx->_owns_scratch) {
        tio_gfx__sixel_free_scratch(ctx);
    } else {
        ctx->_index_data         = NULL;
        ctx->_scratch_painted    = NULL;
        ctx->_scratch_transposed = NULL;
    }
}

void tio_gfx_sixel_init_shared(tio_gfx_sixel_ctx* ctxs, int n,
                                tio_gfx_sixel_params params) {
    size_t cap = tio_gfx__sixel_buf_cap(params.width, params.height,
                                         params.scale_y);
    ctxs[0]._params           = params;
    ctxs[0].width             = params.width;
    ctxs[0].height            = params.height;
    ctxs[0].data_cap          = cap;
    ctxs[0].data              = (char*)TIO_GFX_MALLOC(cap);
    ctxs[0].data_size         = 0;
    ctxs[0].total_generate_ms = 0.0;
    ctxs[0]._owns_scratch     = 1;
    tio_gfx__sixel_alloc_scratch(&ctxs[0], &params);

    for (int i = 1; i < n; i++) {
        ctxs[i]._params             = params;
        ctxs[i].width               = params.width;
        ctxs[i].height              = params.height;
        ctxs[i].data_cap            = cap;
        ctxs[i].data                = (char*)TIO_GFX_MALLOC(cap);
        ctxs[i].data_size           = 0;
        ctxs[i].total_generate_ms   = 0.0;
        ctxs[i]._owns_scratch       = 0;
        ctxs[i]._index_data         = ctxs[0]._index_data;
        ctxs[i]._scratch_painted    = ctxs[0]._scratch_painted;
        ctxs[i]._scratch_transposed = ctxs[0]._scratch_transposed;
    }
}

void tio_gfx_sixel_destroy_shared(tio_gfx_sixel_ctx* ctxs, int n) {
    for (int i = 0; i < n; i++) {
        TIO_GFX_FREE(ctxs[i].data);
        ctxs[i].data      = NULL;
        ctxs[i].data_size = 0;
        ctxs[i].data_cap  = 0;
    }
    tio_gfx__sixel_free_scratch(&ctxs[0]);
}

void tio_gfx_sixel_set_params(tio_gfx_sixel_ctx* ctx,
                               tio_gfx_sixel_params params) {
    size_t new_cap = tio_gfx__sixel_buf_cap(params.width, params.height,
                                             params.scale_y);
    if (new_cap > ctx->data_cap) {
        TIO_GFX_FREE(ctx->data);
        ctx->data_cap  = new_cap;
        ctx->data      = (char*)TIO_GFX_MALLOC(new_cap);
        ctx->data_size = 0;
    }

    if (ctx->_owns_scratch) {
        size_t new_index  = (size_t)(params.width * 6);
        size_t new_paint  = (size_t)params.width * sizeof(int);
        size_t new_trans  = (size_t)params.width * sizeof(uint64_t);
        size_t old_index  = (size_t)(ctx->_params.width * 6);
        size_t old_paint  = (size_t)ctx->_params.width * sizeof(int);
        size_t old_trans  = (size_t)ctx->_params.width * sizeof(uint64_t);
        if (new_index > old_index || new_paint > old_paint || new_trans > old_trans) {
            tio_gfx__sixel_free_scratch(ctx);
            tio_gfx__sixel_alloc_scratch(ctx, &params);
        }
    }

    ctx->_params = params;
    ctx->width   = params.width;
    ctx->height  = params.height;
}

/* ── Quantization ────────────────────────────────────────────────────────── */

/* Quantizes one band (row_start .. row_start+row_height-1) into
 * _index_data[0 .. row_height*width-1].  _index_data is scratch — only the
 * current band is live at any time. */

/* Scalar fallback — all formats, any width. */
static void tio_gfx__sixel_quantize_band_scalar(tio_gfx_sixel_ctx* ctx,
                                                  const void* pixels,
                                                  tio_gfx_pixel_fmt fmt,
                                                  int row_start, int row_height,
                                                  int x_start, int x_end,
                                                  unsigned char* out) {
    const int w         = ctx->_params.width;
    const int num_steps = 5;
    const int step1     = num_steps + 1;
    const int step1sq   = step1 * step1;
    const unsigned char divider = (unsigned char)(256 / num_steps);

    const int dy = ctx->_params.dither_offset_y;
    const int dx = ctx->_params.dither_offset_x;
    for (int y = row_start; y < row_start + row_height; y++) {
        unsigned char bayer_y = (unsigned char)((y + dy) % 16);
        for (int x = x_start; x < x_end; x++) {
            unsigned char bayer_x = (unsigned char)((x + dx) % 16);
            unsigned char t    = BAYER_PATTERN_16X16[bayer_y][bayer_x];
            unsigned char corr = (unsigned char)(t / num_steps);
            unsigned char r, g, b;

            if (fmt == TIO_GFX_FMT_RGB565) {
                const uint16_t* src = (const uint16_t*)pixels;
                tio_gfx__rgb565_to_rgb888(src[y * w + x], &r, &g, &b);
            } else if (fmt == TIO_GFX_FMT_RGBA8) {
                const unsigned char* src = (const unsigned char*)pixels;
                r = src[(y * w + x) * 4 + 0];
                g = src[(y * w + x) * 4 + 1];
                b = src[(y * w + x) * 4 + 2];
            } else { /* TIO_GFX_FMT_BGRA8 */
                const unsigned char* src = (const unsigned char*)pixels;
                b = src[(y * w + x) * 4 + 0];
                g = src[(y * w + x) * 4 + 1];
                r = src[(y * w + x) * 4 + 2];
            }

            unsigned char r_x = (unsigned char)((r + corr) / divider);
            unsigned char g_x = (unsigned char)((g + corr) / divider);
            unsigned char b_x = (unsigned char)((b + corr) / divider);
            *out++ = (unsigned char)(
                r_x * (unsigned char)step1sq +
                g_x * (unsigned char)step1  +
                b_x);
        }
    }
}

#ifdef TIO_GFX_USE_AVX2

/* AVX2 path — RGB565 only, 32 pixels per iteration.
 * Verbatim logic from the v2 AVX2 converter; adapted for band-at-a-time. */
static void tio_gfx__sixel_quantize_band(tio_gfx_sixel_ctx* ctx,
                                          const void* pixels,
                                          tio_gfx_pixel_fmt fmt,
                                          int row_start, int row_height) {
    const int w  = ctx->_params.width;
    const int dx = ctx->_params.dither_offset_x;
    const int dy = ctx->_params.dither_offset_y;
    const int avx_width = (dx % 16 == 0) ? (w / 32) * 32 : 0;

    if (fmt != TIO_GFX_FMT_RGB565 || avx_width == 0) {
        tio_gfx__sixel_quantize_band_scalar(ctx, pixels, fmt,
                                             row_start, row_height,
                                             0, w, ctx->_index_data);
        return;
    }

    const __m256i step1_vec   = _mm256_set1_epi16((short)6);
    const __m256i step1sq_vec = _mm256_set1_epi16((short)36);
    /* division by 51 via mulhi: floor(x * 1286 / 65536) == floor(x / 51)
     * for x in [0, 305] (max r/g/b + max corr = 255 + 50 = 305) */
    const __m256i magic_div51 = _mm256_set1_epi16((short)1286);
    const __m256i mask5_vec   = _mm256_set1_epi16((short)0x1F);
    const __m256i mask6_vec   = _mm256_set1_epi16((short)0x3F);
    const __m256i byte_mask   = _mm256_set1_epi16((short)0x00FF);

    __m256i bayer_vec[16];
    bayer_vec[0]  = _mm256_setr_epi16(0,38,9,47,2,40,12,50,0,38,10,48,3,41,12,50);
    bayer_vec[1]  = _mm256_setr_epi16(25,12,35,22,27,15,37,24,26,13,35,23,28,15,38,25);
    bayer_vec[2]  = _mm256_setr_epi16(6,44,3,41,8,47,5,43,7,45,3,42,9,47,6,44);
    bayer_vec[3]  = _mm256_setr_epi16(31,19,28,16,34,21,31,18,32,19,29,16,34,22,31,19);
    bayer_vec[4]  = _mm256_setr_epi16(1,39,11,49,0,39,10,48,2,40,11,50,1,39,11,49);
    bayer_vec[5]  = _mm256_setr_epi16(27,14,36,24,26,13,35,23,27,15,37,24,26,14,36,23);
    bayer_vec[6]  = _mm256_setr_epi16(8,46,4,43,7,45,4,42,8,46,5,43,7,46,4,42);
    bayer_vec[7]  = _mm256_setr_epi16(33,20,30,17,32,20,29,16,34,21,30,18,33,20,30,17);
    bayer_vec[8]  = _mm256_setr_epi16(0,38,10,48,2,41,12,50,0,38,9,48,2,40,12,50);
    bayer_vec[9]  = _mm256_setr_epi16(25,13,35,22,28,15,37,25,25,13,35,22,28,15,37,25);
    bayer_vec[10] = _mm256_setr_epi16(6,45,3,41,9,47,6,44,6,44,3,41,9,47,5,44);
    bayer_vec[11] = _mm256_setr_epi16(32,19,29,16,34,22,31,18,32,19,28,16,34,21,31,18);
    bayer_vec[12] = _mm256_setr_epi16(2,40,11,49,1,39,10,49,1,40,11,49,1,39,10,48);
    bayer_vec[13] = _mm256_setr_epi16(27,14,37,24,26,14,36,23,27,14,36,24,26,13,36,23);
    bayer_vec[14] = _mm256_setr_epi16(8,46,5,43,7,45,4,42,8,46,5,43,7,45,4,42);
    bayer_vec[15] = _mm256_setr_epi16(33,21,30,18,33,20,29,17,33,21,30,17,32,20,29,17);

    const uint16_t* src = (const uint16_t*)pixels + row_start * w;
    unsigned char*  out = ctx->_index_data;

    for (int y = 0; y < row_height; y++) {
        __m256i t_vec = bayer_vec[(row_start + y + dy) % 16];
        int x = 0;
        for (; x < avx_width; x += 32) {
            __m256i cn[2];
            for (int i = 0; i < 2; i++) {
                __m256i pix = _mm256_loadu_si256((const __m256i*)(src + x + i * 16));
                /* RGB565 → RGB888 via bit expansion (no division needed):
                 * r8 = (r5<<3)|(r5>>2),  g8 = (g6<<2)|(g6>>4),  b8 = (b5<<3)|(b5>>2) */
                __m256i r5  = _mm256_and_si256(_mm256_srli_epi16(pix, 11), mask5_vec);
                __m256i g6  = _mm256_and_si256(_mm256_srli_epi16(pix,  5), mask6_vec);
                __m256i b5  = _mm256_and_si256(pix, mask5_vec);
                __m256i r   = _mm256_or_si256(_mm256_slli_epi16(r5, 3), _mm256_srli_epi16(r5, 2));
                __m256i g   = _mm256_or_si256(_mm256_slli_epi16(g6, 2), _mm256_srli_epi16(g6, 4));
                __m256i b   = _mm256_or_si256(_mm256_slli_epi16(b5, 3), _mm256_srli_epi16(b5, 2));
                /* dither + quantize: mulhi(channel + corr, 1286) == (channel + corr) / 51 */
                __m256i rd  = _mm256_mulhi_epu16(_mm256_add_epi16(r, t_vec), magic_div51);
                __m256i gd  = _mm256_mulhi_epu16(_mm256_add_epi16(g, t_vec), magic_div51);
                __m256i bd  = _mm256_mulhi_epu16(_mm256_add_epi16(b, t_vec), magic_div51);
                cn[i] = _mm256_add_epi16(
                    _mm256_add_epi16(_mm256_mullo_epi16(rd, step1sq_vec),
                                     _mm256_mullo_epi16(gd, step1_vec)),
                    bd);
            }
            cn[0] = _mm256_and_si256(cn[0], byte_mask);
            cn[1] = _mm256_and_si256(cn[1], byte_mask);
            __m256i packed = _mm256_packus_epi16(cn[0], cn[1]);
            packed = _mm256_permute4x64_epi64(packed, _MM_SHUFFLE(3,1,2,0));
            _mm256_storeu_si256((__m256i*)out, packed);
            out += 32;
        }
        /* scalar tail for remaining pixels in this row */
        for (; x < w; x++) {
            unsigned char bayer_x     = (unsigned char)((x + dx) % 16);
            unsigned char bayer_y_idx = (unsigned char)((row_start + y + dy) % 16);
            unsigned char t    = BAYER_PATTERN_16X16[bayer_y_idx][bayer_x];
            unsigned char corr = (unsigned char)(t / 5);
            unsigned char r, g, b;
            tio_gfx__rgb565_to_rgb888(src[x], &r, &g, &b);
            unsigned char r_x = (unsigned char)((r + corr) / (unsigned char)(256/5));
            unsigned char g_x = (unsigned char)((g + corr) / (unsigned char)(256/5));
            unsigned char b_x = (unsigned char)((b + corr) / (unsigned char)(256/5));
            *out++ = (unsigned char)(r_x * 36 + g_x * 6 + b_x);
        }
        src += w;
    }

    (void)fmt;
}

#else /* no AVX2 */

static void tio_gfx__sixel_quantize_band(tio_gfx_sixel_ctx* ctx,
                                          const void* pixels,
                                          tio_gfx_pixel_fmt fmt,
                                          int row_start, int row_height) {
    tio_gfx__sixel_quantize_band_scalar(ctx, pixels, fmt,
                                         row_start, row_height,
                                         0, ctx->_params.width,
                                         ctx->_index_data);
}

#endif /* TIO_GFX_USE_AVX2 */

/* ── Band encoding ───────────────────────────────────────────────────────── */

static const uint64_t tio_gfx__LSB = 0x0101010101010101ULL;
static const uint64_t tio_gfx__MSB = 0x8080808080808080ULL;

static inline int tio_gfx__color_mask(uint64_t* transposed, int x,
                                            int full_column,
                                            uint64_t packed_color) {
    uint64_t column       = transposed[x];
    uint64_t column_match = column ^ packed_color;
    uint64_t z = (tio_gfx__MSB - (column_match & ~tio_gfx__MSB))
                 & ~column_match & tio_gfx__MSB;
    int mask_alt = (int)((z * 0x0002040810204081ULL) >> 56);
    mask_alt &= full_column;
    return mask_alt;
}

/* Reads from _index_data[y * width + i] where y is band-relative (0-based).
 * Caller must have filled _index_data for the current band before calling. */
static inline void tio_gfx__transpose_to_columns(tio_gfx_sixel_ctx* ctx,
                                                   int width,
                                                   int height, int offset) {
    uint64_t* transposed = ctx->_scratch_transposed;
    int       scale_y    = ctx->_params.scale_y;
    for (int i = 0; i < width; i++) {
        uint64_t packed = 0;
        for (int j = 0; j < height; j++) {
            int y = (j + offset * 6) / scale_y;
            y = tio_gfx__clamp_int(y, 0, height - 1);
            unsigned char color_num =
                ctx->_index_data[y * ctx->_params.width + i];
            packed |= ((uint64_t)color_num << (j * 8));
        }
        transposed[i] = packed;
    }
}

static inline char* tio_gfx__encode_row(tio_gfx_sixel_ctx* ctx,
                                              char* buffer,
                                              int width, int height) {
    int*      painted   = ctx->_scratch_painted;
    uint64_t* transposed = ctx->_scratch_transposed;
    int scale_x         = ctx->_params.scale_x;
    int lookahead       = 4;

    for (int i = 0; i < width; i++) painted[i] = 0;

    int      current_color = (int)(transposed[0] & 0xFF);
    uint64_t packed_color  = (uint64_t)current_color * tio_gfx__LSB;
    int x = 0;
    buffer = tio_gfx__sx_set_color(buffer, current_color);

    int full_column = 0;
    for (int i = 0; i < height; i++) full_column |= (1 << i);

    int startx = x, endx = x;
    while (1) {
        int current_mask = 0;
        int painted_mask = 0;
        for (int i = x; i < x + lookahead && i < width; i++) {
            current_mask = tio_gfx__color_mask(transposed, i,
                                                    full_column, packed_color);
            current_mask &= ~painted[i];
            if (current_mask != 0) {
                buffer = tio_gfx__sx_char_repeated(buffer, 0, (i - x) * scale_x);
                x = i; startx = x; endx = x;
                break;
            }
        }
        if (current_mask == 0) {
            for (int i = x; i < width; i++) {
                if (painted[i] != full_column) {
                    buffer = tio_gfx__sx_char_repeated(buffer, 0, (i - x) * scale_x);
                    x = i; startx = x; endx = x;
                    for (int j = 0; j < height; j++) {
                        if (((~painted[i]) & (1 << j)) != 0) {
                            current_color = (int)((transposed[i] >> (j * 8)) & 0xFF);
                            break;
                        }
                    }
                    buffer       = tio_gfx__sx_set_color(buffer, current_color);
                    packed_color = (uint64_t)current_color * tio_gfx__LSB;
                    current_mask = tio_gfx__color_mask(transposed, i,
                                                            full_column, packed_color);
                    break;
                }
            }
        }
        if (current_mask == 0) {
            if (x == 0) break;
            buffer = tio_gfx__sx_cr(buffer);
            x = 0;
            continue;
        }
        for (int i = x; i < endx + lookahead && i < width; i++) {
            int column_mask = tio_gfx__color_mask(transposed, i,
                                                       full_column, packed_color);
            painted_mask |= painted[i];
            if ((painted_mask & (current_mask | column_mask)) == 0) {
                current_mask  |= column_mask;
                painted[i]    |= column_mask;
                if (column_mask) endx = i;
            } else break;
        }
        buffer = tio_gfx__sx_char_repeated(buffer, current_mask,
                                            (endx - startx + 1) * scale_x);
        x = endx + 1;
        if (x == width) { buffer = tio_gfx__sx_cr(buffer); x = 0; }
    }
    return buffer;
}

/* ── Generate ────────────────────────────────────────────────────────────── */
void tio_gfx_sixel_generate(tio_gfx_sixel_ctx* ctx,
                             const void* pixels,
                             tio_gfx_pixel_fmt fmt,
                             int parts) {
    monotonic_timer_t _t; timer_start(&_t);
    char* p = ctx->data;

    if (parts & TIO_GFX_HEADER) {
        memcpy(p, "\x1b[H", 3); p += 3;
        p = tio_gfx__sx_header(p);
        /* emit 216-color 6x6x6 RGB cube palette */
        for (int i = 0; i < 216; i++) {
            int r = (i / 36)       * 20;
            int g = ((i / 6) % 6)  * 20;
            int b = (i % 6)        * 20;
            p = tio_gfx__sx_modify_color(p, i, r, g, b);
        }
        p = tio_gfx__sx_cr(p);
    }

    if (parts & TIO_GFX_PAYLOAD) {
        int w = ctx->_params.width;
        int h = ctx->_params.height;
        for (int row_start = 0; row_start < h; row_start += 6) {
            int row_height = (row_start + 6 <= h) ? 6 : (h - row_start);
            tio_gfx__sixel_quantize_band(ctx, pixels, fmt, row_start, row_height);
            for (int offset = 0; offset < ctx->_params.scale_y; offset++) {
                tio_gfx__transpose_to_columns(ctx, w, row_height, offset);
                p = tio_gfx__encode_row(ctx, p, w, row_height);
                p = tio_gfx__sx_newline(p);
            }
        }
    }

    if (parts & TIO_GFX_FOOTER) {
        p = tio_gfx__sx_terminator(p);
        *p = '\0';
    }

    ctx->data_size          = (size_t)(p - ctx->data);
    ctx->total_generate_ms += timer_elapsed_ms(&_t);
}

/* ── Stats ───────────────────────────────────────────────────────────────── */
void tio_gfx_sixel_reset_stats(tio_gfx_sixel_ctx* ctx) {
    ctx->total_generate_ms = 0.0;
}

void tio_gfx_sixel_print_stats(const tio_gfx_sixel_ctx* ctx) {
    fprintf(stderr,
            "[tio_gfx_sixel] generate total: %.2f ms | last output: %zu bytes\n",
            ctx->total_generate_ms, ctx->data_size);
}

#endif /* TIO_GFX_SIXEL_IMPLEMENTATION */

#endif /* TIO_GFX_SIXEL_H */
