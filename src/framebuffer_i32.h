#ifndef FRAMEBUFFER_I32_H
#define FRAMEBUFFER_I32_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    int width;
    int height;
    int32_t *data;
} framebuffer_i32;

framebuffer_i32 create_framebuffer_i32(int width, int height) {
    framebuffer_i32 fb;
    fb.width = width;
    fb.height = height;
#if defined(__linux__)
    fb.data = (int32_t*)aligned_alloc(32, width * height * sizeof(int32_t)); // 32-byte aligned for AVX
#endif
#if defined(_WIN32)
     fb.data = (int32_t *)malloc(width * height * sizeof(int32_t));
    //fb.data = (int32_t *)_aligned_malloc(width * height * sizeof(int32_t), 32); // 32-byte aligned for AVX
#endif
    if (fb.data == NULL) {
        fprintf(stderr, "Failed to allocate memory for framebuffer\n");
        exit(1);
    }
    memset(fb.data, 0, width * height * sizeof(int32_t)); // Initialize to 0.0f
    return fb;
}

void free_framebuffer_i32(framebuffer_i32 *fb) {
    if (fb->data) {
        free(fb->data);
        fb->data = NULL;
    }
}
int set_pixel_i32(framebuffer_i32 *fb, int x, int y, int32_t val) {
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) return -1;
    int index = (y * fb->width + x);
    fb->data[index] = val;
    return 0;
}

int get_pixel_i32(framebuffer_i32 *fb, int x, int y, int32_t *val) {
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) return -1;
    int index = (y * fb->width + x);
    *val = fb->data[index];
    return 0;
}

int clear_framebuffer_i32(framebuffer_i32 *fb, int32_t val) {
    for (int y = 0; y < fb->height; y++) {
        for (int x = 0; x < fb->width; x++) {
            set_pixel_i32(fb, x, y, val);
        }
    }
    return 0;
}

#endif // FRAMEBUFFER_F_H