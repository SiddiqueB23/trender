#ifndef TIO_GFX_HALFBLOCK_H
#define TIO_GFX_HALFBLOCK_H

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

/* ── Halfblock-specific types ────────────────────────────────────────────── */
typedef enum {
    TIO_GFX_HALFBLOCK_COLOR_24BIT = 0, /* 24-bit true color via ANSI SGR 38;2/48;2 */
    TIO_GFX_HALFBLOCK_COLOR_216   = 1, /* xterm 6x6x6 cube via ANSI SGR 38;5/48;5, Bayer dithered */
} tio_gfx_halfblock_color_mode;

typedef struct {
    int width, height;
    tio_gfx_halfblock_color_mode color_mode;
    int dither_offset_x, dither_offset_y; /* pixel offsets into Bayer matrix; for multithreaded strips */
    int upscale_x, upscale_y; /* each render pixel repeated upscale_x cols; each row pair repeated upscale_y rows */
} tio_gfx_halfblock_params;

#define TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(w, h) \
    ((tio_gfx_halfblock_params){ .width=(w), .height=(h), \
                                  .color_mode=TIO_GFX_HALFBLOCK_COLOR_24BIT, \
                                  .dither_offset_x=0, .dither_offset_y=0, \
                                  .upscale_x=1, .upscale_y=1 })

typedef struct {
    /* public — read after generate */
    int     width, height;

    /* stats */
    double  total_generate_ms;

    /* private */
    tio_gfx_halfblock_params _params;
} tio_gfx_halfblock_ctx;

/* ── Declarations ────────────────────────────────────────────────────────── */
void   tio_gfx_halfblock_init(tio_gfx_halfblock_ctx* ctx,
                               tio_gfx_halfblock_params params);
void   tio_gfx_halfblock_destroy(tio_gfx_halfblock_ctx* ctx);
void   tio_gfx_halfblock_set_params(tio_gfx_halfblock_ctx* ctx,
                                    tio_gfx_halfblock_params params);
int    tio_gfx_halfblock_generate(tio_gfx_halfblock_ctx* ctx,
                                   const void* pixels,
                                   tio_gfx_pixel_fmt fmt,
                                   int parts,
                                   char* out_buf, size_t out_cap);
size_t tio_gfx_halfblock_output_size_hint(tio_gfx_halfblock_params params);
void   tio_gfx_halfblock_reset_stats(tio_gfx_halfblock_ctx* ctx);
void   tio_gfx_halfblock_print_stats(const tio_gfx_halfblock_ctx* ctx);

/* ═══════════════════════════════════════════════════════════════════════════
   IMPLEMENTATION — define TIO_GFX_HALFBLOCK_IMPLEMENTATION in exactly one TU
   ═══════════════════════════════════════════════════════════════════════════ */
#ifdef TIO_GFX_HALFBLOCK_IMPLEMENTATION

#include "timer.h"
#include "bayer_patterns.h"

/* ── Integer-to-ASCII (0-255, no leading zeros) ──────────────────────────── */
static char* tio_gfx__hb_itoa3(char* buf, int n) {
    int h = n / 100;
    *buf = (char)('0' + h); buf += (h != 0);
    n -= h * 100;
    int t = n / 10;
    *buf = (char)('0' + t); buf += (h + t != 0);
    *buf++ = (char)('0' + n - t * 10);
    return buf;
}

/* ── RGB565 → RGB888 ─────────────────────────────────────────────────────── */
static void tio_gfx__hb_rgb565_to_rgb888(uint16_t src,
                                           unsigned char* r,
                                           unsigned char* g,
                                           unsigned char* b) {
    *r = (unsigned char)(((src >> 11) & 0x1F) * 255 / 31);
    *g = (unsigned char)(((src >>  5) & 0x3F) * 255 / 63);
    *b = (unsigned char)((src         & 0x1F) * 255 / 31);
}

/* ── Pixel reader ────────────────────────────────────────────────────────── */
static inline void tio_gfx__hb_read_pixel(const void* pixels,
                                            tio_gfx_pixel_fmt fmt,
                                            int y, int x, int w,
                                            unsigned char* r,
                                            unsigned char* g,
                                            unsigned char* b) {
    if (fmt == TIO_GFX_FMT_RGB565) {
        const uint16_t* src = (const uint16_t*)pixels;
        tio_gfx__hb_rgb565_to_rgb888(src[y * w + x], r, g, b);
    } else if (fmt == TIO_GFX_FMT_RGBA8) {
        const unsigned char* src = (const unsigned char*)pixels;
        *r = src[(y * w + x) * 4 + 0];
        *g = src[(y * w + x) * 4 + 1];
        *b = src[(y * w + x) * 4 + 2];
    } else { /* TIO_GFX_FMT_BGRA8 */
        const unsigned char* src = (const unsigned char*)pixels;
        *b = src[(y * w + x) * 4 + 0];
        *g = src[(y * w + x) * 4 + 1];
        *r = src[(y * w + x) * 4 + 2];
    }
}

/* ── ANSI SGR emitters ───────────────────────────────────────────────────── */
static char* tio_gfx__hb_set_fg(char* p,
                                  unsigned char r, unsigned char g, unsigned char b) {
    memcpy(p, "\x1b[38;2;", 7); p += 7;
    p = tio_gfx__hb_itoa3(p, r); *p++ = ';';
    p = tio_gfx__hb_itoa3(p, g); *p++ = ';';
    p = tio_gfx__hb_itoa3(p, b); *p++ = 'm';
    return p;
}

static char* tio_gfx__hb_set_bg(char* p,
                                  unsigned char r, unsigned char g, unsigned char b) {
    memcpy(p, "\x1b[48;2;", 7); p += 7;
    p = tio_gfx__hb_itoa3(p, r); *p++ = ';';
    p = tio_gfx__hb_itoa3(p, g); *p++ = ';';
    p = tio_gfx__hb_itoa3(p, b); *p++ = 'm';
    return p;
}

static char* tio_gfx__hb_set_fg_bg(char* p,
                                     unsigned char fr, unsigned char fg, unsigned char fb,
                                     unsigned char br, unsigned char bg, unsigned char bb) {
    memcpy(p, "\x1b[38;2;", 7); p += 7;
    p = tio_gfx__hb_itoa3(p, fr); *p++ = ';';
    p = tio_gfx__hb_itoa3(p, fg); *p++ = ';';
    p = tio_gfx__hb_itoa3(p, fb);
    memcpy(p, ";48;2;", 6); p += 6;
    p = tio_gfx__hb_itoa3(p, br); *p++ = ';';
    p = tio_gfx__hb_itoa3(p, bg); *p++ = ';';
    p = tio_gfx__hb_itoa3(p, bb); *p++ = 'm';
    return p;
}

static char* tio_gfx__hb_reset(char* p) {
    memcpy(p, "\x1b[0m", 4); return p + 4;
}

/* ── 256-color (xterm 6x6x6 cube) SGR emitters ──────────────────────────── */
static char* tio_gfx__hb_set_fg5(char* p, int n) {
    memcpy(p, "\x1b[38;5;", 7); p += 7;
    p = tio_gfx__hb_itoa3(p, n); *p++ = 'm';
    return p;
}
static char* tio_gfx__hb_set_bg5(char* p, int n) {
    memcpy(p, "\x1b[48;5;", 7); p += 7;
    p = tio_gfx__hb_itoa3(p, n); *p++ = 'm';
    return p;
}
static char* tio_gfx__hb_set_fg5_bg5(char* p, int fn, int bn) {
    memcpy(p, "\x1b[38;5;", 7); p += 7;
    p = tio_gfx__hb_itoa3(p, fn);
    memcpy(p, ";48;5;", 6); p += 6;
    p = tio_gfx__hb_itoa3(p, bn); *p++ = 'm';
    return p;
}

/* ── xterm 6x6x6 cube quantizer with Bayer ordered dithering ────────────── */
/* Step values: {0, 95, 135, 175, 215, 255} (confirmed xterm standard).
 * Boundaries are the step values themselves (floor), not midpoints.
 * Midpoint boundaries would make dist negative for the lower half of each
 * gap, silently disabling dithering there. Floor boundaries keep dist >= 0
 * across the full gap so the Bayer threshold can dither in both directions. */
static unsigned char tio_gfx__hb_quantize_xterm(unsigned char val,
                                                  unsigned char threshold) {
    static const int sv[6] = {0, 95, 135, 175, 215, 255};
    unsigned char step;
    if      (val <  95) step = 0;
    else if (val < 135) step = 1;
    else if (val < 175) step = 2;
    else if (val < 215) step = 3;
    else if (val < 255) step = 4;
    else                return 5;

    int range = sv[step + 1] - sv[step];
    int dist  = (int)val - sv[step];
    /* Round up to step+1 if dist/range > threshold/255 */
    return (unsigned char)(step + (dist * 255 > (int)threshold * range ? 1 : 0));
}

/* ── Capacity ────────────────────────────────────────────────────────────── */
static size_t tio_gfx__hb_buf_cap(int w, int h, int ux, int uy) {
    /* 28 = max color escape (combined fg+bg 24-bit), 3*ux = chars per render pixel, 10 = reset+crlf+margin */
    size_t per_row = (size_t)w * (32 + 3 * (size_t)ux) + 10;
    size_t n_rows  = (size_t)(h * uy + 1) / 2;
    return per_row * n_rows + 100;
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */
void tio_gfx_halfblock_init(tio_gfx_halfblock_ctx* ctx,
                              tio_gfx_halfblock_params params) {
    ctx->_params           = params;
    ctx->width             = params.width;
    ctx->height            = params.height;
    ctx->total_generate_ms = 0.0;
}

void tio_gfx_halfblock_destroy(tio_gfx_halfblock_ctx* ctx) {
    (void)ctx;
}

void tio_gfx_halfblock_set_params(tio_gfx_halfblock_ctx* ctx,
                                   tio_gfx_halfblock_params params) {
    ctx->_params = params;
    ctx->width   = params.width;
    ctx->height  = params.height;
}

size_t tio_gfx_halfblock_output_size_hint(tio_gfx_halfblock_params params) {
    return tio_gfx__hb_buf_cap(params.width, params.height, params.upscale_x, params.upscale_y);
}

/* ── Payload generators ──────────────────────────────────────────────────── */
static char* tio_gfx__hb_payload_24bit(tio_gfx_halfblock_ctx* ctx,
                                         char* p,
                                         const void* pixels,
                                         tio_gfx_pixel_fmt fmt,
                                         int emit_last_crlf) {
    const int w  = ctx->_params.width;
    const int h  = ctx->_params.height;
    const int ux = ctx->_params.upscale_x > 0 ? ctx->_params.upscale_x : 1;
    const int uy = ctx->_params.upscale_y > 0 ? ctx->_params.upscale_y : 1;
    /* terminal row k uses render pixel rows (2k)/uy (top) and (2k+1)/uy (bottom).
     * this gives each render pixel row uy consecutive terminal half-rows, so
     * a strip [a,b] at uy=2 becomes [a,a,b,b] not [a,b,a,b]. */
    const int num_terminal_rows = h * uy / 2;

    for (int k = 0; k < num_terminal_rows; k++) {
        int top = (2 * k)     / uy;
        int bot = (2 * k + 1) / uy;
        if (bot >= h) bot = h - 1;

        int pfr = -1, pfg = -1, pfb = -1;
        int pbr = -1, pbg = -1, pbb = -1;

        for (int x = 0; x < w; x++) {
            unsigned char fr, fg, fb, br, bg, bb;
            tio_gfx__hb_read_pixel(pixels, fmt, top, x, w, &fr, &fg, &fb);
            tio_gfx__hb_read_pixel(pixels, fmt, bot, x, w, &br, &bg, &bb);

            int fg_same = (fr == (unsigned char)pfr &&
                           fg == (unsigned char)pfg &&
                           fb == (unsigned char)pfb);
            int bg_same = (br == (unsigned char)pbr &&
                           bg == (unsigned char)pbg &&
                           bb == (unsigned char)pbb);

            if (fg_same && bg_same) { /* char only */ }
            else if (fg_same) p = tio_gfx__hb_set_bg(p, br, bg, bb);
            else if (bg_same) p = tio_gfx__hb_set_fg(p, fr, fg, fb);
            else              p = tio_gfx__hb_set_fg_bg(p, fr, fg, fb, br, bg, bb);

            for (int cx = 0; cx < ux; cx++) {
                p[0] = (char)0xE2; p[1] = (char)0x96; p[2] = (char)0x80; p += 3;
            }
            pfr = fr; pfg = fg; pfb = fb;
            pbr = br; pbg = bg; pbb = bb;
        }

        p = tio_gfx__hb_reset(p);
        int is_last = (!emit_last_crlf && k == num_terminal_rows - 1);
        if (!is_last) { p[0] = '\r'; p[1] = '\n'; p += 2; }
    }
    return p;
}

static char* tio_gfx__hb_payload_216(tio_gfx_halfblock_ctx* ctx,
                                       char* p,
                                       const void* pixels,
                                       tio_gfx_pixel_fmt fmt,
                                       int emit_last_crlf) {
    const int w  = ctx->_params.width;
    const int h  = ctx->_params.height;
    const int ux = ctx->_params.upscale_x > 0 ? ctx->_params.upscale_x : 1;
    const int uy = ctx->_params.upscale_y > 0 ? ctx->_params.upscale_y : 1;
    const int oy = ctx->_params.dither_offset_y;
    const int ox = ctx->_params.dither_offset_x;
    const int num_terminal_rows = h * uy / 2;

    for (int k = 0; k < num_terminal_rows; k++) {
        int top = (2 * k)     / uy;
        int bot = (2 * k + 1) / uy;
        if (bot >= h) bot = h - 1;

        int pfg_idx = -1, pbg_idx = -1;

        for (int x = 0; x < w; x++) {
            unsigned char fr, fg, fb, br, bg, bb;
            tio_gfx__hb_read_pixel(pixels, fmt, top, x, w, &fr, &fg, &fb);
            tio_gfx__hb_read_pixel(pixels, fmt, bot, x, w, &br, &bg, &bb);

            unsigned char tbayer = BAYER_PATTERN_16X16[(top + oy) % 16][(x + ox) % 16];
            unsigned char bbayer = BAYER_PATTERN_16X16[(bot + oy) % 16][(x + ox) % 16];

            int fgi = 16 + 36 * tio_gfx__hb_quantize_xterm(fr, tbayer)
                         +  6 * tio_gfx__hb_quantize_xterm(fg, tbayer)
                         +      tio_gfx__hb_quantize_xterm(fb, tbayer);
            int bgi = 16 + 36 * tio_gfx__hb_quantize_xterm(br, bbayer)
                         +  6 * tio_gfx__hb_quantize_xterm(bg, bbayer)
                         +      tio_gfx__hb_quantize_xterm(bb, bbayer);

            int fg_same = (fgi == pfg_idx);
            int bg_same = (bgi == pbg_idx);

            if (fg_same && bg_same) { /* char only */ }
            else if (fg_same)  p = tio_gfx__hb_set_bg5(p, bgi);
            else if (bg_same)  p = tio_gfx__hb_set_fg5(p, fgi);
            else               p = tio_gfx__hb_set_fg5_bg5(p, fgi, bgi);

            for (int cx = 0; cx < ux; cx++) {
                p[0] = (char)0xE2; p[1] = (char)0x96; p[2] = (char)0x80; p += 3;
            }
            pfg_idx = fgi; pbg_idx = bgi;
        }

        p = tio_gfx__hb_reset(p);
        int is_last = (!emit_last_crlf && k == num_terminal_rows - 1);
        if (!is_last) { p[0] = '\r'; p[1] = '\n'; p += 2; }
    }
    return p;
}

/* ── Generate ────────────────────────────────────────────────────────────── */
int tio_gfx_halfblock_generate(tio_gfx_halfblock_ctx* ctx,
                                const void* pixels,
                                tio_gfx_pixel_fmt fmt,
                                int parts,
                                char* out_buf, size_t out_cap) {
    monotonic_timer_t _t; timer_start(&_t);
    char* p = out_buf;
    (void)out_cap;

    if (parts & TIO_GFX_HEADER) {
        memcpy(p, "\x1b[H", 3); p += 3;
    }

    if (parts & TIO_GFX_PAYLOAD) {
        int emit_last_crlf = !(parts & TIO_GFX_FOOTER);
        if (ctx->_params.color_mode == TIO_GFX_HALFBLOCK_COLOR_216)
            p = tio_gfx__hb_payload_216(ctx, p, pixels, fmt, emit_last_crlf);
        else
            p = tio_gfx__hb_payload_24bit(ctx, p, pixels, fmt, emit_last_crlf);
    }

    if (parts & TIO_GFX_FOOTER) {
        p = tio_gfx__hb_reset(p);
        *p = '\0';
    }

    ctx->total_generate_ms += timer_elapsed_ms(&_t);
    return (int)(p - out_buf);
}

/* ── Stats ───────────────────────────────────────────────────────────────── */
void tio_gfx_halfblock_reset_stats(tio_gfx_halfblock_ctx* ctx) {
    ctx->total_generate_ms = 0.0;
}

void tio_gfx_halfblock_print_stats(const tio_gfx_halfblock_ctx* ctx) {
    fprintf(stderr,
            "[tio_gfx_halfblock] generate total: %.2f ms\n",
            ctx->total_generate_ms);
}

#endif /* TIO_GFX_HALFBLOCK_IMPLEMENTATION */

#endif /* TIO_GFX_HALFBLOCK_H */
