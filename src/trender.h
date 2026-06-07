#pragma once

#include "rendering_avx2_u16_mt.h"
#define TIO_GFX_USE_AVX2
#define TIO_GFX_IMPLEMENTATION
#include "tio_gfx.h"
#include "timer.h"
#include "mesh_loading.h"
#include "tio.h"
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
	rendering_ctx_t*   render_ctx;   /* [num_render_ctx]                          */
	tio_gfx_ctx*       gfx_ctx;      /* flat [num_render_ctx * buffer_count]      */
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

/*
 * Layout: gfx_ctx[i * buffer_count + b]  (thread-major, buffer-minor)
 * This keeps buffer slots for the same thread contiguous, so init_shared
 * can share scratch across buffer slots with a single pointer range.
 */
static inline tio_gfx_ctx* gfx_ctx_at(trender_ctx_t* ctx, int b, int i) {
	return &ctx->gfx_ctx[i * ctx->buffer_count + b];
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
	int num_threads, int buffer_count, tio_gfx_backend display_mode,
	tio_gfx_halfblock_color_mode hb_color_mode, int cell_height_px,
	tio_gfx_iterm_encode_fmt iterm_encode_fmt,
	int iterm_jpeg_quality, int iterm_png_compression_level,
	int upscale_x, int upscale_y, int cell_width_px) {
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

	if (num_threads == 1) {
		/* Single-thread: one gfx context, no locks needed. */
		ctx->gfx_ctx      = (tio_gfx_ctx*)malloc(sizeof(tio_gfx_ctx));
		if (!ctx->gfx_ctx) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_ctx)\n"); return -1; }
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
		tio_gfx_init(gfx_ctx_at(ctx, 0, 0), gp);
	} else {
		/* Multi-thread: one ctx per (thread, buffer_slot) pair.
		 * Layout is thread-major so init_shared can share scratch across
		 * buffer slots of the same thread. */
		ctx->gfx_ctx      = (tio_gfx_ctx*)malloc(
		                        ctx->num_render_ctx * buffer_count * sizeof(tio_gfx_ctx));
		if (!ctx->gfx_ctx) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_ctx)\n"); return -1; }
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
				gp.p.kitty.starty_rows    = starty * upscale_y / (cell_height_px > 0 ? cell_height_px : 1);
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
			tio_gfx_init_shared(
			    ctx->gfx_ctx + (i - 1) * buffer_count,
			    buffer_count, gp);
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
static inline void trender_generate_frame(trender_ctx_t* ctx, mesh_t* mesh,
	render_params_t new_params, int thread_id, int release_old_lock) {
	if (ctx->num_threads == 1) {
		render_params_copy(&ctx->render_ctx[0].params, &new_params);
		render_mesh(mesh, &ctx->render_ctx[0]);
	} else if (thread_id >= 1) {
#pragma omp critical
		{
			render_params_copy(&ctx->render_ctx[thread_id - 1].params, &new_params);
		}
		render_mesh(mesh, &ctx->render_ctx[thread_id - 1]);

		tio_gfx_ctx* sc = gfx_ctx_at(ctx, ctx->back[thread_id - 1], thread_id - 1);
		int parts;
		if (ctx->display_mode == TIO_GFX_BACKEND_ITERM) {
			/* Each strip is a self-contained sequence — must have all three parts. */
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

		tio_gfx_generate(sc,
		    ctx->render_ctx[thread_id - 1].output_buffer.data,
		    TIO_GFX_FMT_RGB565, parts);

		int old_back = ctx->back[thread_id - 1];
		ctx->back[thread_id - 1] = (old_back + 1) % ctx->buffer_count;
		set_lock_with_debug(buffer_lock_at(ctx, ctx->back[thread_id - 1], thread_id - 1),
			thread_id, ctx->back[thread_id - 1], thread_id - 1);
		if (release_old_lock) {
			unset_lock_with_debug(buffer_lock_at(ctx, old_back, thread_id - 1),
				thread_id, old_back, thread_id - 1);
		}
	}
}

/* Called from thread_id == 0. Updates ctx->display_time and ctx->total_display_time. */
static inline void trender_display_frame(trender_ctx_t* ctx, tio_ctx_t* tio) {
	const int nt = ctx->num_threads;
	const int bc = ctx->buffer_count;
	ctx->display_time = 0.0;

	if (nt == 1) {
		tio_gfx_generate(gfx_ctx_at(ctx, ctx->back[0], 0),
		    ctx->render_ctx[0].output_buffer.data,
		    TIO_GFX_FMT_RGB565, TIO_GFX_FULL);
		timer_start(&ctx->timer);
		tio_gfx_ctx* fc = gfx_ctx_at(ctx, ctx->front[0], 0);
		tio_write(tio, tio_gfx_data(fc), tio_gfx_data_size(fc));
		ctx->display_time += timer_elapsed_ms(&ctx->timer);
	} else if (nt == 2) {
		timer_start(&ctx->timer);
		tio_gfx_ctx* fc = gfx_ctx_at(ctx, ctx->front[0], 0);
		tio_write(tio, tio_gfx_data(fc), tio_gfx_data_size(fc));
		ctx->display_time += timer_elapsed_ms(&ctx->timer);
		int old_front = ctx->front[0];
		ctx->front[0] = (old_front + 1) % bc;
		set_lock_with_debug(buffer_lock_at(ctx, ctx->front[0], 0), 0, ctx->front[0], 0);
		unset_lock_with_debug(buffer_lock_at(ctx, old_front, 0), 0, old_front, 0);
	} else {
		/* nt >= 3: each strip carries its own header/payload/footer portion.
		 * Strip 0 has HEADER|PAYLOAD, strips 1..last-1 have PAYLOAD,
		 * strip last has PAYLOAD|FOOTER. Write them in order. */
		for (int i = 0; i < ctx->num_render_ctx; i++) {
			timer_start(&ctx->timer);
			tio_gfx_ctx* fc = gfx_ctx_at(ctx, ctx->front[i], i);
			tio_write(tio, tio_gfx_data(fc), tio_gfx_data_size(fc));
			ctx->display_time += timer_elapsed_ms(&ctx->timer);
			int old_front = ctx->front[i];
			ctx->front[i] = (old_front + 1) % bc;
			set_lock_with_debug(buffer_lock_at(ctx, ctx->front[i], i), 0, ctx->front[i], i);
			unset_lock_with_debug(buffer_lock_at(ctx, old_front, i), 0, old_front, i);
		}
	}
	ctx->total_display_time += ctx->display_time;
}

static inline void trender_print_stats(trender_ctx_t* ctx, double whole_time) {
	double total_clear_time            = ctx->render_ctx[0].total_clear_time;
	double total_rasterisation_time    = ctx->render_ctx[0].total_rasterisation_time;
	double total_texture_sampling_time = ctx->render_ctx[0].total_texture_sampling_time;
	double total_generate_ms           = tio_gfx_total_ms(gfx_ctx_at(ctx, 0, 0));
	for (int i = 1; i < ctx->num_render_ctx; i++) {
		total_clear_time            += ctx->render_ctx[i].total_clear_time;
		total_rasterisation_time    += ctx->render_ctx[i].total_rasterisation_time;
		total_texture_sampling_time += ctx->render_ctx[i].total_texture_sampling_time;
	}
	for (int b = 0; b < ctx->buffer_count; b++) {
		for (int i = 0; i < ctx->num_render_ctx; i++) {
			if (b == 0 && i == 0) continue;
			total_generate_ms += tio_gfx_total_ms(gfx_ctx_at(ctx, b, i));
		}
	}
	double total_frame_gen_time =
		total_clear_time + total_rasterisation_time + total_texture_sampling_time +
		total_generate_ms;
	double unaccounted_time = whole_time - total_frame_gen_time - ctx->total_display_time;
	printf("total_clear_time:            %0.2f\r\n", total_clear_time);
	printf("total_rasterisation_time:    %0.2f\r\n", total_rasterisation_time);
	printf("total_texture_sampling_time: %0.2f\r\n", total_texture_sampling_time);
	printf("total_generate_ms:           %0.2f\r\n", total_generate_ms);
	printf("total_frame_gen_time:        %0.2f\r\n", total_frame_gen_time);
	printf("total_display_time:          %0.2f\r\n", ctx->total_display_time);
	printf("Total time:                  %0.2f ms\r\n", whole_time);
	printf("unaccounted_time:            %0.2f\r\n", unaccounted_time);
}
