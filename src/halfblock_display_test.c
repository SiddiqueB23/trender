#define TIO_GFX_HALFBLOCK_IMPLEMENTATION
#include "tio_gfx_halfblock.h"
#include "rainbow.h"
#include "tio.h"
#include "timer.h"
#include <stdio.h>
#include <stdlib.h>

tio_ctx_t tio;
void cleanup(void) { tio_destroy(&tio); }

int main(int argc, char** argv) {
    tio_gfx_halfblock_color_mode mode = TIO_GFX_HALFBLOCK_COLOR_24BIT;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == '2' && argv[i][2] == '\0')
            mode = TIO_GFX_HALFBLOCK_COLOR_216;
        else if (argv[i][0] == '-' && argv[i][1] == '2' && argv[i][2] == '4')
            mode = TIO_GFX_HALFBLOCK_COLOR_24BIT;
        else {
            fprintf(stderr, "Usage: %s [-2 | -24]\n"
                            "  -2   216-color xterm cube with Bayer dithering\n"
                            "  -24  24-bit true color (default)\n", argv[0]);
            return 1;
        }
    }

    tio_init(&tio);
    atexit(cleanup);
    printf("\x1b[2J\x1b[H\x1b[?25l");
    fflush(stdout);

    int term_rows, term_cols;
    if (tio_get_window_size(&tio, &term_rows, &term_cols) == -1) {
        fprintf(stderr, "Unable to get window size\r\n");
        return 1;
    }

    /* Each terminal character row covers 2 pixel rows. */
    int pixel_w = term_cols;
    int pixel_h = (term_rows) * 2;

    tio_gfx_halfblock_params params = TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(pixel_w, pixel_h);
    params.color_mode = mode;
    tio_gfx_halfblock_ctx ctx;
    tio_gfx_halfblock_init(&ctx, params);

    unsigned char* pixels = (unsigned char*)malloc((size_t)(pixel_w * pixel_h * 4));

    monotonic_timer_t t;
    double total_fill_ms = 0.0, total_write_ms = 0.0;

    monotonic_timer_t wall;
    timer_start(&wall);
    for (int frame = 0; frame < 100; frame++) {
        timer_start(&t);
        for (int y = 0; y < pixel_h; y++) {
            for (int x = 0; x < pixel_w; x++) {
                unsigned char r, g, b;
                get_rainbow(x + y + frame, &r, &g, &b);
                int idx = (y * pixel_w + x) * 4;
                pixels[idx + 0] = r;
                pixels[idx + 1] = g;
                pixels[idx + 2] = b;
                pixels[idx + 3] = 255;
            }
        }
        total_fill_ms += timer_elapsed_ms(&t);

        tio_gfx_halfblock_generate(&ctx, pixels, TIO_GFX_FMT_RGBA8, TIO_GFX_FULL);

        timer_start(&t);
        tio_write(&tio, ctx.data, ctx.data_size);
        total_write_ms += timer_elapsed_ms(&t);

        printf("\x1b[H\r\nframe %d  size=%zu bytes\r\n", frame, ctx.data_size);
        fflush(stdout);
    }
    printf("\r\n100 frames in %.1f ms  [%s]\r\n", timer_elapsed_ms(&wall),
           mode == TIO_GFX_HALFBLOCK_COLOR_216 ? "216-color" : "24-bit");
    printf("Fill time:       %.2f ms\r\n", total_fill_ms);
    printf("Generation time: %.2f ms\r\n", ctx.total_generate_ms);
    printf("Write time:      %.2f ms\r\n", total_write_ms);
    printf("Average per frame: %.2f ms (fill) + %.2f ms (generate) + %.2f ms (write) = %.2f ms\r\n",
           total_fill_ms / 100.0, ctx.total_generate_ms / 100.0, total_write_ms / 100.0,
           (total_fill_ms + ctx.total_generate_ms + total_write_ms) / 100.0);
    printf("Screen Size: %d rows, %d cols, %d pixels\r\n", term_rows, term_cols, term_rows * term_cols);
    free(pixels);
    tio_gfx_halfblock_destroy(&ctx);
    printf("\x1b[?25h\r\n");
    fflush(stdout);
    return 0;
}
