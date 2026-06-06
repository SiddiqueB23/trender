#ifndef TIO_GFX_ITERM_H
#define TIO_GFX_ITERM_H

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

/* ── iTerm-specific types ────────────────────────────────────────────────── */
typedef enum {
    TIO_GFX_ITERM_FMT_PNG  = 0,  /* default — best compatibility */
    TIO_GFX_ITERM_FMT_BMP  = 1,  /* fast, no compression */
    TIO_GFX_ITERM_FMT_JPEG = 2,  /* lossy, smallest size */
} tio_gfx_iterm_encode_fmt;

typedef struct {
    int width, height;
    int starty_rows;              /* 0-based terminal row for cursor positioning */
    int cell_height_px;           /* terminal cell height in px; used externally to compute starty_rows */
    tio_gfx_iterm_encode_fmt encode_fmt;
    int jpeg_quality;             /* 1–100; ignored for PNG/BMP */
    int png_compression_level;    /* 0–9; ignored for BMP/JPEG; default 8 matches stb */
    int upscale_x;                /* horizontal display upscale: OSC header width = width * upscale_x */
    int upscale_y;                /* vertical display upscale:   OSC header height = height * upscale_y */
} tio_gfx_iterm_params;

#define TIO_GFX_ITERM_DEFAULT_PARAMS(w, h) \
    ((tio_gfx_iterm_params){ .width=(w), .height=(h), \
                              .starty_rows=0, .cell_height_px=1, \
                              .encode_fmt=TIO_GFX_ITERM_FMT_PNG, \
                              .jpeg_quality=90, \
                              .png_compression_level=8, \
                              .upscale_x=1, .upscale_y=1 })

typedef struct {
    /* public — read after generate */
    char*          data;
    size_t         data_size;
    size_t         data_cap;
    int            width, height;

    /* stats */
    double         total_generate_ms;

    /* private */
    tio_gfx_iterm_params _params;
    unsigned char* _rgb_buf;        /* intermediate w*h*3 RGB scratch (shared across buffer slots) */
    size_t         _rgb_buf_cap;
    char*          _encode_buf;     /* stb output before base64 (shared across buffer slots) */
    size_t         _encode_buf_cap;
    int            _owns_scratch;   /* 1 if this ctx owns _rgb_buf/_encode_buf, 0 if borrowed */
} tio_gfx_iterm_ctx;

/* ── Declarations ────────────────────────────────────────────────────────── */
void tio_gfx_iterm_init(tio_gfx_iterm_ctx* ctx, tio_gfx_iterm_params params);
void tio_gfx_iterm_destroy(tio_gfx_iterm_ctx* ctx);
void tio_gfx_iterm_init_shared(tio_gfx_iterm_ctx* ctxs, int n,
                                 tio_gfx_iterm_params params);
void tio_gfx_iterm_destroy_shared(tio_gfx_iterm_ctx* ctxs, int n);
void tio_gfx_iterm_set_params(tio_gfx_iterm_ctx* ctx,
                                tio_gfx_iterm_params params);
void tio_gfx_iterm_generate(tio_gfx_iterm_ctx* ctx,
                              const void* pixels,
                              tio_gfx_pixel_fmt fmt,
                              int parts);
void tio_gfx_iterm_use_scratch_of(tio_gfx_iterm_ctx* dst, tio_gfx_iterm_ctx* src);
void tio_gfx_iterm_reset_stats(tio_gfx_iterm_ctx* ctx);
void tio_gfx_iterm_print_stats(const tio_gfx_iterm_ctx* ctx);

/* ═══════════════════════════════════════════════════════════════════════════
   IMPLEMENTATION — define TIO_GFX_ITERM_IMPLEMENTATION in exactly one TU
   ═══════════════════════════════════════════════════════════════════════════ */
#ifdef TIO_GFX_ITERM_IMPLEMENTATION

#include "timer.h"

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* ── Buffer capacity ─────────────────────────────────────────────────────── */
static size_t tio_gfx__iterm_encode_buf_cap(int w, int h) {
    return (size_t)w * h * 4 + 1024; /* safe upper bound for PNG/BMP/JPEG */
}

static size_t tio_gfx__iterm_buf_cap(int w, int h) {
    size_t enc = tio_gfx__iterm_encode_buf_cap(w, h);
    return (enc + 2) / 3 * 4 + 256; /* base64 expansion + OSC/cursor overhead */
}

/* ── Base64 ──────────────────────────────────────────────────────────────── */
static const char tio_gfx__iterm_b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t tio_gfx__iterm_base64_encode(char* dst,
                                             const unsigned char* src,
                                             size_t n) {
    char* p = dst;
    size_t i = 0;
    while (i + 3 <= n) {
        uint32_t v = ((uint32_t)src[i] << 16)
                   | ((uint32_t)src[i+1] << 8)
                   |  (uint32_t)src[i+2];
        *p++ = tio_gfx__iterm_b64[(v >> 18) & 63];
        *p++ = tio_gfx__iterm_b64[(v >> 12) & 63];
        *p++ = tio_gfx__iterm_b64[(v >>  6) & 63];
        *p++ = tio_gfx__iterm_b64[(v      ) & 63];
        i += 3;
    }
    if (i < n) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < n) v |= (uint32_t)src[i+1] << 8;
        *p++ = tio_gfx__iterm_b64[(v >> 18) & 63];
        *p++ = tio_gfx__iterm_b64[(v >> 12) & 63];
        *p++ = (i + 1 < n) ? tio_gfx__iterm_b64[(v >> 6) & 63] : '=';
        *p++ = '=';
    }
    return (size_t)(p - dst);
}

/* ── stb write callback ──────────────────────────────────────────────────── */
typedef struct { char* buf; size_t size; } tio_gfx__iterm_wctx;

static void tio_gfx__iterm_write_cb(void* context, void* data, int size) {
    tio_gfx__iterm_wctx* wc = (tio_gfx__iterm_wctx*)context;
    memcpy(wc->buf + wc->size, data, (size_t)size);
    wc->size += (size_t)size;
}

/* ── Pixel conversion: any fmt → packed RGB ──────────────────────────────── */
static void tio_gfx__iterm_to_rgb(unsigned char* dst, const void* pixels,
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
void tio_gfx_iterm_init(tio_gfx_iterm_ctx* ctx, tio_gfx_iterm_params params) {
    ctx->_params           = params;
    ctx->width             = params.width;
    ctx->height            = params.height;
    ctx->data_cap          = tio_gfx__iterm_buf_cap(params.width, params.height);
    ctx->data              = (char*)TIO_GFX_MALLOC(ctx->data_cap);
    ctx->data_size         = 0;
    ctx->total_generate_ms = 0.0;
    ctx->_encode_buf_cap   = tio_gfx__iterm_encode_buf_cap(params.width, params.height);
    ctx->_encode_buf       = (char*)TIO_GFX_MALLOC(ctx->_encode_buf_cap);
    ctx->_rgb_buf_cap      = (size_t)params.width * params.height * 3;
    ctx->_rgb_buf          = (unsigned char*)TIO_GFX_MALLOC(ctx->_rgb_buf_cap);
    ctx->_owns_scratch     = 1;
    stbi_write_png_compression_level = params.png_compression_level;
}

void tio_gfx_iterm_destroy(tio_gfx_iterm_ctx* ctx) {
    TIO_GFX_FREE(ctx->data);
    if (ctx->_owns_scratch) {
        TIO_GFX_FREE(ctx->_encode_buf);
        TIO_GFX_FREE(ctx->_rgb_buf);
    }
    ctx->data            = NULL;
    ctx->_encode_buf     = NULL;
    ctx->_rgb_buf        = NULL;
    ctx->data_cap        = 0;
    ctx->data_size       = 0;
    ctx->_encode_buf_cap = 0;
    ctx->_rgb_buf_cap    = 0;
}

void tio_gfx_iterm_use_scratch_of(tio_gfx_iterm_ctx* dst,
                                    tio_gfx_iterm_ctx* src) {
    if (dst->_owns_scratch) {
        TIO_GFX_FREE(dst->_rgb_buf);
        TIO_GFX_FREE(dst->_encode_buf);
    }
    dst->_rgb_buf        = src->_rgb_buf;
    dst->_rgb_buf_cap    = src->_rgb_buf_cap;
    dst->_encode_buf     = src->_encode_buf;
    dst->_encode_buf_cap = src->_encode_buf_cap;
    dst->_owns_scratch   = 0;
}

void tio_gfx_iterm_init_shared(tio_gfx_iterm_ctx* ctxs, int n,
                                 tio_gfx_iterm_params params) {
    for (int i = 0; i < n; i++) tio_gfx_iterm_init(&ctxs[i], params);
    for (int i = 1; i < n; i++) tio_gfx_iterm_use_scratch_of(&ctxs[i], &ctxs[0]);
}

void tio_gfx_iterm_destroy_shared(tio_gfx_iterm_ctx* ctxs, int n) {
    for (int i = 0; i < n; i++) tio_gfx_iterm_destroy(&ctxs[i]);
}

void tio_gfx_iterm_set_params(tio_gfx_iterm_ctx* ctx,
                                tio_gfx_iterm_params params) {
    size_t new_data_cap   = tio_gfx__iterm_buf_cap(params.width, params.height);
    size_t new_encode_cap = tio_gfx__iterm_encode_buf_cap(params.width, params.height);
    size_t new_rgb_cap    = (size_t)params.width * params.height * 3;
    if (new_data_cap > ctx->data_cap) {
        TIO_GFX_FREE(ctx->data);
        ctx->data_cap  = new_data_cap;
        ctx->data      = (char*)TIO_GFX_MALLOC(new_data_cap);
        ctx->data_size = 0;
    }
    if (new_encode_cap > ctx->_encode_buf_cap) {
        TIO_GFX_FREE(ctx->_encode_buf);
        ctx->_encode_buf_cap = new_encode_cap;
        ctx->_encode_buf     = (char*)TIO_GFX_MALLOC(new_encode_cap);
    }
    if (new_rgb_cap > ctx->_rgb_buf_cap) {
        TIO_GFX_FREE(ctx->_rgb_buf);
        ctx->_rgb_buf_cap = new_rgb_cap;
        ctx->_rgb_buf     = (unsigned char*)TIO_GFX_MALLOC(new_rgb_cap);
    }
    ctx->_params = params;
    ctx->width   = params.width;
    ctx->height  = params.height;
    stbi_write_png_compression_level = params.png_compression_level;
}

/* ── Generate ────────────────────────────────────────────────────────────── */
void tio_gfx_iterm_generate(tio_gfx_iterm_ctx* ctx,
                              const void* pixels,
                              tio_gfx_pixel_fmt fmt,
                              int parts) {
    (void)parts; /* always emit a complete independent image with cursor reposition */
    monotonic_timer_t _t; timer_start(&_t);
    char* p = ctx->data;
    const int w  = ctx->_params.width;
    const int h  = ctx->_params.height;
    const int ux = ctx->_params.upscale_x > 1 ? ctx->_params.upscale_x : 1;
    const int uy = ctx->_params.upscale_y > 1 ? ctx->_params.upscale_y : 1;

    /* cursor reposition */
    p += sprintf(p, "\x1b[%d;1H", ctx->_params.starty_rows + 1);

    /* OSC header specifies full display size; terminal upscales the encoded image */
    p += sprintf(p, "\x1b]1337;File=width=%dpx;height=%dpx;inline=1:", w * ux, h * uy);

    /* convert pixels to packed RGB */
    tio_gfx__iterm_to_rgb(ctx->_rgb_buf, pixels, fmt, w, h);

    /* encode via stb into _encode_buf */
    tio_gfx__iterm_wctx wc = { ctx->_encode_buf, 0 };
    switch (ctx->_params.encode_fmt) {
        case TIO_GFX_ITERM_FMT_PNG:
            stbi_write_png_to_func(tio_gfx__iterm_write_cb, &wc, w, h, 3,
                                   ctx->_rgb_buf, w * 3);
            break;
        case TIO_GFX_ITERM_FMT_BMP:
            stbi_write_bmp_to_func(tio_gfx__iterm_write_cb, &wc, w, h, 3,
                                   ctx->_rgb_buf);
            break;
        case TIO_GFX_ITERM_FMT_JPEG:
            stbi_write_jpg_to_func(tio_gfx__iterm_write_cb, &wc, w, h, 3,
                                   ctx->_rgb_buf, ctx->_params.jpeg_quality);
            break;
    }

    /* base64-encode into output buffer */
    p += tio_gfx__iterm_base64_encode(p,
             (const unsigned char*)ctx->_encode_buf, wc.size);

    /* BEL terminator */
    *p++ = '\a';

    ctx->data_size          = (size_t)(p - ctx->data);
    ctx->total_generate_ms += timer_elapsed_ms(&_t);
}

/* ── Stats ───────────────────────────────────────────────────────────────── */
void tio_gfx_iterm_reset_stats(tio_gfx_iterm_ctx* ctx) {
    ctx->total_generate_ms = 0.0;
}

void tio_gfx_iterm_print_stats(const tio_gfx_iterm_ctx* ctx) {
    fprintf(stderr,
            "[tio_gfx_iterm] generate total: %.2f ms | last output: %zu bytes\n",
            ctx->total_generate_ms, ctx->data_size);
}

#endif /* TIO_GFX_ITERM_IMPLEMENTATION */
#endif /* TIO_GFX_ITERM_H */
