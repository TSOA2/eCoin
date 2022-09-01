#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <netdb.h>

#if 1
#define ENABLE_DEBUG
#define d() \
    fprintf(stderr, "got here.\n"); \
    fflush(stderr);

#endif

#define error(e) perror(e);
#define warn(w) fprintf(stderr, "warning: %s\n", w);

#define PORT "6942"

struct block_header {
	uint32_t nonce;
	uint32_t unix_time;
	uint32_t random_num;
	uint8_t difficulty;
	char merkle_hash[32];
};

struct transaction {
	char block_hash[32];
	char trans_hash[32];
	char signature[256];
};

struct block {
	struct block_header header;
	struct transaction *trans;
	size_t num_trans;
};

struct block_ll {
	struct block b;
	struct block_ll *next;
};

void switch_endian(uint32_t *num);
int get_ipaddr_data(uint32_t *ipaddr, uint32_t *sbnet_mask);
int connect_to_server(const char *server_ip, const char *port);
void *ping_miners_thread(void *args);
int ping_miners(unsigned conn_timeout, unsigned read_timeout);
