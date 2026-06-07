#ifndef FRAMEBUFFER_4I8_H
#define FRAMEBUFFER_4I8_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { // 4 channels, 8 bits per channel
    int width;
    int height;
    unsigned char *data;
} framebuffer_4i8;

framebuffer_4i8 create_framebuffer_4i8(int width, int height) {
    framebuffer_4i8 fb;
    fb.width = width;
    fb.height = height;
#if defined(__linux__) || defined(__APPLE__)
   size_t sz_4i8 = (size_t)width * height * 4;
   sz_4i8 += sz_4i8 % 32 ? 32 - sz_4i8 % 32 : 0;
   fb.data = (unsigned char*)aligned_alloc(32, sz_4i8);
#endif
#if defined(_WIN32)
    fb.data = (unsigned char*)malloc(width * height * 4);
    //fb.data = (unsigned char*)_aligned_malloc(width * height * 4, 32); // 32-byte aligned for AVX
#endif
    if (fb.data == NULL) {
        fprintf(stderr, "Failed to allocate memory for framebuffer\n");
        exit(1);
    }
    return fb;
}

void free_framebuffer_4i8(framebuffer_4i8 *fb) {
    if (fb->data) {
        free(fb->data);
        fb->data = NULL;
    }
}
int set_pixel_4i8(framebuffer_4i8 *fb, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) return -1;
    int index = (y * fb->width + x) * 4;
    fb->data[index + 0] = r;
    fb->data[index + 1] = g;
    fb->data[index + 2] = b;
    fb->data[index + 3] = a;
    return 0;
}

int get_pixel_4i8(framebuffer_4i8 *fb, int x, int y, unsigned char *r, unsigned char *g, unsigned char *b, unsigned char *a) {
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) return -1;
    int index = (y * fb->width + x) * 4;
    *r = fb->data[index + 0];
    *g = fb->data[index + 1];
    *b = fb->data[index + 2];
    *a = fb->data[index + 3];
    return 0;
}

int clear_framebuffer_4i8(framebuffer_4i8 *fb, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    for (int y = 0; y < fb->height; y++) {
        for (int x = 0; x < fb->width; x++) {
            set_pixel_4i8(fb, x, y, r, g, b, a);
        }
    }
    return 0;
}
#endif // FRAMEBUFFER_4I8_H
