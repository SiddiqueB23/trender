/*

Requires thread.h to be included before this file.

Do this:
    #define MPMC_QUEUE_IMPLEMENTATION
before you include this file in *one* C/C++ file to create the implementation.

Usage example:

    #define THREAD_IMPLEMENTATION
    #include "thread.h"

    #define MPMC_QUEUE_IMPLEMENTATION
    #include "mpmc_queue.h"

    #include <stdio.h>

    typedef struct { int x, y; } point_t;
    point_t buf[ 16 ];

    int main( void )
        {
        mpmc_queue_t q;
        mpmc_queue_init( &q, 16, sizeof( point_t ), buf );

        point_t p = { 1, 2 };
        mpmc_queue_produce( &q, &p, MPMC_QUEUE_WAIT_INFINITE );

        point_t out;
        mpmc_queue_consume( &q, &out, MPMC_QUEUE_WAIT_INFINITE );
        printf( "%d %d\n", out.x, out.y );

        mpmc_queue_term( &q );
        return 0;
        }


API
---

mpmc_queue_init
---------------

    void mpmc_queue_init( mpmc_queue_t* q, int capacity, int stride, void* buf )

Initializes the queue. `buf` must point to a caller-owned buffer of at least
`capacity * stride` bytes. The buffer is not copied and must remain valid until
`mpmc_queue_term` is called. Safe for any number of concurrent producers and
consumers.


mpmc_queue_term
---------------

    void mpmc_queue_term( mpmc_queue_t* q )

Terminates the queue and releases internal OS resources. Callers must ensure
no threads are blocked inside produce/consume before calling this.


mpmc_queue_produce
------------------

    int mpmc_queue_produce( mpmc_queue_t* q, const void* item, int timeout_ms )

Copies `stride` bytes from `item` into the queue. If the queue is full the
calling thread sleeps until space opens or `timeout_ms` elapses. Pass
MPMC_QUEUE_WAIT_INFINITE to wait forever, or 0 to return immediately without
blocking. Returns 1 on success, 0 on timeout.


mpmc_queue_consume
------------------

    int mpmc_queue_consume( mpmc_queue_t* q, void* out, int timeout_ms )

Copies `stride` bytes from the head of the queue into `out`. If the queue is
empty the calling thread sleeps until an item is available or `timeout_ms`
elapses. Pass MPMC_QUEUE_WAIT_INFINITE to wait forever, or 0 to return
immediately without blocking. Returns 1 on success, 0 on timeout.


mpmc_queue_count
----------------

    int mpmc_queue_count( mpmc_queue_t* q )

Returns a snapshot of the number of items currently in the queue. The value
may have changed by the time it is used.
*/

#ifndef mpmc_queue_h
#define mpmc_queue_h

#include <stddef.h>

#define MPMC_QUEUE_WAIT_INFINITE ( -1 )

typedef struct mpmc_queue_t mpmc_queue_t;

void mpmc_queue_init( mpmc_queue_t* q, int capacity, int stride, void* buf );
void mpmc_queue_term( mpmc_queue_t* q );

int mpmc_queue_produce( mpmc_queue_t* q, const void* item, int timeout_ms );
int mpmc_queue_consume( mpmc_queue_t* q, void* out,        int timeout_ms );

int mpmc_queue_count( mpmc_queue_t* q );

#endif /* mpmc_queue_h */


/*
----------------------
    STRUCT LAYOUT
    (outside the header guard so every TU that includes this file sees the
    layout without needing MPMC_QUEUE_IMPLEMENTATION)
----------------------
*/

#ifndef mpmc_queue_impl_h
#define mpmc_queue_impl_h

struct mpmc_queue_t
    {
    thread_mutex_t  mutex;
    thread_signal_t not_empty;  /* raised when an item is added   */
    thread_signal_t not_full;   /* raised when an item is removed */
    int             head;
    int             tail;
    int             count;
    int             capacity;
    int             stride;
    unsigned char*  buf;
    };

#endif /* mpmc_queue_impl_h */


/*
----------------------
    IMPLEMENTATION
----------------------
*/

#ifdef MPMC_QUEUE_IMPLEMENTATION
#undef MPMC_QUEUE_IMPLEMENTATION

#include <string.h>


void mpmc_queue_init( mpmc_queue_t* q, int capacity, int stride, void* buf )
    {
    thread_mutex_init( &q->mutex );
    thread_signal_init( &q->not_empty );
    thread_signal_init( &q->not_full );
    q->head     = 0;
    q->tail     = 0;
    q->count    = 0;
    q->capacity = capacity;
    q->stride   = stride;
    q->buf      = (unsigned char*) buf;
    }


void mpmc_queue_term( mpmc_queue_t* q )
    {
    thread_signal_term( &q->not_full );
    thread_signal_term( &q->not_empty );
    thread_mutex_term( &q->mutex );
    }


int mpmc_queue_produce( mpmc_queue_t* q, const void* item, int timeout_ms )
    {
    thread_mutex_lock( &q->mutex );
    while( q->count == q->capacity )
        {
        thread_mutex_unlock( &q->mutex );
        if( timeout_ms == 0 ) return 0;
        if( !thread_signal_wait( &q->not_full,
                timeout_ms == MPMC_QUEUE_WAIT_INFINITE
                    ? THREAD_SIGNAL_WAIT_INFINITE
                    : timeout_ms ) )
            return 0;
        thread_mutex_lock( &q->mutex );
        }

    memcpy( q->buf + q->tail * q->stride, item, (size_t) q->stride );
    q->tail = ( q->tail + 1 ) % q->capacity;
    int space_left = q->capacity - ++q->count;
    thread_mutex_unlock( &q->mutex );

    /* Wake one consumer; if there is still room, chain-wake a waiting producer. */
    thread_signal_raise( &q->not_empty );
    if( space_left > 0 ) thread_signal_raise( &q->not_full );
    return 1;
    }


int mpmc_queue_consume( mpmc_queue_t* q, void* out, int timeout_ms )
    {
    thread_mutex_lock( &q->mutex );
    while( q->count == 0 )
        {
        thread_mutex_unlock( &q->mutex );
        if( timeout_ms == 0 ) return 0;
        if( !thread_signal_wait( &q->not_empty,
                timeout_ms == MPMC_QUEUE_WAIT_INFINITE
                    ? THREAD_SIGNAL_WAIT_INFINITE
                    : timeout_ms ) )
            return 0;
        thread_mutex_lock( &q->mutex );
        }

    memcpy( out, q->buf + q->head * q->stride, (size_t) q->stride );
    q->head = ( q->head + 1 ) % q->capacity;
    int items_left = --q->count;
    thread_mutex_unlock( &q->mutex );

    /* Wake one producer; if there are still items, chain-wake a waiting consumer. */
    thread_signal_raise( &q->not_full );
    if( items_left > 0 ) thread_signal_raise( &q->not_empty );
    return 1;
    }


int mpmc_queue_count( mpmc_queue_t* q )
    {
    thread_mutex_lock( &q->mutex );
    int c = q->count;
    thread_mutex_unlock( &q->mutex );
    return c;
    }


#endif
