#ifndef TIO_H
#define TIO_H

#if defined(_WIN32) || defined(_WIN64)
#define TIO_WINDOWS_IMPLEMENTATION 1
#else
#define TIO_WINDOWS_IMPLEMENTATION 0
#endif

#if defined(__linux__) || defined(__linux) || defined(__gnu_linux__)
#define TIO_LINUX_IMPLEMENTATION 1  
#else
#define TIO_LINUX_IMPLEMENTATION 0
#endif

#include "tio_input.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if TIO_LINUX_IMPLEMENTATION
#include <sys/ioctl.h>
#include <termios.h>
#endif

#if TIO_WINDOWS_IMPLEMENTATION
#include <windows.h>
#endif

#if TIO_LINUX_IMPLEMENTATION
typedef struct {
	int ifd;
	int ofd;
	struct termios orig_termios;
	input_processing_buffer_t ipb;
} tio_ctx_t;

void disable_raw_mode(tio_ctx_t* ctx) {
	tcsetattr(ctx->ifd, TCSAFLUSH, &ctx->orig_termios);
}

int enable_raw_mode(tio_ctx_t* ctx) {
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
int get_cursor_position(tio_ctx_t* ctx, int* rows, int* cols) {
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
int tio_get_window_size(tio_ctx_t* ctx, int* rows, int* cols) {
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
	}
	else {
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

void tio_init(tio_ctx_t* ctx) {
	ctx->ifd = STDIN_FILENO;
	ctx->ofd = STDOUT_FILENO;
	input_processing_buffer_init(&ctx->ipb);
	enable_raw_mode(ctx);
	enable_mouse_reporting();
}

void tio_destroy(tio_ctx_t* ctx) {
	disable_raw_mode(ctx);
	disable_mouse_reporting();
}

int tio_pop_event_queue(tio_ctx_t* ctx, tio_input_event* event) {
	return tio_input_pop_event_queue(event, &ctx->ipb);
}

int tio_get_event_queue_byte_size(tio_ctx_t* ctx) {
	return tio_input_get_event_queue_byte_size(ctx->ifd, &ctx->ipb);
}

int tio_write(tio_ctx_t* ctx, const void* buf, size_t count) {
	if (write(ctx->ofd, buf, count) == -1) {
		return -1;
	}
	return 0;
}
#endif

#if TIO_WINDOWS_IMPLEMENTATION
#include <windows.h>
typedef struct {
	HANDLE output_handle;
	HANDLE input_handle;
	DWORD original_input_mode;
	DWORD original_output_mode;
} tio_ctx_t;

int tio_write(tio_ctx_t* ctx, const void* buf, size_t count) {
	DWORD bytesWritten;
	WriteFile(ctx->output_handle, buf, count, &bytesWritten, NULL);
	return 0;
}

int tio_init(tio_ctx_t* ctx) {
	ctx->input_handle = GetStdHandle(STD_INPUT_HANDLE);
	DWORD input_mode;
	GetConsoleMode(ctx->input_handle, &input_mode);
	ctx->original_input_mode = input_mode;
	input_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE);
	input_mode |= ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
	SetConsoleMode(ctx->input_handle, input_mode);

	ctx->output_handle = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD output_mode;
	GetConsoleMode(ctx->output_handle, &output_mode);
	ctx->original_output_mode = output_mode;
	output_mode |= (ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
	SetConsoleMode(ctx->output_handle, output_mode);
	return 0;
}

int tio_destroy(tio_ctx_t* ctx) {
	SetConsoleMode(ctx->input_handle, ctx->original_input_mode);
	SetConsoleMode(ctx->output_handle, ctx->original_output_mode);
	return 0;
}

int tio_pop_event_queue(tio_ctx_t* ctx, tio_input_event* event) {
	return tio_input_pop_event_queue(ctx->input_handle, event);
}

int tio_get_event_queue_byte_size(tio_ctx_t* ctx) {
	return tio_input_get_event_queue_byte_size(ctx->input_handle);
}

int tio_get_window_size(tio_ctx_t* ctx, int* rows, int* cols) {
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (!GetConsoleScreenBufferInfo(ctx->output_handle, &csbi)) {
		DWORD err = GetLastError();
		fprintf(stderr, "GetConsoleScreenBufferInfo failed: %lu\n", (unsigned long)err);
		return -1;
	}
	*cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
	*rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
	return 0;
}

#endif

#endif // TIO_H