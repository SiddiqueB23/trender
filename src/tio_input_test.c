#include "tio.h"

int main() {
    tio_input_event_queue iq;
    tio_input_queue_init(&iq, 256);

    enable_raw_mode();
    atexit(disable_raw_mode);

    enable_mouse_reporting();
    while (1) {
        while (update_event_queue(&iq, 5, 128) > 0) {
            tio_input_event event = TIO_INPUT_EVENT_INITIALIZER;
            while (tio_input_queue_pop(&iq, &event) == 0) {
                if (event.type == TIO_INPUT_EVENT_TYPE_KEY) {
                    printf("Key event: code=%d (%s)\r\n", event.code, tio_input_event_code_to_string(event.code));
                    if (event.code == 'q' || event.code == 'Q' || event.code == CTRL_Q)
                        goto end;
                } else if (event.type == TIO_INPUT_EVENT_TYPE_MOUSE) {
                    printf("Mouse event: code=%d (%s) at (%d, %d)\r\n", event.code, tio_input_event_code_to_string(event.code), event.position_x, event.position_y);
                }
            }
        }
        // char input_char = -1;
        // int num_bytes = read(STDIN_FILENO, &input_char, 1);
        // if (num_bytes < 0) continue;
        // if (input_char == EOF) continue;
        // printf("Key event: code=%d (%s)\r\n", input_char, tio_input_event_code_to_string(input_char));
        // if (input_char == 'q' || input_char == 'Q' || input_char == CTRL_Q)
        //     break;
        // tio_input_event event = term_read_key();
        // if (event.type == TIO_INPUT_EVENT_TYPE_KEY) {
        //     printf("Key event: code=%d (%s)\r\n", event.code, tio_input_event_code_to_string(event.code));
        //     if (event.code == 'q' || event.code == 'Q' || event.code == CTRL_Q)
        //         break;
        // }else if (event.type == TIO_INPUT_EVENT_TYPE_MOUSE) {
        //     printf("Mouse event: code=%d (%s) at (%d, %d)\r\n", event.code, tio_input_event_code_to_string(event.code), event.position_x, event.position_y);
        // }
    }

end:
    disable_mouse_reporting();

    return 0;
}