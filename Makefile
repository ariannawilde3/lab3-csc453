CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS = -lrt

TARGETS = producer consumer

all: $(TARGETS)

producer: producer.c common.h shm_ring.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

consumer: consumer.c common.h shm_ring.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS)

.PHONY: all clean
