#define TIO_GFX_SIXEL_IMPLEMENTATION
#include "tio_gfx_sixel.h"
#include "tio.h"
#include "timer.h"
#include <stdio.h>
#include <stdlib.h>

tio_ctx_t tio;
void cleanup(void) { tio_destroy(&tio); }

int main(void) {
    tio_init(&tio);
    atexit(cleanup);
    printf("\x1b[2J\x1b[H\x1b[?25l");
    fflush(stdout);

    const int cols = 80 * 8, rows = 50 * 8;
    tio_gfx_sixel_params params = TIO_GFX_SIXEL_DEFAULT_PARAMS(cols, rows);
    tio_gfx_sixel_ctx ctx;
    tio_gfx_sixel_init(&ctx, params);

    unsigned char* pixels = (unsigned char*)malloc((size_t)(cols * rows * 4));
    size_t out_cap = tio_gfx_sixel_output_size_hint(params);
    char*  out_buf = (char*)malloc(out_cap);

    monotonic_timer_t wall;
    timer_start(&wall);
    for (int frame = 0; frame < 100; frame++) {
        for (int i = 0; i < cols * rows * 4; i++)
            pixels[i] = (unsigned char)rand();
        int n = tio_gfx_sixel_generate(&ctx, pixels, TIO_GFX_FMT_RGBA8, TIO_GFX_FULL, out_buf, out_cap);
        tio_write(&tio, out_buf, (size_t)n);
        printf("\r\nframe %d  size=%d bytes\r\n", frame, n);
        fflush(stdout);
    }
    printf("\r\n100 frames in %.1f ms\r\n", timer_elapsed_ms(&wall));
    tio_gfx_sixel_print_stats(&ctx);

    free(out_buf);
    free(pixels);
    tio_gfx_sixel_destroy(&ctx);
    printf("\x1b[?25h");
    fflush(stdout);
    return 0;
}
