#ifndef TEXTURES_H
#define TEXTURES_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef struct {
    void* data;
    int width;
    int height;
    int color_size;
} texture_t;

typedef struct {
	void* data;
    int length;
	int color_size;
}texture_atlas_t;

uint16_t convert_8r8g8b8a_to_5r6g5b(unsigned char r8, unsigned char g8, unsigned char b8) {
    uint16_t r5 = (r8 * 31) / 255;
    uint16_t g6 = (g8 * 63) / 255;
    uint16_t b5 = (b8 * 31) / 255;
    return (r5 << 11) | (g6 << 5) | b5;
}

void convert_8r8g8b8a_to_5r6g5b_texture(texture_t* src) {
	texture_t dst;
    dst.width = src->width;
    dst.height = src->height;
    dst.color_size = 2; // 16 bits per pixel
    dst.data = malloc(dst.width * dst.height * dst.color_size);
    if (!dst.data) return;
    uint32_t pixel_count = dst.width * dst.height;
    for (uint32_t i = 0; i < pixel_count; i++) {
        unsigned char r8 = ((unsigned char*)src->data)[i * 4 + 0];
        unsigned char g8 = ((unsigned char*)src->data)[i * 4 + 1];
        unsigned char b8 = ((unsigned char*)src->data)[i * 4 + 2];
        ((uint16_t*)dst.data)[i] = convert_8r8g8b8a_to_5r6g5b(r8, g8, b8);
    }
	free(src->data);
	*src = dst; // Caller should now use dst as the new texture
}

void append_3f32rgb_to_5r6g5b_texture_atlas(float src[3], texture_atlas_t* atlas) {
    uint16_t* output_ptr = (uint16_t*)atlas->data;
    output_ptr += atlas->length;
    unsigned char r = (unsigned char)clamp_int(lroundf(src[0] * 255.0f), 0, 255);
    unsigned char g = (unsigned char)clamp_int(lroundf(src[1] * 255.0f), 0, 255);
    unsigned char b = (unsigned char)clamp_int(lroundf(src[2] * 255.0f), 0, 255);
    *output_ptr = convert_8r8g8b8a_to_5r6g5b(r, g, b);
    atlas->length += 1;
}

void append_5r6g5b_to_5r6g5b_texture_atlas(uint16_t src, texture_atlas_t* atlas) {
    uint16_t* output_ptr = (uint16_t*)atlas->data;
    output_ptr += atlas->length;
    *output_ptr = src;
    atlas->length += 1;
}

void append_8r8g8b8a_texture_to_5r6g5b_texture_atlas(texture_t* src, texture_atlas_t* atlas) {
    int width = src->width;
    int height = src->height;
    uint32_t pixel_count = width * height;
    uint16_t* output_ptr = (uint16_t*)atlas->data;
	output_ptr += atlas->length;
    for (uint32_t i = 0; i < pixel_count; i++) {
        unsigned char r8 = ((unsigned char*)src->data)[i * 4 + 0];
        unsigned char g8 = ((unsigned char*)src->data)[i * 4 + 1];
        unsigned char b8 = ((unsigned char*)src->data)[i * 4 + 2];
        *output_ptr++ = convert_8r8g8b8a_to_5r6g5b(r8, g8, b8);
    }
	atlas->length += pixel_count;
}

void convert_5r6g5b_to_8r8g8b8a(uint16_t src, unsigned char* rgba_out) {
    uint16_t r5 = (src >> 11) & 0x1F;
    uint16_t g6 = (src >> 5) & 0x3F;
    uint16_t b5 = src & 0x1F;
    rgba_out[0] = (r5 * 255) / 31;
    rgba_out[1] = (g6 * 255) / 63;
    rgba_out[2] = (b5 * 255) / 31;
    rgba_out[3] = 255; // Opaque alpha
}
#endif // TEXTURES_H