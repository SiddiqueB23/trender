#pragma once

#include "rendering_avx2_u16_mt.h"
#include "sixel_display.h"
#include "timer.h"
#include "mesh_loading.h"
#include "tio.h"
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
	rendering_ctx_t*   render_ctx;   /* [num_render_ctx]                        */
	sixel_display_ctx* sixel_ctx;    /* flat [buffer_count * num_render_ctx]    */
	omp_lock_t*        buffer_locks; /* flat [buffer_count * num_render_ctx]    */
	int*               front;        /* [num_render_ctx]                        */
	int*               back;         /* [num_render_ctx]                        */
	int num_threads;
	int buffer_count;
	int num_render_ctx;  /* max(1, num_threads - 1) */
	int rows, cols;
	monotonic_timer_t timer;
	double display_time;
	double total_display_time;
} trender_ctx_t;

/* 2D accessor helpers — replace the old [b][i] array syntax. */
static inline sixel_display_ctx* sixel_ctx_at(trender_ctx_t* ctx, int b, int i) {
	return &ctx->sixel_ctx[b * ctx->num_render_ctx + i];
}
static inline omp_lock_t* buffer_lock_at(trender_ctx_t* ctx, int b, int i) {
	return &ctx->buffer_locks[b * ctx->num_render_ctx + i];
}

static inline void set_lock_with_debug(omp_lock_t* lock, int thread_id, int buffer_id1, int buffer_id2) {
	(void)thread_id; (void)buffer_id1; (void)buffer_id2;
	// printf("\x1b[33mThread %d waiting for lock on buffer %d %d\x1b[0m\r\n", thread_id, buffer_id1, buffer_id2);
	omp_set_lock(lock);
	// printf("\x1b[32mThread %d acquired lock on buffer %d %d\x1b[0m\r\n", thread_id, buffer_id1, buffer_id2);
}

static inline void unset_lock_with_debug(omp_lock_t* lock, int thread_id, int buffer_id1, int buffer_id2) {
	(void)thread_id; (void)buffer_id1; (void)buffer_id2;
	// printf("\x1b[31mThread %d releasing lock on buffer %d %d\x1b[0m\r\n", thread_id, buffer_id1, buffer_id2);
	omp_unset_lock(lock);
}

static inline int trender_ctx_init(trender_ctx_t* ctx, int rows, int cols,
	int num_threads, int buffer_count) {
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
	ctx->display_time     = 0.0;
	ctx->total_display_time = 0.0;

	ctx->render_ctx = (rendering_ctx_t*)malloc(ctx->num_render_ctx * sizeof(rendering_ctx_t));

	if (num_threads == 1) {
		/* Single-thread: one sixel context, no locks needed. */
		ctx->sixel_ctx    = (sixel_display_ctx*)malloc(sizeof(sixel_display_ctx));
		ctx->buffer_locks = NULL;
		ctx->front        = (int*)malloc(sizeof(int));
		ctx->back         = (int*)malloc(sizeof(int));
		ctx->front[0]     = 0;
		ctx->back[0]      = 0;

		ctx->render_ctx[0] = create_rendering_ctx(cols, rows, 0, rows);
		sixel_indexed_bitmap* bitmap = (sixel_indexed_bitmap*)malloc(sizeof(sixel_indexed_bitmap));
		init_sixel_indexed_bitmap(bitmap, cols, rows);
		init_sixel_palette_rgbuniform(&bitmap->palette, 5);
		init_sixel_display_ctx(sixel_ctx_at(ctx, 0, 0), cols, rows);
		sixel_ctx_at(ctx, 0, 0)->bitmap = bitmap;
		sixel_display_ctx_alloc_scratch(sixel_ctx_at(ctx, 0, 0));
	} else {
		ctx->sixel_ctx    = (sixel_display_ctx*)malloc(buffer_count * ctx->num_render_ctx * sizeof(sixel_display_ctx));
		ctx->buffer_locks = (omp_lock_t*)       malloc(buffer_count * ctx->num_render_ctx * sizeof(omp_lock_t));
		ctx->front        = (int*)malloc(ctx->num_render_ctx * sizeof(int));
		ctx->back         = (int*)malloc(ctx->num_render_ctx * sizeof(int));
		for (int i = 0; i < ctx->num_render_ctx; i++) {
			ctx->front[i] = 0;
			ctx->back[i]  = 0;
		}

		for (int i = 1; i < num_threads; i++) {
			int starty = (rows * (i - 1)) / (num_threads - 1);
			int endy   = (rows * ((i - 1) + 1)) / (num_threads - 1);
			starty -= starty % 6;
			endy   -= endy   % 6;
			int rows_per_thread = endy - starty;
			ctx->render_ctx[i - 1] = create_rendering_ctx(cols, rows, starty, endy);
			sixel_indexed_bitmap* bitmap = (sixel_indexed_bitmap*)malloc(sizeof(sixel_indexed_bitmap));
			init_sixel_indexed_bitmap(bitmap, cols, rows_per_thread);
			init_sixel_palette_rgbuniform(&bitmap->palette, 5);
			for (int b = 0; b < buffer_count; b++) {
				init_sixel_display_ctx(sixel_ctx_at(ctx, b, i - 1), cols, rows_per_thread);
				sixel_ctx_at(ctx, b, i - 1)->bitmap = bitmap;
			}
			sixel_display_ctx_alloc_scratch(sixel_ctx_at(ctx, 0, i - 1));
			for (int b = 1; b < buffer_count; b++) {
				sixel_display_ctx_use_scratch_of(sixel_ctx_at(ctx, b, i - 1), sixel_ctx_at(ctx, 0, i - 1));
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
		convert_5r6g5b_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering_216colors_avx2(
			sixel_ctx_at(ctx, ctx->back[thread_id - 1], thread_id - 1),
			ctx->render_ctx[thread_id - 1].output_buffer);
		generate_sixel_display_data(sixel_ctx_at(ctx, ctx->back[thread_id - 1], thread_id - 1));
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
		convert_5r6g5b_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering_216colors_avx2(
			sixel_ctx_at(ctx, ctx->back[0], 0), ctx->render_ctx[0].output_buffer);
		generate_sixel_display_data(sixel_ctx_at(ctx, ctx->back[0], 0));
		timer_start(&ctx->timer);
		tio_write(tio, sixel_ctx_at(ctx, ctx->front[0], 0)->data,
		               sixel_ctx_at(ctx, ctx->front[0], 0)->data_size);
		ctx->display_time += timer_elapsed_ms(&ctx->timer);
	}
	if (nt == 2) {
		timer_start(&ctx->timer);
		tio_write(tio, sixel_ctx_at(ctx, ctx->front[0], 0)->data,
		               sixel_ctx_at(ctx, ctx->front[0], 0)->data_size);
		ctx->display_time += timer_elapsed_ms(&ctx->timer);
		int old_front = ctx->front[0];
		ctx->front[0] = (old_front + 1) % bc;
		set_lock_with_debug(buffer_lock_at(ctx, ctx->front[0], 0), 0, ctx->front[0], 0);
		unset_lock_with_debug(buffer_lock_at(ctx, old_front, 0), 0, old_front, 0);
	} else if (nt >= 3) {
		int footer_len = 2;
		timer_start(&ctx->timer);
		tio_write(tio, sixel_ctx_at(ctx, ctx->front[0], 0)->data,
		               sixel_ctx_at(ctx, ctx->front[0], 0)->data_size - footer_len);
		ctx->display_time += timer_elapsed_ms(&ctx->timer);
		int old_front = ctx->front[0];
		ctx->front[0] = (old_front + 1) % bc;
		set_lock_with_debug(buffer_lock_at(ctx, ctx->front[0], 0), 0, ctx->front[0], 0);
		unset_lock_with_debug(buffer_lock_at(ctx, old_front, 0), 0, old_front, 0);
		for (int i = 1; i < nt - 2; i++) {
			timer_start(&ctx->timer);
			tio_write(tio, sixel_ctx_at(ctx, ctx->front[i], i)->sixel_data,
			               sixel_ctx_at(ctx, ctx->front[i], i)->sixel_data_size - footer_len);
			ctx->display_time += timer_elapsed_ms(&ctx->timer);
			old_front = ctx->front[i];
			ctx->front[i] = (old_front + 1) % bc;
			set_lock_with_debug(buffer_lock_at(ctx, ctx->front[i], i), 0, ctx->front[i], i);
			unset_lock_with_debug(buffer_lock_at(ctx, old_front, i), 0, old_front, i);
		}
		int last = nt - 2;
		timer_start(&ctx->timer);
		tio_write(tio, sixel_ctx_at(ctx, ctx->front[last], last)->sixel_data,
		               sixel_ctx_at(ctx, ctx->front[last], last)->sixel_data_size);
		ctx->display_time += timer_elapsed_ms(&ctx->timer);
		old_front = ctx->front[last];
		ctx->front[last] = (old_front + 1) % bc;
		set_lock_with_debug(buffer_lock_at(ctx, ctx->front[last], last), 0, ctx->front[last], last);
		unset_lock_with_debug(buffer_lock_at(ctx, old_front, last), 0, old_front, last);
	}
	ctx->total_display_time += ctx->display_time;
}

static inline void trender_print_stats(trender_ctx_t* ctx, double whole_time) {
	double total_clear_time            = ctx->render_ctx[0].total_clear_time;
	double total_rasterisation_time    = ctx->render_ctx[0].total_rasterisation_time;
	double total_texture_sampling_time = ctx->render_ctx[0].total_texture_sampling_time;
	double total_generation_time       = sixel_ctx_at(ctx, 0, 0)->total_generation_time;
	double total_conversion_time       = sixel_ctx_at(ctx, 0, 0)->total_conversion_time;
	for (int i = 1; i < ctx->num_render_ctx; i++) {
		total_clear_time            += ctx->render_ctx[i].total_clear_time;
		total_rasterisation_time    += ctx->render_ctx[i].total_rasterisation_time;
		total_texture_sampling_time += ctx->render_ctx[i].total_texture_sampling_time;
	}
	for (int b = 0; b < ctx->buffer_count; b++) {
		for (int i = 0; i < ctx->num_render_ctx; i++) {
			if (b == 0 && i == 0) continue;
			total_generation_time += sixel_ctx_at(ctx, b, i)->total_generation_time;
			total_conversion_time += sixel_ctx_at(ctx, b, i)->total_conversion_time;
		}
	}
	double total_frame_gen_time =
		total_clear_time + total_rasterisation_time + total_texture_sampling_time +
		total_generation_time + total_conversion_time;
	double unaccounted_time = whole_time - total_frame_gen_time - ctx->total_display_time;
	printf("total_clear_time:            %0.2f\r\n", total_clear_time);
	printf("total_rasterisation_time:    %0.2f\r\n", total_rasterisation_time);
	printf("total_texture_sampling_time: %0.2f\r\n", total_texture_sampling_time);
	printf("total_conversion_time:       %0.2f\r\n", total_conversion_time);
	printf("total_generation_time:       %0.2f\r\n", total_generation_time);
	printf("total_frame_gen_time:        %0.2f\r\n", total_frame_gen_time);
	printf("total_display_time:          %0.2f\r\n", ctx->total_display_time);
	printf("Total time:                  %0.2f ms\r\n", whole_time);
	printf("unaccounted_time:            %0.2f\r\n", unaccounted_time);
}
