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
static inline int tio_gfx_generate(tio_gfx_ctx* ctx, const void* pixels,
                                    tio_gfx_pixel_fmt fmt, int parts,
                                    char* out_buf, size_t out_cap) {
    if (ctx->backend == TIO_GFX_BACKEND_SIXEL)
        return tio_gfx_sixel_generate(&ctx->impl.sixel, pixels, fmt, parts, out_buf, out_cap);
    else if (ctx->backend == TIO_GFX_BACKEND_HALFBLOCK)
        return tio_gfx_halfblock_generate(&ctx->impl.halfblock, pixels, fmt, parts, out_buf, out_cap);
    else if (ctx->backend == TIO_GFX_BACKEND_ITERM)
        return tio_gfx_iterm_generate(&ctx->impl.iterm, pixels, fmt, parts, out_buf, out_cap);
    else if (ctx->backend == TIO_GFX_BACKEND_KITTY)
        return tio_gfx_kitty_generate(&ctx->impl.kitty, pixels, fmt, parts, out_buf, out_cap);
    else { fprintf(stderr, "tio_gfx_generate: unknown backend %d\n", (int)ctx->backend); abort(); }
}

static inline size_t tio_gfx_output_size_hint(tio_gfx_params params) {
    if (params.backend == TIO_GFX_BACKEND_SIXEL)
        return tio_gfx_sixel_output_size_hint(params.p.sixel);
    else if (params.backend == TIO_GFX_BACKEND_HALFBLOCK)
        return tio_gfx_halfblock_output_size_hint(params.p.halfblock);
    else if (params.backend == TIO_GFX_BACKEND_ITERM)
        return tio_gfx_iterm_output_size_hint(params.p.iterm);
    else if (params.backend == TIO_GFX_BACKEND_KITTY)
        return tio_gfx_kitty_output_size_hint(params.p.kitty);
    else { fprintf(stderr, "tio_gfx_output_size_hint: unknown backend %d\n", (int)params.backend); abort(); }
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
