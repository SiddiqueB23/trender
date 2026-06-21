#pragma once

#include "rendering_avx2_u16_mt.h"
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ── Kitty display strategy: multi-id, split transmit/display double buffering ──────
 *
 * Continuously streaming a full new frame to kitty every tick is fighting three of the
 * protocol's behaviours, each of which broke a naive approach:
 *
 *   1. Re-transmitting data to an existing image id deletes that image AND all of its
 *      placements ("the existing image and all its placements must be deleted"). So you
 *      cannot update the *currently displayed* id in place — it blanks every frame.
 *   2. Animation frames (a=f) leak storage: editing a frame each tick keeps allocating
 *      coalesced buffers that are never freed, so the 320 MiB graphics quota is hit after
 *      a few hundred frames and kitty evicts our image → permanent black mid-run.
 *   3. A combined transmit-and-display (a=T) creates the placement up front, so a frame
 *      spanning hundreds of chunks can be shown half-received → tearing.
 *
 * The scheme below sidesteps all three:
 *
 *   - Cycle round-robin through K = NUM_IMAGES image ids (BASE .. BASE+K-1), one id per
 *     frame. The id a frame draws to was freed K-1 frames ago, so it is fresh — drawing
 *     to it never disturbs the ids currently on screen (fixes #1).
 *   - Split transmit from display: load the pixels with a=t (transmit only, no placement
 *     ⇒ nothing is shown yet), and only after the final chunk lands issue a=p to display
 *     the now-complete image atomically (fixes #3).
 *   - Free the image from K-1 frames back with a=d,d=I (uppercase d=I frees the data;
 *     lowercase d=i would only drop the placement), keeping storage bounded to ~K images
 *     (fixes #2). Deferring the delete by K-1 frames also leaves the previously displayed
 *     image up while the new one is being prepared.
 *   - Drive the placement z-index more negative each frame (Z_BASE - frame*Z_STEP; all
 *     stay < 0, i.e. below the text overlay). Each completed frame is therefore placed
 *     *behind* the current one and revealed cleanly when the old top image is deleted next
 *     cycle — the swap is "remove the top to reveal the ready frame underneath", with no
 *     compositing flash. Z_STEP tunes the spacing (0 = all same z, rely on atomic a=p).
 *
 * NUM_IMAGES is the pipeline depth: 2 = double buffering, 3 = triple, etc.
 */
#define TRENDER_KITTY_IMAGE_ID_BASE 31
#define TRENDER_KITTY_NUM_IMAGES    3
#define TRENDER_KITTY_Z_BASE        (-1)
#define TRENDER_KITTY_Z_STEP        2

enum task_type {
	PROCESS_PRIMITIVES,
    RENDER_FRAME,
    ENCODE_FRAME,
    DISPLAY_FRAME,
    EXIT_LOOP,
};

typedef struct {
    int type;
    int frame;
    int chunk_y;
    int chunk_x;
    render_params_t* render_params_ptr;
} task_t;

void print_task(task_t *task, int start) {
	static const char* task_type_names[] = {
		"PROCESS_PRIMITIVES", "RENDER_FRAME", "ENCODE_FRAME", "DISPLAY_FRAME", "EXIT_LOOP",
	};
	static const char* start_end[] = {
		"START: ", "END: ",
	};
	
	printf("%s %s frame=%d chunk_y=%d chunk_x=%d\n",
	       start_end[start % 2] ,task_type_names[task->type], task->frame, task->chunk_y, task->chunk_x);
	fflush(stdout);
}

typedef struct {
    /* configuration */
    int num_buffers;
	int num_render_pass_chunks_y;
	int num_render_pass_chunks_x;
	int num_prim_pass_chunks;
    int num_workers;
    int num_frames;
    int queue_capacity;

	/* primitive processing completion state [num_prim_pass_chunks] */
	bool*          	primitive_chunk_procesed;
    /* rendering completion state — flat [num_buffers*2 × num_render_pass_chunks_y × num_render_pass_chunks_x] */
    bool*           rendering_completed;
    /* encode and display completion state — flat [num_buffers*2 × num_render_pass_chunks_y] */
    bool*           encode_completed;
    bool*           display_pending;
    bool*           display_completed;
    thread_mutex_t  state_mutex;

	/* input state */
	input_state_t input_state;
    /* render params — [num_buffers * 2] */
    render_params_t* render_params_buf;
	
    /* queues + backing buffers — [queue_capacity] each */
    mpmc_queue_t  parallel_work_queue;
    task_t*       parallel_work_queue_buf;

    /* scheduler cursors */
    int frame_to_be_displayed;
    int row_to_be_displayed;

	/* Frame generation timer */
	monotonic_timer_t timer;
	double total_frame_gen_time;
} scheduler_ctx_t;

void scheduler_ctx_init(scheduler_ctx_t* sched, int num_buffers,
						int num_render_pass_chunks_y, int num_render_pass_chunks_x, int num_prim_pass_chunks,
                        int num_workers, int num_frames,
                        int queue_capacity) {
    sched->num_buffers              = num_buffers;
    sched->num_render_pass_chunks_y = num_render_pass_chunks_y;
    sched->num_render_pass_chunks_x = num_render_pass_chunks_x;
    sched->num_prim_pass_chunks     = num_prim_pass_chunks;
    sched->num_workers              = num_workers;
    sched->num_frames               = num_frames;
    sched->queue_capacity           = queue_capacity;
    sched->frame_to_be_displayed    = 0;
    sched->row_to_be_displayed      = 0;

    int nb2 = num_buffers * 2;
    sched->rendering_completed       = (bool*)calloc((size_t)(nb2 * num_render_pass_chunks_y * num_render_pass_chunks_x), sizeof(bool));
    sched->encode_completed          = (bool*)calloc((size_t)(nb2 * num_render_pass_chunks_y), sizeof(bool));
    sched->display_pending           = (bool*)calloc((size_t)(nb2 * num_render_pass_chunks_y), sizeof(bool));
    sched->display_completed         = (bool*)calloc((size_t)(nb2 * num_render_pass_chunks_y), sizeof(bool));
    sched->primitive_chunk_procesed  = (bool*)calloc((size_t)(num_prim_pass_chunks), sizeof(bool));
    sched->render_params_buf         = (render_params_t*)calloc((size_t)nb2, sizeof(render_params_t));
    thread_mutex_init(&sched->state_mutex);

    sched->parallel_work_queue_buf = (task_t*)malloc((size_t)queue_capacity * sizeof(task_t));
    mpmc_queue_init(&sched->parallel_work_queue, queue_capacity, sizeof(task_t), sched->parallel_work_queue_buf);

    /* mark back half of display slots as already done (frames that haven't existed yet) */
    for (int i = num_buffers; i < nb2; i++)
        for (int j = 0; j < num_render_pass_chunks_y; j++)
            sched->display_completed[i * num_render_pass_chunks_y + j] = true;

	sched->total_frame_gen_time = 0.0;
}

void scheduler_ctx_destroy(scheduler_ctx_t* sched) {
    free(sched->rendering_completed);
    free(sched->encode_completed);
    free(sched->display_pending);
    free(sched->display_completed);
    free(sched->render_params_buf);
    free(sched->parallel_work_queue_buf);
    thread_mutex_term(&sched->state_mutex);
}

void emit_exit_tasks(scheduler_ctx_t* sched) {
    task_t task = { .type = EXIT_LOOP, .frame = 0, .chunk_y = 0, .chunk_x = 0, .render_params_ptr = NULL };
    for (int i = 0; i < sched->num_workers; i++)
        mpmc_queue_produce(&sched->parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
}

bool frame_rasterisable(scheduler_ctx_t* sched) {
	bool primitive_chunk_processed = true;
	for (int i = 0; i < sched->num_prim_pass_chunks; i++)
        primitive_chunk_processed  = primitive_chunk_processed  && sched->primitive_chunk_procesed[i];
    return primitive_chunk_processed;
}

void reset_rasterisable(scheduler_ctx_t* sched) {
	for (int i = 0; i < sched->num_prim_pass_chunks; i++)
		sched->primitive_chunk_procesed[i] = false;
}

void reset_queuable(scheduler_ctx_t* sched, int frame) {
    int nb2                    = sched->num_buffers * 2;
    int frame_minus_one        = (frame + nb2 - 1) % nb2;
    int frame_minus_num_bufs   = (frame + sched->num_buffers) % nb2;
    int num_y                  = sched->num_render_pass_chunks_y;
    int num_x                  = sched->num_render_pass_chunks_x;
    for (int i = 0; i < num_y * num_x; i++)
        sched->rendering_completed[frame_minus_one * num_y * num_x + i] = false;
    for (int i = 0; i < num_y; i++)
        sched->encode_completed[frame_minus_one * num_y + i] = false;
    for (int i = 0; i < num_y; i++)
        sched->display_completed[frame_minus_num_bufs * num_y + i] = false;
}

bool frame_queueable(scheduler_ctx_t* sched, int frame) {
    int nb2                    = sched->num_buffers * 2;
    int frame_minus_one        = (frame + nb2 - 1) % nb2;
    int frame_minus_num_bufs   = (frame + sched->num_buffers) % nb2;
    bool rendered_all = true, displayed_all = true;
    for (int i = 0; i < sched->num_render_pass_chunks_y; i++)
        rendered_all  = rendered_all  && sched->encode_completed[frame_minus_one * sched->num_render_pass_chunks_y + i];
    for (int i = 0; i < sched->num_render_pass_chunks_y; i++)
        displayed_all = displayed_all && sched->display_completed[frame_minus_num_bufs * sched->num_render_pass_chunks_y + i];
    return rendered_all && displayed_all;
}

typedef struct {
	rendering_ctx_t*      render_ctx;    /* [num_render_pass_chunks_y] — per chunk, configured once at init */
	primitive_pass_ctx_t* primitive_ctx; /* [num_prim_pass_chunks] — per chunk, like render_ctx          */
	tio_gfx_ctx*       gfx_ctx;      /* [num_workers] — per worker, reconfigured per task  */
	/* Output buffers — per (chunk, buffer) */
	char**             gfx_out;      /* flat [num_render_pass_chunks_y * num_buffers]           */
	size_t*            gfx_out_size; /* flat [num_render_pass_chunks_y * num_buffers]           */
	size_t             gfx_out_cap;	 /* largest per-chunk output size; same for all*/
	/* Combined per-row RGB565 buffer, written by ENCODE_FRAME before tio_gfx_generate */
	framebuffer_u16*   combined_row_buffer; /* flat [num_render_pass_chunks_y * num_buffers] */
	/* Precomputed per-chunk gfx params (applied via tio_gfx_set_params per task) */
	tio_gfx_params*    chunk_gp;     /* [num_render_pass_chunks_y]                              */
	/* Parameters */
	int num_workers;
	int num_buffers;
	int num_render_pass_chunks_y;
	int num_render_pass_chunks_x;
	int num_prim_pass_chunks;
	int rows, cols;
	tio_gfx_backend display_mode;
	/* Timer */
	monotonic_timer_t timer;
	double display_time;
	double total_display_time;
	tio_ctx_t* tio;
	/* Scheduler ctx*/
	scheduler_ctx_t sched;
} trender_ctx_t;

static inline tio_gfx_ctx* gfx_ctx_at(trender_ctx_t* ctx, int w) {
	return &ctx->gfx_ctx[w];
}
static inline rendering_ctx_t* render_ctx_at(trender_ctx_t* ctx, int row, int col) {
	return &ctx->render_ctx[row * ctx->num_render_pass_chunks_x + col];
}
static inline framebuffer_u16* combined_row_at(trender_ctx_t* ctx, int buf, int row) {
	return &ctx->combined_row_buffer[row * ctx->num_buffers + buf];
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
	int num_render_pass_chunks_y               = num_workers * 1; /* TODO: independent CLI flag later */
	int num_render_pass_chunks_x               = num_workers * 1;
	int num_prim_pass_chunks                   = num_workers;
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

	ctx->rows                     = rows;
	ctx->cols                     = cols;
	ctx->num_workers              = num_workers;
	ctx->num_buffers              = num_buffers;
	ctx->num_render_pass_chunks_y = num_render_pass_chunks_y;
	ctx->num_prim_pass_chunks     = num_prim_pass_chunks;
	ctx->display_mode             = display_mode;
	ctx->display_time             = 0.0;
	ctx->total_display_time       = 0.0;

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
	 * num_render_pass_chunks_y (chunks divide evenly) — the LCM. */
	int gcd_a = strip_align, gcd_b = ctx->num_render_pass_chunks_y;
	while (gcd_b) { int t = gcd_a % gcd_b; gcd_a = gcd_b; gcd_b = t; }
	int chunk_height_align = strip_align / gcd_a * ctx->num_render_pass_chunks_y;
	ctx->rows -= ctx->rows % chunk_height_align;
	rows = ctx->rows;

	/* cols must be a multiple of 8 (AVX2 raster loop) and of num_render_pass_chunks_x
	 * (chunks divide evenly); clamping to a multiple of 8*num_render_pass_chunks_x
	 * guarantees both, since each chunk's own width is then a multiple of 8. */
	ctx->cols -= ctx->cols % (8 * num_render_pass_chunks_x);
	cols = ctx->cols;

	ctx->num_render_pass_chunks_x = num_render_pass_chunks_x;
	ctx->render_ctx = (rendering_ctx_t*)malloc((size_t)ctx->num_render_pass_chunks_y * num_render_pass_chunks_x * sizeof(rendering_ctx_t));
	if (!ctx->render_ctx) { fprintf(stderr, "trender_ctx_init: out of memory (render_ctx)\n"); return -1; }

	ctx->primitive_ctx = (primitive_pass_ctx_t*)malloc(ctx->num_prim_pass_chunks * sizeof(primitive_pass_ctx_t));
	if (!ctx->primitive_ctx) { fprintf(stderr, "trender_ctx_init: out of memory (primitive_ctx)\n"); return -1; }

	ctx->gfx_ctx = (tio_gfx_ctx*)malloc(ctx->num_workers * sizeof(tio_gfx_ctx));
	if (!ctx->gfx_ctx) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_ctx)\n"); return -1; }

	ctx->gfx_out = (char**)malloc((size_t)ctx->num_render_pass_chunks_y * num_buffers * sizeof(char*));
	if (!ctx->gfx_out) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_out)\n"); return -1; }

	ctx->gfx_out_size = (size_t*)malloc((size_t)ctx->num_render_pass_chunks_y * num_buffers * sizeof(size_t));
	if (!ctx->gfx_out_size) { fprintf(stderr, "trender_ctx_init: out of memory (gfx_out_size)\n"); return -1; }

	ctx->chunk_gp = (tio_gfx_params*)malloc((size_t)ctx->num_render_pass_chunks_y * sizeof(tio_gfx_params));
	if (!ctx->chunk_gp) { fprintf(stderr, "trender_ctx_init: out of memory (chunk_gp)\n"); return -1; }

	/* Per-X-chunk column boundaries — shared across all rows (chunk width doesn't depend on row). */
	int* chunk_startx = (int*)malloc((size_t)num_render_pass_chunks_x * sizeof(int));
	int* chunk_endx   = (int*)malloc((size_t)num_render_pass_chunks_x * sizeof(int));
	for (int x = 0; x < num_render_pass_chunks_x; x++) {
		chunk_startx[x] = (cols * x) / num_render_pass_chunks_x;
		chunk_endx[x]   = (cols * (x + 1)) / num_render_pass_chunks_x;
	}

	/* Precompute per-chunk geometry/params. render_ctx_at(ctx,c,x) is created once with that
	 * cell's geometry; track max chunk height for the per-worker gfx_ctx template + output cap. */
	int max_h = 0;
	ctx->gfx_out_cap = 0;
	for (int c = 0; c < num_render_pass_chunks_y; c++) {
		int starty = (rows * c) / num_render_pass_chunks_y;
		int endy   = (rows * (c + 1)) / num_render_pass_chunks_y;
		starty -= starty % strip_align;
		endy   -= endy   % strip_align;
		int chunk_height = endy - starty;
		if (chunk_height > max_h) max_h = chunk_height;

		/* Per-cell render ctx, configured once (width=cols, height=rows drive projection). */
		for (int x = 0; x < num_render_pass_chunks_x; x++) {
			rendering_ctx_t* cell = render_ctx_at(ctx, c, x);
			*cell = create_rendering_ctx(cols, rows, chunk_startx[x], chunk_endx[x], starty, endy);
			cell->chunk_index = c * num_render_pass_chunks_x + x;
		}

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
			/* full image height on every chunk: the header strip needs it for the a=t v=
			   (total image height) and the footer strip needs it for the a=p put's r=. */
			gp.p.kitty.full_height    = rows;
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

	/* Per-chunk primitive ctx, sized independently from the render chunks above.
	 * chunk_hints is indexed per render-chunk cell (clipping hints are tested
	 * against render-chunk bounds), so create_primitive_pass_ctx takes both
	 * num_render_pass_chunks_y and num_render_pass_chunks_x as its chunk counts. */
	for (int c = 0; c < num_prim_pass_chunks; c++) {
		int num_triangles_per_chunk = (int)mesh->attrib.num_face_num_verts / num_prim_pass_chunks + num_prim_pass_chunks;
		ctx->primitive_ctx[c] = create_primitive_pass_ctx(num_triangles_per_chunk, num_render_pass_chunks_y, num_render_pass_chunks_x);
		ctx->primitive_ctx[c].width  = cols;
		ctx->primitive_ctx[c].height = rows;
	}

	/* Every primitive ctx needs all chunk strip boundaries to compute hint_mask. */
	for (int c = 0; c < num_prim_pass_chunks; c++) {
		for (int k = 0; k < num_render_pass_chunks_y; k++) {
			ctx->primitive_ctx[c].chunk_starty[k] = render_ctx_at(ctx, k, 0)->starty;
			ctx->primitive_ctx[c].chunk_endy[k]   = render_ctx_at(ctx, k, 0)->endy;
		}
		for (int k = 0; k < num_render_pass_chunks_x; k++) {
			ctx->primitive_ctx[c].chunk_startx[k] = chunk_startx[k];
			ctx->primitive_ctx[c].chunk_endx[k]   = chunk_endx[k];
		}
	}
	free(chunk_startx);
	free(chunk_endx);

	/* Per-worker gfx ctxs initialised at the largest chunk size (worst-case scratch);
	 * reconfigured to the actual chunk via tio_gfx_set_params per task. */
	tio_gfx_params gp_max = TIO_GFX_HALFBLOCK_PARAMS(cols, max_h);
	if (display_mode == TIO_GFX_BACKEND_SIXEL)         gp_max = TIO_GFX_SIXEL_PARAMS(cols, max_h);
	else if (display_mode == TIO_GFX_BACKEND_ITERM)    gp_max = TIO_GFX_ITERM_PARAMS(cols, max_h);
	else if (display_mode == TIO_GFX_BACKEND_KITTY)    gp_max = TIO_GFX_KITTY_PARAMS(cols, max_h);
	for (int w = 0; w < num_workers; w++)
		tio_gfx_init(gfx_ctx_at(ctx, w), gp_max);

	/* Every output buffer is the largest-chunk capacity so any chunk fits anywhere. */
	for (int c = 0; c < num_render_pass_chunks_y; c++) {
		for (int b = 0; b < num_buffers; b++) {
			ctx->gfx_out[c * num_buffers + b]      = (char*)malloc(ctx->gfx_out_cap);
			ctx->gfx_out_size[c * num_buffers + b] = 0;
			if (!ctx->gfx_out[c * num_buffers + b]) {
				fprintf(stderr, "trender_ctx_init: out of memory (gfx_out[%d][%d])\n", c, b);
				return -1;
			}
		}
	}

	/* Combined per-row buffer, full row width, double/triple-buffered like gfx_out. */
	ctx->combined_row_buffer = (framebuffer_u16*)malloc((size_t)num_render_pass_chunks_y * num_buffers * sizeof(framebuffer_u16));
	if (!ctx->combined_row_buffer) { fprintf(stderr, "trender_ctx_init: out of memory (combined_row_buffer)\n"); return -1; }
	for (int c = 0; c < num_render_pass_chunks_y; c++) {
		int chunk_height = render_ctx_at(ctx, c, 0)->endy - render_ctx_at(ctx, c, 0)->starty;
		for (int b = 0; b < num_buffers; b++)
			*combined_row_at(ctx, b, c) = create_framebuffer_u16(cols, chunk_height);
	}

	int queue_capacity = (num_render_pass_chunks_y * num_render_pass_chunks_x) * (num_buffers + 2) + num_prim_pass_chunks + num_workers + 8 + 256;
	scheduler_ctx_init(&ctx->sched, num_buffers,
					   num_render_pass_chunks_y, num_render_pass_chunks_x, num_prim_pass_chunks,
					   num_workers,
	                   args->frames, queue_capacity);
	return 0;
}

static inline void trender_ctx_destroy(trender_ctx_t* ctx) {
	int num_render_ctx_cells = ctx->num_render_pass_chunks_y * ctx->num_render_pass_chunks_x;
	for (int c = 0; c < num_render_ctx_cells; c++) {
		free_rendering_ctx(&ctx->render_ctx[c]);
	}
	for (int c = 0; c < ctx->num_prim_pass_chunks; c++) {
		free_primitive_pass_ctx(&ctx->primitive_ctx[c]);
	}
	for (int w = 0; w < ctx->num_workers; w++)
		tio_gfx_destroy(gfx_ctx_at(ctx, w));
	for (int c = 0; c < ctx->num_render_pass_chunks_y; c++)
		for (int b = 0; b < ctx->num_buffers; b++)
			free(ctx->gfx_out[c * ctx->num_buffers + b]);
	free(ctx->gfx_out);
	free(ctx->gfx_out_size);
	for (int c = 0; c < ctx->num_render_pass_chunks_y; c++)
		for (int b = 0; b < ctx->num_buffers; b++)
			free_framebuffer_u16(combined_row_at(ctx, b, c));
	free(ctx->combined_row_buffer);
	free(ctx->render_ctx);
	free(ctx->primitive_ctx);
	free(ctx->gfx_ctx);
	free(ctx->chunk_gp);
	scheduler_ctx_destroy(&ctx->sched);
}

typedef struct { trender_ctx_t* ctx; mesh_t* mesh; int id; } worker_arg_t;

void worker_thread_process_primitives(task_t* task, worker_arg_t* wa, int* override_next_task) {
	(void)override_next_task;
	trender_ctx_t* ctx = wa->ctx;

	int c = task->chunk_y;
	int num_prim_pass_chunks = ctx->sched.num_prim_pass_chunks;
	int num_triangles = wa->mesh->attrib.num_face_num_verts;
	int start_index = (num_triangles * c) / num_prim_pass_chunks;
	int end_index = (num_triangles * (c + 1)) / num_prim_pass_chunks;
	render_params_copy(&ctx->primitive_ctx[c].params, task->render_params_ptr);
	primitive_pass(wa->mesh, &ctx->primitive_ctx[c], start_index, end_index);

	scheduler_ctx_t* sched = &ctx->sched;
	thread_mutex_lock(&sched->state_mutex);
	int chunk_y = task->chunk_y;
    int frame   = task->frame;
    int num_x   = sched->num_render_pass_chunks_x;
    int num_y   = sched->num_render_pass_chunks_y;
	sched->primitive_chunk_procesed[chunk_y] = true;
	if(frame_rasterisable(sched)) {
		reset_rasterisable(sched);
		for (int i = 0; i < num_y; i++) {
			for (int j = 0; j < num_x; j++) {
				task_t render_task = {
					.type = RENDER_FRAME,
					.frame = frame,
					.chunk_y = i,
					.chunk_x = j,
					.render_params_ptr = NULL,
				};
				mpmc_queue_produce(&sched->parallel_work_queue, &render_task, MPMC_QUEUE_WAIT_INFINITE);
			}
		}
	}
	thread_mutex_unlock(&sched->state_mutex);
}

void worker_thread_render_frame(task_t* task, worker_arg_t* wa, int* override_next_task) {
	(void)override_next_task;
	trender_ctx_t* ctx = wa->ctx;

	rendering_ctx_t* rctx = render_ctx_at(ctx, task->chunk_y, task->chunk_x);
	int buf = task->frame % ctx->num_buffers;
	rctx->output_start = combined_row_at(ctx, buf, task->chunk_y)->data + rctx->startx;
	clear_pass(rctx);

	for (int pc = 0; pc < ctx->sched.num_prim_pass_chunks; pc++) {
		/* render_ctx/primitive_ctx are per chunk (configured once); gfx_ctx is per worker (reconfigured per task). */
		render_mesh(wa->mesh, rctx, &ctx->primitive_ctx[pc]);
	}

	scheduler_ctx_t* sched = &ctx->sched;
	int chunk_y = task->chunk_y;
	int chunk_x = task->chunk_x;
	int frame   = task->frame;
	int nb2     = sched->num_buffers * 2;
	int buffer  = frame % nb2;
	int num_x   = sched->num_render_pass_chunks_x;
	int num_y   = sched->num_render_pass_chunks_y;
	thread_mutex_lock(&sched->state_mutex);
	sched->rendering_completed[(buffer * num_y + chunk_y) * num_x + chunk_x] = true;
	bool row_rendered = true;
	for (int i = 0; i < num_x; i++)
		row_rendered = row_rendered && sched->rendering_completed[(buffer * num_y + chunk_y) * num_x + i];
	if (row_rendered) {
		task_t encode_task = {
			.type = ENCODE_FRAME,
			.frame = frame,
			.chunk_y = chunk_y,
			.chunk_x = 0,
			.render_params_ptr = NULL,
		};
		mpmc_queue_produce(&sched->parallel_work_queue, &encode_task, MPMC_QUEUE_WAIT_INFINITE);
		// if (chunk_y + 1 < num_y) {
		// 	for (int i = 0; i < num_x; i++) {
		// 		task_t render_task = {
		// 			.type = RENDER_FRAME,
		// 			.frame = frame,
		// 			.chunk_y = chunk_y + 1,
		// 			.chunk_x = i,
		// 			.render_params_ptr = NULL,
		// 		};
		// 		mpmc_queue_produce(&sched->parallel_work_queue, &render_task, MPMC_QUEUE_WAIT_INFINITE);
		// 	}
		// }
	}
	thread_mutex_unlock(&sched->state_mutex);
}

void worker_thread_encode_frame(task_t* task, worker_arg_t* wa, int* override_next_task) {
	trender_ctx_t* ctx = wa->ctx;
	int w = wa->id;

	int row = task->chunk_y;
	int buf = task->frame % ctx->num_buffers;
	framebuffer_u16* combined = combined_row_at(ctx, buf, row);

	tio_gfx_params gp = ctx->chunk_gp[row];
	if (ctx->display_mode == TIO_GFX_BACKEND_KITTY) {
		/* Cycle through K=NUM_IMAGES ids, split transmit/display double buffering — see header comment. */
		int k   = TRENDER_KITTY_NUM_IMAGES;
		int cur = TRENDER_KITTY_IMAGE_ID_BASE + (task->frame % k);
		int prev = (task->frame >= k - 1)
					? TRENDER_KITTY_IMAGE_ID_BASE + ((task->frame - (k - 1)) % k)
					: 0;
		gp.p.kitty.frame_mode     = TIO_GFX_KITTY_FRAME_PINGPONG;
		gp.p.kitty.image_id       = cur;
		gp.p.kitty.prev_image_id  = prev;
		gp.p.kitty.z_index        = TRENDER_KITTY_Z_BASE - task->frame * TRENDER_KITTY_Z_STEP;
	}
	tio_gfx_set_params(gfx_ctx_at(ctx, w), gp);
	int parts;
	if (ctx->display_mode == TIO_GFX_BACKEND_ITERM) {
		parts = TIO_GFX_FULL;
	} else {
		parts = TIO_GFX_PAYLOAD;
		if (row == 0)                                 parts |= TIO_GFX_HEADER;
		if (row == ctx->num_render_pass_chunks_y - 1) parts |= TIO_GFX_FOOTER;
	}
	*gfx_out_size_at(ctx, buf, row) = (size_t)tio_gfx_generate(
		gfx_ctx_at(ctx, w),
		combined->data,
		TIO_GFX_FMT_RGB565, parts,
		gfx_out_at(ctx, buf, row), ctx->gfx_out_cap);
	
	scheduler_ctx_t* sched = &ctx->sched;
	int chunk_y = task->chunk_y;
	int frame   = task->frame;
	int nb2     = sched->num_buffers * 2;
	int buffer  = frame % nb2;
	int num_y   = sched->num_render_pass_chunks_y;
	thread_mutex_lock(&sched->state_mutex);
	sched->encode_completed[buffer * num_y + chunk_y] = true;
	sched->display_pending[buffer * num_y + chunk_y] = true;
	if (sched->frame_to_be_displayed == frame && sched->row_to_be_displayed == chunk_y) {
		task_t display_task = {
			.type = DISPLAY_FRAME,
			.frame = sched->frame_to_be_displayed,
			.chunk_y = sched->row_to_be_displayed,
			.chunk_x = 0,
			.render_params_ptr = NULL,
		};
		*task = display_task;
		*override_next_task = 1;
	}
	int next_frame = frame + 1;
	if (frame_queueable(sched, next_frame) && next_frame < sched->num_frames) {
		reset_queuable(sched, next_frame);
		render_params_t* rp = &sched->render_params_buf[buffer];
		handle_input(&sched->input_state);
		int should_exit = sched->input_state.quit_requested;
		render_params_copy(rp, &sched->input_state.render_params);
		if (should_exit) {
			emit_exit_tasks(sched);
		} else {
			timer_start(&sched->timer);
			for (int i = 0; i < sched->num_prim_pass_chunks; i++) {
				task_t primitive_task = {
					.type = PROCESS_PRIMITIVES,
					.frame = next_frame,
					.chunk_y = i,
					.chunk_x = 0,
					.render_params_ptr = rp,
				};
				mpmc_queue_produce(&sched->parallel_work_queue, &primitive_task, MPMC_QUEUE_WAIT_INFINITE);
			}
		}
	}
	thread_mutex_unlock(&sched->state_mutex);
}

void worker_thread_display_frame(task_t* task, worker_arg_t* wa, int* override_next_task) {
	trender_ctx_t* ctx = wa->ctx;
	tio_ctx_t* tio = ctx->tio;

	int c   = task->chunk_y;
	int buf = task->frame % ctx->num_buffers;
	if (c == 0) ctx->display_time = 0.0;

	timer_start(&ctx->timer);
	tio_write(tio, gfx_out_at(ctx, buf, c), *gfx_out_size_at(ctx, buf, c));
	ctx->display_time += timer_elapsed_ms(&ctx->timer);


	if (c == ctx->num_render_pass_chunks_y - 1) {
		ctx->total_display_time += ctx->display_time;

		if (ctx->sched.input_state.interactive) {
			printf("\x1b[H");
			printf("\r\n");
			printf("Screen size: %d rows, %d cols, %d pixels          \r\n", ctx->rows, ctx->cols, ctx->rows * ctx->cols);
			printf("Display:       %0.2f                              \r\n", ctx->display_time);
			fflush(stdout);
		}
	}

	scheduler_ctx_t* sched = &ctx->sched;
	int chunk_y = task->chunk_y;
	int frame   = task->frame;
	int nb2     = sched->num_buffers * 2;
	int buffer  = frame % nb2;
	int num_y   = sched->num_render_pass_chunks_y;
	thread_mutex_lock(&sched->state_mutex);
	sched->display_completed[buffer * num_y + chunk_y] = true;
	if (frame == sched->num_frames - 1 && chunk_y == num_y - 1)
		emit_exit_tasks(sched);
	int next_frame = frame + sched->num_buffers;
	if (frame_queueable(sched, next_frame) && next_frame < sched->num_frames) {
		reset_queuable(sched, next_frame);
		render_params_t* rp = &sched->render_params_buf[buffer];
		handle_input(&sched->input_state);
		int should_exit = sched->input_state.quit_requested;
		render_params_copy(rp, &sched->input_state.render_params);
		if (should_exit) {
			emit_exit_tasks(sched);
		} else {
			timer_start(&sched->timer);
			for (int i = 0; i < sched->num_prim_pass_chunks; i++) {
				task_t primitive_task = {
					.type = PROCESS_PRIMITIVES,
					.frame = next_frame,
					.chunk_y = i,
					.chunk_x = 0,
					.render_params_ptr = rp,
				};
				mpmc_queue_produce(&sched->parallel_work_queue, &primitive_task, MPMC_QUEUE_WAIT_INFINITE);
			}
		}
	}
	int buf_disp = sched->frame_to_be_displayed % nb2;
	sched->display_pending[buf_disp * num_y + sched->row_to_be_displayed] = false;
	sched->row_to_be_displayed++;
	if (sched->row_to_be_displayed >= num_y) {
		sched->total_frame_gen_time += timer_elapsed_ms(&sched->timer);
		sched->row_to_be_displayed = 0;
		sched->frame_to_be_displayed++;
	}
	buf_disp = sched->frame_to_be_displayed % nb2;
	if (sched->display_pending[buf_disp * num_y + sched->row_to_be_displayed]) {
		task_t display_task = {
			.type = DISPLAY_FRAME,
			.frame = sched->frame_to_be_displayed,
			.chunk_y = sched->row_to_be_displayed,
			.chunk_x = 0,
			.render_params_ptr = NULL,
		};
		*task = display_task;
		*override_next_task = 1;
	}
	thread_mutex_unlock(&sched->state_mutex);
}

int worker_thread(void* arg) {
	worker_arg_t* wa  = (worker_arg_t*)arg;
	trender_ctx_t* ctx = wa->ctx;
	while (1) {
		task_t task;
		mpmc_queue_consume(&ctx->sched.parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
		if (task.type == EXIT_LOOP) break;

		int override_next_task = 1;
		while (override_next_task) {
			override_next_task = 0;
			// task_t current_task = task;
			// print_task(&current_task, 0);

			switch (task.type) {
				case PROCESS_PRIMITIVES:
					worker_thread_process_primitives(&task, wa, &override_next_task);
					break;
				case RENDER_FRAME:
					worker_thread_render_frame(&task, wa, &override_next_task);
					break;
				case ENCODE_FRAME:
					worker_thread_encode_frame(&task, wa, &override_next_task);
					break;
				case DISPLAY_FRAME:
					worker_thread_display_frame(&task, wa, &override_next_task);
					break;
				default:
					break;
			}
			// print_task(&current_task, 1);

		} 
	}
	return 0;
}

/* Spins up worker threads, runs the display loop on the calling thread, then joins.
 * input_state must be fully initialised (matrices, autofit, etc.). */
static inline void trender_loop(trender_ctx_t* ctx, mesh_t* mesh, tio_ctx_t* tio,
                                const input_state_t* input_state) {
	scheduler_ctx_t* sched = &ctx->sched;
	sched->input_state = *input_state;
	ctx->tio = tio;

	/* Prime frame 0. */
	handle_input(&sched->input_state);
	render_params_t* rp0 = &sched->render_params_buf[0];
	render_params_copy(rp0, &sched->input_state.render_params);
	timer_start(&sched->timer);
	for (int c = 0; c < ctx->num_prim_pass_chunks; c++) {
		task_t task = { .type = PROCESS_PRIMITIVES, .frame = 0, .chunk_y = c, .chunk_x = 0, .render_params_ptr = rp0 };
		mpmc_queue_produce(&sched->parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
	}

	/* Spawn workers. */
	worker_arg_t* wargs   = (worker_arg_t*)malloc((size_t)ctx->num_workers * sizeof(worker_arg_t));
	thread_ptr_t* threads = (thread_ptr_t*)malloc((size_t)ctx->num_workers * sizeof(thread_ptr_t));
	for (int i = 1; i < ctx->num_workers; i++) {
		wargs[i]   = (worker_arg_t){ .ctx = ctx, .mesh = mesh, .id = i };
		threads[i] = thread_create(worker_thread, &wargs[i], THREAD_STACK_SIZE_DEFAULT);
	}

	wargs[0]   = (worker_arg_t){ .ctx = ctx, .mesh = mesh, .id = 0 };
	worker_thread((void *)&wargs[0]);

	for (int i = 1; i < ctx->num_workers; i++)
		thread_join(threads[i]);
	free(threads);
	free(wargs);
}

static inline void trender_print_stats(trender_ctx_t* ctx, int num_frames) {
	const double inv_f = (num_frames > 0) ? 1.0 / num_frames : 0.0;

	/* Render timings are per chunk; generate timing is per worker — they have
	 * different counts, so accumulate and print them separately. */

	/* ── Per-chunk primitive totals ──────────────────────────────────────── */
	/* Primitive chunks are sized independently from render chunks, so they get their own loop bound. */
	printf("Per-chunk primitive totals (ms):\r\n");
	printf("  Chunk   %-12s\r\n", "Primitive");
	double total_primitive_time = 0.0;
	for (int c = 0; c < ctx->num_prim_pass_chunks; c++) {
		double pr = ctx->primitive_ctx[c].total_primitive_time;
		printf("  %-7d %-12.2f\r\n", c, pr);
		total_primitive_time += pr;
	}

	/* ── Per-chunk primitive averages ────────────────────────────────────── */
	printf("Per-chunk primitive averages (ms/frame):\r\n");
	printf("  Chunk   %-12s\r\n", "Primitive");
	for (int c = 0; c < ctx->num_prim_pass_chunks; c++) {
		double pr = ctx->primitive_ctx[c].total_primitive_time;
		printf("  %-7d %-12.3f\r\n", c, pr * inv_f);
	}

	/* ── Per-chunk render totals ─────────────────────────────────────────── */
	int num_render_ctx_cells = ctx->num_render_pass_chunks_y * ctx->num_render_pass_chunks_x;
	printf("Per-chunk render totals (ms):\r\n");
	printf("  (x,y)    %-12s %-12s %-12s\r\n", "Clear", "Raster", "Texture");
	double total_clear_time            = 0.0;
	double total_rasterisation_time    = 0.0;
	double total_texture_sampling_time = 0.0;
	for (int c = 0; c < num_render_ctx_cells; c++) {
		int x = c % ctx->num_render_pass_chunks_x;
		int y = c / ctx->num_render_pass_chunks_x;
		double cl = ctx->render_ctx[c].total_clear_time;
		double ra = ctx->render_ctx[c].total_rasterisation_time;
		double tx = ctx->render_ctx[c].total_texture_sampling_time;
		char coord[16];
		snprintf(coord, sizeof(coord), "(%d,%d)", x, y);
		printf("  %-8s %-12.2f %-12.2f %-12.2f\r\n", coord, cl, ra, tx);
		total_clear_time            += cl;
		total_rasterisation_time    += ra;
		total_texture_sampling_time += tx;
	}

	/* ── Per-chunk render averages ───────────────────────────────────────── */
	printf("Per-chunk render averages (ms/frame):\r\n");
	printf("  (x,y)    %-12s %-12s %-12s\r\n", "Clear", "Raster", "Texture");
	for (int c = 0; c < num_render_ctx_cells; c++) {
		int x = c % ctx->num_render_pass_chunks_x;
		int y = c / ctx->num_render_pass_chunks_x;
		double cl = ctx->render_ctx[c].total_clear_time;
		double ra = ctx->render_ctx[c].total_rasterisation_time;
		double tx = ctx->render_ctx[c].total_texture_sampling_time;
		char coord[16];
		snprintf(coord, sizeof(coord), "(%d,%d)", x, y);
		printf("  %-8s %-12.3f %-12.3f %-12.3f\r\n",
		       coord, cl * inv_f, ra * inv_f, tx * inv_f);
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
	printf("  actual frame gen:      %0.2f\r\n", ctx->sched.total_frame_gen_time);
	printf("Overall averages (ms/frame):\r\n");
	printf("  primitive time:        %0.3f\r\n", total_primitive_time        * inv_f);
	printf("  clear time:            %0.3f\r\n", total_clear_time            * inv_f);
	printf("  rasterisation time:    %0.3f\r\n", total_rasterisation_time    * inv_f);
	printf("  texture_sampling time: %0.3f\r\n", total_texture_sampling_time * inv_f);
	printf("  generate_ms:           %0.3f\r\n", total_generate_ms           * inv_f);
	printf("  frame_gen time:        %0.3f\r\n", total_frame_gen_time        * inv_f);
	printf("  display time:          %0.3f\r\n", ctx->total_display_time     * inv_f);
	printf("  actual frame gen:      %0.2f\r\n", ctx->sched.total_frame_gen_time * inv_f);
}
