#ifndef TIO_H
#define TIO_H

#if defined(_WIN32) || defined(_WIN64)
#define TIO_WINDOWS_IMPLEMENTATION 1
#else
#define TIO_WINDOWS_IMPLEMENTATION 0
#endif

#if defined(__linux__) || defined(__linux) || defined(__gnu_linux__) || defined(__unix__) || (defined(__APPLE__) || defined(__MACH__))
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
#include <sys/select.h>
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

/* Returns 0 on success (exact pixel dimensions via TIOCGWINSZ or \x1b[14t query).
 * Returns -1 if the terminal does not report pixel size. */
int tio_get_window_size_pixels(tio_ctx_t* ctx, int* pixel_w, int* pixel_h) {
	struct winsize ws;
	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
		*pixel_w = ws.ws_xpixel;
		*pixel_h = ws.ws_ypixel;
		return 0;
	}

	/* Query terminal for pixel size: \x1b[14t → response \x1b[4;<h>;<w>t */
	if (write(ctx->ofd, "\x1b[14t", 5) != 5)
		return -1;

	char buf[32];
	unsigned int i = 0;
	while (i < sizeof(buf) - 1) {
		fd_set rfds;
		struct timeval tv = { 0, 200000 }; /* 200 ms per char */
		FD_ZERO(&rfds);
		FD_SET(ctx->ifd, &rfds);
		if (select(ctx->ifd + 1, &rfds, NULL, NULL, &tv) <= 0)
			break;
		if (read(ctx->ifd, buf + i, 1) != 1)
			break;
		if (buf[i] == 't')
			break;
		i++;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[')
		return -1;
	int h, w;
	if (sscanf(buf + 2, "4;%d;%d", &h, &w) != 2 || w <= 0 || h <= 0)
		return -1;
	*pixel_w = w;
	*pixel_h = h;
	return 0;
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
	if (ctx->input_handle == NULL || ctx->input_handle == INVALID_HANDLE_VALUE) {
		/* Try to open the console input directly */
		ctx->input_handle = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	}

	DWORD input_mode = 0;
	if (!GetConsoleMode(ctx->input_handle, &input_mode)) {
		/* If we still can't get a console mode leave input_mode as 0 and continue.
		 * Some environments (redirected output, pipes) won't have a console.
		 */
		ctx->original_input_mode = 0;
	}
	else {
		ctx->original_input_mode = input_mode;
		input_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE);
		input_mode |= ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
		SetConsoleMode(ctx->input_handle, input_mode);
	}

	ctx->output_handle = GetStdHandle(STD_OUTPUT_HANDLE);
	if (ctx->output_handle == NULL || ctx->output_handle == INVALID_HANDLE_VALUE) {
		/* Try to open the console output directly */
		ctx->output_handle = CreateFileA("CONOUT$", GENERIC_WRITE,
			FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	}

	/* If still not valid, try to allocate a new console for this process and reopen handles. */
	if ((ctx->input_handle == NULL || ctx->input_handle == INVALID_HANDLE_VALUE) &&
		(ctx->output_handle == NULL || ctx->output_handle == INVALID_HANDLE_VALUE)) {
		if (AllocConsole()) {
			/* Reopen standard handles */
			if (ctx->input_handle == NULL || ctx->input_handle == INVALID_HANDLE_VALUE)
				ctx->input_handle = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
			if (ctx->output_handle == NULL || ctx->output_handle == INVALID_HANDLE_VALUE)
				ctx->output_handle = CreateFileA("CONOUT$", GENERIC_WRITE,
					FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		}
	}

	DWORD output_mode = 0;
	if (!GetConsoleMode(ctx->output_handle, &output_mode)) {
		DWORD err = GetLastError();
		if (err == ERROR_INVALID_HANDLE) {
			/* Try attaching to parent console and retry */
			AttachConsole(ATTACH_PARENT_PROCESS);
			ctx->output_handle = GetStdHandle(STD_OUTPUT_HANDLE);
			if (!GetConsoleMode(ctx->output_handle, &output_mode)) {
				ctx->original_output_mode = 0;
			} else {
				ctx->original_output_mode = output_mode;
				output_mode |= (ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
				SetConsoleMode(ctx->output_handle, output_mode);
			}
		}
		else {
			ctx->original_output_mode = 0;
		}
	}
	else {
		ctx->original_output_mode = output_mode;
		output_mode |= (ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
		SetConsoleMode(ctx->output_handle, output_mode);
	}

	if (!SetConsoleOutputCP(CP_UTF8) || !SetConsoleCP(CP_UTF8)) {
		fprintf(stderr, "Failed to set console code page\n");
		return 1;
	}

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
		if (err == ERROR_INVALID_HANDLE) {
			/* Try attaching to the parent console and retry */
			AttachConsole(ATTACH_PARENT_PROCESS);
			ctx->output_handle = GetStdHandle(STD_OUTPUT_HANDLE);
			if (GetConsoleScreenBufferInfo(ctx->output_handle, &csbi)) {
				*cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
				*rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
				return 0;
			}
			err = GetLastError();
		}
		/* Try to reopen the console output and retry once */
		HANDLE h = CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (h != INVALID_HANDLE_VALUE && h != NULL) {
			if (GetConsoleScreenBufferInfo(h, &csbi)) {
				CloseHandle(h);
				*cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
				*rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
				return 0;
			}
			CloseHandle(h);
		}

		/* As a last resort try to use the largest possible console window size */
		COORD maxSize = GetLargestConsoleWindowSize(ctx->output_handle);
		if (maxSize.X > 0 && maxSize.Y > 0) {
			*cols = maxSize.X;
			*rows = maxSize.Y;
			return 0;
		}

		fprintf(stderr, "GetConsoleScreenBufferInfo failed: %lu\n", (unsigned long)err);
		/* As a last fallback, return a reasonable default size so caller can continue. */
		*cols = 80;
		*rows = 25;
		return 0;
	}
	*cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
	*rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
	return 0;
}

/* Returns 0 on success with exact pixel dimensions.
 * Returns 1 if pixel_w was estimated from cell height (width unavailable).
 * Returns -1 on failure. */
int tio_get_window_size_pixels(tio_ctx_t* ctx, int* pixel_w, int* pixel_h) {
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (!GetConsoleScreenBufferInfo(ctx->output_handle, &csbi))
		return -1;
	int cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
	int rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
	if (cols <= 0 || rows <= 0)
		return -1;

	int cell_w = 0, cell_h = 0;

	/* GetCurrentConsoleFontEx — works in conhost and Windows Terminal */
	CONSOLE_FONT_INFOEX cfi;
	cfi.cbSize = sizeof(cfi);
	if (GetCurrentConsoleFontEx(ctx->output_handle, FALSE, &cfi)) {
		cell_w = (int)cfi.dwFontSize.X;
		cell_h = (int)cfi.dwFontSize.Y;
	}

	/* dwFontSize.X is sometimes 0 (raster fonts, some ConPTY configs).
	 * Try GetConsoleWindow + GetClientRect to derive cell size from the
	 * actual window pixel dimensions. Works in conhost; NULL in Windows Terminal. */
	if (cell_w == 0) {
		HWND hwnd = GetConsoleWindow();
		RECT rect;
		if (hwnd && GetClientRect(hwnd, &rect) && rect.right > 0 && rect.bottom > 0) {
			cell_w = (int)rect.right  / cols;
			cell_h = (int)rect.bottom / rows;
		}
	}

	if (cell_h == 0)
		return -1;

	/* Last resort: estimate width as half the cell height.
	 * Typical console fonts (Cascadia Code, Consolas) are close to 1:2 ratio. */
	int estimated = 0;
	if (cell_w == 0) {
		cell_w = cell_h / 2;
		estimated = 1;
	}

	*pixel_w = cols * cell_w;
	*pixel_h = rows * cell_h;
	return estimated;
}

#endif

#endif // TIO_H
