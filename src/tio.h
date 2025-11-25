#include "tio_input_event.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static struct termios orig_termios;

void disable_raw_mode(int fd) {
    tcsetattr(fd, TCSAFLUSH, &orig_termios);
}

int enable_raw_mode(int fd) {
    struct termios raw;

    if (!isatty(STDIN_FILENO)) goto fatal;
    if (tcgetattr(fd, &orig_termios) == -1) goto fatal;

    raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSAFLUSH, &raw) < 0) goto fatal;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

tio_input_event term_read_key(int fd) {
    int nread;
    char c, seq[32];

    tio_input_event event = TIO_INPUT_EVENT_INITIALIZER;
    event.type = TIO_INPUT_EVENT_TYPE_KEY;

    if ((nread = read(fd, &c, 1)) == 0) {
        // event.code = KEY_NULL;
        event.type = TIO_INPUT_EVENT_TYPE_UNKNOWN;
        return event;
    }
    if (nread == -1) exit(1);

    // sleep(1);
    switch (c) {
    case ESC: /* escape sequence */
        // /* If this is just an ESC, we'll timeout here. */
        if ((read(fd, seq, 1) == 0)) {
            event.code = ESC;
            return event;
        }
        if ((read(fd, seq + 1, 1) == 0)) {
            event.code = ESC;
            return event;
        }
        /* ESC [ sequences. */
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                /* Extended escape, read additional byte. */
                if (read(fd, seq + 2, 1) == 0) {
                    event.code = ESC;
                    return event;
                };
                if (seq[2] == '~') {
                    switch (seq[1]) {
                    case '3':
                        event.code = DEL_KEY;
                        return event;
                    case '5':
                        event.code = PAGE_UP;
                        return event;
                    case '6':
                        event.code = PAGE_DOWN;
                        return event;
                    }
                }
            } else if (seq[1] == '<' || seq[1] == '6') {
                int i = 2;
                while (i < 31) {
                    if (read(STDIN_FILENO, seq + i, 1) != 1) break;
                    if (seq[i] == 'M' || seq[i] == 'm') break;
                    i++;
                }

                seq[i + 1] = '\0';
                int type, x, y;
                if (sscanf(seq + 1, "<%d;%d;%d", &type, &x, &y) == 3) {
                    event.type = TIO_INPUT_EVENT_TYPE_MOUSE;
                    event.position_x = x;
                    event.position_y = y;
                }
                switch (type) {
                case 0:
                    if (seq[i] == 'M') {
                        event.code = LMB_DOWN;
                        return event;
                    } else if (seq[i] == 'm') {
                        event.code = LMB_UP;
                        return event;
                    }
                    break;
                case 1:
                    if (seq[i] == 'M') {
                        event.code = MMB_DOWN;
                        return event;
                    } else if (seq[i] == 'm') {
                        event.code = MMB_UP;
                        return event;
                    }
                    break;
                case 2:
                    if (seq[i] == 'M') {
                        event.code = RMB_DOWN;
                        return event;
                    } else if (seq[i] == 'm') {
                        event.code = RMB_UP;
                        return event;
                    }
                    break;
                case 32:
                    if (seq[i] == 'M') {
                        event.code = LMB_PRESSED_MOVE;
                        return event;
                    }
                    break;
                case 33:
                    if (seq[i] == 'M') {
                        event.code = MMB_PRESSED_MOVE;
                        return event;
                    }
                    break;
                case 34:
                    if (seq[i] == 'M') {
                        event.code = RMB_PRESSED_MOVE;
                        return event;
                    }
                    break;
                case 35: {
                    event.code = MOUSE_MOVE;
                    return event;
                }
                case 64: {
                    event.code = SCROLL_UP;
                    return event;
                }
                case 65: {
                    event.code = SCROLL_DOWN;
                    return event;
                }
                }
                event.code = ESC;
                return event;
            } else {
                switch (seq[1]) {
                case 'A': {
                    event.code = ARROW_UP;
                    return event;
                }
                case 'B': {
                    event.code = ARROW_DOWN;
                    return event;
                }
                case 'C': {
                    event.code = ARROW_RIGHT;
                    return event;
                }
                case 'D': {
                    event.code = ARROW_LEFT;
                    return event;
                }
                case 'H': {
                    event.code = HOME_KEY;
                    return event;
                }
                case 'F': {
                    event.code = END_KEY;
                    return event;
                }
                }
            }
        }

        /* ESC O sequences. */
        else if (seq[0] == 'O') {
            switch (seq[1]) {
            case 'H': {
                event.code = HOME_KEY;
                return event;
            }
            case 'F': {
                event.code = END_KEY;
                return event;
            }
            }
        }
        break;
    }
    event.code = c;
    return event;
}

/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor is stored at *rows and *cols and 0 is returned. */
int get_cursor_position(int ifd, int ofd, int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    /* Report cursor location */
    if (write(ofd, "\x1b[6n", 4) != 4) return -1;

    /* Read the response: ESC [ rows ; cols R */
    while (i < sizeof(buf) - 1) {
        if (read(ifd, buf + i, 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }

    buf[i] = '\0';

    /* Parse it. */
    if (buf[0] != ESC || buf[1] != '[') return -1;
    if (sscanf(buf + 2, "%d;%d", rows, cols) != 2) return -1;
    return 0;
}

/* Try to get the number of columns in the current terminal. If the ioctl()
 * call fails the function will try to query the terminal itself.
 * Returns 0 on success, -1 on error. */
int get_window_size(int ifd, int ofd, int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        /* ioctl() failed. Try to query the terminal itself. */
        int orig_row, orig_col, retval;

        /* Get the initial position so we can restore it later. */
        retval = get_cursor_position(ifd, ofd, &orig_row, &orig_col);
        if (retval == -1) goto failed;

        /* Go to right/bottom margin and get position. */
        if (write(ofd, "\x1b[999C\x1b[999B", 12) != 12) goto failed;
        retval = get_cursor_position(ifd, ofd, rows, cols);
        if (retval == -1) goto failed;

        /* Restore position. */
        char seq[32];
        snprintf(seq, 32, "\x1b[%d;%dH", orig_row, orig_col);
        if (write(ofd, seq, strlen(seq)) == -1) {
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
    write(STDOUT_FILENO, "\x1b[?1003h", 8);
    write(STDOUT_FILENO, "\x1b[?1006h", 8);
}

void disable_mouse_reporting(void) {
    write(STDOUT_FILENO, "\x1b[?1003l", 8);
    write(STDOUT_FILENO, "\x1b[?1006l", 8);
}