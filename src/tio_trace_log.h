#ifndef TIO_TRACE_LOG_H
#define TIO_TRACE_LOG_H

/*

Event tracing logger. Worker threads call tio_trace_log_append() with a group name
(e.g. identifying which thread it ran on), an event name, and a color; each call
writes one timestamped line to a log file. The matching reader (tio_trace.c) buckets
events by group, then pairs the two occurrences of each name *within* a group into a
start/end interval.

Requires thread.h to be included (for thread_mutex_t). Do this in *one* C file to
create the implementation:

    #define THREAD_IMPLEMENTATION
    #include "thread.h"

    #define TIO_TRACE_LOG_IMPLEMENTATION
    #include "tio_trace_log.h"

Log line format:  <group_name>,<event_name>,<seconds_since_init>,0x<AARRGGBB>

*/

#include <stdio.h>
#include <stdint.h>
#include "timer.h"
#include "thread.h"

typedef struct {
    FILE*             file;
    monotonic_timer_t timer;
    thread_mutex_t    lock;
    int               active;
} tio_trace_log_t;

int  tio_trace_log_init(const char* base_name);   // returns 0 on success, -1 on fopen failure
void tio_trace_log_append(const char* group_name, const char* event_name, uint32_t color);
void tio_trace_log_destroy(void);

#ifdef TIO_TRACE_LOG_IMPLEMENTATION

static tio_trace_log_t g_tio_trace_log;

int tio_trace_log_init(const char* base_name) {
    char filename[512];
    snprintf(filename, sizeof filename, "%s.csv", base_name);

    g_tio_trace_log.file = fopen(filename, "w");
    if (g_tio_trace_log.file == NULL) return -1;

    thread_mutex_init(&g_tio_trace_log.lock);
    timer_start(&g_tio_trace_log.timer);
    g_tio_trace_log.active = 1;
    return 0;
}

void tio_trace_log_append(const char* group_name, const char* event_name, uint32_t color) {
    if (!g_tio_trace_log.active) return;

    double t = timer_elapsed_s(&g_tio_trace_log.timer);

    thread_mutex_lock(&g_tio_trace_log.lock);
    fprintf(g_tio_trace_log.file, "%s,%s,%.9f,0x%08X\n", group_name, event_name, t, color);
    thread_mutex_unlock(&g_tio_trace_log.lock);
}

void tio_trace_log_destroy(void) {
    if (!g_tio_trace_log.active) return;

    fclose(g_tio_trace_log.file);
    thread_mutex_term(&g_tio_trace_log.lock);
    g_tio_trace_log.active = 0;
}

#endif // TIO_TRACE_LOG_IMPLEMENTATION

#endif // TIO_TRACE_LOG_H
