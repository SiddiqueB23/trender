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
	rendering_ctx_t*   render_ctx;   /* [num_threads]      */
	tio_gfx_ctx*       gfx_ctx;      /* [num_threads]      */
	char**             gfx_out;      /* [num_threads]      */
	size_t*            gfx_out_size; /* [num_threads]      */
	size_t*            gfx_out_cap;  /* [num_threads]      */
	int num_threads;
	int rows, cols;
	tio_gfx_backend display_mode;
	monotonic_timer_t timer;
} trender_ctx_t;

static inline int trender_ctx_init(trender_ctx_t* ctx, int rows, int cols,
	const cli_args_t* args) {
	int num_threads                          = args->threads;
	tio_gfx_backend display_mode            = args->display_mode;
	tio_gfx_halfblock_color_mode hb_color_mode = args->hb_color_mode;
	int cell_height_px                       = args->cell_height_px;
	tio_gfx_iterm_encode_fmt iterm_encode_fmt = args->iterm_encode_fmt;
	int iterm_jpeg_quality                   = args->iterm_jpeg_quality;
	int iterm_png_compression_level          = args->iterm_png_compression;
	int upscale_x                            = args->upscale_x;
	int upscale_y                            = args->upscale_y;
	int cell_width_px                        = args->cell_width_px;

	ctx->rows             = rows;
	ctx->cols             = cols;
	ctx->num_threads      = num_threads;
	ctx->display_mode     = display_mode;

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

	ctx->render_ctx = (rendering_ctx_t*)malloc(ctx->num_threads * sizeof(rendering_ctx_t));
	if (!ctx->render_ctx) { fprintf(stderr, "trender_ctx_init: out of memory (render_ctx)\n"); return -1; }
	ctx->gfx_ctx = (tio_gfx_ctx*)malloc(ctx->num_threads * sizeof(tio_gfx_ctx));
	if (!ctx->gfx_ctx) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_ctx)\n"); return -1; }
	ctx->gfx_out = (char**)malloc(ctx->num_threads * sizeof(char*));
	if (!ctx->gfx_out) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_out)\n"); return -1; }
	ctx->gfx_out_size = (size_t*)malloc(ctx->num_threads * sizeof(size_t));
	if (!ctx->gfx_out_size) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_out_size)\n"); return -1; }
	ctx->gfx_out_cap = (size_t*)malloc(ctx->num_threads * sizeof(size_t));
	if (!ctx->gfx_out_cap) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_out_cap)\n"); return -1; }

	tio_gfx_params gp;
	if (display_mode == TIO_GFX_BACKEND_SIXEL) {
		gp.backend = TIO_GFX_BACKEND_SIXEL;
		gp.p.sixel.width = cols;
		gp.p.sixel.palette         = TIO_GFX_SIXEL_PALETTE_216;
		gp.p.sixel.scale_x         = upscale_x;
		gp.p.sixel.scale_y         = upscale_y;
	} else if (display_mode == TIO_GFX_BACKEND_ITERM) {
		gp.backend = TIO_GFX_BACKEND_ITERM;
		gp.p.iterm.width = cols;
		gp.p.iterm.encode_fmt            = iterm_encode_fmt;
		gp.p.iterm.jpeg_quality          = iterm_jpeg_quality;
		gp.p.iterm.png_compression_level = iterm_png_compression_level;
		gp.p.iterm.upscale_x             = upscale_x;
		gp.p.iterm.upscale_y             = upscale_y;
		gp.p.iterm.cell_height_px = cell_height_px;
	} else if (display_mode == TIO_GFX_BACKEND_KITTY) {
		gp.backend = TIO_GFX_BACKEND_KITTY;
		gp.p.kitty.width = cols;
		gp.p.kitty.upscale_x      = upscale_x;
		gp.p.kitty.upscale_y      = upscale_y;
		gp.p.kitty.cell_height_px = cell_height_px;
		gp.p.kitty.cell_width_px  = cell_width_px;
	} else if (display_mode == TIO_GFX_BACKEND_HALFBLOCK) {
		gp.backend = TIO_GFX_BACKEND_HALFBLOCK;
		gp.p.halfblock.width = cols;
		gp.p.halfblock.color_mode      = hb_color_mode;
		gp.p.halfblock.upscale_x       = upscale_x;
		gp.p.halfblock.upscale_y       = upscale_y;
	} else {
		fprintf(stderr, "trender_ctx_init: unknown display_mode %d\n", (int)display_mode);
		return -1;
	}

	for (int i = 0; i < num_threads; i++) {
		int starty = (rows * i) / num_threads;
		int endy   = (rows * (i + 1)) / num_threads;
		starty -= starty % strip_align;
		endy   -= endy   % strip_align;
		int rows_per_thread = endy - starty;
		if(args->verbose)
			printf("Thread %d: rows %d-%d (count %d)\r\n", i, starty, endy, rows_per_thread);

		ctx->render_ctx[i] = create_rendering_ctx(cols, rows, starty, endy);

		if (display_mode == TIO_GFX_BACKEND_SIXEL) {
			gp.p.sixel.height = rows_per_thread;
			gp.p.sixel.dither_offset_y = starty;
		} else if (display_mode == TIO_GFX_BACKEND_ITERM) {
			gp.p.iterm.height = rows_per_thread;
			gp.p.iterm.starty_rows    = starty * upscale_y / (cell_height_px > 0 ? cell_height_px : 1);
			gp.p.iterm.cell_height_px = cell_height_px;
		} else if (display_mode == TIO_GFX_BACKEND_KITTY) {
			gp.p.kitty.height = rows_per_thread;
			gp.p.kitty.full_height    = (i == 0) ? rows : 0;
		} else if (display_mode == TIO_GFX_BACKEND_HALFBLOCK) {
			gp.p.halfblock.height = rows_per_thread;
			gp.p.halfblock.dither_offset_y = starty;
		} else {
			fprintf(stderr, "trender_ctx_init: unknown display_mode %d\n", (int)display_mode);
			return -1;
		}
		tio_gfx_init(&ctx->gfx_ctx[i], gp);
		size_t out_cap = tio_gfx_output_size_hint(gp);
		ctx->gfx_out_cap[i]  = out_cap;
		ctx->gfx_out[i]      = (char*)malloc(out_cap);
		ctx->gfx_out_size[i] = 0;
		if (!ctx->gfx_out[i]) {
			fprintf(stderr, "trender_ctx_init: out of memory (gfx_out[%d])\n", i);
			return -1;
		}
	}
	return 0;
}

static inline void trender_generate_frame(trender_ctx_t* ctx, mesh_t* mesh, int thread_id) {
	render_mesh(mesh, &ctx->render_ctx[thread_id]);

	int parts;
	if (ctx->display_mode == TIO_GFX_BACKEND_ITERM) {
		parts = TIO_GFX_FULL;
	} else if (	ctx->display_mode == TIO_GFX_BACKEND_KITTY ||
				ctx->display_mode == TIO_GFX_BACKEND_SIXEL ||
				ctx->display_mode == TIO_GFX_BACKEND_HALFBLOCK) {
		parts = TIO_GFX_PAYLOAD;
		if (thread_id == 0)                   	parts |= TIO_GFX_HEADER;
		if (thread_id == ctx->num_threads - 1) 	parts |= TIO_GFX_FOOTER;
	} else {
		fprintf(stderr, "trender_generate_frame: unknown display_mode %d\n", (int)ctx->display_mode);
		abort();
	}

	ctx->gfx_out_size[thread_id] = (size_t)tio_gfx_generate(
		&ctx->gfx_ctx[thread_id],
		ctx->render_ctx[thread_id].output_buffer.data,
		TIO_GFX_FMT_RGB565, parts,
		ctx->gfx_out[thread_id], ctx->gfx_out_cap[thread_id]);
}

static inline void trender_print_stats(trender_ctx_t* ctx, int num_frames) {
	const double inv_f = (num_frames > 0) ? 1.0 / num_frames : 0.0;
	const int n = ctx->num_threads;

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
		double gen = tio_gfx_total_ms(&ctx->gfx_ctx[i]);
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
		double gen = tio_gfx_total_ms(&ctx->gfx_ctx[i]);
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
	printf("Overall averages (ms/frame):\r\n");
	printf("  clear time:            %0.3f\r\n", total_clear_time            * inv_f);
	printf("  rasterisation time:    %0.3f\r\n", total_rasterisation_time    * inv_f);
	printf("  texture_sampling time: %0.3f\r\n", total_texture_sampling_time * inv_f);
	printf("  generate_ms:           %0.3f\r\n", total_generate_ms           * inv_f);
	printf("  frame_gen time:        %0.3f\r\n", total_frame_gen_time        * inv_f);
}
