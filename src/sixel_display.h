#ifndef SIXEL_DISPLAY_H
#define SIXEL_DISPLAY_H

#include "bayer_patterns.h"
#include "framebuffer_4i8.h"
#include "utils.h"

typedef int sixel_palette_color[3];

enum sixel_palette_color_type {
    SIXEL_PALETTE_COLOR_TYPE_HLS = 1,
    SIXEL_PALETTE_COLOR_TYPE_RGB = 2,
};

typedef struct {
    int size;
    sixel_palette_color *colors;
    enum sixel_palette_color_type color_type;
} sixel_palette;

typedef struct {
    unsigned char *index_data;
    int width;
    int height;
    sixel_palette palette;
} sixel_indexed_bitmap;

void init_sixel_indexed_bitmap(sixel_indexed_bitmap *bitmap, int width, int height) {
    bitmap->width = width;
    bitmap->height = height;
    bitmap->index_data = (unsigned char *)malloc(sizeof(unsigned char) * width * height);
}

void free_sixel_indexed_bitmap(sixel_indexed_bitmap *bitmap) {
    if (bitmap->index_data) {
        free(bitmap->index_data);
        bitmap->index_data = NULL;
    }
}

void init_sixel_palette(sixel_palette *palette, int size, enum sixel_palette_color_type color_type) {
    palette->size = size;
    palette->color_type = color_type;
    palette->colors = (sixel_palette_color *)malloc(sizeof(sixel_palette_color) * size);
}

void free_sixel_palette(sixel_palette *palette) {
    if (palette->colors) {
        free(palette->colors);
        palette->colors = NULL;
    }
}

void init_sixel_palette_rgbuniform(sixel_palette *palette, int num_steps) {
    int size = (num_steps + 1) * (num_steps + 1) * (num_steps + 1);
    init_sixel_palette(palette, size, SIXEL_PALETTE_COLOR_TYPE_RGB);
    int color_num = 0;
    for (int i = 0; i <= num_steps; i++) {
        for (int j = 0; j <= num_steps; j++) {
            for (int k = 0; k <= num_steps; k++) {
                palette->colors[color_num][0] = (100 * i) / num_steps;
                palette->colors[color_num][1] = (100 * j) / num_steps;
                palette->colors[color_num][2] = (100 * k) / num_steps;
                color_num++;
            }
        }
    }
}

int closest_point(int x, int num_steps) {
    return ((2 * x * num_steps + 255) * num_steps) / 255 / 2 / num_steps;
}

void convert_4i8_to_sixel_indexed_bitmap_rgbuniform(sixel_indexed_bitmap *bitmap, framebuffer_4i8 fb, int num_steps) {
    int img_x = fb.width, img_y = fb.height, img_n = 4;
    unsigned char *image_buffer = fb.data;
    for (int y = 0; y < img_y; y++) {
        for (int x = 0; x < img_x; x++) {
            int r = image_buffer[y * img_n * img_x + x * img_n + 0];
            int g = image_buffer[y * img_n * img_x + x * img_n + 1];
            int b = image_buffer[y * img_n * img_x + x * img_n + 2];

            int r_x = closest_point(r, num_steps);
            int g_x = closest_point(g, num_steps);
            int b_x = closest_point(b, num_steps);

            int color_num = r_x * (num_steps + 1) * (num_steps + 1) + g_x * (num_steps + 1) + b_x;

            bitmap->index_data[y * img_x + x] = color_num;
        }
    }
}

void convert_4i8_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering(sixel_indexed_bitmap *bitmap, framebuffer_4i8 fb, int num_steps) {
    int img_x = fb.width, img_y = fb.height, img_n = 4;
    unsigned char *image_buffer = fb.data;
    unsigned char divider = 256 / num_steps;
    int step_plus_one = num_steps + 1;
    int step_plus_one_squared = step_plus_one * step_plus_one;
    ivec3 steps_vec = {step_plus_one_squared, step_plus_one, 1};
    
    for (int y = 0; y < img_y; y++) {
        unsigned char bayer_y = y % 16;
        for (int x = 0; x < img_x; x++) {
            int bayer_x = x % 16;
            int r = image_buffer[y * img_n * img_x + x * img_n + 0];
            int g = image_buffer[y * img_n * img_x + x * img_n + 1];
            int b = image_buffer[y * img_n * img_x + x * img_n + 2];
            ivec3 color_vec = {r, g, b};
            
            int t = BAYER_PATTERN_16X16[bayer_y][bayer_x];
            int corr = (t / num_steps);
            glm_ivec3_adds(color_vec, corr, color_vec);
            glm_ivec3_divs(color_vec, divider, color_vec);
            // int r_x = (r + corr) / divider;
            // int g_x = (g + corr) / divider;
            // int b_x = (b + corr) / divider;

            // r_x = clamp_int(r_x, 0, num_steps);
            // g_x = clamp_int(g_x, 0, num_steps);
            // b_x = clamp_int(b_x, 0, num_steps);

            // int color_num = r_x * step_plus_one_squared + g_x * step_plus_one + b_x;
            int color_num = glm_ivec3_dot(color_vec, steps_vec);
            // color_num = 215;
            bitmap->index_data[y * img_x + x] = color_num;
        }
    }
}

typedef struct {
    sixel_indexed_bitmap bitmap;
    char *data;
    char *header_data;
    char *sixel_data;
    int header_valid;
    int header_data_size;
    int data_size;
} sixel_display_ctx;

void init_sixel_display_ctx(sixel_display_ctx *ctx, int width, int height) {
    init_sixel_indexed_bitmap(&ctx->bitmap, width, height);
    ctx->data = (char *)malloc((width * height * 8) + 1024);
    ctx->header_data = ctx->data;
    ctx->sixel_data = NULL;
    ctx->header_valid = 0;
    ctx->header_data_size = 0;
    ctx->data_size = 0;
}

void free_sixel_display_ctx(sixel_display_ctx *ctx) {
    if (ctx->data) {
        free(ctx->data);
        ctx->data = NULL;
    }
}

char *fast_itoa(char *buffer, int value) {
    if (value == 0) {
        *buffer++ = '0';
        return buffer;
    }

    // A temporary buffer to store the digits in reverse order.
    // An int can have at most 10 digits, plus a sign.
    char temp[12];
    char *p = temp;

    int is_negative = (value < 0);
    if (is_negative) {
        value = -value;
    }

    // Extract digits in reverse order
    while (value > 0) {
        *p++ = '0' + (value % 10);
        value /= 10;
    }

    if (is_negative) {
        *p++ = '-';
    }

    // Reverse the temporary string and copy it to the output buffer
    while (p > temp) {
        *buffer++ = *--p;
    }

    return buffer;
}

void generate_sixel_display_data(sixel_display_ctx *ctx) {
    if (!ctx->header_valid) {
        char *palette_ptr = ctx->header_data; // Reset pointer to the start
        palette_ptr += sprintf(palette_ptr, "\x1b[H\x1bP9;1;;q");

        for (int i = 0; i <= ctx->bitmap.palette.size; i++) {
            palette_ptr += sprintf(palette_ptr, "#%d;2;%d;%d;%d", i,
                                   ctx->bitmap.palette.colors[i][0],
                                   ctx->bitmap.palette.colors[i][1],
                                   ctx->bitmap.palette.colors[i][2]);
        }

        ctx->header_data_size = palette_ptr - ctx->header_data; // Store the final length
        ctx->header_valid = 1;
        ctx->sixel_data = palette_ptr;
    }

    char *buffer_ptr = ctx->sixel_data;

    for (int i = 0; i < ctx->bitmap.height; i += 6) {
        for (int y_offset = 0; y_offset < 6; y_offset++) {
            int y = i + y_offset;
            if (y >= ctx->bitmap.height) break;
            for (int x = 0; x < ctx->bitmap.width; x++) {
                int color_num = ctx->bitmap.index_data[y * ctx->bitmap.width + x];

                *buffer_ptr++ = '#';
                buffer_ptr = fast_itoa(buffer_ptr, color_num);
                *buffer_ptr++ = (char)(0x3F + (1 << (y_offset)));
            }
            *buffer_ptr++ = '$';
        }
        *buffer_ptr++ = '-';
    }

    // Append Sixel terminator to the pixel buffer
    buffer_ptr += sprintf(buffer_ptr, "\x1b\\");
    *buffer_ptr = '\0';
    ctx->data_size = buffer_ptr - ctx->data;
}

#endif // SIXEL_DISPLAY_H