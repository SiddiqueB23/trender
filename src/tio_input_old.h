#include "tio_input_event.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

tio_input_event term_read_key() {
    int fd = STDIN_FILENO;
    int nread;
    char c, seq[32];

    tio_input_event event = TIO_INPUT_EVENT_INITIALIZER;
    event.type = TIO_INPUT_EVENT_TYPE_KEY;

    if ((nread = read(fd, &c, 1)) == 0) {
        // event.code = KEY_NULL;
        event.type = TIO_INPUT_EVENT_TYPE_UNKNOWN;
        return event;
    }
    if (nread == -1)
        exit(1);

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
                    if (read(STDIN_FILENO, seq + i, 1) != 1)
                        break;
                    if (seq[i] == 'M' || seq[i] == 'm')
                        break;
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

typedef struct {
    char *buffer;
    int max_len;
    int front;
    int back;
    int len;
} input_processing_buffer;

void input_processing_buffer_init(input_processing_buffer *ipb, int max_len) {
    ipb->buffer = (char *)malloc(max_len * sizeof(char));
    ipb->max_len = max_len;
    ipb->front = 0;
    ipb->back = 0;
    ipb->len = 0;
}

int update_input_processing_buffer(input_processing_buffer *ipb, int max_input_chars_processed) {
    int characters_read = 0;
    while (ipb->len < ipb->max_len) {
        // char c = -1;
        int max_input_bytes = (ipb->front <= ipb->back) ? (ipb->max_len - ipb->back) : (ipb->front - ipb->back - 1);
        // max_input_bytes = 1;
        // if (max_input_bytes == 0) break;
        if (max_input_bytes > max_input_chars_processed - characters_read)
            max_input_bytes = max_input_chars_processed - characters_read;
        if (max_input_bytes == 0) break;
        int num_bytes = read(STDIN_FILENO, &(ipb->buffer[ipb->back]), max_input_bytes);
        if (num_bytes <= 0) break;
        // ipb->buffer[ipb->back] = c;
        ipb->back = (ipb->back + num_bytes) % ipb->max_len;
        ipb->len += num_bytes;
        characters_read += num_bytes;
    }
    return characters_read;
}

int get_input_processing_buffer_char(input_processing_buffer *ipb, char *c, int index) {
    if (ipb->len == 0 || index >= ipb->len) {
        *c = -1;
        return -1;
    }
    *c = ipb->buffer[(ipb->front + index) % ipb->max_len];
    return 0;
}

typedef struct {
    tio_input_event *events;
    int max_events;
    int front;
    int back;
    int len;
    input_processing_buffer ipb;
} tio_input_event_queue;

void tio_input_queue_init(tio_input_event_queue *iq, int max_events) {
    iq->events = (tio_input_event *)malloc(max_events * sizeof(tio_input_event));
    iq->max_events = max_events;
    iq->front = 0;
    iq->back = 0;
    iq->len = 0;
    input_processing_buffer_init(&iq->ipb, max_events);
}

int tio_input_queue_push(tio_input_event_queue *iq, tio_input_event event) {
    if (iq->len < iq->max_events) {
        iq->events[iq->back] = event;
        iq->back = (iq->back + 1) % iq->max_events;
        iq->len++;
        return 0;
    }
    return -1;
}

int tio_input_queue_pop(tio_input_event_queue *iq, tio_input_event *event) {
    if (iq->len == 0) {
        return -1;
    }
    *event = iq->events[iq->front];
    iq->front = (iq->front + 1) % iq->max_events;
    iq->len--;
    return 0;
}

int process_input_buffer_mouse_sequence(input_processing_buffer *ipb, tio_input_event *event) {
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
            break;
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

int process_input_buffer_keyboard_sequence(input_processing_buffer *ipb, tio_input_event *event) {
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

int process_input_buffer_single_character(input_processing_buffer *ipb, tio_input_event *event) {
    char c;
    if (get_input_processing_buffer_char(ipb, &c, 0) == -1) {
        return 0;
    }

    event->type = TIO_INPUT_EVENT_TYPE_KEY;
    event->code = (int)c;

    return 1;
}

int update_event_queue(tio_input_event_queue *iq, int max_events_processed, int max_input_chars_processed) {
    input_processing_buffer *ipb = &iq->ipb;
    int current_stdin_bytes = 0;
    if (ioctl(STDIN_FILENO, FIONREAD, &current_stdin_bytes) == -1) {
        perror("ioctl");
        return 1;
    }
    update_input_processing_buffer(ipb, current_stdin_bytes);
    int events_processed = 0;
    int total_processed = 0;
    while (ipb->len > 0 &&
           (iq->len < iq->max_events - 0) &&
           (events_processed < max_events_processed) &&
           (total_processed < max_input_chars_processed)) {
        tio_input_event event = TIO_INPUT_EVENT_INITIALIZER;
        int processed = 0;
        if (processed == 0) {
            processed = process_input_buffer_keyboard_sequence(ipb, &event);
        }
        if (processed == 0) {
            processed = process_input_buffer_mouse_sequence(ipb, &event);
        }
        if (processed == 0) {
            processed = process_input_buffer_single_character(ipb, &event);
        }
        if (processed == 0) {
            break;
        }

        if (tio_input_queue_push(iq, event) != 0) {
            break;
        }

        ipb->front = (ipb->front + processed) % ipb->max_len;
        ipb->len -= processed;
        total_processed += processed;
        events_processed++;
    }
    return events_processed;
}
