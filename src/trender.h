#pragma once

#include "rendering_avx2_u16_mt.h"
#define TIO_GFX_USE_AVX2
#define TIO_GFX_IMPLEMENTATION
#include "tio_gfx.h"
#include "args.h"
#include "timer.h"
#include "mesh_loading.h"
#include "tio.h"
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
	rendering_ctx_t*   render_ctx;   /* [num_render_ctx]                          */
	tio_gfx_ctx*       gfx_ctx;      /* [num_render_ctx]                          */
	char**             gfx_out;      /* flat [num_render_ctx * buffer_count]      */
	size_t*            gfx_out_size; /* flat [num_render_ctx * buffer_count]      */
	size_t*            gfx_out_cap;  /* [num_render_ctx]                          */
	omp_lock_t*        buffer_locks; /* flat [buffer_count * num_render_ctx]      */
	int*               front;        /* [num_render_ctx]                          */
	int*               back;         /* [num_render_ctx]                          */
	int num_threads;
	int buffer_count;
	int num_render_ctx;  /* max(1, num_threads - 1) */
	int rows, cols;
	tio_gfx_backend display_mode;
	monotonic_timer_t timer;
	double display_time;
	double total_display_time;
} trender_ctx_t;

static inline tio_gfx_ctx* gfx_ctx_at(trender_ctx_t* ctx, int i) {
	return &ctx->gfx_ctx[i];
}
static inline char* gfx_out_at(trender_ctx_t* ctx, int b, int i) {
	return ctx->gfx_out[i * ctx->buffer_count + b];
}
static inline size_t* gfx_out_size_at(trender_ctx_t* ctx, int b, int i) {
	return &ctx->gfx_out_size[i * ctx->buffer_count + b];
}
static inline omp_lock_t* buffer_lock_at(trender_ctx_t* ctx, int b, int i) {
	return &ctx->buffer_locks[b * ctx->num_render_ctx + i];
}

static inline void set_lock_with_debug(omp_lock_t* lock, int thread_id, int buffer_id1, int buffer_id2) {
	(void)thread_id; (void)buffer_id1; (void)buffer_id2;
	omp_set_lock(lock);
}

static inline void unset_lock_with_debug(omp_lock_t* lock, int thread_id, int buffer_id1, int buffer_id2) {
	(void)thread_id; (void)buffer_id1; (void)buffer_id2;
	omp_unset_lock(lock);
}

static inline int trender_ctx_init(trender_ctx_t* ctx, int rows, int cols,
	const cli_args_t* args) {
	int num_threads                          = args->threads;
	int buffer_count                         = args->buffers;
	tio_gfx_backend display_mode            = args->display_mode;
	tio_gfx_halfblock_color_mode hb_color_mode = args->hb_color_mode;
	int cell_height_px                       = args->cell_height_px;
	tio_gfx_iterm_encode_fmt iterm_encode_fmt = args->iterm_encode_fmt;
	int iterm_jpeg_quality                   = args->iterm_jpeg_quality;
	int iterm_png_compression_level          = args->iterm_png_compression;
	int upscale_x                            = args->upscale_x;
	int upscale_y                            = args->upscale_y;
	int cell_width_px                        = args->cell_width_px;
	if (num_threads == 1) {
		buffer_count = 1;
	} else if (buffer_count < 3) {
		fprintf(stderr, "trender_ctx_init: buffer_count must be >= 3 for multi-threaded rendering\r\n");
		return -1;
	}
	ctx->rows             = rows;
	ctx->cols             = cols;
	ctx->num_threads      = num_threads;
	ctx->buffer_count     = buffer_count;
	ctx->num_render_ctx   = (num_threads == 1) ? 1 : (num_threads - 1);
	ctx->display_mode     = display_mode;
	ctx->display_time     = 0.0;
	ctx->total_display_time = 0.0;

	/* Strip boundary alignment per backend. */
	int strip_align;
	if (display_mode == TIO_GFX_BACKEND_SIXEL)
	    strip_align = 6;
	else if (display_mode == TIO_GFX_BACKEND_HALFBLOCK)
	    strip_align = 2;
	else if (display_mode == TIO_GFX_BACKEND_ITERM)
	    strip_align = cell_height_px > 0 ? cell_height_px : 1;
	else if (display_mode == TIO_GFX_BACKEND_KITTY)
	    strip_align = cell_height_px > 0 ? cell_height_px : 1;
	else {
	    fprintf(stderr, "trender_ctx_init: unknown display_mode %d\n", (int)display_mode);
	    return -1;
	}

	ctx->render_ctx = (rendering_ctx_t*)malloc(ctx->num_render_ctx * sizeof(rendering_ctx_t));
	if (!ctx->render_ctx) { fprintf(stderr, "trender_ctx_init: out of memory (render_ctx)\n"); return -1; }

	ctx->gfx_ctx = (tio_gfx_ctx*)malloc(ctx->num_render_ctx * sizeof(tio_gfx_ctx));
	if (!ctx->gfx_ctx) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_ctx)\n"); return -1; }

	ctx->gfx_out = (char**)malloc(ctx->num_render_ctx * buffer_count * sizeof(char*));
	if (!ctx->gfx_out) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_out)\n"); return -1; }

	ctx->gfx_out_size = (size_t*)malloc(ctx->num_render_ctx * buffer_count * sizeof(size_t));
	if (!ctx->gfx_out_size) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_out_size)\n"); return -1; }

	ctx->gfx_out_cap = (size_t*)malloc(ctx->num_render_ctx * sizeof(size_t));
	if (!ctx->gfx_out_cap) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_out_cap)\n"); return -1; }

	if (num_threads == 1) {
		ctx->buffer_locks = NULL;
		ctx->front        = (int*)malloc(sizeof(int));
		if (!ctx->front) { fprintf(stderr, "trender_ctx_init: out of memory (front)\n"); return -1; }
		ctx->back         = (int*)malloc(sizeof(int));
		if (!ctx->back)  { fprintf(stderr, "trender_ctx_init: out of memory (back)\n");  return -1; }
		ctx->front[0]     = 0;
		ctx->back[0]      = 0;

		ctx->render_ctx[0] = create_rendering_ctx(cols, rows, 0, rows);
		tio_gfx_params gp;
		if (display_mode == TIO_GFX_BACKEND_SIXEL) {
			gp = TIO_GFX_SIXEL_PARAMS(cols, rows);
			gp.p.sixel.scale_x = upscale_x;
			gp.p.sixel.scale_y = upscale_y;
		} else if (display_mode == TIO_GFX_BACKEND_ITERM) {
			gp = TIO_GFX_ITERM_PARAMS(cols, rows);
			gp.p.iterm.encode_fmt            = iterm_encode_fmt;
			gp.p.iterm.jpeg_quality          = iterm_jpeg_quality;
			gp.p.iterm.png_compression_level = iterm_png_compression_level;
			gp.p.iterm.upscale_x             = upscale_x;
			gp.p.iterm.upscale_y             = upscale_y;
		} else if (display_mode == TIO_GFX_BACKEND_KITTY) {
			gp = TIO_GFX_KITTY_PARAMS(cols, rows);
			gp.p.kitty.upscale_x      = upscale_x;
			gp.p.kitty.upscale_y      = upscale_y;
			gp.p.kitty.cell_height_px = cell_height_px;
			gp.p.kitty.cell_width_px  = cell_width_px;
		} else if (display_mode == TIO_GFX_BACKEND_HALFBLOCK) {
			gp = TIO_GFX_HALFBLOCK_PARAMS(cols, rows);
			gp.p.halfblock.color_mode = hb_color_mode;
			gp.p.halfblock.upscale_x  = upscale_x;
			gp.p.halfblock.upscale_y  = upscale_y;
		} else {
			fprintf(stderr, "trender_ctx_init: unknown display_mode %d\n", (int)display_mode);
			return -1;
		}
		tio_gfx_init(gfx_ctx_at(ctx, 0), gp);
		size_t out_cap = tio_gfx_output_size_hint(gp);
		ctx->gfx_out_cap[0]  = out_cap;
		ctx->gfx_out[0]      = (char*)malloc(out_cap);
		ctx->gfx_out_size[0] = 0;
		if (!ctx->gfx_out[0]) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_out[0])\n"); return -1; }
	} else {
		ctx->buffer_locks = (omp_lock_t*)malloc(
		                        buffer_count * ctx->num_render_ctx * sizeof(omp_lock_t));
		if (!ctx->buffer_locks) { fprintf(stderr, "trender_ctx_init: out of memory (buffer_locks)\n"); return -1; }
		ctx->front        = (int*)malloc(ctx->num_render_ctx * sizeof(int));
		if (!ctx->front) { fprintf(stderr, "trender_ctx_init: out of memory (front)\n"); return -1; }
		ctx->back         = (int*)malloc(ctx->num_render_ctx * sizeof(int));
		if (!ctx->back)  { fprintf(stderr, "trender_ctx_init: out of memory (back)\n");  return -1; }
		for (int i = 0; i < ctx->num_render_ctx; i++) {
			ctx->front[i] = 0;
			ctx->back[i]  = 0;
		}

		for (int i = 1; i < num_threads; i++) {
			int starty = (rows * (i - 1)) / (num_threads - 1);
			int endy   = (rows * ((i - 1) + 1)) / (num_threads - 1);
			starty -= starty % strip_align;
			endy   -= endy   % strip_align;
			int rows_per_thread = endy - starty;
			ctx->render_ctx[i - 1] = create_rendering_ctx(cols, rows, starty, endy);

			tio_gfx_params gp;
			if (display_mode == TIO_GFX_BACKEND_SIXEL) {
				gp = TIO_GFX_SIXEL_PARAMS(cols, rows_per_thread);
				gp.p.sixel.scale_x         = upscale_x;
				gp.p.sixel.scale_y         = upscale_y;
				gp.p.sixel.dither_offset_y = starty;
			} else if (display_mode == TIO_GFX_BACKEND_ITERM) {
				gp = TIO_GFX_ITERM_PARAMS(cols, rows_per_thread);
				gp.p.iterm.encode_fmt            = iterm_encode_fmt;
				gp.p.iterm.jpeg_quality          = iterm_jpeg_quality;
				gp.p.iterm.png_compression_level = iterm_png_compression_level;
				gp.p.iterm.upscale_x             = upscale_x;
				gp.p.iterm.upscale_y             = upscale_y;
				gp.p.iterm.starty_rows    = starty * upscale_y / (cell_height_px > 0 ? cell_height_px : 1);
				gp.p.iterm.cell_height_px = cell_height_px;
			} else if (display_mode == TIO_GFX_BACKEND_KITTY) {
				gp = TIO_GFX_KITTY_PARAMS(cols, rows_per_thread);
				gp.p.kitty.upscale_x      = upscale_x;
				gp.p.kitty.upscale_y      = upscale_y;
				gp.p.kitty.cell_height_px = cell_height_px;
				gp.p.kitty.cell_width_px  = cell_width_px;
				gp.p.kitty.full_height    = (i == 1) ? rows : 0;
			} else if (display_mode == TIO_GFX_BACKEND_HALFBLOCK) {
				gp = TIO_GFX_HALFBLOCK_PARAMS(cols, rows_per_thread);
				gp.p.halfblock.color_mode      = hb_color_mode;
				gp.p.halfblock.upscale_x       = upscale_x;
				gp.p.halfblock.upscale_y       = upscale_y;
				gp.p.halfblock.dither_offset_y = starty;
			} else {
				fprintf(stderr, "trender_ctx_init: unknown display_mode %d\n", (int)display_mode);
				return -1;
			}
			tio_gfx_init(gfx_ctx_at(ctx, i - 1), gp);
			size_t out_cap = tio_gfx_output_size_hint(gp);
			ctx->gfx_out_cap[i - 1] = out_cap;
			for (int b = 0; b < buffer_count; b++) {
				ctx->gfx_out[(i - 1) * buffer_count + b]      = (char*)malloc(out_cap);
				ctx->gfx_out_size[(i - 1) * buffer_count + b] = 0;
				if (!ctx->gfx_out[(i - 1) * buffer_count + b]) {
					fprintf(stderr, "trender_ctx_init: out of memory (gfx_out[%d][%d])\n", i - 1, b);
					return -1;
				}
			}
		}

		for (int b = 0; b < buffer_count; b++) {
			for (int i = 0; i < ctx->num_render_ctx; i++) {
				omp_init_lock(buffer_lock_at(ctx, b, i));
			}
		}
	}
	return 0;
}

/* release_old_lock=0 for the warmup frame (no previous lock held), 1 for all in-loop frames. */
static inline void trender_generate_frame(trender_ctx_t* ctx, mesh_t* mesh, int thread_id, int release_old_lock) {
	if (ctx->num_threads == 1) {
		render_mesh(mesh, &ctx->render_ctx[0]);
	} else if (thread_id >= 1) {
		render_mesh(mesh, &ctx->render_ctx[thread_id - 1]);

		int ri = thread_id - 1;
		int parts;
		if (ctx->display_mode == TIO_GFX_BACKEND_ITERM) {
			parts = TIO_GFX_FULL;
		} else if (ctx->display_mode == TIO_GFX_BACKEND_KITTY ||
		           ctx->display_mode == TIO_GFX_BACKEND_SIXEL ||
		           ctx->display_mode == TIO_GFX_BACKEND_HALFBLOCK) {
			parts = TIO_GFX_PAYLOAD;
			if (thread_id == 1)                   parts |= TIO_GFX_HEADER;
			if (thread_id == ctx->num_render_ctx) parts |= TIO_GFX_FOOTER;
		} else {
			fprintf(stderr, "trender_generate_frame: unknown display_mode %d\n", (int)ctx->display_mode);
			abort();
		}

		int back = ctx->back[ri];
		*gfx_out_size_at(ctx, back, ri) = (size_t)tio_gfx_generate(
		    gfx_ctx_at(ctx, ri),
		    ctx->render_ctx[ri].output_buffer.data,
		    TIO_GFX_FMT_RGB565, parts,
		    gfx_out_at(ctx, back, ri), ctx->gfx_out_cap[ri]);

		int old_back = ctx->back[ri];
		ctx->back[ri] = (old_back + 1) % ctx->buffer_count;
		set_lock_with_debug(buffer_lock_at(ctx, ctx->back[ri], ri),
			thread_id, ctx->back[ri], ri);
		if (release_old_lock) {
			unset_lock_with_debug(buffer_lock_at(ctx, old_back, ri),
				thread_id, old_back, ri);
		}
	}
}

/* Called from thread_id == 0. Updates ctx->display_time and ctx->total_display_time. */
static inline void trender_display_frame(trender_ctx_t* ctx, tio_ctx_t* tio) {
	const int nt = ctx->num_threads;
	const int bc = ctx->buffer_count;
	ctx->display_time = 0.0;

	if (nt == 1) {
		*gfx_out_size_at(ctx, 0, 0) = (size_t)tio_gfx_generate(
		    gfx_ctx_at(ctx, 0),
		    ctx->render_ctx[0].output_buffer.data,
		    TIO_GFX_FMT_RGB565, TIO_GFX_FULL,
		    gfx_out_at(ctx, 0, 0), ctx->gfx_out_cap[0]);
		timer_start(&ctx->timer);
		tio_write(tio, gfx_out_at(ctx, ctx->front[0], 0),
		               *gfx_out_size_at(ctx, ctx->front[0], 0));
		ctx->display_time += timer_elapsed_ms(&ctx->timer);
	} else if (nt == 2) {
		timer_start(&ctx->timer);
		tio_write(tio, gfx_out_at(ctx, ctx->front[0], 0),
		               *gfx_out_size_at(ctx, ctx->front[0], 0));
		ctx->display_time += timer_elapsed_ms(&ctx->timer);
		int old_front = ctx->front[0];
		ctx->front[0] = (old_front + 1) % bc;
		set_lock_with_debug(buffer_lock_at(ctx, ctx->front[0], 0), 0, ctx->front[0], 0);
		unset_lock_with_debug(buffer_lock_at(ctx, old_front, 0), 0, old_front, 0);
	} else {
		for (int i = 0; i < ctx->num_render_ctx; i++) {
			timer_start(&ctx->timer);
			tio_write(tio, gfx_out_at(ctx, ctx->front[i], i),
			               *gfx_out_size_at(ctx, ctx->front[i], i));
			ctx->display_time += timer_elapsed_ms(&ctx->timer);
			int old_front = ctx->front[i];
			ctx->front[i] = (old_front + 1) % bc;
			set_lock_with_debug(buffer_lock_at(ctx, ctx->front[i], i), 0, ctx->front[i], i);
			unset_lock_with_debug(buffer_lock_at(ctx, old_front, i), 0, old_front, i);
		}
	}
	ctx->total_display_time += ctx->display_time;
}

static inline void trender_print_stats(trender_ctx_t* ctx, int num_frames) {
	const double inv_f = (num_frames > 0) ? 1.0 / num_frames : 0.0;
	const int n = ctx->num_render_ctx;

	/* ── Per-thread table ────────────────────────────────────────────────── */
	printf("Per-thread totals (ms):\r\n");
	printf("  Thread  %-12s %-12s %-12s %-12s %-12s\r\n",
	       "Clear", "Raster", "Texture", "Generate", "Thread-total");
	double total_clear_time            = 0.0;
	double total_rasterisation_time    = 0.0;
	double total_texture_sampling_time = 0.0;
	double total_generate_ms           = 0.0;
	for (int i = 0; i < n; i++) {
		double cl  = ctx->render_ctx[i].total_clear_time;
		double ra  = ctx->render_ctx[i].total_rasterisation_time;
		double tx  = ctx->render_ctx[i].total_texture_sampling_time;
		double gen = tio_gfx_total_ms(gfx_ctx_at(ctx, i));
		double thr = cl + ra + tx + gen;
		printf("  %-7d %-12.2f %-12.2f %-12.2f %-12.2f %-12.2f\r\n",
		       i + 1, cl, ra, tx, gen, thr);
		total_clear_time            += cl;
		total_rasterisation_time    += ra;
		total_texture_sampling_time += tx;
		total_generate_ms           += gen;
	}

	printf("Per-thread averages (ms/frame):\r\n");
	printf("  Thread  %-12s %-12s %-12s %-12s %-12s\r\n",
	       "Clear", "Raster", "Texture", "Generate", "Thread-total");
	for (int i = 0; i < n; i++) {
		double cl  = ctx->render_ctx[i].total_clear_time;
		double ra  = ctx->render_ctx[i].total_rasterisation_time;
		double tx  = ctx->render_ctx[i].total_texture_sampling_time;
		double gen = tio_gfx_total_ms(gfx_ctx_at(ctx, i));
		printf("  %-7d %-12.3f %-12.3f %-12.3f %-12.3f %-12.3f\r\n",
		       i + 1, cl * inv_f, ra * inv_f, tx * inv_f, gen * inv_f,
		       (cl + ra + tx + gen) * inv_f);
	}

	/* ── Overall totals ──────────────────────────────────────────────────── */
	double total_frame_gen_time = total_clear_time + total_rasterisation_time +
	                              total_texture_sampling_time + total_generate_ms;
	printf("Overall totals (ms):\r\n");
	printf("  clear time:            %0.2f\r\n", total_clear_time);
	printf("  rasterisation time:    %0.2f\r\n", total_rasterisation_time);
	printf("  texture_sampling time: %0.2f\r\n", total_texture_sampling_time);
	printf("  generate_ms:           %0.2f\r\n", total_generate_ms);
	printf("  frame_gen time:        %0.2f\r\n", total_frame_gen_time);
	printf("  display time:          %0.2f\r\n", ctx->total_display_time);
	printf("Overall averages (ms/frame):\r\n");
	printf("  clear time:            %0.3f\r\n", total_clear_time            * inv_f);
	printf("  rasterisation time:    %0.3f\r\n", total_rasterisation_time    * inv_f);
	printf("  texture_sampling time: %0.3f\r\n", total_texture_sampling_time * inv_f);
	printf("  generate_ms:           %0.3f\r\n", total_generate_ms           * inv_f);
	printf("  frame_gen time:        %0.3f\r\n", total_frame_gen_time        * inv_f);
	printf("  display time:          %0.3f\r\n", ctx->total_display_time     * inv_f);
}
