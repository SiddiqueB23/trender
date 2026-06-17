#define TIO_GFX_HALFBLOCK_IMPLEMENTATION
#include "tio_gfx_halfblock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

static void test_init_destroy(void) {
    tio_gfx_halfblock_ctx ctx;
    tio_gfx_halfblock_init(&ctx, TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(80, 24));
    CHECK(ctx.width  == 80);
    CHECK(ctx.height == 24);
    tio_gfx_halfblock_destroy(&ctx);
    printf("PASS: test_init_destroy\n");
}

static void test_set_params(void) {
    tio_gfx_halfblock_ctx ctx;
    tio_gfx_halfblock_init(&ctx, TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(80, 24));

    tio_gfx_halfblock_set_params(&ctx, TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(80, 48));
    CHECK(ctx.width  == 80);
    CHECK(ctx.height == 48);

    tio_gfx_halfblock_set_params(&ctx, TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(80, 24));
    CHECK(ctx.width  == 80);
    CHECK(ctx.height == 24);

    tio_gfx_halfblock_destroy(&ctx);
    printf("PASS: test_set_params\n");
}

static void test_generate_header(void) {
    tio_gfx_halfblock_ctx ctx;
    tio_gfx_halfblock_params p = TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(8, 4);
    tio_gfx_halfblock_init(&ctx, p);

    size_t cap = tio_gfx_halfblock_output_size_hint(p);
    char* buf = (char*)malloc(cap);

    int n = tio_gfx_halfblock_generate(&ctx, NULL, TIO_GFX_FMT_RGBA8, TIO_GFX_HEADER, buf, cap);
    CHECK(n == 3);
    CHECK(buf[0] == '\x1b' && buf[1] == '[' && buf[2] == 'H');

    n = tio_gfx_halfblock_generate(&ctx, NULL, TIO_GFX_FMT_RGBA8, TIO_GFX_FOOTER, buf, cap);
    CHECK(n == 4);
    CHECK(buf[0] == '\x1b' && buf[1] == '[' &&
          buf[2] == '0'    && buf[3] == 'm');

    free(buf);
    tio_gfx_halfblock_destroy(&ctx);
    printf("PASS: test_generate_header\n");
}

static void test_generate_full(void) {
    tio_gfx_halfblock_ctx ctx;
    tio_gfx_halfblock_params p = TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(8, 4);
    tio_gfx_halfblock_init(&ctx, p);

    size_t cap = tio_gfx_halfblock_output_size_hint(p);
    char* buf = (char*)malloc(cap);

    unsigned char pixels[8 * 4 * 4];
    for (int i = 0; i < 8 * 4; i++) {
        pixels[i*4+0] = 255; pixels[i*4+1] = 0;
        pixels[i*4+2] = 0;   pixels[i*4+3] = 255;
    }

    int n = tio_gfx_halfblock_generate(&ctx, pixels, TIO_GFX_FMT_RGBA8, TIO_GFX_FULL, buf, cap);
    CHECK(n > 0);
    CHECK(buf[0] == '\x1b' && buf[1] == '[' && buf[2] == 'H');
    CHECK(buf[n - 4] == '\x1b');
    CHECK(buf[n - 3] == '[');
    CHECK(buf[n - 2] == '0');
    CHECK(buf[n - 1] == 'm');

    free(buf);
    tio_gfx_halfblock_destroy(&ctx);
    printf("PASS: test_generate_full\n");
}

static void test_generate_odd_height(void) {
    tio_gfx_halfblock_ctx ctx;
    tio_gfx_halfblock_params p = TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(4, 3);
    tio_gfx_halfblock_init(&ctx, p);

    size_t cap = tio_gfx_halfblock_output_size_hint(p);
    char* buf = (char*)malloc(cap);

    unsigned char pixels[4 * 3 * 4];
    memset(pixels, 128, sizeof(pixels));

    int n = tio_gfx_halfblock_generate(&ctx, pixels, TIO_GFX_FMT_RGBA8, TIO_GFX_FULL, buf, cap);
    CHECK(n > 0);

    free(buf);
    tio_gfx_halfblock_destroy(&ctx);
    printf("PASS: test_generate_odd_height\n");
}

static void test_stats(void) {
    tio_gfx_halfblock_ctx ctx;
    tio_gfx_halfblock_params p = TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(8, 4);
    tio_gfx_halfblock_init(&ctx, p);

    size_t cap = tio_gfx_halfblock_output_size_hint(p);
    char* buf = (char*)malloc(cap);
    unsigned char pixels[8 * 4 * 4];
    memset(pixels, 64, sizeof(pixels));

    tio_gfx_halfblock_generate(&ctx, pixels, TIO_GFX_FMT_RGBA8, TIO_GFX_FULL, buf, cap);
    CHECK(ctx.total_generate_ms >= 0.0);

    tio_gfx_halfblock_print_stats(&ctx);

    tio_gfx_halfblock_reset_stats(&ctx);
    CHECK(ctx.total_generate_ms == 0.0);

    free(buf);
    tio_gfx_halfblock_destroy(&ctx);
    printf("PASS: test_stats\n");
}

int main(void) {
    test_init_destroy();
    test_set_params();
    test_generate_header();
    test_generate_full();
    test_generate_odd_height();
    test_stats();
    printf("All tests passed.\n");
    return 0;
}
