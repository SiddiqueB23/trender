#pragma once

#include "rendering_avx2_u16_mt2.h"
#define TIO_GFX_USE_AVX2
#define TIO_GFX_IMPLEMENTATION
#include "tio_gfx.h"
#include "args.h"
#include "timer.h"
#include "mesh_loading.h"
#include "tio.h"
#define  THREAD_IMPLEMENTATION
#include "thread.h"
#define MPMC_QUEUE_IMPLEMENTATION
#include "mpmc_queue.h"
#include "trender_input.h"

#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

enum task_type {
	PROCESS_PRIMITIVES,
    RENDER_FRAME,
    DISPLAY_FRAME,
    EXIT_LOOP,
};

typedef struct {
    int type;
    int frame;
    int chunk;
    render_params_t* render_params_ptr;
} task_t;

typedef struct {
    /* configuration */
    int num_buffers;
    int num_chunks;
    int num_workers;
    int num_frames;
    int queue_capacity;

	bool*          	primitive_chunk_procesed; /* [num_chunks] */
    /* completion state — flat [num_buffers*2 × num_chunks] */
    bool*           rendering_completed;
    bool*           display_completed;
    thread_mutex_t  state_mutex;

	/* input state */
	input_state_t input_state;
    /* render params — [num_buffers * 2] */
    render_params_t* render_params_buf;
	
    /* queues + backing buffers — [queue_capacity] each */
    mpmc_queue_t  parallel_work_queue;
    task_t*       parallel_work_queue_buf;
    mpmc_queue_t  display_queue;
    task_t*       display_queue_buf;

    /* scheduler cursors */
    int frame_to_be_displayed;
    int chunk_to_be_displayed;
} scheduler_ctx_t;

void scheduler_ctx_init(scheduler_ctx_t* sched,
                        int num_buffers, int num_chunks,
                        int num_workers, int num_frames,
                        int queue_capacity) {
    sched->num_buffers            = num_buffers;
    sched->num_chunks             = num_chunks;
    sched->num_workers            = num_workers;
    sched->num_frames             = num_frames;
    sched->queue_capacity         = queue_capacity;
    sched->frame_to_be_displayed  = 0;
    sched->chunk_to_be_displayed  = 0;

    int nb2 = num_buffers * 2;
    sched->rendering_completed       = (bool*)calloc((size_t)(nb2 * num_chunks), sizeof(bool));
    sched->display_completed         = (bool*)calloc((size_t)(nb2 * num_chunks), sizeof(bool));
    sched->primitive_chunk_procesed = (bool*)calloc((size_t)(num_chunks), sizeof(bool));
    sched->render_params_buf         = (render_params_t*)calloc((size_t)nb2, sizeof(render_params_t));
    thread_mutex_init(&sched->state_mutex);
	
    sched->parallel_work_queue_buf = (task_t*)malloc((size_t)queue_capacity * sizeof(task_t));
    sched->display_queue_buf       = (task_t*)malloc((size_t)queue_capacity * sizeof(task_t));
    mpmc_queue_init(&sched->parallel_work_queue, queue_capacity, sizeof(task_t), sched->parallel_work_queue_buf);
    mpmc_queue_init(&sched->display_queue,       queue_capacity, sizeof(task_t), sched->display_queue_buf);

    /* mark back half of display slots as already done (frames that haven't existed yet) */
    for (int i = num_buffers; i < nb2; i++)
        for (int j = 0; j < num_chunks; j++)
            sched->display_completed[i * num_chunks + j] = true;
}

void scheduler_ctx_destroy(scheduler_ctx_t* sched) {
    free(sched->rendering_completed);
    free(sched->display_completed);
    free(sched->render_params_buf);
    free(sched->parallel_work_queue_buf);
    free(sched->display_queue_buf);
    thread_mutex_term(&sched->state_mutex);
}

void print_task(task_t task) {
    printf("TASK: %d %d %d\n", task.type, task.frame, task.chunk);
}

void print_state(scheduler_ctx_t* sched) {
    int nb2 = sched->num_buffers * 2;
    printf("R: ");
    for (int i = 0; i < nb2; i++) {
        for (int j = 0; j < sched->num_chunks; j++)
            printf("%d", sched->rendering_completed[i * sched->num_chunks + j]);
        printf(" ");
    }
    printf("\n");
    printf("D: ");
    for (int i = 0; i < nb2; i++) {
        for (int j = 0; j < sched->num_chunks; j++)
            printf("%d", sched->display_completed[i * sched->num_chunks + j]);
        printf(" ");
    }
    printf("\n");
}

void emit_exit_tasks(scheduler_ctx_t* sched) {
    task_t task = { .type = EXIT_LOOP, .frame = 0, .chunk = 0, .render_params_ptr = NULL };
    for (int i = 0; i < sched->num_workers; i++)
        mpmc_queue_produce(&sched->parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
    mpmc_queue_produce(&sched->display_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
}

bool frame_rasterisable(scheduler_ctx_t* sched) {
	bool primitive_chunk_processed = true;
	for (int i = 0; i < sched->num_chunks; i++)
        primitive_chunk_processed  = primitive_chunk_processed  && sched->primitive_chunk_procesed[i];
    return primitive_chunk_processed;
}

void reset_rasterisable(scheduler_ctx_t* sched) {
	for (int i = 0; i < sched->num_chunks; i++)
		sched->primitive_chunk_procesed[i] = false;
}

void reset_queuable(scheduler_ctx_t* sched, int frame) {
    int nb2                    = sched->num_buffers * 2;
    int frame_minus_one        = (frame + nb2 - 1) % nb2;
    int frame_minus_num_bufs   = (frame + sched->num_buffers) % nb2;
    for (int i = 0; i < sched->num_chunks; i++)
        sched->rendering_completed[frame_minus_one * sched->num_chunks + i] = false;
    for (int i = 0; i < sched->num_chunks; i++)
        sched->display_completed[frame_minus_num_bufs * sched->num_chunks + i] = false;
}

bool frame_queueable(scheduler_ctx_t* sched, int frame) {
    int nb2                    = sched->num_buffers * 2;
    int frame_minus_one        = (frame + nb2 - 1) % nb2;
    int frame_minus_num_bufs   = (frame + sched->num_buffers) % nb2;
    bool rendered_all = true, displayed_all = true;
    for (int i = 0; i < sched->num_chunks; i++)
        rendered_all  = rendered_all  && sched->rendering_completed[frame_minus_one * sched->num_chunks + i];
    for (int i = 0; i < sched->num_chunks; i++)
        displayed_all = displayed_all && sched->display_completed[frame_minus_num_bufs * sched->num_chunks + i];
    return rendered_all && displayed_all;
}

void run_scheduler(scheduler_ctx_t* sched, task_t completed_task) {
    thread_mutex_lock(&sched->state_mutex);
    int chunk  = completed_task.chunk;
    int frame  = completed_task.frame;
    int nb2    = sched->num_buffers * 2;
    int buffer = frame % nb2;

	if (completed_task.type == PROCESS_PRIMITIVES) {
		sched->primitive_chunk_procesed[chunk] = true;
		if(frame_rasterisable(sched)) {
			reset_rasterisable(sched);
			for (int i = 0; i < sched->num_chunks; i++) {
				task_t task = {
					.type = RENDER_FRAME,
					.frame = frame,
					.chunk = i,
					.render_params_ptr = NULL,
				};
				mpmc_queue_produce(&sched->parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
			}
		}
	}
    if (completed_task.type == RENDER_FRAME) {
        sched->rendering_completed[buffer * sched->num_chunks + chunk] = true;
        int buf_disp = sched->frame_to_be_displayed % nb2;
        while (sched->frame_to_be_displayed == frame &&
               sched->rendering_completed[buf_disp * sched->num_chunks + sched->chunk_to_be_displayed]) {
            task_t task = {
                .type = DISPLAY_FRAME,
                .frame = sched->frame_to_be_displayed,
                .chunk = sched->chunk_to_be_displayed,
                .render_params_ptr = NULL,
            };
            mpmc_queue_produce(&sched->display_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
            sched->chunk_to_be_displayed++;
            if (sched->chunk_to_be_displayed >= sched->num_chunks) {
                sched->chunk_to_be_displayed = 0;
                sched->frame_to_be_displayed++;
            }
        }
    }
    if (completed_task.type == DISPLAY_FRAME) {
        sched->display_completed[buffer * sched->num_chunks + chunk] = true;
        if (frame == sched->num_frames - 1 && chunk == sched->num_chunks - 1)
            emit_exit_tasks(sched);
    }
    if (completed_task.type == DISPLAY_FRAME || completed_task.type == RENDER_FRAME) {
        int next_frame = sched->num_frames;
        if (completed_task.type == RENDER_FRAME)  next_frame = frame + 1;
        if (completed_task.type == DISPLAY_FRAME) next_frame = frame + sched->num_buffers;
        if (frame_queueable(sched, next_frame) && next_frame < sched->num_frames) {
            reset_queuable(sched, next_frame);
            render_params_t* rp = &sched->render_params_buf[buffer];
            handle_input(&sched->input_state);
            int should_exit = sched->input_state.quit_requested;
			render_params_copy(rp, &sched->input_state.render_params);
            if (should_exit) {
                emit_exit_tasks(sched);
            } else {
                for (int i = 0; i < sched->num_chunks; i++) {
                    task_t task = {
                        .type = PROCESS_PRIMITIVES,
                        .frame = next_frame,
                        .chunk = i,
                        .render_params_ptr = rp,
                    };
                    mpmc_queue_produce(&sched->parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
                }
            }
        }
    }
    thread_mutex_unlock(&sched->state_mutex);
}

typedef struct {
	rendering_ctx_t*      render_ctx;    /* [num_chunks] — per chunk, configured once at init */
	primitive_pass_ctx_t* primitive_ctx; /* [num_chunks] — per chunk, like render_ctx          */
	tio_gfx_ctx*       gfx_ctx;      /* [num_workers] — per worker, reconfigured per task  */
	/* Output buffers — per (chunk, buffer) */
	char**             gfx_out;      /* flat [num_chunks * num_buffers]           */
	size_t*            gfx_out_size; /* flat [num_chunks * num_buffers]           */
	size_t             gfx_out_cap;	 /* largest per-chunk output size; same for all*/
	/* Precomputed per-chunk gfx params (applied via tio_gfx_set_params per task) */
	tio_gfx_params*    chunk_gp;     /* [num_chunks]                              */
	/* Parameters */
	int num_workers;
	int num_buffers;
	int num_chunks;
	int rows, cols;
	tio_gfx_backend display_mode;
	/* Timer */
	monotonic_timer_t timer;
	double display_time;
	double total_display_time;
	/* Scheduler ctx*/
	scheduler_ctx_t sched;
} trender_ctx_t;

static inline tio_gfx_ctx* gfx_ctx_at(trender_ctx_t* ctx, int w) {
	return &ctx->gfx_ctx[w];
}
static inline char* gfx_out_at(trender_ctx_t* ctx, int b, int c) {
	return ctx->gfx_out[c * ctx->num_buffers + b];
}
static inline size_t* gfx_out_size_at(trender_ctx_t* ctx, int b, int c) {
	return &ctx->gfx_out_size[c * ctx->num_buffers + b];
}

static inline int trender_ctx_init(trender_ctx_t* ctx, mesh_t* mesh, int rows, int cols,
	const cli_args_t* args) {
	int num_workers                            = args->threads;
	int num_chunks                             = num_workers; /* TODO: independent CLI flag later */
	int num_buffers                            = args->buffers;
	tio_gfx_backend display_mode               = args->display_mode;
	tio_gfx_halfblock_color_mode hb_color_mode = args->hb_color_mode;
	int cell_height_px                         = args->cell_height_px;
	tio_gfx_iterm_encode_fmt iterm_encode_fmt  = args->iterm_encode_fmt;
	int iterm_jpeg_quality                     = args->iterm_jpeg_quality;
	int iterm_png_compression_level            = args->iterm_png_compression;
	int upscale_x                              = args->upscale_x;
	int upscale_y                              = args->upscale_y;
	int cell_width_px                          = args->cell_width_px;

	if (num_workers < 1) {
		fprintf(stderr, "trender_ctx_init: need at least 1 worker thread\n");
		return -1;
	}
	/* The scheduler + main-writer pipeline always overlaps render and display,
	 * so it needs at least double buffering even for a single worker. args.h
	 * coerces buffers=1 when threads==1, so bump it back up here. */
	if (num_buffers < 2) {
		if (args->verbose)
			printf("trender_ctx_init: bumping buffers from %d to 3 (pipeline needs >= 2)\r\n", num_buffers);
		num_buffers = 3;
	}

	ctx->rows               = rows;
	ctx->cols               = cols;
	ctx->num_workers        = num_workers;
	ctx->num_buffers        = num_buffers;
	ctx->num_chunks         = num_chunks;
	ctx->display_mode       = display_mode;
	ctx->display_time       = 0.0;
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
	    strip_align = 1;
	else {
	    fprintf(stderr, "trender_ctx_init: unknown display_mode %d\n", (int)display_mode);
	    return -1;
	}

	/* rows must be a multiple of both strip_align (aligned strip boundaries) and
	 * num_chunks (chunks divide evenly) — the LCM. */
	int gcd_a = strip_align, gcd_b = ctx->num_chunks;
	while (gcd_b) { int t = gcd_a % gcd_b; gcd_a = gcd_b; gcd_b = t; }
	int chunk_height_align = strip_align / gcd_a * ctx->num_chunks;
	ctx->rows -= ctx->rows % chunk_height_align;
	rows = ctx->rows;

	ctx->render_ctx = (rendering_ctx_t*)malloc(ctx->num_chunks * sizeof(rendering_ctx_t));
	if (!ctx->render_ctx) { fprintf(stderr, "trender_ctx_init: out of memory (render_ctx)\n"); return -1; }

	ctx->primitive_ctx = (primitive_pass_ctx_t*)malloc(ctx->num_chunks * sizeof(primitive_pass_ctx_t));
	if (!ctx->primitive_ctx) { fprintf(stderr, "trender_ctx_init: out of memory (primitive_ctx)\n"); return -1; }

	ctx->gfx_ctx = (tio_gfx_ctx*)malloc(ctx->num_workers * sizeof(tio_gfx_ctx));
	if (!ctx->gfx_ctx) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_ctx)\n"); return -1; }

	ctx->gfx_out = (char**)malloc((size_t)ctx->num_chunks * num_buffers * sizeof(char*));
	if (!ctx->gfx_out) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_out)\n"); return -1; }

	ctx->gfx_out_size = (size_t*)malloc((size_t)ctx->num_chunks * num_buffers * sizeof(size_t));
	if (!ctx->gfx_out_size) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_out_size)\n"); return -1; }

	ctx->chunk_gp = (tio_gfx_params*)malloc((size_t)ctx->num_chunks * sizeof(tio_gfx_params));
	if (!ctx->chunk_gp) { fprintf(stderr, "trender_ctx_init: out of memory (chunk_gp)\n"); return -1; }

	/* Precompute per-chunk geometry/params. render_ctx[c] is created once with this chunk's
	 * geometry; track max chunk height for the per-worker gfx_ctx template + output cap. */
	int max_h = 0;
	ctx->gfx_out_cap = 0;
	for (int c = 0; c < num_chunks; c++) {
		int starty = (rows * c) / num_chunks;
		int endy   = (rows * (c + 1)) / num_chunks;
		starty -= starty % strip_align;
		endy   -= endy   % strip_align;
		int chunk_height = endy - starty;
		if (chunk_height > max_h) max_h = chunk_height;

		/* Per-chunk render ctx, configured once (width=cols, height=rows drive projection). */
		ctx->render_ctx[c] = create_rendering_ctx(cols, rows, starty, endy);
		ctx->render_ctx[c].chunk_index = c;
		int num_triangles_per_chunk = (int)mesh->attrib.num_face_num_verts / num_chunks + num_chunks;
		ctx->primitive_ctx[c] = create_primitive_pass_ctx(num_triangles_per_chunk, num_chunks);
		ctx->primitive_ctx[c].width  = cols;
		ctx->primitive_ctx[c].height = rows;

		tio_gfx_params gp;
		if (display_mode == TIO_GFX_BACKEND_SIXEL) {
			gp = TIO_GFX_SIXEL_PARAMS(cols, chunk_height);
			gp.p.sixel.scale_x         = upscale_x;
			gp.p.sixel.scale_y         = upscale_y;
			gp.p.sixel.dither_offset_y = starty;
		} else if (display_mode == TIO_GFX_BACKEND_ITERM) {
			gp = TIO_GFX_ITERM_PARAMS(cols, chunk_height);
			gp.p.iterm.encode_fmt            = iterm_encode_fmt;
			gp.p.iterm.jpeg_quality          = iterm_jpeg_quality;
			gp.p.iterm.png_compression_level = iterm_png_compression_level;
			gp.p.iterm.upscale_x             = upscale_x;
			gp.p.iterm.upscale_y             = upscale_y;
			gp.p.iterm.starty_rows    = starty * upscale_y / (cell_height_px > 0 ? cell_height_px : 1);
			gp.p.iterm.cell_height_px = cell_height_px;
		} else if (display_mode == TIO_GFX_BACKEND_KITTY) {
			gp = TIO_GFX_KITTY_PARAMS(cols, chunk_height);
			gp.p.kitty.upscale_x      = upscale_x;
			gp.p.kitty.upscale_y      = upscale_y;
			gp.p.kitty.cell_height_px = cell_height_px;
			gp.p.kitty.cell_width_px  = cell_width_px;
			gp.p.kitty.full_height    = (c == 0) ? rows : 0;
		} else if (display_mode == TIO_GFX_BACKEND_HALFBLOCK) {
			gp = TIO_GFX_HALFBLOCK_PARAMS(cols, chunk_height);
			gp.p.halfblock.color_mode      = hb_color_mode;
			gp.p.halfblock.upscale_x       = upscale_x;
			gp.p.halfblock.upscale_y       = upscale_y;
			gp.p.halfblock.dither_offset_y = starty;
		} else {
			fprintf(stderr, "trender_ctx_init: unknown display_mode %d\n", (int)display_mode);
			return -1;
		}
		ctx->chunk_gp[c] = gp;
		size_t cap = tio_gfx_output_size_hint(gp);
		if (cap > ctx->gfx_out_cap) ctx->gfx_out_cap = cap;
	}

	/* Every primitive ctx needs all chunk strip boundaries to compute hint_mask. */
	for (int c = 0; c < num_chunks; c++) {
		for (int k = 0; k < num_chunks; k++) {
			ctx->primitive_ctx[c].chunk_starty[k] = ctx->render_ctx[k].starty;
			ctx->primitive_ctx[c].chunk_endy[k]   = ctx->render_ctx[k].endy;
		}
	}

	/* Per-worker gfx ctxs initialised at the largest chunk size (worst-case scratch);
	 * reconfigured to the actual chunk via tio_gfx_set_params per task. */
	tio_gfx_params gp_max = TIO_GFX_HALFBLOCK_PARAMS(cols, max_h);
	if (display_mode == TIO_GFX_BACKEND_SIXEL)         gp_max = TIO_GFX_SIXEL_PARAMS(cols, max_h);
	else if (display_mode == TIO_GFX_BACKEND_ITERM)    gp_max = TIO_GFX_ITERM_PARAMS(cols, max_h);
	else if (display_mode == TIO_GFX_BACKEND_KITTY)    gp_max = TIO_GFX_KITTY_PARAMS(cols, max_h);
	for (int w = 0; w < num_workers; w++)
		tio_gfx_init(gfx_ctx_at(ctx, w), gp_max);

	/* Every output buffer is the largest-chunk capacity so any chunk fits anywhere. */
	for (int c = 0; c < num_chunks; c++) {
		for (int b = 0; b < num_buffers; b++) {
			ctx->gfx_out[c * num_buffers + b]      = (char*)malloc(ctx->gfx_out_cap);
			ctx->gfx_out_size[c * num_buffers + b] = 0;
			if (!ctx->gfx_out[c * num_buffers + b]) {
				fprintf(stderr, "trender_ctx_init: out of memory (gfx_out[%d][%d])\n", c, b);
				return -1;
			}
		}
	}

	int queue_capacity = num_chunks * (num_buffers + 2) + num_workers + 8;
	scheduler_ctx_init(&ctx->sched, num_buffers, num_chunks, num_workers,
	                   args->frames, queue_capacity);
	return 0;
}

static inline void trender_ctx_destroy(trender_ctx_t* ctx) {
	for (int c = 0; c < ctx->num_chunks; c++) {
		free_rendering_ctx(&ctx->render_ctx[c]);
		free_primitive_pass_ctx(&ctx->primitive_ctx[c]);
	}
	for (int w = 0; w < ctx->num_workers; w++)
		tio_gfx_destroy(gfx_ctx_at(ctx, w));
	for (int c = 0; c < ctx->num_chunks; c++)
		for (int b = 0; b < ctx->num_buffers; b++)
			free(ctx->gfx_out[c * ctx->num_buffers + b]);
	free(ctx->gfx_out);
	free(ctx->gfx_out_size);
	free(ctx->render_ctx);
	free(ctx->primitive_ctx);
	free(ctx->gfx_ctx);
	free(ctx->chunk_gp);
	scheduler_ctx_destroy(&ctx->sched);
}

typedef struct { trender_ctx_t* ctx; mesh_t* mesh; int id; } worker_arg_t;

int worker_thread(void* arg) {
	worker_arg_t* wa  = (worker_arg_t*)arg;
	trender_ctx_t* ctx = wa->ctx;
	int w = wa->id;
	while (1) {
		task_t task;
		mpmc_queue_consume(&ctx->sched.parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
		if (task.type == EXIT_LOOP) break;

		if (task.type == PROCESS_PRIMITIVES) {
			int c = task.chunk;
			int num_chunks = ctx->sched.num_chunks;
			int num_triangles = wa->mesh->attrib.num_face_num_verts;
			int start_index = (num_triangles * c) / num_chunks;
			int end_index = (num_triangles * (c + 1)) / num_chunks;
			render_params_copy(&ctx->primitive_ctx[c].params, task.render_params_ptr);
			primitive_pass(wa->mesh, &ctx->primitive_ctx[c], start_index, end_index);
		}

		if (task.type == RENDER_FRAME) {
			int c   = task.chunk;
			int buf = task.frame % ctx->num_buffers;
			clear_pass(&ctx->render_ctx[c]);

			for (int pc = 0; pc < ctx->sched.num_chunks; pc++) {
				/* render_ctx/primitive_ctx are per chunk (configured once); gfx_ctx is per worker (reconfigured per task). */
				render_mesh(wa->mesh, &ctx->render_ctx[c], &ctx->primitive_ctx[pc]);
			}

			tio_gfx_set_params(gfx_ctx_at(ctx, w), ctx->chunk_gp[c]);
			int parts;
			if (ctx->display_mode == TIO_GFX_BACKEND_ITERM) {
				parts = TIO_GFX_FULL;
			} else {
				parts = TIO_GFX_PAYLOAD;
				if (c == 0)                    parts |= TIO_GFX_HEADER;
				if (c == ctx->num_chunks - 1)  parts |= TIO_GFX_FOOTER;
			}
			*gfx_out_size_at(ctx, buf, c) = (size_t)tio_gfx_generate(
				gfx_ctx_at(ctx, w),
				ctx->render_ctx[c].output_buffer.data,
				TIO_GFX_FMT_RGB565, parts,
				gfx_out_at(ctx, buf, c), ctx->gfx_out_cap);
		}
		
		run_scheduler(&ctx->sched, task);
	}
	return 0;
}

/* Spins up worker threads, runs the display loop on the calling thread, then joins.
 * input_state must be fully initialised (matrices, autofit, etc.). */
static inline void trender_loop(trender_ctx_t* ctx, mesh_t* mesh, tio_ctx_t* tio,
                                const input_state_t* input_state, const cli_args_t* args) {
	scheduler_ctx_t* sched = &ctx->sched;
	sched->input_state = *input_state;

	monotonic_timer_t frame_timer;
	timer_start(&frame_timer);
	double previous_end_time = 0.0;

	/* Prime frame 0. */
	handle_input(&sched->input_state);
	render_params_t* rp0 = &sched->render_params_buf[0];
	render_params_copy(rp0, &sched->input_state.render_params);
	for (int c = 0; c < ctx->num_chunks; c++) {
		task_t task = { .type = PROCESS_PRIMITIVES, .frame = 0, .chunk = c, .render_params_ptr = rp0 };
		mpmc_queue_produce(&sched->parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
	}

	/* Spawn workers. */
	worker_arg_t* wargs   = (worker_arg_t*)malloc((size_t)ctx->num_workers * sizeof(worker_arg_t));
	thread_ptr_t* threads = (thread_ptr_t*)malloc((size_t)ctx->num_workers * sizeof(thread_ptr_t));
	for (int i = 0; i < ctx->num_workers; i++) {
		wargs[i]   = (worker_arg_t){ .ctx = ctx, .mesh = mesh, .id = i };
		threads[i] = thread_create(worker_thread, &wargs[i], THREAD_STACK_SIZE_DEFAULT);
	}

	/* Display loop (this thread). */
	while (1) {
		task_t task;
		mpmc_queue_consume(&sched->display_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
		if (task.type == EXIT_LOOP) break;

		int c   = task.chunk;
		int buf = task.frame % ctx->num_buffers;
		if (c == 0) ctx->display_time = 0.0;

		timer_start(&ctx->timer);
		tio_write(tio, gfx_out_at(ctx, buf, c), *gfx_out_size_at(ctx, buf, c));
		ctx->display_time += timer_elapsed_ms(&ctx->timer);

		run_scheduler(sched, task);

		if (c == ctx->num_chunks - 1) {
			ctx->total_display_time += ctx->display_time;
			double current_end_time = timer_elapsed_ms(&frame_timer);
			double frame_time = current_end_time - previous_end_time;
			previous_end_time = current_end_time;
			double processing_time = fmax(0.0, frame_time - ctx->display_time);

			if (args->interactive) {
				thread_mutex_lock(&sched->state_mutex);
				float cam_x = sched->input_state.camera_x;
				float cam_y = sched->input_state.camera_y;
				float cam_z = sched->input_state.camera_z;
				thread_mutex_unlock(&sched->state_mutex);
				printf("\x1b[H");
				printf("\r\n");
				printf("Screen size: %d rows, %d cols, %d pixels          \r\n", ctx->rows, ctx->cols, ctx->rows * ctx->cols);
				printf("Camera position: (%0.2f, %0.2f, %0.2f)            \r\n", cam_x, cam_y, cam_z);
				printf("Processing:    %0.2f                              \r\n", processing_time);
				printf("Display:       %0.2f                              \r\n", ctx->display_time);
				printf("Frame time:    %0.2f                              \r\n", frame_time);
				fflush(stdout);
			}
		}
	}

	for (int i = 0; i < ctx->num_workers; i++)
		thread_join(threads[i]);
	free(threads);
	free(wargs);
}

static inline void trender_print_stats(trender_ctx_t* ctx, int num_frames) {
	const double inv_f = (num_frames > 0) ? 1.0 / num_frames : 0.0;

	/* Render timings are per chunk; generate timing is per worker — they have
	 * different counts, so accumulate and print them separately. */

	/* ── Per-chunk render totals ─────────────────────────────────────────── */
	printf("Per-chunk render totals (ms):\r\n");
	printf("  Chunk   %-12s %-12s %-12s %-12s\r\n", "Primitive", "Clear", "Raster", "Texture");
	double total_primitive_time        = 0.0;
	double total_clear_time            = 0.0;
	double total_rasterisation_time    = 0.0;
	double total_texture_sampling_time = 0.0;
	for (int c = 0; c < ctx->num_chunks; c++) {
		double pr = ctx->primitive_ctx[c].total_primitive_time;
		double cl = ctx->render_ctx[c].total_clear_time;
		double ra = ctx->render_ctx[c].total_rasterisation_time;
		double tx = ctx->render_ctx[c].total_texture_sampling_time;
		printf("  %-7d %-12.2f %-12.2f %-12.2f %-12.2f\r\n", c, pr, cl, ra, tx);
		total_primitive_time        += pr;
		total_clear_time            += cl;
		total_rasterisation_time    += ra;
		total_texture_sampling_time += tx;
	}

	/* ── Per-chunk render averages ───────────────────────────────────────── */
	printf("Per-chunk render averages (ms/frame):\r\n");
	printf("  Chunk   %-12s %-12s %-12s %-12s\r\n", "Primitive", "Clear", "Raster", "Texture");
	for (int c = 0; c < ctx->num_chunks; c++) {
		double pr = ctx->primitive_ctx[c].total_primitive_time;
		double cl = ctx->render_ctx[c].total_clear_time;
		double ra = ctx->render_ctx[c].total_rasterisation_time;
		double tx = ctx->render_ctx[c].total_texture_sampling_time;
		printf("  %-7d %-12.3f %-12.3f %-12.3f %-12.3f\r\n",
		       c, pr * inv_f, cl * inv_f, ra * inv_f, tx * inv_f);
	}

	/* ── Per-worker generate table ───────────────────────────────────────── */
	printf("Per-worker generate totals (ms):\r\n");
	printf("  Worker  %-12s\r\n", "Generate");
	double total_generate_ms = 0.0;
	for (int w = 0; w < ctx->num_workers; w++) {
		double gen = tio_gfx_total_ms(gfx_ctx_at(ctx, w));
		printf("  %-7d %-12.2f\r\n", w, gen);
		total_generate_ms += gen;
	}

	/* ── Overall totals ──────────────────────────────────────────────────── */
	double total_frame_gen_time = total_primitive_time + total_clear_time + total_rasterisation_time +
	                              total_texture_sampling_time + total_generate_ms;
	printf("Overall totals (ms):\r\n");
	printf("  primitive time:        %0.2f\r\n", total_primitive_time);
	printf("  clear time:            %0.2f\r\n", total_clear_time);
	printf("  rasterisation time:    %0.2f\r\n", total_rasterisation_time);
	printf("  texture_sampling time: %0.2f\r\n", total_texture_sampling_time);
	printf("  generate_ms:           %0.2f\r\n", total_generate_ms);
	printf("  frame_gen time:        %0.2f\r\n", total_frame_gen_time);
	printf("  display time:          %0.2f\r\n", ctx->total_display_time);
	printf("Overall averages (ms/frame):\r\n");
	printf("  primitive time:        %0.3f\r\n", total_primitive_time        * inv_f);
	printf("  clear time:            %0.3f\r\n", total_clear_time            * inv_f);
	printf("  rasterisation time:    %0.3f\r\n", total_rasterisation_time    * inv_f);
	printf("  texture_sampling time: %0.3f\r\n", total_texture_sampling_time * inv_f);
	printf("  generate_ms:           %0.3f\r\n", total_generate_ms           * inv_f);
	printf("  frame_gen time:        %0.3f\r\n", total_frame_gen_time        * inv_f);
	printf("  display time:          %0.3f\r\n", ctx->total_display_time     * inv_f);
}
