#ifndef TIMER_H
#define TIMER_H

#if defined(_WIN32) || defined(_WIN64) 
#define TIMER_WINDOWS_IMPLEMENTATION 1
#else
#define TIMER_WINDOWS_IMPLEMENTATION 0
#endif

#if defined(__linux__) || defined(__linux) || defined(__gnu_linux__)
#define TIMER_LINUX_IMPLEMENTATION 1  
#define TIMER_WINDOWS_IMPLEMENTATION 0
#else
#define TIMER_LINUX_IMPLEMENTATION 0
#define TIMER_WINDOWS_IMPLEMENTATION 1
#endif

#if TIMER_WINDOWS_IMPLEMENTATION
#include <windows.h>
#else
 //#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE
#include <time.h>
#endif

typedef struct{
#if TIMER_WINDOWS_IMPLEMENTATION
    LARGE_INTEGER frequency;
    LARGE_INTEGER start_time;
#else
    struct timespec start_time;
#endif
} monotonic_timer_t;

static inline void timer_start(monotonic_timer_t* timer) {
    if (timer == NULL) return;
#if TIMER_WINDOWS_IMPLEMENTATION
    QueryPerformanceFrequency(&timer->frequency);
    QueryPerformanceCounter(&timer->start_time);
#else
    clock_gettime(CLOCK_MONOTONIC, &timer->start_time);
#endif
}

static inline double timer_elapsed_s(monotonic_timer_t* timer) {
    if (timer == NULL) return 0.0;
#if TIMER_WINDOWS_IMPLEMENTATION
    LARGE_INTEGER current_time;
    QueryPerformanceCounter(&current_time);
    return (double)(current_time.QuadPart - timer->start_time.QuadPart) / timer->frequency.QuadPart;
#else
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    return (current_time.tv_sec - timer->start_time.tv_sec) +
           (current_time.tv_nsec - timer->start_time.tv_nsec) / 1e9;
#endif
}

static inline double timer_elapsed_ms(monotonic_timer_t* timer) {
    return timer_elapsed_s(timer) * 1000.0;
}

static inline double timer_elapsed_us(monotonic_timer_t* timer) {
    return timer_elapsed_s(timer) * 1000000.0;
}

#endif // TIMER_H

