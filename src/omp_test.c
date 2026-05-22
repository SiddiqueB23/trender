#include "sixel_display.h"
#include "timer.h"
#include "tio.h"
#include "cglm/cglm.h"
#include "rainbow.h"
#include <utils.h>
#include <omp.h>

tio_ctx_t ctx;

void cleanup(void) {
	tio_destroy(&ctx);
}

#define NUM_THREADS 3

void rainbow_gradient(framebuffer_4i8* fb, int time, int thread_id) {
#if NUM_THREADS > 1
	int starty = (fb->height * (thread_id - 1)) / (NUM_THREADS - 1);
	int endy = (fb->height * ((thread_id - 1) + 1)) / (NUM_THREADS - 1);
#else
	int starty = 0;
	int endy = fb->height;
#endif
	for (int y = starty; y < endy; y++) {
		for (int x = 0; x < fb->width; x++) {
			unsigned char r, g, b;
			int value = x + y + time * (thread_id + 1);
			get_rainbow(value, &r, &g, &b);
			int idx = (y * fb->width + x) * 4;
			fb->data[idx + 0] = r;
			fb->data[idx + 1] = g;
			fb->data[idx + 2] = b;
			fb->data[idx + 3] = 255;
		}
	}
}

int main() {

	tio_init(&ctx);
	atexit(cleanup);
	printf("\x1b[2J");   // Clear screen
	printf("\x1b[H");    // Move cursor to home
	printf("\x1b[?25l"); // Hide cursor
	fflush(stdout);

	int rows, cols;
	if (tio_get_window_size(&ctx, &rows, &cols) == -1) {
		fprintf(stderr, "Unable to get window size\n");
		return 1;
	}
	cols = 960;
	rows = 540;
	framebuffer_4i8 fb = create_framebuffer_4i8(cols, rows);

	sixel_display_ctx sixel_ctx;
	init_sixel_display_ctx(&sixel_ctx, cols, rows);
	init_sixel_indexed_bitmap(&sixel_ctx.bitmap, cols, rows);
	init_sixel_palette_rgbuniform(&sixel_ctx.bitmap.palette, 5);

	monotonic_timer_t timer, timer_whole;
	timer_start(&timer_whole);

	double total_draw_time = 0.0;
	double total_conversion_time = 0.0;
	double total_generation_time = 0.0;
	double total_display_time = 0.0;

	double draw_time = 0.0;
	double conversion_time = 0.0;
	double generation_time = 0.0;
	double display_time = 0.0;

	double previous_end_time = timer_elapsed_ms(&timer_whole);
	double total_frame_time = 0.0;

#pragma omp parallel num_threads(NUM_THREADS)
	{
		int thread_id = omp_get_thread_num();
		int num_frames = 1000;
		while (num_frames--) {
			if (thread_id == 0) {
				int current_event_queue_bytes_size = tio_get_event_queue_byte_size(&ctx);
				int event_bytes_processed = 0;
				while (event_bytes_processed < current_event_queue_bytes_size) {
					tio_input_event event = TIO_INPUT_EVENT_INITIALIZER;
					int bytes_processed = tio_pop_event_queue(&ctx, &event);
					event_bytes_processed += bytes_processed;
					if (event.type == TIO_INPUT_EVENT_TYPE_KEY) {
						if (event.code == 'Q' || event.code == CTRL_Q) {
							goto end;
						}
					}
				}

				timer_start(&timer);
			}

			if (thread_id != 0 || NUM_THREADS == 1)
				rainbow_gradient(&fb, num_frames, thread_id);
#pragma omp barrier

			if (thread_id == 0) {
				draw_time = timer_elapsed_ms(&timer);
				total_draw_time += draw_time;

				timer_start(&timer);
				convert_4i8_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering_216colors(&sixel_ctx.bitmap, fb);
				conversion_time = timer_elapsed_ms(&timer);
				total_conversion_time += conversion_time;

				timer_start(&timer);
				generate_sixel_display_data(&sixel_ctx);
				generation_time = timer_elapsed_ms(&timer);
				total_generation_time += generation_time;

				timer_start(&timer);
				if (tio_write(&ctx, sixel_ctx.data, sixel_ctx.data_size) == -1) {
					goto end;
				}
				display_time = timer_elapsed_ms(&timer);
				total_display_time += display_time;

				double current_end_time = timer_elapsed_ms(&timer_whole);
				double frame_time = current_end_time - previous_end_time;
				previous_end_time = current_end_time;
				total_frame_time += frame_time;

				printf("\x1b[H");    // Move cursor to home
				printf("\r\n");
				printf("Draw:          %0.2f\r\n", draw_time);
				printf("Conversion:    %0.2f\r\n", conversion_time);
				printf("Generation:    %0.2f\r\n", generation_time);
				printf("Display:       %0.2f\r\n", display_time);
				printf("Frame time:    %0.2f\r\n", frame_time);
				fflush(stdout);
			}
		}

	end:

// #pragma omp barrier
		if (thread_id == 0) {
			printf("\r\n");
			printf("Total times:\r\n");
			printf("Draw:          %0.2f\r\n", total_draw_time);
			printf("Conversion:    %0.2f\r\n", total_conversion_time);
			printf("Generation:    %0.2f\r\n", total_generation_time);
			printf("Display:       %0.2f\r\n", total_display_time);
			printf("Total frame time: %0.2f ms\r\n", total_frame_time);
			fflush(stdout);
			free_framebuffer_4i8(&fb);

			double whole_elapsed_ms = timer_elapsed_ms(&timer_whole);
			printf("\r\nTotal time for %d frames: %0.2f ms\r\n", 100, whole_elapsed_ms);
			printf("\x1b[?25h"); // Show cursor
			fflush(stdout);
		}
	}

	return 0;

}