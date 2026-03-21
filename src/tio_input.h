#ifndef TIO_INPUT_H
#define TIO_INPUT_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "tio_input_event.h"

#define TIO_INPUT_PROCESSING_BUFFER_MAX_LEN 32

typedef struct input_processing_buffer {
    char buffer[TIO_INPUT_PROCESSING_BUFFER_MAX_LEN];
    int front;
    int back;
    int len;
} input_processing_buffer_t;

void input_processing_buffer_init(input_processing_buffer_t *ipb) {
    ipb->front = 0;
    ipb->back = 0;
    ipb->len = 0;
}

int update_input_processing_buffer(input_processing_buffer_t *ipb) {
    int max_input_bytes = (ipb->front <= ipb->back) ? (TIO_INPUT_PROCESSING_BUFFER_MAX_LEN - ipb->back) : (ipb->front - ipb->back - 1);
    if (max_input_bytes <= 0) {
        return 0;
    }
    int bytes_read = read(STDIN_FILENO, &(ipb->buffer[ipb->back]), max_input_bytes);
    if(bytes_read <= 0) {
        return 0;
    }
    ipb->back = (ipb->back + bytes_read) % TIO_INPUT_PROCESSING_BUFFER_MAX_LEN;
    ipb->len += bytes_read;
    return bytes_read;
}

int get_input_processing_buffer_char(input_processing_buffer_t *ipb, char *c, int index) {
    if (ipb->len == 0 || index >= ipb->len) {
        *c = -1;
        return -1;
    }
    *c = ipb->buffer[(ipb->front + index) % TIO_INPUT_PROCESSING_BUFFER_MAX_LEN];
    return 0;
}

int process_input_buffer_mouse_sequence(input_processing_buffer_t *ipb, tio_input_event *event) {
    char c;
    int processed = 0;
    if (get_input_processing_buffer_char(ipb, &c, 0) == -1 || c != ESC) {
        return 0;
    }
    if (get_input_processing_buffer_char(ipb, &c, 1) == -1 || c != '[') {
        return 0;
    }
    if (get_input_processing_buffer_char(ipb, &c, 2) == -1 || c != '<') {
        return 0;
    }
    processed = 3;
    int i = 3;
    int semicolon_count = 0;
    int x = 0, y = 0, type = 0;
    while (1) {
        if (get_input_processing_buffer_char(ipb, &c, i) == -1) {
            return 0;
        }
        if (isdigit(c)) {
            if (semicolon_count == 0) {
                type = type * 10 + (c - '0');
            } else if (semicolon_count == 1) {
                x = x * 10 + (c - '0');
            } else if (semicolon_count == 2) {
                y = y * 10 + (c - '0');
            }
        } else if (c == ';') {
            semicolon_count++;
        }
        if (c == 'M' || c == 'm') {
            if (semicolon_count < 2) {
                return 0;
            }
            processed++;
            break;
        }
        i++;
        processed++;
    }
    event->type = TIO_INPUT_EVENT_TYPE_MOUSE;
    event->position_x = x;
    event->position_y = y;
    int pressed = -1, released = -1;
    if (c == 'M') {
        pressed = 1;
    } else if (c == 'm') {
        released = 1;
    }
    if (pressed == 1) {
        switch (type) {
        case 0:
            event->code = LMB_DOWN;
            return processed;
        case 1:
            event->code = MMB_DOWN;
            return processed;
        case 2:
            event->code = CTRL_RMB_DOWN;
            return processed;
        case 16:
            event->code = CTRL_LMB_DOWN;
            return processed;
        case 17:
            event->code = CTRL_MMB_DOWN;
            return processed;
        case 18:
            event->code = CTRL_RMB_DOWN;
            return processed;
        case 32:
            event->code = LMB_PRESSED_MOVE;
            return processed;
        case 33:
            event->code = MMB_PRESSED_MOVE;
            return processed;
        case 34:
            event->code = RMB_PRESSED_MOVE;
            return processed;
        case 35:
            event->code = MOUSE_MOVE;
            return processed;
        case 48:
            event->code = CTRL_LMB_PRESSED_MOVE;
            return processed;
        case 49:
            event->code = CTRL_MMB_PRESSED_MOVE;
            return processed;
        case 50:
            event->code = CTRL_RMB_PRESSED_MOVE;
            return processed;
        case 51:
            event->code = CTRL_MOUSE_MOVE;
            return processed;
        case 64:
            event->code = SCROLL_UP;
            return processed;
        case 65:
            event->code = SCROLL_DOWN;
            return processed;
        }
    } else if (released == 1) {
        switch (type) {
        case 0:
            event->code = LMB_UP;
            return processed;
        case 1:
            event->code = MMB_UP;
            return processed;
        case 2:
            event->code = CTRL_RMB_UP;
            return processed;
        case 16:
            event->code = CTRL_LMB_UP;
            return processed;
        case 17:
            event->code = CTRL_MMB_UP;
            return processed;
        case 18:
            event->code = CTRL_RMB_UP;
            return processed;
        }
    }
    event->type = TIO_INPUT_EVENT_TYPE_UNKNOWN;
    return 0;
}

#define TIO_KEYBOARD_SEQUENCE_COUNT 82

const char *keyboard_sequence_strings[TIO_KEYBOARD_SEQUENCE_COUNT] = {
    "\x1b[A",
    "\x1b[B",
    "\x1b[C",
    "\x1b[D",

    "\x1b[1;5A",
    "\x1b[1;5B",
    "\x1b[1;5C",
    "\x1b[1;5D",

    "\x1b[H",
    "\x1b[F",
    "\x1b[2~",
    "\x1b[3~",
    "\x1b[5~",
    "\x1b[6~",

    "\x1b[1;5H",
    "\x1b[1;5F",
    "\x1b[2;5~",
    "\x1b[3;5~",
    "\x1b[5;5~",
    "\x1b[6;5~",

    "\x1bOP",
    "\x1bOQ",
    "\x1bOR",
    "\x1bOS",
    "\x1b[15~",
    "\x1b[17~",
    "\x1b[18~",
    "\x1b[19~",
    "\x1b[20~",
    "\x1b[21~",
    "\x1b[23~",
    "\x1b[24~",

    "\x1b[1;5P",
    "\x1b[1;5Q",
    "\x1b[1;5R",
    "\x1b[1;5S",
    "\x1b[15;5~",
    "\x1b[17;5~",
    "\x1b[18;5~",
    "\x1b[19;5~",
    "\x1b[20;5~",
    "\x1b[21;5~",
    "\x1b[23;5~",
    "\x1b[24;5~",

    "\x1b[E",
    "\x1b[1;5E",
    "\x1b[1;2E",
    "\x1b[1;6E",

    "\x1b[1;2P",
    "\x1b[1;2Q",
    "\x1b[1;2R",
    "\x1b[1;2S",
    "\x1b[15;2~",
    "\x1b[17;2~",
    "\x1b[18;2~",
    "\x1b[19;2~",
    "\x1b[20;2~",
    "\x1b[21;2~",
    "\x1b[23;2~",
    "\x1b[24;2~",

    "\x1b[1;6P",
    "\x1b[1;6Q",
    "\x1b[1;6R",
    "\x1b[1;6S",
    "\x1b[15;6~",
    "\x1b[17;6~",
    "\x1b[18;6~",
    "\x1b[19;6~",
    "\x1b[20;6~",
    "\x1b[21;6~",
    "\x1b[23;6~",
    "\x1b[24;6~",

    "\x1b[1;2H",
    "\x1b[1;2F",
    "\x1b[3;2~",
    "\x1b[5;2~",
    "\x1b[6;2~",

    "\x1b[1;6H",
    "\x1b[1;6F",
    "\x1b[3;6~",
    "\x1b[5;6~",
    "\x1b[6;6~",

};

int keyboard_sequence_strings_lens[TIO_KEYBOARD_SEQUENCE_COUNT] = {
    3,
    3,
    3,
    3,

    6,
    6,
    6,
    6,

    3,
    3,
    4,
    4,
    4,
    4,

    6,
    6,
    6,
    6,
    6,
    6,

    3,
    3,
    3,
    3,
    5,
    5,
    5,
    5,
    5,
    5,
    5,
    5,

    6,
    6,
    6,
    6,
    7,
    7,
    7,
    7,
    7,
    7,
    7,
    7,

    3,
    6,
    6,
    6,

    6,
    6,
    6,
    6,
    7,
    7,
    7,
    7,
    7,
    7,
    7,
    7,

    6,
    6,
    6,
    6,
    7,
    7,
    7,
    7,
    7,
    7,
    7,
    7,

    6,
    6,
    6,
    6,
    6,

    6,
    6,
    6,
    6,
    6,
};

int keyboard_sequence_keys[TIO_KEYBOARD_SEQUENCE_COUNT] = {
    ARROW_UP,
    ARROW_DOWN,
    ARROW_RIGHT,
    ARROW_LEFT,

    CTRL_ARROW_UP,
    CTRL_ARROW_DOWN,
    CTRL_ARROW_RIGHT,
    CTRL_ARROW_LEFT,

    HOME_KEY,
    END_KEY,
    INSERT_KEY,
    DEL_KEY,
    PAGE_UP,
    PAGE_DOWN,

    CTRL_HOME_KEY,
    CTRL_END_KEY,
    CTRL_INSERT_KEY,
    CTRL_DEL_KEY,
    CTRL_PAGE_UP,
    CTRL_PAGE_DOWN,

    FUNCTION_1,
    FUNCTION_2,
    FUNCTION_3,
    FUNCTION_4,
    FUNCTION_5,
    FUNCTION_6,
    FUNCTION_7,
    FUNCTION_8,
    FUNCTION_9,
    FUNCTION_10,
    FUNCTION_11,
    FUNCTION_12,

    CTRL_FUNCTION_1,
    CTRL_FUNCTION_2,
    CTRL_FUNCTION_3,
    CTRL_FUNCTION_4,
    CTRL_FUNCTION_5,
    CTRL_FUNCTION_6,
    CTRL_FUNCTION_7,
    CTRL_FUNCTION_8,
    CTRL_FUNCTION_9,
    CTRL_FUNCTION_10,
    CTRL_FUNCTION_11,
    CTRL_FUNCTION_12,

    BEGIN,
    CTRL_BEGIN,
    SHIFT_BEGIN,
    CTRL_SHIFT_BEGIN,

    SHIFT_FUNCTION_1,
    SHIFT_FUNCTION_2,
    SHIFT_FUNCTION_3,
    SHIFT_FUNCTION_4,
    SHIFT_FUNCTION_5,
    SHIFT_FUNCTION_6,
    SHIFT_FUNCTION_7,
    SHIFT_FUNCTION_8,
    SHIFT_FUNCTION_9,
    SHIFT_FUNCTION_10,
    SHIFT_FUNCTION_11,
    SHIFT_FUNCTION_12,

    CTRL_SHIFT_FUNCTION_1,
    CTRL_SHIFT_FUNCTION_2,
    CTRL_SHIFT_FUNCTION_3,
    CTRL_SHIFT_FUNCTION_4,
    CTRL_SHIFT_FUNCTION_5,
    CTRL_SHIFT_FUNCTION_6,
    CTRL_SHIFT_FUNCTION_7,
    CTRL_SHIFT_FUNCTION_8,
    CTRL_SHIFT_FUNCTION_9,
    CTRL_SHIFT_FUNCTION_10,
    CTRL_SHIFT_FUNCTION_11,
    CTRL_SHIFT_FUNCTION_12,

    SHIFT_HOME_KEY,
    SHIFT_END_KEY,
    SHIFT_DEL_KEY,
    SHIFT_PAGE_UP,
    SHIFT_PAGE_DOWN,

    CTRL_SHIFT_HOME_KEY,
    CTRL_SHIFT_END_KEY,
    CTRL_SHIFT_DEL_KEY,
    CTRL_SHIFT_PAGE_UP,
    CTRL_SHIFT_PAGE_DOWN,
};

int process_input_buffer_keyboard_sequence(input_processing_buffer_t *ipb, tio_input_event *event) {
    for (int i = 0; i < TIO_KEYBOARD_SEQUENCE_COUNT; i++) {
        const char *seq = keyboard_sequence_strings[i];
        int seq_len = keyboard_sequence_strings_lens[i];
        if (ipb->len >= seq_len) {
            int match = 1;
            for (int j = 0; j < seq_len; j++) {
                char c;
                if (get_input_processing_buffer_char(ipb, &c, j) == -1 || c != seq[j]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                event->type = TIO_INPUT_EVENT_TYPE_KEY;
                event->code = keyboard_sequence_keys[i];
                return seq_len;
            }
        }
    }
    return 0;
}

int process_input_buffer_single_character(input_processing_buffer_t *ipb, tio_input_event *event) {
    char c;
    if (get_input_processing_buffer_char(ipb, &c, 0) == -1) {
        return 0;
    }

    event->type = TIO_INPUT_EVENT_TYPE_KEY;
    event->code = (int)c;

    return 1;
}

int tio_input_pop_event_queue(tio_input_event *input_event, input_processing_buffer_t *ipb) {
    int ipb_updated = update_input_processing_buffer(ipb);
    while (ipb_updated > 0) {
        ipb_updated = update_input_processing_buffer(ipb);
    }
    int processed = 0;
    if (processed == 0) {
        processed = process_input_buffer_keyboard_sequence(ipb, input_event);
    }
    if (processed == 0) {
        processed = process_input_buffer_mouse_sequence(ipb, input_event);
    }
    if (processed == 0) {
        processed = process_input_buffer_single_character(ipb, input_event);
    }
    ipb->front = (ipb->front + processed) % TIO_INPUT_PROCESSING_BUFFER_MAX_LEN;
    ipb->len -= processed;
    return processed;
}

int tio_input_get_event_queue_byte_size(int ifd, input_processing_buffer_t *ipb) {
    int current_stdin_bytes = 0;
    if (ioctl(ifd, FIONREAD, &current_stdin_bytes) == -1) {
        perror("ioctl");
        return 1;
    }
    return current_stdin_bytes + ipb->len;
}

#endif // TIO_INPUT_H