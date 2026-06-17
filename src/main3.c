#include "trender3.h"
#include "framebuffer_4i8.h"
#include "framebuffer_f.h"
#include "raycast.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

tio_ctx_t tio_ctx;

void get_bounding_box(mesh_t* mesh, float* minx, float* miny, float* minz, float* maxx, float* maxy, float* maxz) {
	*minx = FLT_MAX;
	*miny = FLT_MAX;
	*minz = FLT_MAX;
	*maxx = FLT_MIN;
	*maxy = FLT_MIN;
	*maxz = FLT_MIN;

	for (int i = 0; i < (int)mesh->attrib.num_vertices; i++) {
		float x = mesh->attrib.vertices[3 * i + 0];
		float y = mesh->attrib.vertices[3 * i + 1];
		float z = mesh->attrib.vertices[3 * i + 2];
		*minx = min_float(*minx, x);
		*miny = min_float(*miny, y);
		*minz = min_float(*minz, z);
		*maxx = max_float(*maxx, x);
		*maxy = max_float(*maxy, y);
		*maxz = max_float(*maxz, z);
	}
}

int compute_display_size(cli_args_t* args, int* out_rows, int* out_cols) {
	int term_rows = 0, term_cols = 0;
	if (tio_get_window_size(&tio_ctx, &term_rows, &term_cols) == -1) {
		fprintf(stderr, "Unable to get window size\r\n");
		return 1;
	}
	if (args->verbose)
		printf("Terminal size: %d rows, %d cols\r\n", term_rows, term_cols);
	int rows, cols;
	if (args->display_mode == TIO_GFX_BACKEND_SIXEL) {
		int pixel_w = 0, pixel_h = 0;
		int ret = tio_get_window_size_pixels(&tio_ctx, &pixel_w, &pixel_h);
		if (ret == -1 || pixel_w <= 0 || pixel_h <= 0) {
			pixel_w = 960;
			pixel_h = 540;
			if (args->verbose)
				printf("Pixel size: unavailable, using default %dx%d\r\n", pixel_w, pixel_h);
		} else {
			if (args->verbose)
				printf("Pixel size: %dx%d%s\r\n",
				       pixel_w, pixel_h, ret == 1 ? " [estimated]" : "");
		}
		cols = (args->width  > 0) ? args->width  : pixel_w;
		rows = (args->height > 0) ? args->height : pixel_h;
		/* Align to multiples of (8*scale_x) and (6*scale_y) so that after
		 * dividing by scale the render dimensions are still aligned to 8 and 6. */
		cols -= cols % (8 * args->upscale_x);
		rows -= rows % (6 * args->upscale_y);
		cols /= args->upscale_x;
		rows /= args->upscale_y;
	} else if (args->display_mode == TIO_GFX_BACKEND_ITERM) {
		int pixel_w = 0, pixel_h = 0;
		int ret = tio_get_window_size_pixels(&tio_ctx, &pixel_w, &pixel_h);
		if (ret == -1 || pixel_w <= 0 || pixel_h <= 0) {
			pixel_w = 960;
			pixel_h = 540;
			if (args->verbose)
				printf("Pixel size: unavailable, using default %dx%d\r\n", pixel_w, pixel_h);
		} else {
			if (args->verbose)
				printf("Pixel size: %dx%d%s\r\n",
				       pixel_w, pixel_h, ret == 1 ? " [estimated]" : "");
		}
		cols = (args->width  > 0) ? args->width  : pixel_w;
		rows = (args->height > 0) ? args->height : pixel_h;
		args->cell_height_px = (term_rows > 0 && pixel_h > 0) ? pixel_h / term_rows : 1;
		if (args->cell_height_px < 1) args->cell_height_px = 1;
		cols -= cols % (8 * args->upscale_x);
		rows -= rows % (args->cell_height_px * args->upscale_y);
		cols /= args->upscale_x;
		rows /= args->upscale_y;
	} else if (args->display_mode == TIO_GFX_BACKEND_KITTY) {
		int pixel_w = 0, pixel_h = 0;
		int ret = tio_get_window_size_pixels(&tio_ctx, &pixel_w, &pixel_h);
		if (ret == -1 || pixel_w <= 0 || pixel_h <= 0) {
			pixel_w = 960;
			pixel_h = 540;
			if (args->verbose)
				printf("Pixel size: unavailable, using default %dx%d\r\n", pixel_w, pixel_h);
		} else {
			if (args->verbose)
				printf("Pixel size: %dx%d%s\r\n",
				       pixel_w, pixel_h, ret == 1 ? " [estimated]" : "");
		}
		cols = (args->width  > 0) ? args->width  : pixel_w;
		rows = (args->height > 0) ? args->height : pixel_h;
		args->cell_height_px = (term_rows > 0 && pixel_h > 0) ? pixel_h / term_rows : 1;
		args->cell_width_px  = (term_cols > 0 && pixel_w > 0) ? pixel_w / term_cols : 1;
		if (args->cell_height_px < 1) args->cell_height_px = 1;
		if (args->cell_width_px  < 1) args->cell_width_px  = 1;
		cols -= cols % (8 * args->upscale_x);
		rows -= rows % (args->cell_height_px * args->upscale_y);
		cols /= args->upscale_x;
		rows /= args->upscale_y;
	} else if (args->display_mode == TIO_GFX_BACKEND_HALFBLOCK) {
		cols = (args->width  > 0) ? args->width  : term_cols;
		rows = (args->height > 0) ? args->height : term_rows * 2;
		cols -= cols % (8 * args->upscale_x);
		rows -= rows % (2 * args->upscale_y);
		cols /= args->upscale_x;
		rows /= args->upscale_y;
	} else {
		fprintf(stderr, "trender: unknown display mode %d\r\n", (int)args->display_mode);
		return 1;
	}
	*out_rows = rows;
	*out_cols = cols;
	return 0;
}

void cleanup(void) {
	tio_destroy(&tio_ctx);
}

int main(int argc, char* argv[]) {

	printf("Parsing arguments...\r\n");
	cli_args_t args;
	if (parse_args(argc, argv, &args) != 0) {
		print_usage(argv[0]);
		return 1;
	}
	
	if (args.verbose) {
		printf("Loading mesh...\r\n");
	}
	mesh_t mesh;
	int ret = load_obj(args.obj_path, &mesh);
	if (ret != 0) {
		fprintf(stderr, "Failed to load mesh: %d\r\nFilepath: %s\r\n", ret, args.obj_path);
		return 1;
	}
	
	if (args.verbose) {
		print_material_info(mesh.materials, mesh.num_materials);
	}

	input_state_t input_state = INPUT_STATE_INITIALIZER;

	if (args.center || args.autofit) {
		float minx, miny, minz, maxx, maxy, maxz;
		get_bounding_box(&mesh, &minx, &miny, &minz, &maxx, &maxy, &maxz);
		if (args.verbose) {
			printf("Mesh bounding box:\r\n");
			printf("  Min: (%.2f, %.2f, %.2f)\r\n", minx, miny, minz);
			printf("  Max: (%.2f, %.2f, %.2f)\r\n", maxx, maxy, maxz);
		}
		input_state.translate_x = -(minx + maxx) / 2.0f;
		input_state.translate_y = -(miny + maxy) / 2.0f;
		input_state.translate_z = -(minz + maxz) / 2.0f;
		if (args.autofit) {
			float largest_extent = fabsf(max_float(max_float((float)(maxx - minx), (float)(maxy - miny)), (float)(maxz - minz)));
			if (largest_extent < 1e-6f) largest_extent = 1.0f;
			input_state.scale_x = 2.0f / largest_extent;
			input_state.scale_y = 2.0f / largest_extent;
			input_state.scale_z = 2.0f / largest_extent;
			float sx = (maxx - minx) * input_state.scale_x;
			float sy = (maxy - miny) * input_state.scale_y;
			float sz = (maxz - minz) * input_state.scale_z;
			float half_max = 0.5f * max_float(max_float(sx, sy), sz);
			input_state.camera_z = -(half_max / tanf(glm_rad(45.0f)) + sz * 0.5f + near_plane);
		}
	}
	
	tio_init(&tio_ctx);
	atexit(cleanup);
	
	int rows, cols;
	if (compute_display_size(&args, &rows, &cols) != 0) {
		return 1;
	}

	if (args.verbose)
		printf("Output size: %d rows, %d cols\r\n", rows, cols);

	if (cols <= 0 || rows <= 0) {
		fprintf(stderr, "trender: computed output size is zero (%dx%d) — terminal may be too small\r\n", cols, rows);
		return 1;
	}

	init_input(&input_state, &tio_ctx, args.interactive, args.rotate, cols, rows);

	if (args.interactive) {
		printf("\x1b[2J");   // Clear screen
		printf("\x1b[H");    // Move cursor to home
	}
	printf("\x1b[?25l"); // Hide cursor
	fflush(stdout);

	trender_ctx_t ctx;
	if (trender_ctx_init(&ctx, rows, cols, &args) != 0)
		return 1;

	/* trender_ctx_init may align rows down to a multiple of the chunk height. */
	rows = ctx.rows;

	monotonic_timer_t timer_whole;
	timer_start(&timer_whole);

	trender_loop(&ctx, &mesh, &tio_ctx, &input_state, &args);

	double whole_time            = timer_elapsed_ms(&timer_whole);
	double total_frame_time      = whole_time;
	double total_processing_time = fmax(0.0, whole_time - ctx.total_display_time);

	/* Move cursor below the rendered image so the shell prompt appears cleanly */
	int cursor_row;
	if (args.display_mode == TIO_GFX_BACKEND_SIXEL)
		cursor_row = rows * args.upscale_y / 6 + 2;
	else if (args.display_mode == TIO_GFX_BACKEND_HALFBLOCK)
		cursor_row = rows * args.upscale_y / 2 + 2;
	else if (args.display_mode == TIO_GFX_BACKEND_ITERM)
		cursor_row = rows * args.upscale_y / args.cell_height_px + 2;
	else if (args.display_mode == TIO_GFX_BACKEND_KITTY)
		cursor_row = rows * args.upscale_y / args.cell_height_px + 2;
	else {
		fprintf(stderr, "trender: unknown display mode %d\r\n", (int)args.display_mode);
		return 1;
	}
	printf("\x1b[%d;1H\r\n", cursor_row);
	printf("\x1b[?25h"); // Show cursor
	fflush(stdout);

	if (args.verbose) {
		printf("\r\n");
		printf("Screen size: %d rows, %d cols, %d pixels          \r\n", rows, cols, rows * cols);
		printf("Total times:        \r\n");
		printf("Processing:    %0.2f\r\n", total_processing_time);
		printf("Display:       %0.2f\r\n", ctx.total_display_time);
		printf("Frame time:    %0.2f\r\n", total_frame_time);
		printf("Average times:      \r\n");
		printf("Processing:    %0.2f\r\n", total_processing_time / (float)args.frames);
		printf("Display:       %0.2f\r\n", ctx.total_display_time / (float)args.frames);
		printf("Frame time:    %0.2f\r\n", total_frame_time / (float)args.frames);
		trender_print_stats(&ctx, args.frames);
		printf("Total time:            %0.2f\r\n", whole_time);

	}

	trender_ctx_destroy(&ctx);
	return 0;
}
