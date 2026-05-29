#ifndef FRAMEBUFFER_F_H
#define FRAMEBUFFER_F_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	int width;
	int height;
	float* data;
} framebuffer_f;

framebuffer_f create_framebuffer_f(int width, int height) {
	framebuffer_f fb;
	fb.width = width;
	fb.height = height;
#if defined(__linux__) || defined(__APPLE__)
	fb.data = (float*)aligned_alloc(32, width * height * sizeof(float)); // 32-byte aligned for AVX
#endif
#if defined(_WIN32)
	fb.data = (float*)malloc(width * height * sizeof(float));
	//fb.data = (float *)_aligned_malloc(width * height * sizeof(float), 32); // 32-byte aligned for AVX
#endif
	if (fb.data == NULL) {
		fprintf(stderr, "Failed to allocate memory for framebuffer\n");
		exit(1);
	}
	memset(fb.data, 0, width * height * sizeof(float)); // Initialize to 0.0f
	return fb;
}

void free_framebuffer_f(framebuffer_f* fb) {
	if (fb->data) {
		free(fb->data);
		fb->data = NULL;
	}
}
int set_pixel_f(framebuffer_f* fb, int x, int y, float val) {
	if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) return -1;
	int index = (y * fb->width + x);
	fb->data[index] = val;
	return 0;
}

int get_pixel_f(framebuffer_f* fb, int x, int y, float* val) {
	if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) return -1;
	int index = (y * fb->width + x);
	*val = fb->data[index];
	return 0;
}

int clear_framebuffer_f(framebuffer_f* fb, float val) {
	int length = fb->width * fb->height;
	float* data = fb->data;
	for (int i = 0;i < length;i++) {
		data[i] = val;
	}
	return 0;
}

#endif // FRAMEBUFFER_F_H
