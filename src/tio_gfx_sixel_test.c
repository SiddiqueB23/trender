#define TIO_GFX_USE_AVX2
#define TIO_GFX_SIXEL_IMPLEMENTATION
#include "tio_gfx_sixel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Always-on check — CHECK() is a no-op in Release builds */
#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

static void test_init_destroy(void) {
    tio_gfx_sixel_ctx ctx;
    tio_gfx_sixel_init(&ctx, TIO_GFX_SIXEL_DEFAULT_PARAMS(80, 24));
    CHECK(ctx.width  == 80);
    CHECK(ctx.height == 24);
    CHECK(ctx._index_data         != NULL);
    CHECK(ctx._scratch_painted    != NULL);
    CHECK(ctx._scratch_transposed != NULL);
    tio_gfx_sixel_destroy(&ctx);
    CHECK(ctx._index_data         == NULL);
    CHECK(ctx._scratch_painted    == NULL);
    CHECK(ctx._scratch_transposed == NULL);
    printf("PASS: test_init_destroy\n");
}

static void test_set_params(void) {
    tio_gfx_sixel_ctx ctx;
    tio_gfx_sixel_init(&ctx, TIO_GFX_SIXEL_DEFAULT_PARAMS(80, 24));

    tio_gfx_sixel_set_params(&ctx, TIO_GFX_SIXEL_DEFAULT_PARAMS(80, 48));
    CHECK(ctx.width  == 80);
    CHECK(ctx.height == 48);

    tio_gfx_sixel_set_params(&ctx, TIO_GFX_SIXEL_DEFAULT_PARAMS(80, 24));
    CHECK(ctx.width  == 80);
    CHECK(ctx.height == 24);

    tio_gfx_sixel_destroy(&ctx);
    printf("PASS: test_set_params\n");
}

static void test_generate_header(void) {
    tio_gfx_sixel_ctx ctx;
    tio_gfx_sixel_init(&ctx, TIO_GFX_SIXEL_DEFAULT_PARAMS(8, 8));

    size_t cap = tio_gfx_sixel_output_size_hint(TIO_GFX_SIXEL_DEFAULT_PARAMS(8, 8));
    char* buf = (char*)malloc(cap);

    int n = tio_gfx_sixel_generate(&ctx, NULL, TIO_GFX_FMT_RGBA8, TIO_GFX_HEADER, buf, cap);
    CHECK(n > 0);
    CHECK(buf[0] == '\x1b' && buf[1] == '[' && buf[2] == 'H');
    CHECK(buf[3] == '\x1b' && buf[4] == 'P');

    n = tio_gfx_sixel_generate(&ctx, NULL, TIO_GFX_FMT_RGBA8, TIO_GFX_FOOTER, buf, cap);
    CHECK(n == 2);
    CHECK(buf[0] == '\x1b' && buf[1] == '\\');

    free(buf);
    tio_gfx_sixel_destroy(&ctx);
    printf("PASS: test_generate_header\n");
}

static void test_generate_full(void) {
    tio_gfx_sixel_ctx ctx;
    tio_gfx_sixel_params p = TIO_GFX_SIXEL_DEFAULT_PARAMS(16, 12);
    tio_gfx_sixel_init(&ctx, p);

    size_t cap = tio_gfx_sixel_output_size_hint(p);
    char* buf = (char*)malloc(cap);

    unsigned char pixels[16 * 12 * 4];
    for (int i = 0; i < 16 * 12; i++) {
        pixels[i*4+0] = 255; pixels[i*4+1] = 0;
        pixels[i*4+2] = 0;   pixels[i*4+3] = 255;
    }

    int n = tio_gfx_sixel_generate(&ctx, pixels, TIO_GFX_FMT_RGBA8, TIO_GFX_FULL, buf, cap);
    CHECK(n > 0);
    CHECK(buf[0] == '\x1b' && buf[1] == '[' && buf[2] == 'H');
    CHECK(buf[n - 2] == '\x1b');
    CHECK(buf[n - 1] == '\\');

    free(buf);
    tio_gfx_sixel_destroy(&ctx);
    printf("PASS: test_generate_full\n");
}

static void test_stats(void) {
    tio_gfx_sixel_ctx ctx;
    tio_gfx_sixel_params p = TIO_GFX_SIXEL_DEFAULT_PARAMS(8, 6);
    tio_gfx_sixel_init(&ctx, p);

    size_t cap = tio_gfx_sixel_output_size_hint(p);
    char* buf = (char*)malloc(cap);
    unsigned char pixels[8 * 6 * 4];
    memset(pixels, 128, sizeof(pixels));

    tio_gfx_sixel_generate(&ctx, pixels, TIO_GFX_FMT_RGBA8, TIO_GFX_FULL, buf, cap);
    CHECK(ctx.total_generate_ms >= 0.0);

    tio_gfx_sixel_print_stats(&ctx);

    tio_gfx_sixel_reset_stats(&ctx);
    CHECK(ctx.total_generate_ms == 0.0);

    free(buf);
    tio_gfx_sixel_destroy(&ctx);
    printf("PASS: test_stats\n");
}

int main(void) {
    test_init_destroy();
    test_set_params();
    test_generate_header();
    test_generate_full();
    test_stats();
    printf("All tests passed.\n");
    return 0;
}
