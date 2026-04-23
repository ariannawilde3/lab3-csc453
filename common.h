#ifndef IPC_LAB_COMMON_H
#define IPC_LAB_COMMON_H

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

static inline void die(const char *msg) {
    perror(msg);
    _Exit(1);
}

static inline void die2(const char *msg, const char *detail) {
    fprintf(stderr, "%s: %s\n", msg, detail);
    _Exit(1);
}

/* Fixed-size "log record" (32 bytes) */
typedef struct record {
    uint64_t ts_ns;
    uint32_t endpoint_id;
    uint16_t status;
    uint16_t method;
    uint32_t payload_bytes;
    uint32_t user_hash;
    uint64_t seq;
} record_t;

/* Simple xorshift64* PRNG for reproducible generation */
static inline uint64_t xorshift64star(uint64_t *state) {
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 2685821657736338717ULL;
}

/* A cheap record "checksum" mixer */
static inline uint64_t mix_record(const record_t *r) {
    uint64_t x = r->ts_ns ^ (uint64_t)r->endpoint_id << 32 ^ r->seq;
    x ^= (uint64_t)r->status << 48;
    x ^= (uint64_t)r->method << 32;
    x ^= (uint64_t)r->payload_bytes;
    x ^= (uint64_t)r->user_hash << 16;
    /* one more mixing step */
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return x;
}

/* Robust I/O helpers for pipes */
static inline ssize_t read_full(int fd, void *buf, size_t n) {
    uint8_t *p = (uint8_t*)buf;
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r == 0) return (ssize_t)got; /* EOF */
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (size_t)r;
    }
    return (ssize_t)got;
}

static inline int write_full(int fd, const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t*)buf;
    size_t sent = 0;
    while (sent < n) {
        ssize_t w = write(fd, p + sent, n - sent);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)w;
    }
    return 0;
}

#endif
