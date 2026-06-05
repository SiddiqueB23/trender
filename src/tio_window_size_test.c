#include "tio.h"
#include <stdio.h>
#include <stdlib.h>

tio_ctx_t tio;
void cleanup(void) { tio_destroy(&tio); }

int main(void) {
    tio_init(&tio);
    atexit(cleanup);

    int rows, cols;
    if (tio_get_window_size(&tio, &rows, &cols) == -1) {
        fprintf(stderr, "tio_get_window_size failed\r\n");
        return 1;
    }
    printf("Window size:  %d rows x %d cols\r\n", rows, cols);

    int pixel_w, pixel_h;
    int ret = tio_get_window_size_pixels(&tio, &pixel_w, &pixel_h);
    if (ret == -1) {
        printf("Pixel size:   unavailable\r\n");
    } else {
        printf("Pixel size:   %d x %d px  [%s]\r\n",
               pixel_w, pixel_h,
               ret == 1 ? "estimated" : "exact");
        printf("Cell size:    %d x %d px/char\r\n",
               pixel_w / cols, pixel_h / rows);
    }

    return 0;
}
