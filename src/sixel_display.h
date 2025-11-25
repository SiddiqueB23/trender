#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bayer_patterns.h"
#include "framebuffer_4i8.h"
#include "timer.h"

#ifdef __linux__
#include <unistd.h>
#endif
#ifdef __WIN32
#define _WIN32_WINNT 0x0A00
#include <Windows.h>
#endif
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define CLAMP(x, xmin, xmax) \
    (x) = MAX((xmin), (x));  \
    (x) = MIN((xmax), (x))
#define CLAMPED(x, xmin, xmax) MAX((xmin), MIN((xmax), (x)))

int closest_point(int x, int num_steps) {
    return ((2 * x * num_steps + 255) * num_steps) / 255 / 2 / num_steps;
}

void display_framebuffer_4i8_sixel(framebuffer_4i8 fb, int num_steps) {
    int img_x = fb.width, img_y = fb.height, img_n = 4;
    unsigned char *image_buffer = fb.data;
    printf("\x1b[H");
    printf("\x1bP0;1;;q");
    int color_num = 0;
    for (int i = 0; i <= num_steps; i++) {
        for (int j = 0; j <= num_steps; j++) {
            for (int k = 0; k <= num_steps; k++) {
                printf("#%d;2;%d;%d;%d", color_num,
                       (100 * i) / num_steps,
                       (100 * j) / num_steps,
                       (100 * k) / num_steps);
                color_num++;
            }
        }
    }
    for (int i = 0; i < img_y; i += 6) {
        for (int y = i; y < i + 6 && y < img_y; y++) {
            for (int x = 0; x < img_x; x++) {
                int r = image_buffer[y * img_n * img_x + x * img_n + 0];
                int g = image_buffer[y * img_n * img_x + x * img_n + 1];
                int b = image_buffer[y * img_n * img_x + x * img_n + 2];

                int r_x = closest_point(r, num_steps);
                int g_x = closest_point(g, num_steps);
                int b_x = closest_point(b, num_steps);

                color_num = r_x * (num_steps + 1) * (num_steps + 1) + g_x * (num_steps + 1) + b_x;
                printf("#%d%c", color_num, (char)(0x3F + (1 << (y % 6))));
            }
            printf("$");
        }
        printf("-");
    }
    printf("\x1b\\");

    fflush(stdout);
}

void display_framebuffer_4i8_sixel_ordered_dithering(framebuffer_4i8 fb, int num_steps) {
    int img_x = fb.width, img_y = fb.height, img_n = 4;
    unsigned char *image_buffer = fb.data;
    printf("\x1b[H");
    printf("\x1bP0;1;;q");
    int color_num = 0;
    for (int i = 0; i <= num_steps; i++) {
        for (int j = 0; j <= num_steps; j++) {
            for (int k = 0; k <= num_steps; k++) {
                printf("#%d;2;%d;%d;%d", color_num,
                       (100 * i) / num_steps,
                       (100 * j) / num_steps,
                       (100 * k) / num_steps);
                color_num++;
            }
        }
    }

    int divider = 256 / num_steps;
    for (int i = 0; i < img_y; i += 6) {
        for (int y = i; y < i + 6 && y < img_y; y++) {
            int bayer_y = y % 16;
            for (int x = 0; x < img_x; x++) {
                int bayer_x = x % 16;
                int r = image_buffer[y * img_n * img_x + x * img_n + 0];
                int g = image_buffer[y * img_n * img_x + x * img_n + 1];
                int b = image_buffer[y * img_n * img_x + x * img_n + 2];

                int t = BAYER_PATTERN_16X16[bayer_y][bayer_x];
                int corr = (t / num_steps);
                int r_x = (r + corr) / divider;
                int g_x = (g + corr) / divider;
                int b_x = (b + corr) / divider;

                CLAMP(r_x, 0, num_steps);
                CLAMP(g_x, 0, num_steps);
                CLAMP(b_x, 0, num_steps);

                color_num = r_x * (num_steps + 1) * (num_steps + 1) + g_x * (num_steps + 1) + b_x;
                printf("#%d%c", color_num, (char)(0x3F + (1 << (y % 6))));
            }
            printf("$");
        }
        printf("-");
    }
    printf("\x1b\\");

    fflush(stdout);
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

// A struct to hold our pre-rendered number string and its length.
// str[4] is enough for 3 digits + null terminator.
// len is the number of characters (1, 2, or 3).
typedef struct {
    char str[4];
    uint8_t len;
} lut_entry_t;

// The global Look-Up Table and a flag to ensure it's initialized only once.
static lut_entry_t g_number_lut[256];
static bool g_lut_initialized = false;

// This function populates the LUT. It uses sprintf, but since it only
// runs ONCE, the performance impact is zero during the main loop.
void initialize_number_lut() {
    if (g_lut_initialized) {
        return;
    }
    for (int i = 0; i < 256; i++) {
        // Render the number into the struct's string buffer
        int length = sprintf(g_number_lut[i].str, "%d", i);
        g_number_lut[i].len = (uint8_t)length;
    }
    g_lut_initialized = true;
}

// The final, extremely fast conversion function.
char *fast_itoa_lut255(char *buffer, int value) {
    // This lookup and copy is much faster than any arithmetic.
    const lut_entry_t *entry = &g_number_lut[value];
    memcpy(buffer, entry->str, entry->len);
    return buffer + entry->len;
}

void display_framebuffer_4i8_sixel_ordered_dithering_optimized(framebuffer_4i8 fb, int num_steps) {
    clock_t current_start_time = clock();

    // --- 1. Pre-calculation Stage for Static Palette ---

    // Use static variables to generate and store the palette only on the first call
    static char palette_buffer[4000];
    static size_t palette_len = 0;
    static int last_num_steps = -1;

    if (num_steps != last_num_steps) {
        char *palette_ptr = palette_buffer; // Reset pointer to the start
        palette_ptr += sprintf(palette_ptr, "\x1b[H\x1bP0;1;;q");
        int color_num = 0;
        for (int i = 0; i <= num_steps; i++) {
            for (int j = 0; j <= num_steps; j++) {
                for (int k = 0; k <= num_steps; k++) {
                    palette_ptr += sprintf(palette_ptr, "#%d;2;%d;%d;%d", color_num++,
                                           (100 * i) / num_steps,
                                           (100 * j) / num_steps,
                                           (100 * k) / num_steps);
                }
            }
        }
        palette_len = palette_ptr - palette_buffer; // Store the final length
        last_num_steps = num_steps;
        *palette_ptr = '\0';
    }

    // --- 2. Buffer Allocation for Pixel Data Only ---

    size_t pixel_buffer_size = (fb.width * fb.height * 8) + 1024;
    static int is_pixel_buffer_initialized = 0;
    static char *pixel_buffer = NULL;
    if (!is_pixel_buffer_initialized) {
        pixel_buffer = (char *)malloc(pixel_buffer_size);
        is_pixel_buffer_initialized = 1;
    }
    if (!pixel_buffer) {
        return;
    }
    char *buffer_ptr = pixel_buffer;

    const int palette_span = num_steps + 1;
    const int palette_span_squared = palette_span * palette_span;
    const int divider = 256 / num_steps;

    // --- 3. Optimized Pixel Processing Loop ---

    for (int i = 0; i < fb.height; i += 6) {
        for (int y_offset = 0; y_offset < 6; y_offset++) {
            int y = i + y_offset;
            if (y >= fb.height) break;

            unsigned char *row_ptr = fb.data + (y * fb.width * 4);
            int bayer_y = y & 15;

            for (int x = 0; x < fb.width; x++) {
                int bayer_x = x & 15;

                int r = row_ptr[0];
                int g = row_ptr[1];
                int b = row_ptr[2];
                row_ptr += 4;

                int t = BAYER_PATTERN_16X16[bayer_y][bayer_x];
                int corr = t / num_steps;

                int r_x = (r + corr) / divider;
                int g_x = (g + corr) / divider;
                int b_x = (b + corr) / divider;
                CLAMP(r_x, 0, num_steps);
                CLAMP(g_x, 0, num_steps);
                CLAMP(b_x, 0, num_steps);

                int color_num = r_x * palette_span_squared + g_x * palette_span + b_x;

                *buffer_ptr++ = '#';
                buffer_ptr = fast_itoa_lut255(buffer_ptr, color_num);
                *buffer_ptr++ = (char)(0x3F + (1 << (y_offset)));
            }
            *buffer_ptr++ = '$';
        }
        *buffer_ptr++ = '-';
    }

    // Append Sixel terminator to the pixel buffer
    buffer_ptr += sprintf(buffer_ptr, "\x1b\\");
    *buffer_ptr = '\0';
    // --- 4. Final Write and Cleanup ---

    monotonic_timer_t timer;

    timer_start(&timer);
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD bytesWritten;
    WriteFile(hConsole, palette_buffer, palette_len, &bytesWritten, NULL);
    WriteFile(hConsole, pixel_buffer, buffer_ptr - pixel_buffer, &bytesWritten, NULL);
#endif
#ifdef __linux__
    write(STDOUT_FILENO, palette_buffer, palette_len);
    write(STDOUT_FILENO, pixel_buffer, buffer_ptr - pixel_buffer);
#endif
    double elapsed_ms = timer_elapsed_ms(&timer);

    printf("%0.2f\r\n", elapsed_ms);
    fflush(stdout);
}