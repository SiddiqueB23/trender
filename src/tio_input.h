#ifndef TIO_INPUT_H
#define TIO_INPUT_H

#if defined(_WIN32) || defined(_WIN64)
#define TIO_INPUT_WINDOWS_IMPLEMENTATION 1
#else
#define TIO_INPUT_WINDOWS_IMPLEMENTATION 0
#endif

#if defined(__linux__) || defined(__linux) || defined(__gnu_linux__)
#define TIO_INPUT_LINUX_IMPLEMENTATION 1  
#else
#define TIO_INPUT_LINUX_IMPLEMENTATION 0
#endif


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include "tio_input_event.h"

#if TIO_INPUT_LINUX_IMPLEMENTATION
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#if TIO_INPUT_WINDOWS_IMPLEMENTATION
#include <windows.h>
#endif

#if TIO_INPUT_LINUX_IMPLEMENTATION

#define TIO_INPUT_PROCESSING_BUFFER_MAX_LEN 32

typedef struct input_processing_buffer {
	char buffer[TIO_INPUT_PROCESSING_BUFFER_MAX_LEN];
	int front;
	int back;
	int len;
} input_processing_buffer_t;

void input_processing_buffer_init(input_processing_buffer_t* ipb) {
	ipb->front = 0;
	ipb->back = 0;
	ipb->len = 0;
}

int update_input_processing_buffer(input_processing_buffer_t* ipb) {
	int max_input_bytes = (ipb->front <= ipb->back) ? (TIO_INPUT_PROCESSING_BUFFER_MAX_LEN - ipb->back) : (ipb->front - ipb->back - 1);
	if (max_input_bytes <= 0) {
		return 0;
	}
	int bytes_read = read(STDIN_FILENO, &(ipb->buffer[ipb->back]), max_input_bytes);
	if (bytes_read <= 0) {
		return 0;
	}
	ipb->back = (ipb->back + bytes_read) % TIO_INPUT_PROCESSING_BUFFER_MAX_LEN;
	ipb->len += bytes_read;
	return bytes_read;
}

int get_input_processing_buffer_char(input_processing_buffer_t* ipb, char* c, int index) {
	if (ipb->len == 0 || index >= ipb->len) {
		*c = -1;
		return -1;
	}
	*c = ipb->buffer[(ipb->front + index) % TIO_INPUT_PROCESSING_BUFFER_MAX_LEN];
	return 0;
}

int process_input_buffer_mouse_sequence(input_processing_buffer_t* ipb, tio_input_event* event) {
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
			}
			else if (semicolon_count == 1) {
				x = x * 10 + (c - '0');
			}
			else if (semicolon_count == 2) {
				y = y * 10 + (c - '0');
			}
		}
		else if (c == ';') {
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
	}
	else if (c == 'm') {
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
	}
	else if (released == 1) {
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

const char* keyboard_sequence_strings[TIO_KEYBOARD_SEQUENCE_COUNT] = {
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

int process_input_buffer_keyboard_sequence(input_processing_buffer_t* ipb, tio_input_event* event) {
	for (int i = 0; i < TIO_KEYBOARD_SEQUENCE_COUNT; i++) {
		const char* seq = keyboard_sequence_strings[i];
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

int process_input_buffer_single_character(input_processing_buffer_t* ipb, tio_input_event* event) {
	char c;
	if (get_input_processing_buffer_char(ipb, &c, 0) == -1) {
		return 0;
	}

	event->type = TIO_INPUT_EVENT_TYPE_KEY;
	event->code = (int)c;

	return 1;
}

int tio_input_get_event_queue_byte_size(int ifd, input_processing_buffer_t* ipb) {
	int current_stdin_bytes = 0;
	if (ioctl(ifd, FIONREAD, &current_stdin_bytes) == -1) {
		perror("ioctl");
		return 1;
	}
	return current_stdin_bytes + ipb->len;
}

int tio_input_pop_event_queue(tio_input_event* input_event, input_processing_buffer_t* ipb) {
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
#endif

#if TIO_INPUT_WINDOWS_IMPLEMENTATION
int tio_input_get_event_queue_byte_size(HANDLE input_handle) {
	DWORD events = 0;
	if (!GetNumberOfConsoleInputEvents(input_handle, &events)) {
		DWORD err = GetLastError();
		fprintf(stderr, "GetNumberOfConsoleInputEvents failed: %lu\n", (unsigned long)err);
		return 0;
	}
	return (int)events;
}

int process_mouse_event_record(const MOUSE_EVENT_RECORD* mouseEvent, tio_input_event* input_event) {
	input_event->type = TIO_INPUT_EVENT_TYPE_MOUSE;
	input_event->position_x = mouseEvent->dwMousePosition.X;
	input_event->position_y = mouseEvent->dwMousePosition.Y;
	/* Map some common mouse flags to internal codes */
	{
        DWORD flags = mouseEvent->dwEventFlags;
		DWORD btn = mouseEvent->dwButtonState;
		DWORD ctrlState = mouseEvent->dwControlKeyState;
		int ctrlPressed = (ctrlState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
		/* Keep track of previous button state to detect releases */
		static DWORD last_btn_state = 0;

		if (flags & MOUSE_WHEELED) {
			/* Wheel delta is in the high-order word of dwButtonState */
			SHORT delta = (SHORT)HIWORD(mouseEvent->dwButtonState);
			if (delta > 0) input_event->code = ctrlPressed ? CTRL_SCROLL_UP : SCROLL_UP;
			else input_event->code = ctrlPressed ? CTRL_SCROLL_DOWN : SCROLL_DOWN;
		} else if (flags & MOUSE_MOVED) {
			if (btn != 0) {
				/* Mouse moved while a button is pressed -> PRESSED_MOVE for the highest-priority button */
				if (btn & FROM_LEFT_1ST_BUTTON_PRESSED) input_event->code = ctrlPressed ? CTRL_LMB_PRESSED_MOVE : LMB_PRESSED_MOVE;
				else if (btn & FROM_LEFT_2ND_BUTTON_PRESSED) input_event->code = ctrlPressed ? CTRL_MMB_PRESSED_MOVE : MMB_PRESSED_MOVE;
				else if (btn & RIGHTMOST_BUTTON_PRESSED) input_event->code = ctrlPressed ? CTRL_RMB_PRESSED_MOVE : RMB_PRESSED_MOVE;
				else input_event->code = ctrlPressed ? CTRL_MOUSE_MOVE : MOUSE_MOVE;
			} else {
				input_event->code = ctrlPressed ? CTRL_MOUSE_MOVE : MOUSE_MOVE;
			}
		} else if (flags & DOUBLE_CLICK) {
			/* Map double click to a button down event when a button is involved */
			if (btn & FROM_LEFT_1ST_BUTTON_PRESSED) input_event->code = ctrlPressed ? CTRL_LMB_DOWN : LMB_DOWN;
			else if (btn & RIGHTMOST_BUTTON_PRESSED) input_event->code = ctrlPressed ? CTRL_RMB_DOWN : RMB_DOWN;
			else if (btn & FROM_LEFT_2ND_BUTTON_PRESSED) input_event->code = ctrlPressed ? CTRL_MMB_DOWN : MMB_DOWN;
			else input_event->code = 0;
		} else {
			/* Button press / release event (flags == 0) */
			DWORD changed = btn ^ last_btn_state;
			if (changed != 0) {
				/* Check which button changed and whether it is now pressed or released */
				if (changed & FROM_LEFT_1ST_BUTTON_PRESSED) {
					if (btn & FROM_LEFT_1ST_BUTTON_PRESSED) input_event->code = ctrlPressed ? CTRL_LMB_DOWN : LMB_DOWN;
					else input_event->code = ctrlPressed ? CTRL_LMB_UP : LMB_UP;
				} else if (changed & FROM_LEFT_2ND_BUTTON_PRESSED) {
					if (btn & FROM_LEFT_2ND_BUTTON_PRESSED) input_event->code = ctrlPressed ? CTRL_MMB_DOWN : MMB_DOWN;
					else input_event->code = ctrlPressed ? CTRL_MMB_UP : MMB_UP;
				} else if (changed & RIGHTMOST_BUTTON_PRESSED) {
					if (btn & RIGHTMOST_BUTTON_PRESSED) input_event->code = ctrlPressed ? CTRL_RMB_DOWN : RMB_DOWN;
					else input_event->code = ctrlPressed ? CTRL_RMB_UP : RMB_UP;
				} else {
					input_event->code = 0;
				}
			} else {
				input_event->code = 0;
			}
		}

		last_btn_state = btn;
	}
	return 1;
}
int process_key_event_record(const KEY_EVENT_RECORD* keyEvent, tio_input_event* input_event) {
	input_event->type = TIO_INPUT_EVENT_TYPE_KEY;
	if (!keyEvent->bKeyDown) {
		input_event->type = TIO_INPUT_EVENT_TYPE_UNKNOWN;
		return 1;
	}
	DWORD ctrlState = keyEvent->dwControlKeyState;
	int ctrlPressed = (ctrlState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
	if (ctrlPressed) {
		char ch = keyEvent->uChar.AsciiChar;
		if (ch != 0 && isalpha((unsigned char)ch)) {
			char up = (char)toupper((unsigned char)ch);
			input_event->code = (int)(up - 'A') + 1; /* CTRL_A == 1 */
			return 1;
		}
		switch (keyEvent->wVirtualKeyCode) {
		case VK_UP:
			input_event->code = CTRL_ARROW_UP;
			return 1;
		case VK_DOWN:
			input_event->code = CTRL_ARROW_DOWN;
			return 1;
		case VK_LEFT:
			input_event->code = CTRL_ARROW_LEFT;
			return 1;
		case VK_RIGHT:
			input_event->code = CTRL_ARROW_RIGHT;
			return 1;
		case VK_HOME:
			input_event->code = CTRL_HOME_KEY;
			return 1;
		case VK_END:
			input_event->code = CTRL_END_KEY;
			return 1;
		case VK_INSERT:
			input_event->code = CTRL_INSERT_KEY;
			return 1;
		case VK_DELETE:
			input_event->code = CTRL_DEL_KEY;
			return 1;
		case VK_PRIOR:
			input_event->code = CTRL_PAGE_UP;
			return 1;
		case VK_NEXT:
			input_event->code = CTRL_PAGE_DOWN;
			return 1;
		case VK_F1:
			input_event->code = CTRL_FUNCTION_1;
			return 1;
		case VK_F2:
			input_event->code = CTRL_FUNCTION_2; 
			return 1;
		case VK_F3:
			input_event->code = CTRL_FUNCTION_3;
			return 1;
		case VK_F4:
			input_event->code = CTRL_FUNCTION_4;
			return 1;
		case VK_F5:
			input_event->code = CTRL_FUNCTION_5;
			return 1;
		case VK_F6:
			input_event->code = CTRL_FUNCTION_6;
			return 1;
		case VK_F7:
			input_event->code = CTRL_FUNCTION_7;
			return 1;
		case VK_F8:
			input_event->code = CTRL_FUNCTION_8;
			return 1;
		case VK_F9:
			input_event->code = CTRL_FUNCTION_9;
			return 1;
		case VK_F10:
			input_event->code = CTRL_FUNCTION_10;
			return 1;
		case VK_F11:
			input_event->code = CTRL_FUNCTION_11;
			return 1;
		case VK_F12:
			input_event->code = CTRL_FUNCTION_12;
			return 1;
		default:
			break;
		}
	}
	switch (keyEvent->wVirtualKeyCode) {
	case VK_UP:
		input_event->code = ARROW_UP;
		return 1;
	case VK_DOWN:
		input_event->code = ARROW_DOWN;
		return 1;
	case VK_LEFT:
		input_event->code = ARROW_LEFT;
		return 1;
	case VK_RIGHT:
		input_event->code = ARROW_RIGHT;
		return 1;
	case VK_HOME:
		input_event->code = HOME_KEY;
		return 1;
	case VK_END:
		input_event->code = END_KEY;
		return 1;
	case VK_INSERT:
		input_event->code = INSERT_KEY;
		return 1;
	case VK_DELETE:
		input_event->code = DEL_KEY;
		return 1;
	case VK_PRIOR:
		input_event->code = PAGE_UP;
		return 1;
	case VK_NEXT:
		input_event->code = PAGE_DOWN;
		return 1;
	case VK_F1:
		input_event->code = FUNCTION_1;
		return 1;
	case VK_F2:
		input_event->code = FUNCTION_2;
		return 1;
	case VK_F3:
		input_event->code = FUNCTION_3;
		return 1;
	case VK_F4:
		input_event->code = FUNCTION_4;
		return 1;
	case VK_F5:
		input_event->code = FUNCTION_5;
		return 1;
	case VK_F6:
		input_event->code = FUNCTION_6;
		return 1;
	case VK_F7:
		input_event->code = FUNCTION_7;
		return 1;
	case VK_F8:
		input_event->code = FUNCTION_8;
		return 1;
	case VK_F9:
		input_event->code = FUNCTION_9;
		return 1;
	case VK_F10:
		input_event->code = FUNCTION_10;
		return 1;
	case VK_F11:
		input_event->code = FUNCTION_11;
		return 1;
	case VK_F12:
		input_event->code = FUNCTION_12;
		return 1;
	}
	if (keyEvent->uChar.AsciiChar == 0 && keyEvent->wVirtualKeyCode != 0) {
		input_event->type = TIO_INPUT_EVENT_TYPE_UNKNOWN;
		return 1;
	}
	input_event->code = (int)keyEvent->uChar.AsciiChar;
	return 1;
}

int tio_input_pop_event_queue(HANDLE input_handle, tio_input_event* input_event) {
	INPUT_RECORD record;
	DWORD eventsRead;
	if (!ReadConsoleInput(input_handle, &record, 1, &eventsRead)) {
		DWORD err = GetLastError();
		fprintf(stderr, "ReadConsoleInput failed: %lu\n", (unsigned long)err);
		return 0;
	}
	if (eventsRead == 0) {
		return 0;
	}

	switch (record.EventType) {
	case KEY_EVENT:
		/* Only handle key down events to avoid duplicates on key up */
		
		return process_key_event_record(&record.Event.KeyEvent, input_event);
	case MOUSE_EVENT:
		if (!record.Event.KeyEvent.bKeyDown) {
			input_event->type = TIO_INPUT_EVENT_TYPE_UNKNOWN;
			return 1;
		}
		return process_mouse_event_record(&record.Event.MouseEvent, input_event);
	default:
		input_event->type = TIO_INPUT_EVENT_TYPE_UNKNOWN;
		return 1;
	}

}
#endif

#endif // TIO_INPUT_H