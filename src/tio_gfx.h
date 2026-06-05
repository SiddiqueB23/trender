#ifndef TIO_GFX_H
#define TIO_GFX_H

#ifdef TIO_GFX_IMPLEMENTATION
#  define TIO_GFX_SIXEL_IMPLEMENTATION
#  define TIO_GFX_HALFBLOCK_IMPLEMENTATION
#endif

#include "tio_gfx_sixel.h"
#include "tio_gfx_halfblock.h"

/* ── Backend tag ─────────────────────────────────────────────────────────── */
typedef enum {
    TIO_GFX_BACKEND_SIXEL     = 0,
    TIO_GFX_BACKEND_HALFBLOCK = 1,
} tio_gfx_backend;

/* ── Unified params ──────────────────────────────────────────────────────── */
typedef struct {
    tio_gfx_backend backend;
    union {
        tio_gfx_sixel_params    sixel;
        tio_gfx_halfblock_params halfblock;
    } p;
} tio_gfx_params;

#define TIO_GFX_SIXEL_PARAMS(w, h) \
    ((tio_gfx_params){ .backend = TIO_GFX_BACKEND_SIXEL, \
                       .p.sixel = TIO_GFX_SIXEL_DEFAULT_PARAMS(w, h) })

#define TIO_GFX_HALFBLOCK_PARAMS(w, h) \
    ((tio_gfx_params){ .backend = TIO_GFX_BACKEND_HALFBLOCK, \
                       .p.halfblock = TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(w, h) })

/* ── Unified ctx ─────────────────────────────────────────────────────────── */
typedef struct {
    tio_gfx_backend backend;
    union {
        tio_gfx_sixel_ctx    sixel;
        tio_gfx_halfblock_ctx halfblock;
    } impl;
} tio_gfx_ctx;

/* ── Field accessors ─────────────────────────────────────────────────────── */
static inline char* tio_gfx_data(const tio_gfx_ctx* c) {
    return c->backend == TIO_GFX_BACKEND_SIXEL
        ? c->impl.sixel.data : c->impl.halfblock.data;
}
static inline size_t tio_gfx_data_size(const tio_gfx_ctx* c) {
    return c->backend == TIO_GFX_BACKEND_SIXEL
        ? c->impl.sixel.data_size : c->impl.halfblock.data_size;
}
static inline double tio_gfx_total_ms(const tio_gfx_ctx* c) {
    return c->backend == TIO_GFX_BACKEND_SIXEL
        ? c->impl.sixel.total_generate_ms : c->impl.halfblock.total_generate_ms;
}

/* ── Init / destroy ──────────────────────────────────────────────────────── */
static inline void tio_gfx_init(tio_gfx_ctx* ctx, tio_gfx_params params) {
    ctx->backend = params.backend;
    if (params.backend == TIO_GFX_BACKEND_SIXEL)
        tio_gfx_sixel_init(&ctx->impl.sixel, params.p.sixel);
    else if (params.backend == TIO_GFX_BACKEND_HALFBLOCK)
        tio_gfx_halfblock_init(&ctx->impl.halfblock, params.p.halfblock);
}

static inline void tio_gfx_init_shared(tio_gfx_ctx* ctxs, int n,
                                        tio_gfx_params params) {
    if (params.backend == TIO_GFX_BACKEND_SIXEL) {
        for (int i = 0; i < n; i++) {
            ctxs[i].backend = TIO_GFX_BACKEND_SIXEL;
            tio_gfx_sixel_init(&ctxs[i].impl.sixel, params.p.sixel);
        }
        for (int i = 1; i < n; i++)
            tio_gfx_sixel_use_scratch_of(&ctxs[i].impl.sixel, &ctxs[0].impl.sixel);
    } else if (params.backend == TIO_GFX_BACKEND_HALFBLOCK) {
        for (int i = 0; i < n; i++) {
            ctxs[i].backend = TIO_GFX_BACKEND_HALFBLOCK;
            tio_gfx_halfblock_init(&ctxs[i].impl.halfblock, params.p.halfblock);
        }
    }
}

static inline void tio_gfx_destroy(tio_gfx_ctx* ctx) {
    if (ctx->backend == TIO_GFX_BACKEND_SIXEL)
        tio_gfx_sixel_destroy(&ctx->impl.sixel);
    else if (ctx->backend == TIO_GFX_BACKEND_HALFBLOCK)
        tio_gfx_halfblock_destroy(&ctx->impl.halfblock);
}

static inline void tio_gfx_destroy_shared(tio_gfx_ctx* ctxs, int n) {
    for (int i = 0; i < n; i++)
        tio_gfx_destroy(&ctxs[i]);
}

/* ── set_params ──────────────────────────────────────────────────────────── */
static inline void tio_gfx_set_params(tio_gfx_ctx* ctx, tio_gfx_params params) {
    if (ctx->backend == TIO_GFX_BACKEND_SIXEL && params.backend == TIO_GFX_BACKEND_SIXEL)
        tio_gfx_sixel_set_params(&ctx->impl.sixel, params.p.sixel);
    else if (ctx->backend == TIO_GFX_BACKEND_HALFBLOCK && params.backend == TIO_GFX_BACKEND_HALFBLOCK)
        tio_gfx_halfblock_set_params(&ctx->impl.halfblock, params.p.halfblock);
}

/* ── Generate ────────────────────────────────────────────────────────────── */
static inline void tio_gfx_generate(tio_gfx_ctx* ctx, const void* pixels,
                                     tio_gfx_pixel_fmt fmt, int parts) {
    if (ctx->backend == TIO_GFX_BACKEND_SIXEL)
        tio_gfx_sixel_generate(&ctx->impl.sixel, pixels, fmt, parts);
    else if (ctx->backend == TIO_GFX_BACKEND_HALFBLOCK)
        tio_gfx_halfblock_generate(&ctx->impl.halfblock, pixels, fmt, parts);
}

/* ── Stats ───────────────────────────────────────────────────────────────── */
static inline void tio_gfx_reset_stats(tio_gfx_ctx* ctx) {
    if (ctx->backend == TIO_GFX_BACKEND_SIXEL)
        tio_gfx_sixel_reset_stats(&ctx->impl.sixel);
    else if (ctx->backend == TIO_GFX_BACKEND_HALFBLOCK)
        tio_gfx_halfblock_reset_stats(&ctx->impl.halfblock);
}

static inline void tio_gfx_print_stats(const tio_gfx_ctx* ctx) {
    if (ctx->backend == TIO_GFX_BACKEND_SIXEL)
        tio_gfx_sixel_print_stats(&ctx->impl.sixel);
    else if (ctx->backend == TIO_GFX_BACKEND_HALFBLOCK)
        tio_gfx_halfblock_print_stats(&ctx->impl.halfblock);
}

#endif /* TIO_GFX_H */
