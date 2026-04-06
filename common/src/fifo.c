#include <stdatomic.h>
#include <stddef.h>

#include "fifo.h"

#define FIFO_SIZE 256
#define FIFO_MASK (FIFO_SIZE - 1)

static char fifo_buffer[FIFO_SIZE];
static atomic_size_t fifo_head = 0;
static atomic_size_t fifo_tail = 0;

void fifo_init(void)
{
    atomic_store(&fifo_head, 0);
    atomic_store(&fifo_tail, 0);
}

void fifo_push(char c)
{
    size_t head= atomic_load_explicit(&fifo_head, memory_order_relaxed);
    size_t next = (head+ 1) & FIFO_MASK;
    size_t tail = atomic_load_explicit(&fifo_tail, memory_order_acquire);

    if (next != tail) {
        fifo_buffer[head] = c;
        atomic_store_explicit(&fifo_head, next, memory_order_release);
    }
}

int fifo_pop(void)
{
    size_t tail = atomic_load_explicit(&fifo_tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&fifo_head, memory_order_acquire);

    if (tail == head)
        return -1;

    char c = fifo_buffer[tail];
    atomic_store_explicit(&fifo_tail, (tail + 1) & FIFO_MASK, memory_order_release);
    return (int)(unsigned char)c;
}

bool fifo_is_empty(void)
{
    return atomic_load_explicit(&fifo_head, memory_order_acquire) == 
           atomic_load_explicit(&fifo_tail, memory_order_relaxed);
}
