# Makefile for RDMA Server and Client (RoCE)

CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -libverbs

TARGETS = rdma_server rdma_client
SRCS = rdma_server.c rdma_client.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGETS)

rdma_server: rdma_server.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

rdma_client: rdma_client.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGETS) $(OBJS)
