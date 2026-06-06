#define TIO_GFX_HALFBLOCK_IMPLEMENTATION
#include "tio_gfx_halfblock.h"
#include "rainbow.h"
#include "tio.h"
#include "timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

tio_ctx_t tio;
void cleanup(void) { tio_destroy(&tio); }

int main(int argc, char** argv) {
    tio_gfx_halfblock_color_mode mode = TIO_GFX_HALFBLOCK_COLOR_24BIT;
    int upscale_x = 1, upscale_y = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-2") == 0) {
            mode = TIO_GFX_HALFBLOCK_COLOR_216;
        } else if (strcmp(argv[i], "-24") == 0) {
            mode = TIO_GFX_HALFBLOCK_COLOR_24BIT;
        } else if (strcmp(argv[i], "-ux") == 0 && i + 1 < argc) {
            upscale_x = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-uy") == 0 && i + 1 < argc) {
            upscale_y = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Usage: %s [-2 | -24] [-ux N] [-uy N]\n"
                            "  -2     216-color xterm cube with Bayer dithering\n"
                            "  -24    24-bit true color (default)\n"
                            "  -ux N  horizontal upscale factor (default 1)\n"
                            "  -uy N  vertical upscale factor (default 1)\n", argv[0]);
            return 1;
        }
    }
    if (upscale_x < 1) upscale_x = 1;
    if (upscale_y < 1) upscale_y = 1;

    tio_init(&tio);
    atexit(cleanup);
    printf("\x1b[2J\x1b[H\x1b[?25l");
    fflush(stdout);

    int term_rows, term_cols;
    if (tio_get_window_size(&tio, &term_rows, &term_cols) == -1) {
        fprintf(stderr, "Unable to get window size\r\n");
        return 1;
    }

    /* align to (8*ux) cols and (2*uy) pixel rows before dividing, matching main.c */
    int render_w = term_cols;
    int render_h = term_rows * 2;
    render_w -= render_w % (8 * upscale_x);
    render_h -= render_h % (2 * upscale_y);
    render_w /= upscale_x;
    render_h /= upscale_y;

    tio_gfx_halfblock_params params = TIO_GFX_HALFBLOCK_DEFAULT_PARAMS(render_w, render_h);
    params.color_mode = mode;
    params.upscale_x  = upscale_x;
    params.upscale_y  = upscale_y;
    tio_gfx_halfblock_ctx ctx;
    tio_gfx_halfblock_init(&ctx, params);

    unsigned char* pixels = (unsigned char*)malloc((size_t)(render_w * render_h * 4));

    monotonic_timer_t t;
    double total_fill_ms = 0.0, total_write_ms = 0.0;

    monotonic_timer_t wall;
    timer_start(&wall);
    for (int frame = 0; frame < 1000; frame++) {
        timer_start(&t);
        for (int y = 0; y < render_h; y++) {
            for (int x = 0; x < render_w; x++) {
                unsigned char r, g, b;
                get_rainbow(x + y + frame, &r, &g, &b);
                int idx = (y * render_w + x) * 4;
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
    printf("\r\n1000 frames in %.1f ms  [%s  ux=%d uy=%d]\r\n", timer_elapsed_ms(&wall),
           mode == TIO_GFX_HALFBLOCK_COLOR_216 ? "216-color" : "24-bit",
           upscale_x, upscale_y);
    printf("Render res:      %d x %d px (term %d x %d)\r\n",
           render_w, render_h, term_cols, term_rows);
    printf("Fill time:       %.2f ms\r\n", total_fill_ms);
    printf("Generation time: %.2f ms\r\n", ctx.total_generate_ms);
    printf("Write time:      %.2f ms\r\n", total_write_ms);
    printf("Average per frame: %.2f ms (fill) + %.2f ms (generate) + %.2f ms (write) = %.2f ms\r\n",
           total_fill_ms / 1000.0, ctx.total_generate_ms / 1000.0, total_write_ms / 1000.0,
           (total_fill_ms + ctx.total_generate_ms + total_write_ms) / 1000.0);
    printf("Screen Size: %d rows, %d cols, %d pixels\r\n", term_rows, term_cols, term_rows * term_cols);
    free(pixels);
    tio_gfx_halfblock_destroy(&ctx);
    printf("\x1b[?25h\r\n");
    fflush(stdout);
    return 0;
}
