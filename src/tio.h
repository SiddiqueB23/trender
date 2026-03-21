#ifndef TIO_H
#define TIO_H
#include "tio_input.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>

#ifdef __linux__
#include <unistd.h>
#endif
#ifdef __WIN32
#define _WIN32_WINNT 0x0A00
#include <Windows.h>
#endif

typedef struct tio_ctx {
    int ifd;
    int ofd;
    int rows, cols;
    struct termios orig_termios;
    input_processing_buffer_t ipb;
} tio_ctx_t;

void disable_raw_mode(tio_ctx_t *ctx) {
    tcsetattr(ctx->ifd, TCSAFLUSH, &ctx->orig_termios);
}

int enable_raw_mode(tio_ctx_t *ctx) {
    struct termios raw;

    if (!isatty(ctx->ifd))
        goto fatal;
    if (tcgetattr(ctx->ifd, &ctx->orig_termios) == -1)
        goto fatal;

    raw = ctx->orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    // raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(ctx->ifd, TCSAFLUSH, &raw) < 0)
        goto fatal;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor is stored at *rows and *cols and 0 is returned. */
int get_cursor_position(tio_ctx_t *ctx, int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    /* Report cursor location */
    if (write(ctx->ofd, "\x1b[6n", 4) != 4)
        return -1;

    /* Read the response: ESC [ rows ; cols R */
    while (i < sizeof(buf) - 1) {
        if (read(ctx->ifd, buf + i, 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }

    buf[i] = '\0';

    /* Parse it. */
    if (buf[0] != ESC || buf[1] != '[')
        return -1;
    if (sscanf(buf + 2, "%d;%d", rows, cols) != 2)
        return -1;
    return 0;
}

/* Try to get the number of columns in the current terminal. If the ioctl()
 * call fails the function will try to query the terminal itself.
 * Returns 0 on success, -1 on error. */
int get_window_size(tio_ctx_t *ctx, int *rows, int *cols) {
    struct winsize ws;

    int retval = ioctl(1, TIOCGWINSZ, &ws);
    if (retval == -1 || ws.ws_col == 0) {
        /* ioctl() failed. Try to query the terminal itself. */
        int orig_row, orig_col, retval;

        /* Get the initial position so we can restore it later. */
        retval = get_cursor_position(ctx, &orig_row, &orig_col);
        if (retval == -1)
            goto failed;

        /* Go to right/bottom margin and get position. */
        if (write(ctx->ofd, "\x1b[999C\x1b[999B", 12) != 12)
            goto failed;
        retval = get_cursor_position(ctx, rows, cols);
        if (retval == -1)
            goto failed;

        /* Restore position. */
        char seq[32];
        snprintf(seq, 32, "\x1b[%d;%dH", orig_row, orig_col);
        if (write(ctx->ofd, seq, strlen(seq)) == -1) {
            /* Can't recover... */
        }
        return 0;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }

failed:
    return -1;
}

void enable_mouse_reporting(void) {
    printf("\x1b[?1003h");
    printf("\x1b[?1006h");
    fflush(stdout);
}

void disable_mouse_reporting(void) {
    printf("\x1b[?1003l");
    printf("\x1b[?1006l");
    fflush(stdout);
}

void tio_init(tio_ctx_t *ctx) {
    ctx->ifd = STDIN_FILENO;
    ctx->ofd = STDOUT_FILENO;
    input_processing_buffer_init(&ctx->ipb);
    enable_raw_mode(ctx);
    get_window_size(ctx, &ctx->rows, &ctx->cols);
}

void tio_destroy(tio_ctx_t *ctx) {
    disable_raw_mode(ctx);
}

int tio_pop_event_queue(tio_ctx_t *ctx, tio_input_event *event) {
    return tio_input_pop_event_queue(event, &ctx->ipb);
}

int tio_get_event_queue_byte_size(tio_ctx_t *ctx) {
    return tio_input_get_event_queue_byte_size(ctx->ifd, &ctx->ipb);
}

int tio_write(tio_ctx_t *ctx, const void *buf, size_t count) {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD bytesWritten;
    WriteFile(hConsole, buf, count, &bytesWritten, NULL);
#endif
#ifdef __linux__
    if (write(ctx->ofd, buf, count) == -1) {
        return -1;
    }
#endif
    return 0;
}
#endif // TIO_H