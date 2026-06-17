#define  THREAD_IMPLEMENTATION
#include "thread.h"
#define MPMC_QUEUE_IMPLEMENTATION
#include "mpmc_queue.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "rainbow.h"

typedef struct {
    unsigned char r, g, b;
} render_params_t;

enum task_type {
    RENDER_FRAME,
    DISPLAY_FRAME,
    EXIT_LOOP,
};

typedef struct {
    int type;
    int frame;
    int chunk;
    render_params_t* render_params_ptr;
} task_t;

typedef struct {
    /* configuration */
    int num_buffers;
    int num_chunks;
    int num_workers;
    int num_frames;
    int queue_capacity;

    /* completion state — flat [num_buffers*2 × num_chunks] */
    bool*           rendering_completed;
    bool*           display_completed;
    thread_mutex_t  state_mutex;

    /* render params — [num_buffers * 2] */
    render_params_t* render_params_buf;

    /* queues + backing buffers — [queue_capacity] each */
    mpmc_queue_t  parallel_work_queue;
    task_t*       parallel_work_queue_buf;
    mpmc_queue_t  display_queue;
    task_t*       display_queue_buf;

    /* scheduler cursors */
    int frame_to_be_displayed;
    int chunk_to_be_displayed;
} scheduler_ctx_t;

typedef struct { scheduler_ctx_t* sched; int id; } worker_arg_t;

void scheduler_ctx_init(scheduler_ctx_t* sched,
                        int num_buffers, int num_chunks,
                        int num_workers, int num_frames,
                        int queue_capacity) {
    sched->num_buffers            = num_buffers;
    sched->num_chunks             = num_chunks;
    sched->num_workers            = num_workers;
    sched->num_frames             = num_frames;
    sched->queue_capacity         = queue_capacity;
    sched->frame_to_be_displayed  = 0;
    sched->chunk_to_be_displayed  = 0;

    int nb2 = num_buffers * 2;
    sched->rendering_completed     = (bool*)calloc((size_t)(nb2 * num_chunks), sizeof(bool));
    sched->display_completed       = (bool*)calloc((size_t)(nb2 * num_chunks), sizeof(bool));
    sched->render_params_buf       = (render_params_t*)calloc((size_t)nb2, sizeof(render_params_t));
    sched->parallel_work_queue_buf = (task_t*)malloc((size_t)queue_capacity * sizeof(task_t));
    sched->display_queue_buf       = (task_t*)malloc((size_t)queue_capacity * sizeof(task_t));

    thread_mutex_init(&sched->state_mutex);
    mpmc_queue_init(&sched->parallel_work_queue, queue_capacity, sizeof(task_t), sched->parallel_work_queue_buf);
    mpmc_queue_init(&sched->display_queue,       queue_capacity, sizeof(task_t), sched->display_queue_buf);

    /* mark back half of display slots as already done (frames that haven't existed yet) */
    for (int i = num_buffers; i < nb2; i++)
        for (int j = 0; j < num_chunks; j++)
            sched->display_completed[i * num_chunks + j] = true;
}

void scheduler_ctx_destroy(scheduler_ctx_t* sched) {
    free(sched->rendering_completed);
    free(sched->display_completed);
    free(sched->render_params_buf);
    free(sched->parallel_work_queue_buf);
    free(sched->display_queue_buf);
    thread_mutex_term(&sched->state_mutex);
}

void handle_input(render_params_t* render_params, int* should_exit) {
    get_rainbow(rand(), &render_params->r, &render_params->g, &render_params->b);
    *should_exit = 0;
}

void print_task(task_t task) {
    printf("TASK: %d %d %d\n", task.type, task.frame, task.chunk);
}

void print_state(scheduler_ctx_t* sched) {
    int nb2 = sched->num_buffers * 2;
    printf("R: ");
    for (int i = 0; i < nb2; i++) {
        for (int j = 0; j < sched->num_chunks; j++)
            printf("%d", sched->rendering_completed[i * sched->num_chunks + j]);
        printf(" ");
    }
    printf("\n");
    printf("D: ");
    for (int i = 0; i < nb2; i++) {
        for (int j = 0; j < sched->num_chunks; j++)
            printf("%d", sched->display_completed[i * sched->num_chunks + j]);
        printf(" ");
    }
    printf("\n");
}

void emit_exit_tasks(scheduler_ctx_t* sched) {
    task_t task = { .type = EXIT_LOOP, .frame = 0, .chunk = 0, .render_params_ptr = NULL };
    for (int i = 0; i < sched->num_workers; i++)
        mpmc_queue_produce(&sched->parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
    mpmc_queue_produce(&sched->display_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
}

void reset_queuable(scheduler_ctx_t* sched, int frame) {
    int nb2                    = sched->num_buffers * 2;
    int frame_minus_one        = (frame + nb2 - 1) % nb2;
    int frame_minus_num_bufs   = (frame + sched->num_buffers) % nb2;
    for (int i = 0; i < sched->num_chunks; i++)
        sched->rendering_completed[frame_minus_one * sched->num_chunks + i] = false;
    for (int i = 0; i < sched->num_chunks; i++)
        sched->display_completed[frame_minus_num_bufs * sched->num_chunks + i] = false;
}

bool frame_queueable(scheduler_ctx_t* sched, int frame) {
    int nb2                    = sched->num_buffers * 2;
    int frame_minus_one        = (frame + nb2 - 1) % nb2;
    int frame_minus_num_bufs   = (frame + sched->num_buffers) % nb2;
    bool rendered_all = true, displayed_all = true;
    for (int i = 0; i < sched->num_chunks; i++)
        rendered_all  = rendered_all  && sched->rendering_completed[frame_minus_one * sched->num_chunks + i];
    for (int i = 0; i < sched->num_chunks; i++)
        displayed_all = displayed_all && sched->display_completed[frame_minus_num_bufs * sched->num_chunks + i];
    return rendered_all && displayed_all;
}

void run_scheduler(scheduler_ctx_t* sched, task_t completed_task) {
    thread_mutex_lock(&sched->state_mutex);
    int chunk  = completed_task.chunk;
    int frame  = completed_task.frame;
    int nb2    = sched->num_buffers * 2;
    int buffer = frame % nb2;

    if (completed_task.type == RENDER_FRAME) {
        sched->rendering_completed[buffer * sched->num_chunks + chunk] = true;
        int buf_disp = sched->frame_to_be_displayed % nb2;
        while (sched->frame_to_be_displayed == frame &&
               sched->rendering_completed[buf_disp * sched->num_chunks + sched->chunk_to_be_displayed]) {
            task_t task = {
                .type = DISPLAY_FRAME,
                .frame = sched->frame_to_be_displayed,
                .chunk = sched->chunk_to_be_displayed,
                .render_params_ptr = NULL,
            };
            mpmc_queue_produce(&sched->display_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
            sched->chunk_to_be_displayed++;
            if (sched->chunk_to_be_displayed >= sched->num_chunks) {
                sched->chunk_to_be_displayed = 0;
                sched->frame_to_be_displayed++;
            }
        }
    }
    if (completed_task.type == DISPLAY_FRAME) {
        sched->display_completed[buffer * sched->num_chunks + chunk] = true;
        if (frame == sched->num_frames - 1 && chunk == sched->num_chunks - 1)
            emit_exit_tasks(sched);
    }
    if (completed_task.type == DISPLAY_FRAME || completed_task.type == RENDER_FRAME) {
        int next_frame = sched->num_frames;
        if (completed_task.type == RENDER_FRAME)  next_frame = frame + 1;
        if (completed_task.type == DISPLAY_FRAME) next_frame = frame + sched->num_buffers;
        if (frame_queueable(sched, next_frame) && next_frame < sched->num_frames) {
            reset_queuable(sched, next_frame);
            render_params_t* rp = &sched->render_params_buf[buffer];
            int should_exit = 0;
            handle_input(rp, &should_exit);
            if (should_exit) {
                emit_exit_tasks(sched);
            } else {
                for (int i = 0; i < sched->num_chunks; i++) {
                    task_t task = {
                        .type = RENDER_FRAME,
                        .frame = next_frame,
                        .chunk = i,
                        .render_params_ptr = rp,
                    };
                    mpmc_queue_produce(&sched->parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
                }
            }
        }
    }
    thread_mutex_unlock(&sched->state_mutex);
}

/* output_buffers is not scheduling state — kept as a plain global */
char output_buffers[3][5][256];

void display_frame(char* buf) {
    printf("%s\r\n", buf);
}

int writer_thread(void* arg) {
    scheduler_ctx_t* sched = (scheduler_ctx_t*)arg;
    while (1) {
        task_t task;
        mpmc_queue_consume(&sched->display_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
        if (task.type == EXIT_LOOP) break;
        display_frame(output_buffers[task.frame % sched->num_buffers][task.chunk]);
        run_scheduler(sched, task);
    }
    return 0;
}

void generate_output(scheduler_ctx_t* sched, task_t task, int worker_id) {
    char* p   = output_buffers[task.frame % sched->num_buffers][task.chunk];
    char* end = p + 256;
    p += snprintf(p, (size_t)(end - p), "Frame %d chunk %d processed on worker:%d",
                  task.frame, task.chunk, worker_id);
    p += snprintf(p, (size_t)(end - p), "\x1b[38;2;%d;%d;%dm",
                  (int)task.render_params_ptr->r,
                  (int)task.render_params_ptr->g,
                  (int)task.render_params_ptr->b);
    for (int i = 0; i < task.chunk; i++) *p++ = '-';
    *p++ = '\\';
    p += snprintf(p, (size_t)(end - p), "%d", task.frame);
    *p++ = '\\';
    for (int i = 0; i < sched->num_chunks - task.chunk; i++) *p++ = '-';
    p += snprintf(p, (size_t)(end - p), "\x1b[0m");
}

int worker_thread(void* arg) {
    worker_arg_t* wa = (worker_arg_t*)arg;
    while (1) {
        task_t task;
        mpmc_queue_consume(&wa->sched->parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
        if (task.type == EXIT_LOOP) break;
        generate_output(wa->sched, task, wa->id);
        run_scheduler(wa->sched, task);
    }
    return 0;
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    srand(time(NULL));

    scheduler_ctx_t sched;
    scheduler_ctx_init(&sched,
        /*num_buffers=*/3, /*num_chunks=*/5,
        /*num_workers=*/4, /*num_frames=*/10,
        /*queue_capacity=*/111);

    worker_arg_t worker_args[4];
    thread_ptr_t thread_ptrs[4];
    for (int i = 0; i < sched.num_workers; i++) {
        worker_args[i] = (worker_arg_t){ .sched = &sched, .id = i };
        thread_ptrs[i] = thread_create(worker_thread, &worker_args[i], THREAD_STACK_SIZE_DEFAULT);
    }

    int should_exit = 0;
    render_params_t* rp = &sched.render_params_buf[0];
    handle_input(rp, &should_exit);
    for (int i = 0; i < sched.num_chunks; i++) {
        task_t task = {
            .type = RENDER_FRAME,
            .frame = 0,
            .chunk = i,
            .render_params_ptr = rp,
        };
        mpmc_queue_produce(&sched.parallel_work_queue, &task, MPMC_QUEUE_WAIT_INFINITE);
    }

    writer_thread(&sched);

    for (int i = 0; i < sched.num_workers; i++)
        thread_join(thread_ptrs[i]);

    scheduler_ctx_destroy(&sched);
    printf("Finished whole execution\n");
    return 0;
}
