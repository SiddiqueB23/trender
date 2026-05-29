#pragma once

#define NUM_THREADS 2
#define BUFFER_COUNT 3

#include "rendering_avx2_u16_mt.h"
#include "sixel_display.h"
#include "timer.h"
#include "mesh_loading.h"
#include "tio.h"
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
	rendering_ctx_t render_ctx[NUM_THREADS];
	sixel_display_ctx sixel_ctx[BUFFER_COUNT][NUM_THREADS];
	omp_lock_t buffer_locks[BUFFER_COUNT][NUM_THREADS];
	int front[NUM_THREADS];
	int back[NUM_THREADS];
	int rows, cols;
	monotonic_timer_t timer;
	double display_time;
	double total_display_time;
} trender_ctx_t;

static inline void set_lock_with_debug(omp_lock_t* lock, int thread_id, int buffer_id1, int buffer_id2) {
	// printf("\x1b[33mThread %d waiting for lock on buffer %d %d\x1b[0m\r\n", thread_id, buffer_id1, buffer_id2);
	omp_set_lock(lock);
	// printf("\x1b[32mThread %d acquired lock on buffer %d %d\x1b[0m\r\n", thread_id, buffer_id1, buffer_id2);
}

static inline void unset_lock_with_debug(omp_lock_t* lock, int thread_id, int buffer_id1, int buffer_id2) {
	// printf("\x1b[31mThread %d releasing lock on buffer %d %d\x1b[0m\r\n", thread_id, buffer_id1, buffer_id2);
	omp_unset_lock(lock);
}

static inline void trender_ctx_init(trender_ctx_t* ctx, int rows, int cols) {
	ctx->rows = rows;
	ctx->cols = cols;
	ctx->display_time = 0.0;
	ctx->total_display_time = 0.0;
	for (int i = 0; i < NUM_THREADS; i++) {
		ctx->front[i] = 0;
		ctx->back[i] = 0;
	}
	if (NUM_THREADS == 1) {
		ctx->render_ctx[0] = create_rendering_ctx(cols, rows, 0, rows);
		sixel_indexed_bitmap* bitmap = (sixel_indexed_bitmap*)malloc(sizeof(sixel_indexed_bitmap));
		init_sixel_indexed_bitmap(bitmap, cols, rows);
		init_sixel_palette_rgbuniform(&bitmap->palette, 5);
		for (int b = 0; b < BUFFER_COUNT; b++) {
			init_sixel_display_ctx(&ctx->sixel_ctx[b][0], cols, rows);
			ctx->sixel_ctx[b][0].bitmap = bitmap;
		}
	} else {
		for (int i = 1; i < NUM_THREADS; i++) {
			int starty = (rows * (i - 1)) / (NUM_THREADS - 1);
			int endy   = (rows * ((i - 1) + 1)) / (NUM_THREADS - 1);
			starty -= starty % 6;
			endy   -= endy   % 6;
			int rows_per_thread = endy - starty;
			ctx->render_ctx[i - 1] = create_rendering_ctx(cols, rows, starty, endy);
			sixel_indexed_bitmap* bitmap = (sixel_indexed_bitmap*)malloc(sizeof(sixel_indexed_bitmap));
			init_sixel_indexed_bitmap(bitmap, cols, rows_per_thread);
			init_sixel_palette_rgbuniform(&bitmap->palette, 5);
			for (int b = 0; b < BUFFER_COUNT; b++) {
				init_sixel_display_ctx(&ctx->sixel_ctx[b][i - 1], cols, rows_per_thread);
				ctx->sixel_ctx[b][i - 1].bitmap = bitmap;
			}
		}
	}
	for (int b = 0; b < BUFFER_COUNT; b++) {
		for (int i = 0; i < NUM_THREADS - 1; i++) {
			omp_init_lock(&ctx->buffer_locks[b][i]);
		}
	}
}

// release_old_lock=0 for the warmup frame (no previous lock held), 1 for all in-loop frames.
static inline void trender_generate_frame(trender_ctx_t* ctx, mesh_t* mesh,
	render_params_t new_params, int thread_id, int release_old_lock) {
	if (NUM_THREADS == 1) {
		render_params_copy(&ctx->render_ctx[0].params, &new_params);
		render_mesh(mesh, &ctx->render_ctx[0]);
	} else if (thread_id >= 1) {
#pragma omp critical
		{
			render_params_copy(&ctx->render_ctx[thread_id - 1].params, &new_params);
		}
		render_mesh(mesh, &ctx->render_ctx[thread_id - 1]);
		convert_5r6g5b_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering_216colors_avx2(
			&ctx->sixel_ctx[ctx->back[thread_id - 1]][thread_id - 1],
			ctx->render_ctx[thread_id - 1].output_buffer);
		generate_sixel_display_data(&ctx->sixel_ctx[ctx->back[thread_id - 1]][thread_id - 1]);
		int old_back = ctx->back[thread_id - 1];
		ctx->back[thread_id - 1] = (old_back + 1) % BUFFER_COUNT;
		set_lock_with_debug(&ctx->buffer_locks[ctx->back[thread_id - 1]][thread_id - 1],
			thread_id, ctx->back[thread_id - 1], thread_id - 1);
		if (release_old_lock) {
			unset_lock_with_debug(&ctx->buffer_locks[old_back][thread_id - 1],
				thread_id, old_back, thread_id - 1);
		}
	}
}

// Called from thread_id == 0. Handles the single- and multi-threaded display paths.
// Updates ctx->display_time and ctx->total_display_time.
static inline void trender_display_frame(trender_ctx_t* ctx, tio_ctx_t* tio) {
	ctx->display_time = 0.0;
	if (NUM_THREADS == 1) {
		convert_5r6g5b_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering_216colors_avx2(
			&ctx->sixel_ctx[ctx->back[0]][0], ctx->render_ctx[0].output_buffer);
		generate_sixel_display_data(&ctx->sixel_ctx[ctx->back[0]][0]);
		timer_start(&ctx->timer);
		tio_write(tio, ctx->sixel_ctx[ctx->front[0]][0].data, ctx->sixel_ctx[ctx->front[0]][0].data_size);
		ctx->display_time += timer_elapsed_ms(&ctx->timer);
	}
	if (NUM_THREADS == 2) {
		timer_start(&ctx->timer);
		tio_write(tio, ctx->sixel_ctx[ctx->front[0]][0].data, ctx->sixel_ctx[ctx->front[0]][0].data_size);
		ctx->display_time += timer_elapsed_ms(&ctx->timer);
		int old_front = ctx->front[0];
		ctx->front[0] = (old_front + 1) % BUFFER_COUNT;
		set_lock_with_debug(&ctx->buffer_locks[ctx->front[0]][0], 0, ctx->front[0], 0);
		unset_lock_with_debug(&ctx->buffer_locks[old_front][0], 0, old_front, 0);
	} else if (NUM_THREADS >= 3) {
		int footer_len = 2;
		timer_start(&ctx->timer);
		tio_write(tio, ctx->sixel_ctx[ctx->front[0]][0].data, ctx->sixel_ctx[ctx->front[0]][0].data_size - footer_len);
		ctx->display_time += timer_elapsed_ms(&ctx->timer);
		int old_front = ctx->front[0];
		ctx->front[0] = (old_front + 1) % BUFFER_COUNT;
		set_lock_with_debug(&ctx->buffer_locks[ctx->front[0]][0], 0, ctx->front[0], 0);
		unset_lock_with_debug(&ctx->buffer_locks[old_front][0], 0, old_front, 0);
		for (int i = 1; i < NUM_THREADS - 2; i++) {
			timer_start(&ctx->timer);
			tio_write(tio, ctx->sixel_ctx[ctx->front[i]][i].sixel_data, ctx->sixel_ctx[ctx->front[i]][i].sixel_data_size - footer_len);
			ctx->display_time += timer_elapsed_ms(&ctx->timer);
			old_front = ctx->front[i];
			ctx->front[i] = (old_front + 1) % BUFFER_COUNT;
			set_lock_with_debug(&ctx->buffer_locks[ctx->front[i]][i], 0, ctx->front[i], i);
			unset_lock_with_debug(&ctx->buffer_locks[old_front][i], 0, old_front, i);
		}
		timer_start(&ctx->timer);
		tio_write(tio, ctx->sixel_ctx[ctx->front[NUM_THREADS - 2]][NUM_THREADS - 2].sixel_data,
			ctx->sixel_ctx[ctx->front[NUM_THREADS - 2]][NUM_THREADS - 2].sixel_data_size);
		ctx->display_time += timer_elapsed_ms(&ctx->timer);
		old_front = ctx->front[NUM_THREADS - 2];
		ctx->front[NUM_THREADS - 2] = (old_front + 1) % BUFFER_COUNT;
		set_lock_with_debug(&ctx->buffer_locks[ctx->front[NUM_THREADS - 2]][NUM_THREADS - 2], 0, ctx->front[NUM_THREADS - 2], NUM_THREADS - 2);
		unset_lock_with_debug(&ctx->buffer_locks[old_front][NUM_THREADS - 2], 0, old_front, NUM_THREADS - 2);
	}
	ctx->total_display_time += ctx->display_time;
}

static inline void trender_print_stats(trender_ctx_t* ctx, double whole_time) {
	double total_clear_time            = ctx->render_ctx[0].total_clear_time;
	double total_rasterisation_time    = ctx->render_ctx[0].total_rasterisation_time;
	double total_texture_sampling_time = ctx->render_ctx[0].total_texture_sampling_time;
	double total_generation_time       = ctx->sixel_ctx[0][0].total_generation_time;
	double total_conversion_time       = ctx->sixel_ctx[0][0].total_conversion_time;
	for (int i = 1; i < NUM_THREADS - 1; i++) {
		total_clear_time            += ctx->render_ctx[i].total_clear_time;
		total_rasterisation_time    += ctx->render_ctx[i].total_rasterisation_time;
		total_texture_sampling_time += ctx->render_ctx[i].total_texture_sampling_time;
	}
	for (int b = 0; b < BUFFER_COUNT; b++) {
		for (int i = 0; i < NUM_THREADS - 1; i++) {
			if (b == 0 && i == 0) continue;
			total_generation_time += ctx->sixel_ctx[b][i].total_generation_time;
			total_conversion_time += ctx->sixel_ctx[b][i].total_conversion_time;
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
	printf("Total time for %d frames:    %0.2f ms\r\n", 500, whole_time);
	printf("unaccounted_time:            %0.2f\r\n", unaccounted_time);
}
