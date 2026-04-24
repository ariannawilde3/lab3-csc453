#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

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
        "  %s --ipc=pipe\n"
        "  %s --ipc=shm --name /shmname\n"
        "\n"
        "Pipe mode reads binary log records from stdin.\n"
        "SHM mode attaches to shared memory created by producer.\n"
        "\n"
        "Options:\n"
        "  --name /X   (shm) shared memory name used by producer\n",
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
    const char *name = NULL;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--ipc=", 6) == 0) {
            mode = parse_ipc(argv[i] + 6);
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            name = argv[++i];
        } else {
            usage(argv[0]);
        }
    }

    if (mode == IPC_SHM && (!name || name[0] != '/')) {
        die2("SHM mode requires --name starting with '/'", name ? name : "(null)");
    }

    /* Simple analytics: endpoint hit counts */
    const uint32_t ENDPOINTS = 10000;
    uint64_t *hits = (uint64_t*)calloc(ENDPOINTS, sizeof(uint64_t));
    if (!hits) die("calloc(hits)");

    uint64_t checksum = 0;
    uint64_t consumed = 0;

    if (mode == IPC_PIPE) {
        uint64_t t0 = now_ns();

        for (;;) {
            record_t r;
            ssize_t got = read_full(STDIN_FILENO, &r, sizeof(record_t));
            if (got < 0) die("read_full(stdin)");
            if (got == 0) break; /* EOF */
            if (got != (ssize_t)sizeof(record_t)) {
                die2("Partial record read from pipe", "input corrupted?");
            }

            if (r.endpoint_id < ENDPOINTS) hits[r.endpoint_id]++;
            checksum ^= mix_record(&r);
            consumed++;
        }

        uint64_t t1 = now_ns();
        double sec = (t1 - t0) / 1e9;

        fprintf(stderr, "CONSUMER(pipe): consumed=%llu checksum=0x%llx time=%.3fs rate=%.2f Mrec/s\n",
                (unsigned long long)consumed,
                (unsigned long long)checksum,
                sec,
                (consumed / sec) / 1e6);

        free(hits);
        return 0;
    }

    /* ====== SHM MODE - Students implement this ====== */
    /* See LAB.md "Building the SHM Producer and Consumer" section for step-by-step instructions */

    int fd = -1;
    for (int attempt = 0; attempt < 200; attempt++) {
        fd = shm_open(name, O_RDWR, 0600);
        if (fd >= 0) {
            break;
        }
        usleep(10 * 1000);
    }

    if (fd < 0) {
        perror("shm_open");
        exit(1);
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("fstat");
        exit(1);
    }

    size_t shm_size = (size_t) st.st_size;

    void *mem = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    close(fd);
    shm_header_t *hdr = (shm_header_t *)mem;

    int readyOK= 0;
    for (int attempt = 0; attempt < 200; attempt++) {
        if (hdr -> ready) {
            readyOK = 1;
            break;
        }
        usleep(10 * 1000);
    }

    if(!readyOK) {
        fprintf(stderr, "producer never set ready\n");
        exit(1);
    }

    record_t *ring = (record_t *)((uint8_t *)mem + sizeof(shm_header_t));

    uint64_t t0 = now_ns();

    uint64_t tail = 0;

    while (1) {

        uint64_t head = hdr -> head;

        if (tail < head) {
            record_t r = ring[tail % hdr->capacity];

            tail++;
            hdr -> tail = tail;

            if (r.endpoint_id < ENDPOINTS) hits[r.endpoint_id]++;
                checksum ^= mix_record(&r);
                consumed++;
            } else {
                if (hdr->done && tail >= hdr->produced) {
                    break;
                }
            }
        }
    }

    uint64_t t1 = now_ns();
    double sec = (t1 - t0) / 1e9;

    hdr -> consumed = consumed;
    hdr -> checksum_cons = checksum;

    fprintf(stderr, "CONSUMER(shm): consumed=%llu checksum=0x%llx time=%.3fs rate=%.2f Mrec/s\n",
        (unsigned long long)consumed, (unsigned long long)checksum, sec, (consumed / sec) / 1e6);

    munmap(mem, shm_size);
    shm_unlink(name);
    free(hits);
    return 0;
}

