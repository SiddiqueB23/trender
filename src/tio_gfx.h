#ifndef TIO_GFX_H
#define TIO_GFX_H

#ifdef TIO_GFX_IMPLEMENTATION
#  define TIO_GFX_SIXEL_IMPLEMENTATION
#  define TIO_GFX_HALFBLOCK_IMPLEMENTATION
#  define TIO_GFX_ITERM_IMPLEMENTATION
#  define TIO_GFX_KITTY_IMPLEMENTATION
#endif

#include "tio_gfx_sixel.h"
#include "tio_gfx_halfblock.h"
#include "tio_gfx_iterm.h"
#include "tio_gfx_kitty.h"

/* ── Backend tag ─────────────────────────────────────────────────────────── */
typedef enum {
    TIO_GFX_BACKEND_SIXEL     = 0,
    TIO_GFX_BACKEND_HALFBLOCK = 1,
    TIO_GFX_BACKEND_ITERM     = 2,
    TIO_GFX_BACKEND_KITTY     = 3,
} tio_gfx_backend;

/* ── Unified params ──────────────────────────────────────────────────────── */
typedef struct {
    tio_gfx_backend backend;
    union {
        tio_gfx_sixel_params     sixel;
        tio_gfx_halfblock_params halfblock;
        tio_gfx_iterm_params     iterm;
        tio_gfx_kitty_params     kitty;
    } p;
} tio_gfx_params;

#define TIO_GFX_SIXEL_PARAMS(w, h) \
    ((tio_gfx_params){ .backend = TIO_GFX_BACKEND_SIXEL, \
                       .p.sixel = TIO_GFX_SIXEL_DEFAULT_PARAMS(w, h) })

#define TIO_GFX_HALFBLOCK_PARAMS(w, h) \
    ((tio_gfx_params){ .backend = TIO_GFX_BACKEND_HALFBLOCK, \
                       .p.halfblock = TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(w, h) })

#define TIO_GFX_ITERM_PARAMS(w, h) \
    ((tio_gfx_params){ .backend = TIO_GFX_BACKEND_ITERM, \
                       .p.iterm = TIO_GFX_ITERM_DEFAULT_PARAMS(w, h) })

#define TIO_GFX_KITTY_PARAMS(w, h) \
    ((tio_gfx_params){ .backend = TIO_GFX_BACKEND_KITTY, \
                       .p.kitty = TIO_GFX_KITTY_DEFAULT_PARAMS(w, h) })

/* ── Unified ctx ─────────────────────────────────────────────────────────── */
typedef struct {
    tio_gfx_backend backend;
    union {
        tio_gfx_sixel_ctx     sixel;
        tio_gfx_halfblock_ctx halfblock;
        tio_gfx_iterm_ctx     iterm;
        tio_gfx_kitty_ctx     kitty;
    } impl;
} tio_gfx_ctx;

/* ── Field accessors ─────────────────────────────────────────────────────── */
static inline char* tio_gfx_data(const tio_gfx_ctx* c) {
    if (c->backend == TIO_GFX_BACKEND_SIXEL)           return c->impl.sixel.data;
    else if (c->backend == TIO_GFX_BACKEND_HALFBLOCK)  return c->impl.halfblock.data;
    else if (c->backend == TIO_GFX_BACKEND_ITERM)      return c->impl.iterm.data;
    else if (c->backend == TIO_GFX_BACKEND_KITTY)      return c->impl.kitty.data;
    else { fprintf(stderr, "tio_gfx_data: unknown backend %d\n", (int)c->backend); abort(); }
}
static inline size_t tio_gfx_data_size(const tio_gfx_ctx* c) {
    if (c->backend == TIO_GFX_BACKEND_SIXEL)           return c->impl.sixel.data_size;
    else if (c->backend == TIO_GFX_BACKEND_HALFBLOCK)  return c->impl.halfblock.data_size;
    else if (c->backend == TIO_GFX_BACKEND_ITERM)      return c->impl.iterm.data_size;
    else if (c->backend == TIO_GFX_BACKEND_KITTY)      return c->impl.kitty.data_size;
    else { fprintf(stderr, "tio_gfx_data_size: unknown backend %d\n", (int)c->backend); abort(); }
}
static inline double tio_gfx_total_ms(const tio_gfx_ctx* c) {
    if (c->backend == TIO_GFX_BACKEND_SIXEL)           return c->impl.sixel.total_generate_ms;
    else if (c->backend == TIO_GFX_BACKEND_HALFBLOCK)  return c->impl.halfblock.total_generate_ms;
    else if (c->backend == TIO_GFX_BACKEND_ITERM)      return c->impl.iterm.total_generate_ms;
    else if (c->backend == TIO_GFX_BACKEND_KITTY)      return c->impl.kitty.total_generate_ms;
    else { fprintf(stderr, "tio_gfx_total_ms: unknown backend %d\n", (int)c->backend); abort(); }
}

/* ── Init / destroy ──────────────────────────────────────────────────────── */
static inline void tio_gfx_init(tio_gfx_ctx* ctx, tio_gfx_params params) {
    ctx->backend = params.backend;
    if (params.backend == TIO_GFX_BACKEND_SIXEL)
        tio_gfx_sixel_init(&ctx->impl.sixel, params.p.sixel);
    else if (params.backend == TIO_GFX_BACKEND_HALFBLOCK)
        tio_gfx_halfblock_init(&ctx->impl.halfblock, params.p.halfblock);
    else if (params.backend == TIO_GFX_BACKEND_ITERM)
        tio_gfx_iterm_init(&ctx->impl.iterm, params.p.iterm);
    else if (params.backend == TIO_GFX_BACKEND_KITTY)
        tio_gfx_kitty_init(&ctx->impl.kitty, params.p.kitty);
    else { fprintf(stderr, "tio_gfx_init: unknown backend %d\n", (int)params.backend); abort(); }
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
    } else if (params.backend == TIO_GFX_BACKEND_ITERM) {
        for (int i = 0; i < n; i++) {
            ctxs[i].backend = TIO_GFX_BACKEND_ITERM;
            tio_gfx_iterm_init(&ctxs[i].impl.iterm, params.p.iterm);
        }
        for (int i = 1; i < n; i++)
            tio_gfx_iterm_use_scratch_of(&ctxs[i].impl.iterm, &ctxs[0].impl.iterm);
    } else if (params.backend == TIO_GFX_BACKEND_KITTY) {
        for (int i = 0; i < n; i++) {
            ctxs[i].backend = TIO_GFX_BACKEND_KITTY;
            tio_gfx_kitty_init(&ctxs[i].impl.kitty, params.p.kitty);
        }
        for (int i = 1; i < n; i++)
            tio_gfx_kitty_use_scratch_of(&ctxs[i].impl.kitty, &ctxs[0].impl.kitty);
    } else { fprintf(stderr, "tio_gfx_init_shared: unknown backend %d\n", (int)params.backend); abort(); }
}

static inline void tio_gfx_destroy(tio_gfx_ctx* ctx) {
    if (ctx->backend == TIO_GFX_BACKEND_SIXEL)
        tio_gfx_sixel_destroy(&ctx->impl.sixel);
    else if (ctx->backend == TIO_GFX_BACKEND_HALFBLOCK)
        tio_gfx_halfblock_destroy(&ctx->impl.halfblock);
    else if (ctx->backend == TIO_GFX_BACKEND_ITERM)
        tio_gfx_iterm_destroy(&ctx->impl.iterm);
    else if (ctx->backend == TIO_GFX_BACKEND_KITTY)
        tio_gfx_kitty_destroy(&ctx->impl.kitty);
    else { fprintf(stderr, "tio_gfx_destroy: unknown backend %d\n", (int)ctx->backend); abort(); }
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
    else if (ctx->backend == TIO_GFX_BACKEND_ITERM && params.backend == TIO_GFX_BACKEND_ITERM)
        tio_gfx_iterm_set_params(&ctx->impl.iterm, params.p.iterm);
    else if (ctx->backend == TIO_GFX_BACKEND_KITTY && params.backend == TIO_GFX_BACKEND_KITTY)
        tio_gfx_kitty_set_params(&ctx->impl.kitty, params.p.kitty);
    else { fprintf(stderr, "tio_gfx_set_params: unknown backend %d\n", (int)ctx->backend); abort(); }
}

/* ── Generate ────────────────────────────────────────────────────────────── */
static inline void tio_gfx_generate(tio_gfx_ctx* ctx, const void* pixels,
                                     tio_gfx_pixel_fmt fmt, int parts) {
    if (ctx->backend == TIO_GFX_BACKEND_SIXEL)
        tio_gfx_sixel_generate(&ctx->impl.sixel, pixels, fmt, parts);
    else if (ctx->backend == TIO_GFX_BACKEND_HALFBLOCK)
        tio_gfx_halfblock_generate(&ctx->impl.halfblock, pixels, fmt, parts);
    else if (ctx->backend == TIO_GFX_BACKEND_ITERM)
        tio_gfx_iterm_generate(&ctx->impl.iterm, pixels, fmt, parts);
    else if (ctx->backend == TIO_GFX_BACKEND_KITTY)
        tio_gfx_kitty_generate(&ctx->impl.kitty, pixels, fmt, parts);
    else { fprintf(stderr, "tio_gfx_generate: unknown backend %d\n", (int)ctx->backend); abort(); }
}

/* ── Stats ───────────────────────────────────────────────────────────────── */
static inline void tio_gfx_reset_stats(tio_gfx_ctx* ctx) {
    if (ctx->backend == TIO_GFX_BACKEND_SIXEL)
        tio_gfx_sixel_reset_stats(&ctx->impl.sixel);
    else if (ctx->backend == TIO_GFX_BACKEND_HALFBLOCK)
        tio_gfx_halfblock_reset_stats(&ctx->impl.halfblock);
    else if (ctx->backend == TIO_GFX_BACKEND_ITERM)
        tio_gfx_iterm_reset_stats(&ctx->impl.iterm);
    else if (ctx->backend == TIO_GFX_BACKEND_KITTY)
        tio_gfx_kitty_reset_stats(&ctx->impl.kitty);
    else { fprintf(stderr, "tio_gfx_reset_stats: unknown backend %d\n", (int)ctx->backend); abort(); }
}

static inline void tio_gfx_print_stats(const tio_gfx_ctx* ctx) {
    if (ctx->backend == TIO_GFX_BACKEND_SIXEL)
        tio_gfx_sixel_print_stats(&ctx->impl.sixel);
    else if (ctx->backend == TIO_GFX_BACKEND_HALFBLOCK)
        tio_gfx_halfblock_print_stats(&ctx->impl.halfblock);
    else if (ctx->backend == TIO_GFX_BACKEND_ITERM)
        tio_gfx_iterm_print_stats(&ctx->impl.iterm);
    else if (ctx->backend == TIO_GFX_BACKEND_KITTY)
        tio_gfx_kitty_print_stats(&ctx->impl.kitty);
    else { fprintf(stderr, "tio_gfx_print_stats: unknown backend %d\n", (int)ctx->backend); abort(); }
}

#endif /* TIO_GFX_H */
