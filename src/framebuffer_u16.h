#ifndef FRAMEBUFFER_U16_H
#define FRAMEBUFFER_U16_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct {
	int width;
	int height;
	uint16_t* data;
} framebuffer_u16;

framebuffer_u16 create_framebuffer_u16(int width, int height) {
	framebuffer_u16 fb;
	fb.width = width;
	fb.height = height;
#if defined(__linux__) || defined(__APPLE__)
	fb.data = (uint16_t*)aligned_alloc(32, width * height * sizeof(uint16_t)); // 32-byte aligned for AVX
#endif
#if defined(_WIN32)
	fb.data = (uint16_t*)malloc(width * height * sizeof(uint16_t));
	//fb.data = (uint16_t *)_aligned_malloc(width * height * sizeof(uint16_t), 32); // 32-byte aligned for AVX
#endif
	if (fb.data == NULL) {
		fprintf(stderr, "Failed to allocate memory for framebuffer\n");
		exit(1);
	}
	memset(fb.data, 0, width * height * sizeof(uint16_t)); // Initialize to 0.0f
	return fb;
}

void free_framebuffer_u16(framebuffer_u16* fb) {
	if (fb->data) {
		free(fb->data);
		fb->data = NULL;
	}
}
int set_pixel_u16(framebuffer_u16* fb, int x, int y, uint16_t val) {
	if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) return -1;
	int index = (y * fb->width + x);
	fb->data[index] = val;
	return 0;
}

int get_pixel_u16(framebuffer_u16* fb, int x, int y, uint16_t* val) {
	if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) return -1;
	int index = (y * fb->width + x);
	*val = fb->data[index];
	return 0;
}

int clear_framebuffer_u16(framebuffer_u16* fb, uint16_t val) {
	if (val == (uint16_t)0x0000 || val == (uint16_t)0xFFFF) {
		memset(fb->data, (int)(val & 0xFF), fb->width * fb->height * sizeof(uint16_t));
		return 0;
	}
	for (int y = 0; y < fb->height; y++) {
		for (int x = 0; x < fb->width; x++) {
			set_pixel_u16(fb, x, y, val);
		}
	}
	return 0;
}

#endif // FRAMEBUFFER_F_H