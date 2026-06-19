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

/* ── Frame mode ──────────────────────────────────────────────────────────── */
/* kitty deletes an image's placements whenever its id is re-transmitted ("the existing
   image and all its placements must be deleted"), so you cannot update the *currently
   displayed* id without blanking it — and our image spans hundreds of chunks, so the
   blank lasts almost the whole frame. (Animation frames avoid that but leak storage;
   they were the first thing tried.) PINGPONG does what actually works: alternate between
   two image ids. Each frame transmits+displays (a=T) the *idle* id — the previously
   displayed id is untouched and stays on screen during the transmit — then deletes the
   previous id (a=d,d=I, which frees data). Continuous picture, tear-free (atomic a=T),
   and never more than two images in storage. */
#ifndef TIO_GFX_KITTY_FRAME_MODE_DEFINED
#define TIO_GFX_KITTY_FRAME_MODE_DEFINED
typedef enum {
    /* Legacy: fresh transmit-and-display (a=T) every frame, new image id each time. */
    TIO_GFX_KITTY_FRAME_LEGACY = 0,
    /* Ping-pong: a=T to image_id (the idle id) then a=d,d=I of prev_image_id. */
    TIO_GFX_KITTY_FRAME_PINGPONG = 1,
} tio_gfx_kitty_frame_mode;
#endif

/* ── Kitty-specific types ────────────────────────────────────────────────── */
typedef struct {
    int width, height;    /* encoded image pixel dimensions (= render resolution) */
    int full_height;      /* total image height across all strips (0 = use height); header strip only */
    int cell_height_px;   /* terminal cell height in px */
    int cell_width_px;    /* terminal cell width in px */
    int upscale_x;        /* horizontal upscale factor; c = width * upscale_x / cell_width_px */
    int upscale_y;        /* vertical upscale factor;   r = full_height * upscale_y / cell_height_px */
    /* Ping-pong double buffering. frame_mode==LEGACY ⇒ a=T-per-frame, no id. */
    int image_id;         /* this frame's kitty image id (i=), the idle id to draw to */
    int prev_image_id;    /* previous frame's id to delete (a=d,d=I); 0 = none */
    int z_index;          /* placement z (z=); negative = below text. -1 = default */
    int frame_mode;       /* tio_gfx_kitty_frame_mode for this generate */
} tio_gfx_kitty_params;

#define TIO_GFX_KITTY_DEFAULT_PARAMS(w, h) \
    ((tio_gfx_kitty_params){ .width=(w), .height=(h), .full_height=0, \
                              .cell_height_px=1, .cell_width_px=1, \
                              .upscale_x=1, .upscale_y=1, \
                              .image_id=0, .prev_image_id=0, .z_index=-1, \
                              .frame_mode=TIO_GFX_KITTY_FRAME_LEGACY })

typedef struct {
    /* public — read after generate */
    int            width, height;

    /* stats */
    double         total_generate_ms;

    /* private */
    tio_gfx_kitty_params _params;
    unsigned char* _rgb_buf;     /* width*height*3 bytes */
    size_t         _rgb_buf_cap;
} tio_gfx_kitty_ctx;

/* ── Declarations ────────────────────────────────────────────────────────── */
void   tio_gfx_kitty_init(tio_gfx_kitty_ctx* ctx, tio_gfx_kitty_params params);
void   tio_gfx_kitty_destroy(tio_gfx_kitty_ctx* ctx);
void   tio_gfx_kitty_set_params(tio_gfx_kitty_ctx* ctx,
                                 tio_gfx_kitty_params params);
int    tio_gfx_kitty_generate(tio_gfx_kitty_ctx* ctx,
                               const void* pixels,
                               tio_gfx_pixel_fmt fmt,
                               int parts,
                               char* out_buf, size_t out_cap);
size_t tio_gfx_kitty_output_size_hint(tio_gfx_kitty_params params);
/* Build a "delete image" escape sequence (a=d) into out_buf; returns bytes written.
   out_buf must hold at least 32 bytes. Used to free a frame-mode image on exit. */
int    tio_gfx_kitty_delete_image_seq(int image_id, char* out_buf, size_t out_cap);
void   tio_gfx_kitty_reset_stats(tio_gfx_kitty_ctx* ctx);
void   tio_gfx_kitty_print_stats(const tio_gfx_kitty_ctx* ctx);

/* ═══════════════════════════════════════════════════════════════════════════
   IMPLEMENTATION — define TIO_GFX_KITTY_IMPLEMENTATION in exactly one TU
   ═══════════════════════════════════════════════════════════════════════════ */
#ifdef TIO_GFX_KITTY_IMPLEMENTATION

#include "timer.h"

/* ── Integer-to-ASCII ────────────────────────────────────────────────────── */
static char* tio_gfx__kitty_itoa_lt1000(char* p, int n) {
    int h = n / 100;
    *p = (char)('0' + h); p += (h != 0);
    n -= h * 100;
    int t = n / 10;
    *p = (char)('0' + t); p += (h + t != 0);
    *p++ = (char)('0' + n - t * 10);
    return p;
}
static char* tio_gfx__kitty_itoa_lt10000(char* p, int n) {
    int th = n / 1000;
    *p = (char)('0' + th); p += (th != 0);
    n -= th * 1000;
    int h = n / 100;
    *p = (char)('0' + h); p += (th != 0 || h != 0);
    n -= h * 100;
    int t = n / 10;
    *p = (char)('0' + t); p += (th != 0 || h != 0 || t != 0);
    *p++ = (char)('0' + n - t * 10);
    return p;
}

/* Signed int → ASCII, full int range (used for the z-index, which may be < -9999). */
static char* tio_gfx__kitty_itoa_i32(char* p, int n) {
    if (n < 0) { *p++ = '-'; }
    char tmp[12];
    int i = 0;
    unsigned int u = (n < 0) ? (unsigned int)(-(long)n) : (unsigned int)n;
    do { tmp[i++] = (char)('0' + (u % 10)); u /= 10; } while (u);
    while (i) *p++ = tmp[--i];
    return p;
}

/* 4096 base64 chars = 3072 raw bytes per chunk */
#define TIO_GFX__KITTY_CHUNK_BYTES 3072

/* ── Buffer capacity ─────────────────────────────────────────────────────── */
static size_t tio_gfx__kitty_buf_cap(int w, int h) {
    size_t rgb = (size_t)w * h * 3;
    size_t b64 = (rgb + 2) / 3 * 4;
    size_t n_chunks = rgb / TIO_GFX__KITTY_CHUNK_BYTES + 2; /* +2: at least 1 + safety */
    /* 80 = max header per chunk (incl. a wide negative z), 32 = cursor escape,
       96 = footer a=p display + a=d delete (PINGPONG) */
    return b64 + n_chunks * 80 + 32 + 96;
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
    ctx->total_generate_ms = 0.0;
    ctx->_rgb_buf_cap      = tio_gfx__kitty_rgb_buf_cap(params.width, params.height);
    ctx->_rgb_buf          = (unsigned char*)TIO_GFX_MALLOC(ctx->_rgb_buf_cap);
}

void tio_gfx_kitty_destroy(tio_gfx_kitty_ctx* ctx) {
    TIO_GFX_FREE(ctx->_rgb_buf);
    ctx->_rgb_buf     = NULL;
    ctx->_rgb_buf_cap = 0;
}

void tio_gfx_kitty_set_params(tio_gfx_kitty_ctx* ctx,
                                tio_gfx_kitty_params params) {
    ctx->_params = params;
    ctx->width   = params.width;
    ctx->height  = params.height;
}

size_t tio_gfx_kitty_output_size_hint(tio_gfx_kitty_params params) {
    return tio_gfx__kitty_buf_cap(params.width, params.height);
}

int tio_gfx_kitty_delete_image_seq(int image_id, char* out_buf, size_t out_cap) {
    (void)out_cap;
    char* p = out_buf;
    /* d=I (uppercase) deletes by id AND frees the image data; d=i only drops placements. */
    memcpy(p, "\x1b_Ga=d,d=I,i=", 13); p += 13;
    p = tio_gfx__kitty_itoa_lt10000(p, image_id);
    memcpy(p, ",q=2", 4); p += 4;
    *p++ = '\x1b'; *p++ = '\\';
    return (int)(p - out_buf);
}

/* ── Generate ────────────────────────────────────────────────────────────── */
int tio_gfx_kitty_generate(tio_gfx_kitty_ctx* ctx,
                             const void* pixels,
                             tio_gfx_pixel_fmt fmt,
                             int parts,
                             char* out_buf, size_t out_cap) {
    monotonic_timer_t _t; timer_start(&_t);
    (void)out_cap;

    const int w      = ctx->_params.width;
    const int h      = ctx->_params.height;
    const int full_h = (ctx->_params.full_height > 0) ? ctx->_params.full_height : h;
    const int cw     = ctx->_params.cell_width_px  > 0 ? ctx->_params.cell_width_px  : 1;
    const int ch     = ctx->_params.cell_height_px > 0 ? ctx->_params.cell_height_px : 1;
    const int c_cols = w      * ctx->_params.upscale_x / cw;
    const int c_rows = full_h * ctx->_params.upscale_y / ch;
    /* last chunk m=0 only when this strip carries the footer (terminates the image) */
    const int last_m = (parts & TIO_GFX_FOOTER) ? 0 : 1;

    const int id   = ctx->_params.image_id;
    const int prev = ctx->_params.prev_image_id;
    const int mode = ctx->_params.frame_mode;

    char* p = out_buf;

    /* cursor home on the header strip — anchors the placement at the top-left cell. */
    if (parts & TIO_GFX_HEADER) {
        memcpy(p, "\x1b[H", 3); p += 3;
    }

    /* convert pixels to packed RGB */
    tio_gfx__kitty_to_rgb(ctx->_rgb_buf, pixels, fmt, w, h);

    /* emit chunked APC sequences */
    size_t rgb_total = (size_t)w * h * 3;
    size_t offset = 0;
    int emit_init = (parts & TIO_GFX_HEADER); /* emit the action keys on the first chunk of the header strip */
    do {
        size_t chunk = rgb_total - offset;
        if (chunk > TIO_GFX__KITTY_CHUNK_BYTES) chunk = TIO_GFX__KITTY_CHUNK_BYTES;
        int more = (offset + chunk < rgb_total) ? 1 : last_m;
        if (emit_init) {
            if (mode == TIO_GFX_KITTY_FRAME_PINGPONG) {
                /* transmit ONLY (a=t) to the fresh idle id: no placement is created, so
                   kitty cannot display a partially-received image. The footer issues the
                   a=p display once the final chunk (m=0) has landed. */
                memcpy(p, "\x1b_Ga=t,i=", 9); p += 9;
                p = tio_gfx__kitty_itoa_lt10000(p, id);
                memcpy(p, ",f=24,s=", 8); p += 8;
                p = tio_gfx__kitty_itoa_lt10000(p, w);
                memcpy(p, ",v=", 3); p += 3;
                p = tio_gfx__kitty_itoa_lt10000(p, full_h);
                memcpy(p, ",m=", 3); p += 3;
                *p++ = (char)('0' + more);
                memcpy(p, ",q=2;", 5); p += 5;
            } else {
                /* LEGACY: transmit-and-display a fresh image every frame. */
                memcpy(p, "\x1b_Ga=T,f=24,s=", 14); p += 14;
                p = tio_gfx__kitty_itoa_lt10000(p, w);
                memcpy(p, ",v=", 3); p += 3;
                p = tio_gfx__kitty_itoa_lt10000(p, full_h);
                memcpy(p, ",c=", 3); p += 3;
                p = tio_gfx__kitty_itoa_lt1000(p, c_cols);
                memcpy(p, ",r=", 3); p += 3;
                p = tio_gfx__kitty_itoa_lt1000(p, c_rows);
                memcpy(p, ",C=1,m=", 7); p += 7;
                *p++ = (char)('0' + more);
                memcpy(p, ",q=2,z=", 7); p += 7;
                p = tio_gfx__kitty_itoa_i32(p, ctx->_params.z_index);
                *p++ = ';';
            }
            emit_init = 0;
        } else {
            memcpy(p, "\x1b_Gm=0,q=2;", 11);
            p[5] = (char)('0' + more);
            p += 11;
        }
        p += tio_gfx__kitty_base64_encode(p, ctx->_rgb_buf + offset, chunk);
        *p++ = '\x1b'; *p++ = '\\';
        offset += chunk;
    } while (offset < rgb_total);

    /* footer strip in PINGPONG mode: the full image has now been received, so display it
       (a=p) — the placement appears atomically with complete pixels — then free the old
       image from a few frames back (d=I deletes by id AND frees the data; lowercase d=i
       would only drop the placement). */
    if ((parts & TIO_GFX_FOOTER) && mode == TIO_GFX_KITTY_FRAME_PINGPONG) {
        memcpy(p, "\x1b_Ga=p,i=", 9); p += 9;
        p = tio_gfx__kitty_itoa_lt10000(p, id);
        memcpy(p, ",c=", 3); p += 3;
        p = tio_gfx__kitty_itoa_lt1000(p, c_cols);
        memcpy(p, ",r=", 3); p += 3;
        p = tio_gfx__kitty_itoa_lt1000(p, c_rows);
        memcpy(p, ",C=1,z=", 7); p += 7;
        p = tio_gfx__kitty_itoa_i32(p, ctx->_params.z_index);
        memcpy(p, ",q=2", 4); p += 4;
        *p++ = '\x1b'; *p++ = '\\';

        if (prev != 0) {
            memcpy(p, "\x1b_Ga=d,d=I,i=", 13); p += 13;
            p = tio_gfx__kitty_itoa_lt10000(p, prev);
            memcpy(p, ",q=2", 4); p += 4;
            *p++ = '\x1b'; *p++ = '\\';
        }
    }

    ctx->total_generate_ms += timer_elapsed_ms(&_t);
    return (int)(p - out_buf);
}

/* ── Stats ───────────────────────────────────────────────────────────────── */
void tio_gfx_kitty_reset_stats(tio_gfx_kitty_ctx* ctx) {
    ctx->total_generate_ms = 0.0;
}

void tio_gfx_kitty_print_stats(const tio_gfx_kitty_ctx* ctx) {
    fprintf(stderr,
            "[tio_gfx_kitty] generate total: %.2f ms\n",
            ctx->total_generate_ms);
}

#endif /* TIO_GFX_KITTY_IMPLEMENTATION */
#endif /* TIO_GFX_KITTY_H */
