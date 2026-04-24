#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "shm_ring.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

typedef enum { IPC_PIPE, IPC_SHM } ipc_mode_t;

static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) die("clock_gettime");
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage:\n"
        "  %s --ipc=pipe --records N\n"
        "  %s --ipc=shm  --records N --name /shmname [--capacity C]\n"
        "\n"
        "Pipe mode writes binary log records to stdout.\n"
        "SHM mode uses shared memory with a lock-free ring buffer.\n"
        "\n"
        "Options:\n"
        "  --records N     Number of records to produce\n"
        "  --name /X       (shm) shared memory name\n"
        "  --capacity C    (shm) ring capacity in records (default 1<<20)\n",
        argv0, argv0
    );
    _Exit(2);
}

static ipc_mode_t parse_ipc(const char *s) {
    if (strcmp(s, "pipe") == 0) return IPC_PIPE;
    if (strcmp(s, "shm") == 0) return IPC_SHM;
    die2("Unknown --ipc value", s);
    return IPC_PIPE;
}

int main(int argc, char **argv) {
    ipc_mode_t mode = IPC_PIPE;
    uint64_t records = 0;
    const char *name = NULL;
    uint32_t capacity = (1u << 20); /* ~1M records */

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--ipc=", 6) == 0) {
            mode = parse_ipc(argv[i] + 6);
        } else if (strcmp(argv[i], "--records") == 0 && i + 1 < argc) {
            records = (uint64_t)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            name = argv[++i];
        } else if (strcmp(argv[i], "--capacity") == 0 && i + 1 < argc) {
            capacity = (uint32_t)strtoul(argv[++i], NULL, 10);
            if (capacity < 1024) capacity = 1024;
        } else {
            usage(argv[0]);
        }
    }

    if (records == 0) usage(argv[0]);
    if (mode == IPC_SHM && (!name || name[0] != '/')) {
        die2("SHM mode requires --name starting with '/'", name ? name : "(null)");
    }

    uint64_t rng = 0x123456789abcdef0ULL;
    uint64_t checksum = 0;

    if (mode == IPC_PIPE) {
        uint64_t t0 = now_ns();

        for (uint64_t i = 0; i < records; i++) {
            record_t r;
            r.ts_ns = now_ns();
            uint64_t rv = xorshift64star(&rng);
            r.endpoint_id = (uint32_t)(rv % 10000u);
            r.method = (uint16_t)(rv % 4u);
            r.status = (uint16_t)((rv % 100u) < 92 ? 200 : ((rv % 100u) < 98 ? 404 : 500));
            r.payload_bytes = (uint32_t)(rv % 8192u);
            r.user_hash = (uint32_t)(xorshift64star(&rng) & 0xffffffffu);
            r.seq = i;
            checksum ^= mix_record(&r);

            if (write_full(STDOUT_FILENO, &r, sizeof(record_t)) != 0) {
                die("write_full(stdout)");
            }
        }

        uint64_t t1 = now_ns();
        double sec = (t1 - t0) / 1e9;

        fprintf(stderr, "PRODUCER(pipe): produced=%llu checksum=0x%llx time=%.3fs rate=%.2f Mrec/s\n",
                (unsigned long long)records,
                (unsigned long long)checksum,
                sec,
                (records / sec) / 1e6);

        return 0;
    }

    /* ====== SHM MODE - Students implement this ====== */
    /* See LAB.md "Building the SHM Producer and Consumer" section for step-by-step instructions */

    shm_unlink(name);


    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        perror("shm_open");
        exit(1);
    }

    size_t shm_size = sizeof(shm_header_t) + capacity * sizeof(record_t);
    
    if (ftruncate(fd, shm_size) != 0) {
        perror("ftruncate");
        exit(1);
    }

    shm_header_t *hdr = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (hdr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

     record_t *ring = (record_t *)((uint8_t)hdr + sizeof(shm_header_t));

    close(fd);

  
   

    memset(hdr, 0, sizeof(*hdr));
    hdr -> capacity = capacity;
    hdr -> ready = 1;

    uint64_t t0 = now_ns();
    uint64_t head = 0;

    for (uint64_t i = 0; i < records; i++) {
        record_t r;
        r.ts_ns = now_ns();
        uint64_t rv = xorshift64star(&rng);
        r.endpoint_id = (uint32_t)(rv % 10000u);
        r.method = (uint16_t)(rv % 4u);
        r.status = (uint16_t)((rv % 100u) < 92 ? 200 : ((rv % 100u) < 98 ? 404 : 500));
        r.payload_bytes = (uint32_t)(rv % 8192u);
        r.user_hash = (uint32_t)(xorshift64star(&rng) & 0xffffffffu);
        r.seq = i;

        checksum ^= mix_record(&r);

        while (head - hdr-> tail >= capacity) {
            ring[head % capacity] = r;
            head++;
            hdr->head = head;
        }
        
    }
    
    uint64_t t1 = now_ns();
    double sec = (t1 - t0) / 1e9;

    hdr->produced = records;
    hdr->checksum_prod = checksum;
    hdr->done = 1;

    fprintf(stderr, "PRODUCER(shm): produced=%llu checksum=0x%llx time=%.3fs rate=%.2f Mrec/s\n",
        (unsigned long long)records, (unsigned long long)checksum, sec, (records / sec) / 1e6);
    
    munmap(hdr, shm_size);
    return 0;
}
