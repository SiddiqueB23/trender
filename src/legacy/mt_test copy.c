#define  THREAD_IMPLEMENTATION
#include "thread.h"
#define MPMC_QUEUE_IMPLEMENTATION
#include "mpmc_queue.h"

#include <stdio.h>
#include <stdint.h>

const int num_workers = 4;
const int num_frames = 10;
#define NUM_CHUNKS 5
#define NUM_BUFFERS 3

typedef uint8_t chunk_mask_t;
chunk_mask_t processed_chunks[NUM_BUFFERS] = {};
chunk_mask_t displayed_chunks[NUM_BUFFERS] = {};
chunk_mask_t full_chunks_mask = (1LL << NUM_CHUNKS) - 1;

typedef struct {
    int frame_id;
    int buffer_id;
    int chunk_id;
    int should_exit;
} chunk_params;

chunk_params chunk_param_buf[NUM_BUFFERS][NUM_CHUNKS];

typedef struct {
    int buffer_id;
    int chunk_id;
    chunk_mask_t chunk_mask;
    int from_writer;
} output_params;

char output_buffers[NUM_BUFFERS][NUM_CHUNKS][64];

typedef struct {
    int buffer_id;
    int chunk_id;
    int should_exit;
} writer_params;

mpmc_queue_t chunk_queue;
mpmc_queue_t output_queue;
mpmc_queue_t write_queue;
chunk_params chunk_queue_buf[100];
output_params output_queue_buf[100];
writer_params write_queue_buf[100];

int debug_1;

int writer_thread(void* arg) {
    while(1){
        writer_params p;
        mpmc_queue_consume(&write_queue, &p, MPMC_QUEUE_WAIT_INFINITE);
        if(debug_1)printf("[writer] consumed write_queue: buf=%d chunk=%d exit=%d\n", p.buffer_id, p.chunk_id, p.should_exit);
        if(p.should_exit) break;
        printf("\x1b[33m%s\x1b[0m\n", output_buffers[p.buffer_id%NUM_BUFFERS][p.chunk_id]);
        output_params op;
        op.from_writer = 1;
        op.buffer_id = p.buffer_id;
        op.chunk_id = p.chunk_id;
        op.chunk_mask = (1LL << p.chunk_id);
        if(debug_1)printf("[writer] producing output_queue: buf=%d chunk=%d from_writer=1\n", op.buffer_id, op.chunk_id);
        mpmc_queue_produce(&output_queue, &op, MPMC_QUEUE_WAIT_INFINITE);
    }
}

void generate_output(chunk_params p, int worker_id) {
    char* output_ptr = output_buffers[p.buffer_id%NUM_BUFFERS][p.chunk_id];
    output_ptr += snprintf(output_ptr, 64, "Frame %d chunk %d processed on worker:%d", p.frame_id, p.chunk_id, worker_id );
    for(int i=0;i<p.chunk_id;i++)*output_ptr++ = '-';
    *output_ptr++ = '\\';
    output_ptr += snprintf(output_ptr, 64, "%d", p.frame_id);
    *output_ptr++ = '\\';
    for(int i=0;i<NUM_CHUNKS - p.chunk_id;i++)*output_ptr++ = '-';
}

int worker_thread(void* arg) {
    int worker_id = *(int*)arg;
    while(1){
        chunk_params p;
        mpmc_queue_consume(&chunk_queue, &p, MPMC_QUEUE_WAIT_INFINITE);
        if(debug_1)printf("[worker %d] consumed chunk_queue: frame=%d buf=%d chunk=%d exit=%d\n", worker_id, p.frame_id, p.buffer_id, p.chunk_id, p.should_exit);
        if(p.should_exit) break;
        generate_output(p, worker_id);
        output_params op;
        op.from_writer = 0;
        op.buffer_id = p.buffer_id;
        op.chunk_id = p.chunk_id;
        op.chunk_mask = (1LL << p.chunk_id);
        if(debug_1)printf("[worker %d] producing output_queue: frame=%d buf=%d chunk=%d from_writer=0\n", worker_id, p.frame_id, op.buffer_id, op.chunk_id);
        mpmc_queue_produce(&output_queue, &op, MPMC_QUEUE_WAIT_INFINITE);
    }
}

int main(int argc, char* argv[]) {
    debug_1 = argc > 1;
    mpmc_queue_init(&chunk_queue, 100, sizeof(chunk_params), chunk_queue_buf);
    mpmc_queue_init(&output_queue, 100, sizeof(output_params), output_queue_buf);
    mpmc_queue_init(&write_queue, 100, sizeof(writer_params), write_queue_buf);

    int worker_ids[4];
    thread_ptr_t thread_ptrs[4], writer_thread_ptr;
    for(int i=0;i<num_workers;i++){
        worker_ids[i] = i;
        thread_ptrs[i] = thread_create(worker_thread, &worker_ids[i], THREAD_STACK_SIZE_DEFAULT);
    }
    writer_thread_ptr = thread_create(writer_thread, NULL, THREAD_STACK_SIZE_DEFAULT);

    for(int i=0;i<NUM_BUFFERS && i<num_frames;i++){
        for(int j=0;j<NUM_CHUNKS;j++){
            chunk_param_buf[i][j].frame_id = i;
            chunk_param_buf[i][j].buffer_id = i;
            chunk_param_buf[i][j].chunk_id = j;
            chunk_param_buf[i][j].should_exit = 0;
            if(debug_1)printf("[main] producing chunk_queue (seed): frame=%d buf=%d chunk=%d\n", i, i, j);
            mpmc_queue_produce(&chunk_queue, &chunk_param_buf[i][j], MPMC_QUEUE_WAIT_INFINITE);
        }
    }

    int current_frame = 0;
    int current_chunk = 0;
    while(current_frame < num_frames){
        output_params op;
        mpmc_queue_consume(&output_queue, &op, MPMC_QUEUE_WAIT_INFINITE);
        if(debug_1)printf("[main] consumed output_queue: buf=%d chunk=%d from_writer=%d  (current_frame=%d current_chunk=%d)\n", op.buffer_id, op.chunk_id, op.from_writer, current_frame, current_chunk);
        if(op.from_writer){
            displayed_chunks[op.buffer_id] |= op.chunk_mask;
            if(displayed_chunks[current_frame%NUM_BUFFERS] == full_chunks_mask){
                printf("Frame %d fully displayed\n", current_frame);
                int current_buf = current_frame%NUM_BUFFERS;
                processed_chunks[current_buf] = 0;
                displayed_chunks[current_buf] = 0;
                for(int j=0;j<NUM_CHUNKS && current_frame + NUM_BUFFERS < num_frames;j++){
                    chunk_param_buf[current_buf][j].frame_id = current_frame + NUM_BUFFERS;
                    chunk_param_buf[current_buf][j].buffer_id = current_buf;
                    chunk_param_buf[current_buf][j].chunk_id = j;
                    chunk_param_buf[current_buf][j].should_exit = 0;
                    if(debug_1)printf("[main] producing chunk_queue (redispatch): frame=%d buf=%d chunk=%d\n", current_frame + NUM_BUFFERS, current_buf, j);
                    mpmc_queue_produce(&chunk_queue, &chunk_param_buf[current_buf][j], MPMC_QUEUE_WAIT_INFINITE);
                }
                current_frame++;
                current_chunk = 0;
            }
        } else {
            processed_chunks[op.buffer_id] |= op.chunk_mask;
        }
        chunk_mask_t current_frame_processed_chunk_mask = processed_chunks[current_frame % NUM_BUFFERS];
        chunk_mask_t curret_chunk_mask = (1LL << current_chunk);
        while(curret_chunk_mask & current_frame_processed_chunk_mask){
            writer_params wp;
            wp.buffer_id = current_frame%NUM_BUFFERS;
            wp.chunk_id = current_chunk;
            wp.should_exit = 0;
            if(debug_1)printf("[main] producing write_queue: buf=%d chunk=%d\n", wp.buffer_id, wp.chunk_id);
            mpmc_queue_produce(&write_queue, &wp, MPMC_QUEUE_WAIT_INFINITE);
            current_chunk++;
            curret_chunk_mask <<= 1;
        }
    }

    for(int i=0;i<num_workers;i++){
        chunk_params cp;
        cp.should_exit = 1;
        if(debug_1)printf("[main] producing chunk_queue (exit signal for worker %d)\n", i);
        mpmc_queue_produce(&chunk_queue, &cp, MPMC_QUEUE_WAIT_INFINITE);
    }
    writer_params wp;
    wp.should_exit = 1;
    if(debug_1)printf("[main] producing write_queue (exit signal for writer)\n");
    mpmc_queue_produce(&write_queue, &wp, MPMC_QUEUE_WAIT_INFINITE);

    for(int i=0;i<num_workers;i++){
        thread_join(thread_ptrs[i]);
    }
    thread_join(writer_thread_ptr);

    printf("Finished whole execution\n");
    return 0;
}
