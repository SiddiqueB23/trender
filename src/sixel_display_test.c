#include "sixel_display.h"
#include "timer.h"
#include "tio.h"
#include <stdio.h>
#include <stdlib.h>

tio_ctx_t ctx;

void cleanup(void) {
	tio_destroy(&ctx);
}

void random_bitmap(sixel_indexed_bitmap* bitmap) {
	for (int i = 0; i < bitmap->width * bitmap->height; i++) {
		bitmap->index_data[i] = rand() % bitmap->palette.size;
	}
}

void checkered_bitmap(sixel_indexed_bitmap* bitmap, int size, int color1, int color2) {
	for (int y = 0;y < bitmap->height;y++) {
		for (int x = 0;x < bitmap->width;x++) {
			if (((x / size) % 2 + (y / size) % 2) % 2 == 0)
				bitmap->index_data[y * bitmap->width + x] = color1;
			else
				bitmap->index_data[y * bitmap->width + x] = color2;
		}
	}
}

void random_squares_bitmap(sixel_indexed_bitmap* bitmap, int size) {
	for (int y = 0;y < bitmap->height;y += size) {
		for (int x = 0;x < bitmap->width;x += size) {
			int color = rand() % bitmap->palette.size;
			for (int i = 0;i < size && i + y < bitmap->height;i++) {
				for (int j = 0;j < size && j + x < bitmap->width;j++) {
					bitmap->index_data[(y + i) * bitmap->width + (x + j)] = color;
				}
			}
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
	cols = 80;
	rows = 50;
	// rows -= 2;
	rows *= 8;
	cols *= 8;
	printf("Window size: %d rows, %d cols\n", rows, cols);

	sixel_display_ctx sixel_ctx;
	init_sixel_display_ctx(&sixel_ctx, cols, rows);
	init_sixel_indexed_bitmap(&sixel_ctx.bitmap, cols, rows);
	init_sixel_palette_rgbuniform(&sixel_ctx.bitmap.palette, 5);

	monotonic_timer_t timer, timer_whole;
	timer_start(&timer_whole);
	int num_frames = 100;
	while (num_frames--) {
		//printf("\x1b[2J");   // Clear screen
		//fflush(stdout);

		timer_start(&timer);
		//random_bitmap(&sixel_ctx.bitmap);
		random_squares_bitmap(&sixel_ctx.bitmap, 1);
		//checkered_bitmap(&sixel_ctx.bitmap, 3, 50, 200);
		double randomization_elapsed_ms = timer_elapsed_ms(&timer);

		timer_start(&timer);
		generate_sixel_display_data(&sixel_ctx);
		double generation_elapsed_ms = timer_elapsed_ms(&timer);

		timer_start(&timer);
		if (tio_write(&ctx, sixel_ctx.data, sixel_ctx.data_size) == -1) {
			goto end;
		}
		double display_elapsed_ms = timer_elapsed_ms(&timer);

		printf("\r\n");
		printf("Sixel Data Size: %d bytes\r\n", sixel_ctx.data_size);
		printf("Randomization: %0.2f\r\n", randomization_elapsed_ms);
		printf("Generation:    %0.2f\r\n", generation_elapsed_ms);
		printf("Display:       %0.2f\r\n", display_elapsed_ms);
		fflush(stdout);
	}
end:
	double whole_elapsed_ms = timer_elapsed_ms(&timer_whole);
	printf("\r\nTotal time for %d frames: %0.2f ms\r\n", 100, whole_elapsed_ms);

	printf("\x1b[?25h"); // Show cursor
	fflush(stdout);

}