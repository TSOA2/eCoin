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

/* Switch the endianness of a uint32_t */
void switch_endian(uint32_t *num)
{
	/*
	 * Isn't it beautiful?
	 */

	uint32_t a, b, c, d;
	a = (*num & 0x000000ff) << 24;
	b = (*num & 0x0000ff00) << 8;
	c = (*num & 0x00ff0000) >> 8;
	d = (*num & 0xff000000) >> 24;

	*num = a | b | c | d;
}

/* Get the subnet mask in big endian format */
int get_ipaddr_data(uint32_t *ipaddr, uint32_t *sbnet_mask)
{
	struct ifaddrs *ifstart, *ifindex;
	struct sockaddr_in *subnet_data;
	struct sockaddr_in *ipaddr_data;

	if (getifaddrs(&ifstart) < 0) {
		error("getifaddrs()");
		goto fail;
	}

	/*
	 * Don't want the loopback interface ("lo")
	 */

	for (ifindex = ifstart; ifindex != NULL; ifindex = ifindex->ifa_next) {
		if (ifindex->ifa_addr && ifindex->ifa_addr->sa_family == AF_INET &&
				ifindex->ifa_netmask && strcmp(ifindex->ifa_name, "lo")) {

			/*
			 * Store the IP address
			 */

			subnet_data = (struct sockaddr_in *) ifindex->ifa_netmask;
			ipaddr_data = (struct sockaddr_in *) ifindex->ifa_addr;

			*sbnet_mask = (uint32_t) subnet_data->sin_addr.s_addr;
			*ipaddr = (uint32_t) ipaddr_data->sin_addr.s_addr;

			freeifaddrs(ifstart);
			return 0;
		}
	}

fail:
	/*
	 * No valid interfaces could be found,
	 * maybe default to a predefined subnet mask.
	 * (255.255.255.0)
	 */

	*sbnet_mask = 0x00ffffff;

	/*
	 * Set the default ip address to
	 * 192.168.1.1
	 */

	*ipaddr = 0x0101a8c0;

	/*
	 * Still let calling function know we
	 * couldn't find any valid interfaces.
	 */

	freeifaddrs(ifstart);
	return -1;
}

/* Return a file descriptor linking us to a server */
int connect_to_server(const char *server_ip, const char *port)
{
	int server_fd;
	int err_val;
	struct addrinfo hints, *info, *idx;

	/*
	 * Only AF_INET for now. Later on we
	 * could possibly add AF_INET6
	 */

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	/*
	 * getaddrinfo has it's own errors specified
	 * by it's return value
	 */

	if ((err_val = getaddrinfo(server_ip, port, &hints, &info)) != 0) {
		fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(err_val));
		return -1;
	}

	/*
	 * Loop through the info supplied by getaddrinfo
	 * and find a connection
	 */

	for (idx = info; idx != NULL; idx = idx->ai_next) {
		if ((server_fd = socket(idx->ai_family, idx->ai_socktype, idx->ai_protocol)) < 0) {
			continue;
		}

		if (connect(server_fd, idx->ai_addr, idx->ai_addrlen) < 0) {
			close(server_fd);
			continue;
		}

		break;
	}

	if (idx == NULL) {
		/*
		 * Could not establish connection with server
		 */

		return -1;
	}

	freeaddrinfo(info);
	return server_fd;
}

/* This helps manage connection timeouts */
void *ping_miners_thread(void *args)
{
	void **actual_args = (void **) args;
	char *server_ip = actual_args[0];
	char *port = actual_args[1];
	int *fd = actual_args[2];
	int *thread_done = actual_args[3];

	if (pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL) != 0) {
		error("pthread_setcanceltype()");
		*thread_done = 1;
		*fd = -1;
		return NULL;
	}

	if ((*fd = connect_to_server(server_ip, port)) < 0) {
		
#ifdef ENABLE_DEBUG
		fprintf(stderr, "ping failed.\n");
#endif

		;
	}

	*thread_done = 1;
	return NULL;
}

/* Ping the entire network in search of miners */
int ping_miners(unsigned conn_timeout, unsigned read_timeout)
{
	uint32_t ipaddr; /* This machine's ip address */
	uint32_t subnet; /* Subnet mask for this network */
	uint32_t host_num; /* Number of hosts on this network */
	uint32_t first_ip; /* First ip to try */

	int server_fd, thread_done = 0; /* The server file descriptor */

	struct in_addr tmp; /* For inet_ntoa */

	fd_set *fd_list;

	struct timeval timeout;

	/*
	 * Note: the send ping message must
	 * be the same size as the expected recv
	 * message.
	 */

	char ping_msg[] = "ECOIN PING!";
	char expected[] = "PING ECOIN!";

	size_t ping_size = sizeof(ping_msg);

	char response[ping_size];

	void *thread_data_p[4];

	pthread_t thread;

	struct timeval last_time, current_time;
	uint32_t ctime, ltime;

	/*
	 * Send a warning to stderr, but get_ipaddr_data
	 * already sends a guess of the subnet mask 
	 * and ip address.
	 */

	if (get_ipaddr_data(&ipaddr, &subnet) < 0) {

#ifdef ENABLE_DEBUG
		warn("could not get subnet mask / ip address");
#endif

		;
	}

	/*
	 * Is this the best way to do this?
	 */

	switch_endian(&subnet);
	host_num = 0xffffffff - subnet;
	switch_endian(&subnet);

	first_ip = ipaddr & subnet;

	printf("%lu\n", host_num);

	/*
	 * The reason we switch endianness is because
	 * get_ipaddr_data returns the IP address 255.255.255.0
	 * as the bytes 00-ff-ff-ff (big endian), and to increment
	 * the IP address, you'd have to switch it so you'd increment
	 * 255.255.255.0, not 0.255.255.255. However, you do
	 * switch it when passing it to inet_ntoa because
	 * inet_ntoa takes big endian format.
	 */

	switch_endian(&first_ip);

	for (uint32_t i = 0; i < host_num; i++, first_ip++) {
		/*
		 * Convert this uint32_t to an in_addr
		 */

		struct in_addr tmp;
		tmp.s_addr = first_ip;

		/*
		 * Remember, we are switching this because inet_ntoa
		 * takes big endian format
		 */

		switch_endian(&tmp.s_addr);

#ifdef ENABLE_DEBUG
		fprintf(stderr, "pinging %s... ", inet_ntoa(tmp));
#endif

		/*
		 * Get the connection file descriptor in a pthread
		 * so we can detect timeouts. For example, this is
		 * a two second timeout for the thread.
		 */

		thread_done = 0;
		thread_data_p[0] = inet_ntoa(tmp);
		thread_data_p[1] = PORT;
		thread_data_p[2] = &server_fd;
		thread_data_p[3] = &thread_done;

		/*
		 * Create the thread, pass a pointer to an array of
		 * pointers to the thread, casting it as a void pointer.
		 */

		if (pthread_create(&thread, NULL, ping_miners_thread, thread_data_p) < 0) {
			error("pthread_create()");
			continue;
		}

		/*
		 * Wait until the thread is done or it times out.
		 */

		gettimeofday(&last_time, NULL);
		current_time = last_time;
		ltime = 1000000 * last_time.tv_sec + last_time.tv_usec;

		while (!thread_done) {
			gettimeofday(&current_time, NULL);
			ctime = 1000000 * current_time.tv_sec + current_time.tv_usec;
			
			if (ctime - ltime >= conn_timeout) {
				fprintf(stderr, "timeout.\n");

				pthread_cancel(thread);
				server_fd = -1;
				break;
			}

			/*
			 * Sleep here for a bit because we don't need to donate
			 * all of our time checking every single nanosecond.
			 */

			usleep(50);
		}

		pthread_join(thread, NULL);

		if (server_fd < 0) {
			continue;
		}

		/*
		 * Ping the server with a short message
		 */

		if (send(server_fd, ping_msg, ping_size, 0) != ping_size) {
			error("send()");
			close(server_fd);
			continue;
		}

		/*
		 * Ok, let me explain before you berate me for using select. I
		 * used select because I don't need this to be fast or
		 * efficient (yet), and select is simply the most readable
		 * and *portable* function I found.
		 */

		timeout.tv_sec = 0;
		timeout.tv_usec = read_timeout;

		FD_SET(server_fd, fd_list);

		int select_ret = select(1, fd_list, NULL, NULL, &timeout);
		switch (select_ret) {
			/*
			 * The server is ready to read from.
			 */

			case 1: break;

			/*
			 * The ping has timed out.
			 */

			case 0:
				{
#ifdef ENABLE_DEBUG
					fprintf(stderr, "timeout.\n");
#endif

					FD_CLR(server_fd, fd_list);
					close(server_fd);
					continue;
				}

			/*
			 * An error occured
			 */
			case -1:
				{
					error("select()");
					FD_CLR(server_fd, fd_list);
					close(server_fd);
					continue;
				}

			/*
			 * Undefined
			 */
			default:
				{
#ifdef ENABLE_DEBUG
					fprintf(stderr, "select returned strange result.\n");
#endif
					FD_CLR(server_fd, fd_list);
					close(server_fd);
					continue;
				}

		}

		FD_CLR(server_fd, fd_list);

		/*
		 * Recieve an echo from the server
		 */

		if (recv(server_fd, response, ping_size, 0) != ping_size) {
			error("recv()");
			close(server_fd);
			continue;
		}

		close(server_fd);

		/*
		 * Is the echo malformed or is it expected?
		 */

		if (strcmp(response, expected)) {

#ifdef ENABLE_DEBUG
			fprintf(stderr, "got response, but malformed.\n");
#endif

		} else {

#ifdef ENABLE_DEBUG
			fprintf(stderr, "found miner!\n");
#endif

			return 1;
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	const unsigned connection_timeout = 500; /* In microseconds */
	const unsigned reading_timeout = 500;

	if (ping_miners(connection_timeout, reading_timeout)) {
		fprintf(stderr, "=== MINER'S BEEN FOUND ===\n");
	} else {
		fprintf(stderr, "=== NO MINER FOUND ===\n");
	}
}
