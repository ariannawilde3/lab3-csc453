#ifndef IPC_LAB_SHM_RING_H
#define IPC_LAB_SHM_RING_H

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stddef.h>

/* Shared memory layout: header + ring buffer records[]
 * Lock-free single-producer, single-consumer ring buffer.
 * Producer writes to head, consumer reads from tail.
 * Uses volatile to prevent compiler optimizations of memory accesses.
 */
typedef struct shm_header {
    uint32_t capacity;              /* number of record slots (must be power of 2) */
    uint32_t _pad0;
    volatile uint64_t head;         /* write index (producer updates) */
    volatile uint64_t tail;         /* read index (consumer updates) */
    uint64_t produced;              /* total records producer produced (at end) */
    uint64_t consumed;              /* total records consumer consumed (at end) */
    uint64_t checksum_prod;         /* final checksum from producer */
    uint64_t checksum_cons;         /* final checksum from consumer */
    uint32_t done;                  /* producer sets to 1 when finished */
    uint32_t ready;                 /* producer sets to 1 after initialization */
} shm_header_t;

#endif
