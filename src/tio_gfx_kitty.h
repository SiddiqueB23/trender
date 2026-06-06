#ifndef TIO_GFX_KITTY_H
#define TIO_GFX_KITTY_H

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

/* ── Kitty-specific types ────────────────────────────────────────────────── */
typedef struct {
    int width, height;    /* encoded image pixel dimensions (= render resolution) */
    int full_height;      /* total image height across all strips (0 = use height); header strip only */
    int starty_rows;      /* 0-based terminal row for cursor positioning; header strip only */
    int cell_height_px;   /* terminal cell height in px */
    int cell_width_px;    /* terminal cell width in px */
    int upscale_x;        /* horizontal upscale factor; c = width * upscale_x / cell_width_px */
    int upscale_y;        /* vertical upscale factor;   r = full_height * upscale_y / cell_height_px */
} tio_gfx_kitty_params;

#define TIO_GFX_KITTY_DEFAULT_PARAMS(w, h) \
    ((tio_gfx_kitty_params){ .width=(w), .height=(h), .full_height=0, \
                              .starty_rows=0, .cell_height_px=1, \
                              .cell_width_px=1, .upscale_x=1, .upscale_y=1 })

typedef struct {
    /* public — read after generate */
    char*          data;
    size_t         data_size;
    size_t         data_cap;
    int            width, height;

    /* stats */
    double         total_generate_ms;

    /* private */
    tio_gfx_kitty_params _params;
    unsigned char* _rgb_buf;     /* width*height*3 bytes, shared across buffer slots */
    size_t         _rgb_buf_cap;
    int            _owns_scratch;
} tio_gfx_kitty_ctx;

/* ── Declarations ────────────────────────────────────────────────────────── */
void tio_gfx_kitty_init(tio_gfx_kitty_ctx* ctx, tio_gfx_kitty_params params);
void tio_gfx_kitty_destroy(tio_gfx_kitty_ctx* ctx);
void tio_gfx_kitty_init_shared(tio_gfx_kitty_ctx* ctxs, int n,
                                 tio_gfx_kitty_params params);
void tio_gfx_kitty_destroy_shared(tio_gfx_kitty_ctx* ctxs, int n);
void tio_gfx_kitty_set_params(tio_gfx_kitty_ctx* ctx,
                                tio_gfx_kitty_params params);
void tio_gfx_kitty_generate(tio_gfx_kitty_ctx* ctx,
                              const void* pixels,
                              tio_gfx_pixel_fmt fmt,
                              int parts);
void tio_gfx_kitty_use_scratch_of(tio_gfx_kitty_ctx* dst, tio_gfx_kitty_ctx* src);
void tio_gfx_kitty_reset_stats(tio_gfx_kitty_ctx* ctx);
void tio_gfx_kitty_print_stats(const tio_gfx_kitty_ctx* ctx);

/* ═══════════════════════════════════════════════════════════════════════════
   IMPLEMENTATION — define TIO_GFX_KITTY_IMPLEMENTATION in exactly one TU
   ═══════════════════════════════════════════════════════════════════════════ */
#ifdef TIO_GFX_KITTY_IMPLEMENTATION

#include "timer.h"

/* 4096 base64 chars = 3072 raw bytes per chunk */
#define TIO_GFX__KITTY_CHUNK_BYTES 3072

/* ── Buffer capacity ─────────────────────────────────────────────────────── */
static size_t tio_gfx__kitty_buf_cap(int w, int h) {
    size_t rgb = (size_t)w * h * 3;
    size_t b64 = (rgb + 2) / 3 * 4;
    size_t n_chunks = rgb / TIO_GFX__KITTY_CHUNK_BYTES + 2; /* +2: at least 1 + safety */
    return b64 + n_chunks * 60 + 32; /* 60 = max header per chunk, 32 = cursor escape */
}

static size_t tio_gfx__kitty_rgb_buf_cap(int w, int h) {
    return (size_t)w * h * 3;
}

/* ── Base64 ──────────────────────────────────────────────────────────────── */
static const char tio_gfx__kitty_b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t tio_gfx__kitty_base64_encode(char* dst,
                                             const unsigned char* src,
                                             size_t n) {
    char* p = dst;
    size_t i = 0;
    while (i + 3 <= n) {
        uint32_t v = ((uint32_t)src[i] << 16)
                   | ((uint32_t)src[i+1] << 8)
                   |  (uint32_t)src[i+2];
        *p++ = tio_gfx__kitty_b64[(v >> 18) & 63];
        *p++ = tio_gfx__kitty_b64[(v >> 12) & 63];
        *p++ = tio_gfx__kitty_b64[(v >>  6) & 63];
        *p++ = tio_gfx__kitty_b64[(v      ) & 63];
        i += 3;
    }
    if (i < n) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < n) v |= (uint32_t)src[i+1] << 8;
        *p++ = tio_gfx__kitty_b64[(v >> 18) & 63];
        *p++ = tio_gfx__kitty_b64[(v >> 12) & 63];
        *p++ = (i + 1 < n) ? tio_gfx__kitty_b64[(v >> 6) & 63] : '=';
        *p++ = '=';
    }
    return (size_t)(p - dst);
}

/* ── Pixel conversion: any fmt → packed RGB ──────────────────────────────── */
static void tio_gfx__kitty_to_rgb(unsigned char* dst, const void* pixels,
                                    tio_gfx_pixel_fmt fmt, int w, int h) {
    int n = w * h;
    if (fmt == TIO_GFX_FMT_RGB565) {
        const uint16_t* src = (const uint16_t*)pixels;
        for (int i = 0; i < n; i++) {
            uint16_t v = src[i];
            dst[i*3+0] = (unsigned char)(((v >> 11) & 0x1F) * 255 / 31);
            dst[i*3+1] = (unsigned char)(((v >>  5) & 0x3F) * 255 / 63);
            dst[i*3+2] = (unsigned char)( (v        & 0x1F) * 255 / 31);
        }
    } else if (fmt == TIO_GFX_FMT_RGBA8) {
        const unsigned char* src = (const unsigned char*)pixels;
        for (int i = 0; i < n; i++) {
            dst[i*3+0] = src[i*4+0];
            dst[i*3+1] = src[i*4+1];
            dst[i*3+2] = src[i*4+2];
        }
    } else { /* BGRA8 */
        const unsigned char* src = (const unsigned char*)pixels;
        for (int i = 0; i < n; i++) {
            dst[i*3+0] = src[i*4+2];
            dst[i*3+1] = src[i*4+1];
            dst[i*3+2] = src[i*4+0];
        }
    }
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */
void tio_gfx_kitty_init(tio_gfx_kitty_ctx* ctx, tio_gfx_kitty_params params) {
    ctx->_params           = params;
    ctx->width             = params.width;
    ctx->height            = params.height;
    ctx->data_cap          = tio_gfx__kitty_buf_cap(params.width, params.height);
    ctx->data              = (char*)TIO_GFX_MALLOC(ctx->data_cap);
    ctx->data_size         = 0;
    ctx->total_generate_ms = 0.0;
    ctx->_rgb_buf_cap      = tio_gfx__kitty_rgb_buf_cap(params.width, params.height);
    ctx->_rgb_buf          = (unsigned char*)TIO_GFX_MALLOC(ctx->_rgb_buf_cap);
    ctx->_owns_scratch     = 1;
}

void tio_gfx_kitty_destroy(tio_gfx_kitty_ctx* ctx) {
    TIO_GFX_FREE(ctx->data);
    if (ctx->_owns_scratch)
        TIO_GFX_FREE(ctx->_rgb_buf);
    ctx->data        = NULL;
    ctx->_rgb_buf    = NULL;
    ctx->data_cap    = 0;
    ctx->data_size   = 0;
    ctx->_rgb_buf_cap = 0;
}

void tio_gfx_kitty_use_scratch_of(tio_gfx_kitty_ctx* dst,
                                    tio_gfx_kitty_ctx* src) {
    if (dst->_owns_scratch)
        TIO_GFX_FREE(dst->_rgb_buf);
    dst->_rgb_buf      = src->_rgb_buf;
    dst->_rgb_buf_cap  = src->_rgb_buf_cap;
    dst->_owns_scratch = 0;
}

void tio_gfx_kitty_init_shared(tio_gfx_kitty_ctx* ctxs, int n,
                                 tio_gfx_kitty_params params) {
    for (int i = 0; i < n; i++) tio_gfx_kitty_init(&ctxs[i], params);
    for (int i = 1; i < n; i++) tio_gfx_kitty_use_scratch_of(&ctxs[i], &ctxs[0]);
}

void tio_gfx_kitty_destroy_shared(tio_gfx_kitty_ctx* ctxs, int n) {
    for (int i = 0; i < n; i++) tio_gfx_kitty_destroy(&ctxs[i]);
}

void tio_gfx_kitty_set_params(tio_gfx_kitty_ctx* ctx,
                                tio_gfx_kitty_params params) {
    size_t new_data_cap = tio_gfx__kitty_buf_cap(params.width, params.height);
    size_t new_rgb_cap  = tio_gfx__kitty_rgb_buf_cap(params.width, params.height);
    if (new_data_cap > ctx->data_cap) {
        TIO_GFX_FREE(ctx->data);
        ctx->data_cap  = new_data_cap;
        ctx->data      = (char*)TIO_GFX_MALLOC(new_data_cap);
        ctx->data_size = 0;
    }
    if (new_rgb_cap > ctx->_rgb_buf_cap) {
        TIO_GFX_FREE(ctx->_rgb_buf);
        ctx->_rgb_buf_cap = new_rgb_cap;
        ctx->_rgb_buf     = (unsigned char*)TIO_GFX_MALLOC(new_rgb_cap);
    }
    ctx->_params = params;
    ctx->width   = params.width;
    ctx->height  = params.height;
}

/* ── Generate ────────────────────────────────────────────────────────────── */
void tio_gfx_kitty_generate(tio_gfx_kitty_ctx* ctx,
                              const void* pixels,
                              tio_gfx_pixel_fmt fmt,
                              int parts) {
    monotonic_timer_t _t; timer_start(&_t);

    const int w      = ctx->_params.width;
    const int h      = ctx->_params.height;
    const int full_h = (ctx->_params.full_height > 0) ? ctx->_params.full_height : h;
    const int cw     = ctx->_params.cell_width_px  > 0 ? ctx->_params.cell_width_px  : 1;
    const int ch     = ctx->_params.cell_height_px > 0 ? ctx->_params.cell_height_px : 1;
    const int c_cols = w      * ctx->_params.upscale_x / cw;
    const int c_rows = full_h * ctx->_params.upscale_y / ch;
    /* last chunk m=0 only when this strip carries the footer (terminates the image) */
    const int last_m = (parts & TIO_GFX_FOOTER) ? 0 : 1;

    char* p = ctx->data;

    /* cursor reposition — header strip only */
    if (parts & TIO_GFX_HEADER)
        p += sprintf(p, "\x1b[%d;1H", ctx->_params.starty_rows + 1);

    /* convert pixels to packed RGB */
    tio_gfx__kitty_to_rgb(ctx->_rgb_buf, pixels, fmt, w, h);

    /* emit chunked APC sequences */
    size_t rgb_total = (size_t)w * h * 3;
    size_t offset = 0;
    int emit_init = (parts & TIO_GFX_HEADER); /* emit a=T on first chunk of header strip */
    do {
        size_t chunk = rgb_total - offset;
        if (chunk > TIO_GFX__KITTY_CHUNK_BYTES) chunk = TIO_GFX__KITTY_CHUNK_BYTES;
        int more = (offset + chunk < rgb_total) ? 1 : last_m;
        if (emit_init) {
            p += sprintf(p, "\x1b_Ga=T,f=24,s=%d,v=%d,c=%d,r=%d,C=1,m=%d,q=2,z=-1;",
                         w, full_h, c_cols, c_rows, more);
            emit_init = 0;
        } else {
            p += sprintf(p, "\x1b_Gm=%d,q=2;", more);
        }
        p += tio_gfx__kitty_base64_encode(p, ctx->_rgb_buf + offset, chunk);
        *p++ = '\x1b'; *p++ = '\\';
        offset += chunk;
    } while (offset < rgb_total);

    ctx->data_size          = (size_t)(p - ctx->data);
    ctx->total_generate_ms += timer_elapsed_ms(&_t);
}

/* ── Stats ───────────────────────────────────────────────────────────────── */
void tio_gfx_kitty_reset_stats(tio_gfx_kitty_ctx* ctx) {
    ctx->total_generate_ms = 0.0;
}

void tio_gfx_kitty_print_stats(const tio_gfx_kitty_ctx* ctx) {
    fprintf(stderr,
            "[tio_gfx_kitty] generate total: %.2f ms | last output: %zu bytes\n",
            ctx->total_generate_ms, ctx->data_size);
}

#endif /* TIO_GFX_KITTY_IMPLEMENTATION */
#endif /* TIO_GFX_KITTY_H */
