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
    CHECK(ctx.data     != NULL);
    CHECK(ctx.data_cap  > 0);
    CHECK(ctx.data_size == 0);
    CHECK(ctx.width    == 80);
    CHECK(ctx.height   == 24);
    CHECK(ctx._owns_scratch == 0);
    tio_gfx_halfblock_destroy(&ctx);
    CHECK(ctx.data == NULL);
    printf("PASS: test_init_destroy\n");
}

static void test_init_shared(void) {
    tio_gfx_halfblock_ctx ctxs[3];
    tio_gfx_halfblock_init_shared(ctxs, 3, TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(80, 24));

    CHECK(ctxs[0].data != NULL);
    CHECK(ctxs[1].data != NULL);
    CHECK(ctxs[2].data != NULL);
    CHECK(ctxs[0].data != ctxs[1].data);
    CHECK(ctxs[1].data != ctxs[2].data);
    CHECK(ctxs[0]._owns_scratch == 0);
    CHECK(ctxs[1]._owns_scratch == 0);

    tio_gfx_halfblock_destroy_shared(ctxs, 3);
    CHECK(ctxs[0].data == NULL);
    CHECK(ctxs[1].data == NULL);
    printf("PASS: test_init_shared\n");
}

static void test_set_params(void) {
    tio_gfx_halfblock_ctx ctx;
    tio_gfx_halfblock_init(&ctx, TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(80, 24));

    char*  old_data = ctx.data;
    size_t old_cap  = ctx.data_cap;

    /* grow — must reallocate */
    tio_gfx_halfblock_set_params(&ctx, TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(80, 48));
    CHECK(ctx.width    == 80);
    CHECK(ctx.height   == 48);
    CHECK(ctx.data_cap  > old_cap);
    CHECK(ctx.data     != old_data);

    /* shrink — no realloc */
    char* ptr_before = ctx.data;
    tio_gfx_halfblock_set_params(&ctx, TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(80, 24));
    CHECK(ctx.data == ptr_before);

    tio_gfx_halfblock_destroy(&ctx);
    printf("PASS: test_set_params\n");
}

static void test_generate_header(void) {
    tio_gfx_halfblock_ctx ctx;
    tio_gfx_halfblock_init(&ctx, TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(8, 4));

    tio_gfx_halfblock_generate(&ctx, NULL, TIO_GFX_FMT_RGBA8, TIO_GFX_HEADER);
    CHECK(ctx.data_size == 3);
    CHECK(ctx.data[0] == '\x1b' && ctx.data[1] == '[' && ctx.data[2] == 'H');

    tio_gfx_halfblock_generate(&ctx, NULL, TIO_GFX_FMT_RGBA8, TIO_GFX_FOOTER);
    CHECK(ctx.data_size == 4);
    /* \x1b[0m */
    CHECK(ctx.data[0] == '\x1b' && ctx.data[1] == '[' &&
          ctx.data[2] == '0'    && ctx.data[3] == 'm');

    tio_gfx_halfblock_destroy(&ctx);
    printf("PASS: test_generate_header\n");
}

static void test_generate_full(void) {
    tio_gfx_halfblock_ctx ctx;
    tio_gfx_halfblock_init(&ctx, TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(8, 4));

    /* solid red RGBA8 */
    unsigned char pixels[8 * 4 * 4];
    for (int i = 0; i < 8 * 4; i++) {
        pixels[i*4+0] = 255; pixels[i*4+1] = 0;
        pixels[i*4+2] = 0;   pixels[i*4+3] = 255;
    }

    tio_gfx_halfblock_generate(&ctx, pixels, TIO_GFX_FMT_RGBA8, TIO_GFX_FULL);
    CHECK(ctx.data_size > 0);
    /* starts with cursor-home */
    CHECK(ctx.data[0] == '\x1b' && ctx.data[1] == '[' && ctx.data[2] == 'H');
    /* ends with reset */
    CHECK(ctx.data[ctx.data_size - 4] == '\x1b');
    CHECK(ctx.data[ctx.data_size - 3] == '[');
    CHECK(ctx.data[ctx.data_size - 2] == '0');
    CHECK(ctx.data[ctx.data_size - 1] == 'm');

    tio_gfx_halfblock_destroy(&ctx);
    printf("PASS: test_generate_full\n");
}

static void test_generate_odd_height(void) {
    tio_gfx_halfblock_ctx ctx;
    /* odd height — last row has only one pixel row */
    tio_gfx_halfblock_init(&ctx, TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(4, 3));

    unsigned char pixels[4 * 3 * 4];
    memset(pixels, 128, sizeof(pixels));

    tio_gfx_halfblock_generate(&ctx, pixels, TIO_GFX_FMT_RGBA8, TIO_GFX_FULL);
    CHECK(ctx.data_size > 0);

    tio_gfx_halfblock_destroy(&ctx);
    printf("PASS: test_generate_odd_height\n");
}

static void test_stats(void) {
    tio_gfx_halfblock_ctx ctx;
    tio_gfx_halfblock_init(&ctx, TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(8, 4));
    unsigned char pixels[8 * 4 * 4];
    memset(pixels, 64, sizeof(pixels));

    tio_gfx_halfblock_generate(&ctx, pixels, TIO_GFX_FMT_RGBA8, TIO_GFX_FULL);
    CHECK(ctx.total_generate_ms >= 0.0);

    tio_gfx_halfblock_print_stats(&ctx);

    tio_gfx_halfblock_reset_stats(&ctx);
    CHECK(ctx.total_generate_ms == 0.0);

    tio_gfx_halfblock_destroy(&ctx);
    printf("PASS: test_stats\n");
}

int main(void) {
    test_init_destroy();
    test_init_shared();
    test_set_params();
    test_generate_header();
    test_generate_full();
    test_generate_odd_height();
    test_stats();
    printf("All tests passed.\n");
    return 0;
}
