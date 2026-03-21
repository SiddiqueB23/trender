#include "tio.h"

tio_ctx_t ctx;

void cleanup(void) {
    tio_destroy(&ctx);
    disable_mouse_reporting();
}

void display_input_processing_buffer_raw() {
    input_processing_buffer_t *ipb = &(ctx.ipb);
    for (int i = 0; i < TIO_INPUT_PROCESSING_BUFFER_MAX_LEN; i++) {
        char c = ctx.ipb.buffer[i];
        int in_queue = ((ipb->front <= ipb->back) && (i >= ipb->front && i < ipb->back)) ||
                       ((ipb->front > ipb->back) && (i >= ipb->front || i < ipb->back));
        // printf("\x1b[0m");
        // printf("[");
        if (in_queue) {
            if(ipb->front<ipb->back) {
                printf("\x1b[38;2;255;%d;0m", (int)(255.0 * (i - ipb->front) / (ipb->back - ipb->front)));
            } else {
                printf("\x1b[38;2;255;%d;0m", (int)(255.0 * ((i >= ipb->front ? i - ipb->front : TIO_INPUT_PROCESSING_BUFFER_MAX_LEN - ipb->front + i) / (float)(TIO_INPUT_PROCESSING_BUFFER_MAX_LEN - ipb->front + ipb->back))));
            }
        }
        if (isprint(c)) {
            printf(" %c", c);
        } else {
            printf("%02x", (unsigned char)c);
        }
        printf("\x1b[0m ");
        // printf("]");
    }
    fflush(stdout);
}

int main() {
    tio_init(&ctx);
    atexit(cleanup);

    enable_mouse_reporting();
    while (1) {
        int current_event_queue_bytes_size = tio_get_event_queue_byte_size(&ctx);
        int event_bytes_processed = 0;
        while (event_bytes_processed < current_event_queue_bytes_size) {
            tio_input_event event = TIO_INPUT_EVENT_INITIALIZER;
            int bytes_processed = tio_input_pop_event_queue(&event, &ctx.ipb);
            event_bytes_processed += bytes_processed;
            // printf("\x1b[2J\x1b[H\r\n");
            printf("\r\n");
            display_input_processing_buffer_raw();
            printf("\r\nBytes in event queue: %d, bytes processed: %d", current_event_queue_bytes_size, event_bytes_processed);
            fflush(stdout);
            if (event.type == TIO_INPUT_EVENT_TYPE_KEY) {
                // if (event.code == ESC)
                //     printf("\",\r\n\"\\x1b");
                // else if (isprint((char)event.code))
                //     printf("%c", (char)event.code);
                // else
                //     printf("\r\n!ERRORRRR!\r\n");
                printf("\r\nKey event: code=%d (%s)", event.code, tio_input_event_code_to_string(event.code));
                fflush(stdout);
                if (event.code == CTRL_Q)
                    goto end;
            } else if (event.type == TIO_INPUT_EVENT_TYPE_MOUSE) {
                printf("\r\nMouse event: code=%d (%s) at (%d, %d)", event.code, tio_input_event_code_to_string(event.code), event.position_x, event.position_y);
                fflush(stdout);
            }
        }
        fflush(stdout);
    }

end:
    return 0;
}