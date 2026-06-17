#define  THREAD_IMPLEMENTATION
#include "thread.h"
#define MPMC_QUEUE_IMPLEMENTATION
#include "mpmc_queue.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "rainbow.h"

const int num_workers = 4;
const int num_frames = 10;
#define NUM_CHUNKS 5
#define NUM_BUFFERS 3

enum task_type{
    RENDER_FRAME,
    DISPLAY_FRAME,
    EXIT_LOOP,
};

typedef struct {
    bool rendering_completed[NUM_BUFFERS*2][NUM_CHUNKS];
    bool display_completed[NUM_BUFFERS*2][NUM_CHUNKS];
} completion_state_t;

completion_state_t state;
thread_mutex_t state_mutex;

void print_state(completion_state_t s){
    printf("R: ");
    for(int i=0;i<NUM_BUFFERS*2;i++){
        for(int j=0;j<NUM_CHUNKS;j++){
            printf("%d", s.rendering_completed[i][j]);
        }
        printf(" ");
    }
    printf("\n");
    printf("D: ");
    for(int i=0;i<NUM_BUFFERS*2;i++){
        for(int j=0;j<NUM_CHUNKS;j++){
            printf("%d", s.display_completed[i][j]);
        }
        printf(" ");
    }
    printf("\n");
}

void initialise_state() {
    for(int i=NUM_BUFFERS;i<NUM_BUFFERS*2;i++) {
        for(int j = 0;j<NUM_CHUNKS;j++) {
            state.display_completed[i][j] = true;
        }
    }
}

void reset_queuable(int frame) {
    int frame_minus_one = (frame + NUM_BUFFERS*2 - 1) % (NUM_BUFFERS*2);
    int frame_minus_num_buffers = (frame + NUM_BUFFERS) % (NUM_BUFFERS*2);
    for(int i=0;i<NUM_CHUNKS;i++){
        state.rendering_completed[frame_minus_one][i] = false;
    }
    for(int i=0;i<NUM_CHUNKS;i++){
        state.display_completed[frame_minus_num_buffers][i] = false;
    }
}

bool frame_queueable(int frame){
    bool rendered_all_frame_minus_one = true;
    bool displayed_all_frame_minus_num_buffers = true;
    int frame_minus_one = (frame + NUM_BUFFERS*2 - 1) % (NUM_BUFFERS*2);
    int frame_minus_num_buffers = (frame + NUM_BUFFERS) % (NUM_BUFFERS*2);
    for(int i=0;i<NUM_CHUNKS;i++){
        rendered_all_frame_minus_one = rendered_all_frame_minus_one && state.rendering_completed[frame_minus_one][i];
    }
    for(int i=0;i<NUM_CHUNKS;i++){
        displayed_all_frame_minus_num_buffers = displayed_all_frame_minus_num_buffers && state.display_completed[frame_minus_num_buffers][i];
    }
    return rendered_all_frame_minus_one && displayed_all_frame_minus_num_buffers;
}

typedef struct {
    unsigned char r,g,b;
} render_params_t;

render_params_t render_params_buf[NUM_BUFFERS * 2];

void handle_input(render_params_t* render_params, int* should_exit){
    // Mimic input_handling
    get_rainbow(rand(), &render_params->r, &render_params->g, &render_params->b);
    *should_exit = 0;
}

typedef struct {
    int type;
    int frame;
    int chunk;
    render_params_t* render_params_ptr;
} task_t;

void print_task(task_t task) {
    printf("TASK: %d %d %d\n", task.type, task.frame, task.chunk);
}

mpmc_queue_t parallel_work_queue;
mpmc_queue_t display_queue;
task_t parallel_work_queue_buf[111];
task_t display_queue_buf[111];

void emit_exit_tasks(void) {
    task_t task = {
        .type = EXIT_LOOP,
        .frame = 0,
        .chunk = 0,
        .render_params_ptr = NULL,
    };
    for(int i=0;i< num_workers;i++){
        mpmc_queue_produce(&parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
    }
    mpmc_queue_produce(&display_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
}

int frame_to_be_displayed = 0;
int chunk_to_be_displayed = 0;

void run_scheduler(task_t completed_task) {
    thread_mutex_lock(&state_mutex);
    // Update state
    int chunk = completed_task.chunk;
    int frame = completed_task.frame;
    int buffer = frame % (NUM_BUFFERS*2);
    if(completed_task.type == RENDER_FRAME){
        state.rendering_completed[buffer][chunk] = true;
        int buffer_to_be_dispayed = frame_to_be_displayed % (NUM_BUFFERS*2);
        while(frame_to_be_displayed == frame && state.rendering_completed[buffer_to_be_dispayed][chunk_to_be_displayed]){
            task_t task = {
                .type = DISPLAY_FRAME,
                .frame = frame_to_be_displayed,
                .chunk = chunk_to_be_displayed,
                .render_params_ptr = NULL,
            };
            mpmc_queue_produce(&display_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
            chunk_to_be_displayed++;
            if(chunk_to_be_displayed >= NUM_CHUNKS){
                chunk_to_be_displayed = 0;
                frame_to_be_displayed++;
            }
        }
    }
    if(completed_task.type == DISPLAY_FRAME) {
        state.display_completed[buffer][chunk] = true;
        if(frame == num_frames - 1 && chunk == NUM_CHUNKS - 1) {
            emit_exit_tasks();
        }
    }
    // Queue new tasks
    if(completed_task.type == DISPLAY_FRAME || completed_task.type == RENDER_FRAME) {
        int next_frame = num_frames;
        if(completed_task.type == RENDER_FRAME) next_frame = frame + 1;
        if(completed_task.type == DISPLAY_FRAME) next_frame = frame + NUM_BUFFERS;
        int queue_next_frame = frame_queueable(next_frame);
        // print_task(completed_task);
        // print_state(state);
        // printf("Frame Queueable: %d\n", queue_next_frame);
        if(queue_next_frame) {
            if(next_frame < num_frames){
                reset_queuable(next_frame);
                render_params_t* render_params_ptr = &render_params_buf[buffer];
                int should_exit = 0;
                handle_input(render_params_ptr, &should_exit);
                if(should_exit){
                    emit_exit_tasks();
                }else{
                    for(int i=0;i< NUM_CHUNKS;i++){
                        task_t task = {
                            .type = RENDER_FRAME,
                            .frame = next_frame,
                            .chunk = i,
                            .render_params_ptr = render_params_ptr,
                        };
                        mpmc_queue_produce(&parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
                    }
                }
            }
        }
    }
    thread_mutex_unlock(&state_mutex);
}

char output_buffers[NUM_BUFFERS][NUM_CHUNKS][256];

void display_frame(char* buf){
    printf("%s\r\n", buf);
}

int writer_thread(void* arg) {
    while(1){
        task_t task;
        mpmc_queue_consume(&display_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
        if(task.type == EXIT_LOOP) break;
        display_frame(output_buffers[task.frame%NUM_BUFFERS][task.chunk]);
        run_scheduler(task);
    }
}

void generate_output(task_t task, int worker_id) {
    char* output_ptr = output_buffers[task.frame%NUM_BUFFERS][task.chunk];
    char* output_ptr_end = output_ptr + 256;
    output_ptr += snprintf(output_ptr, output_ptr_end - output_ptr, "Frame %d chunk %d processed on worker:%d", task.frame, task.chunk, worker_id );
    int r = (int)task.render_params_ptr->r;
    int g = (int)task.render_params_ptr->g;
    int b = (int)task.render_params_ptr->b;
    output_ptr += snprintf(output_ptr, output_ptr_end - output_ptr, "\x1b[38;2;%d;%d;%dm", r, g, b);
    for(int i=0;i<task.chunk;i++)*output_ptr++ = '-';
    *output_ptr++ = '\\';
    output_ptr += snprintf(output_ptr, output_ptr_end - output_ptr, "%d", task.frame);
    *output_ptr++ = '\\';
    for(int i=0;i<NUM_CHUNKS - task.chunk;i++)*output_ptr++ = '-';
    output_ptr += snprintf(output_ptr, output_ptr_end - output_ptr, "\x1b[0m");
}

int worker_thread(void* arg) {
    int worker_id = *(int*)arg;
    while(1){
        task_t task;
        mpmc_queue_consume(&parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
        if(task.type == EXIT_LOOP) break;
        generate_output(task, worker_id);
        run_scheduler(task);
    }
}

int main(int argc, char* argv[]) {
    srand(time(NULL));

    thread_mutex_init(&state_mutex);
    initialise_state();

    mpmc_queue_init(&parallel_work_queue, 100, sizeof(task_t), parallel_work_queue_buf);
    mpmc_queue_init(&display_queue, 100, sizeof(task_t), display_queue_buf);

    int worker_ids[4];
    thread_ptr_t thread_ptrs[4];
    for(int i=0;i<num_workers;i++){
        worker_ids[i] = i;
        thread_ptrs[i] = thread_create(worker_thread, &worker_ids[i], THREAD_STACK_SIZE_DEFAULT);
    }

    int should_exit = 0;
    render_params_t* render_params_ptr = &render_params_buf[0];
    handle_input(render_params_ptr, &should_exit);
    for(int i=0;i<NUM_CHUNKS;i++){
        task_t task = {
            .type = RENDER_FRAME,
            .frame = 0,
            .chunk = i,
            .render_params_ptr = render_params_ptr,
        };
        mpmc_queue_produce(&parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
    }

    writer_thread(NULL);

    for(int i=0;i<num_workers;i++){
        thread_join(thread_ptrs[i]);
    }

    printf("Finished whole execution\n");
    return 0;
}
