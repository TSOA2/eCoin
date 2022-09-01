/*
 * bc.c
 * TSOA, 2022
 *
 * Functions for processing blocks/transactions are here.
 */

#include "common.h"

/* Convert four bytes to a uint32_t */
uint32_t bytes_to_32(char *buf)
{
	uint32_t *p = (uint32_t *) buf;
	return *p;
}

/* Ask the "server" (or node, this should be renamed)
 * for data about the blockchain.
 */

struct block_ll *download_blockchain(int server_fd)
{
	const char *prompt_msg = "GIMME BLOCKCHAIN";

	const size_t recv_chunk = 2048;
	size_t recv_size = recv_chunk;
	char *recv_buf = malloc(recv_size);

	if (recv_buf == NULL) {

#ifdef ENABLE_DEBUG
		error("malloc()");
#endif

		return NULL;
	}

	if (send(server_fd, prompt_msg, sizeof(prompt_msg), 0) != sizeof(prompt_msg)) {

#ifdef ENABLE_DEBUG
		error("send()");
#endif
	
		return NULL;
	}

	for (;;) {
		int read_ret = read(server_fd, recv_buf + recv_size - recv_chunk, recv_chunk, 0);
		if (read_ret == 0) {
			break;
		} else if (read_ret < 0) {

#ifdef ENABLE_DEBUG
			error("read()");
#endif

			return NULL;
		}

		recv_size += recv_chunk;
		recv_buf = realloc(recv_buf, recv_size);
		if (recv_buf == NULL) {

#ifdef ENABLE_DEBUG
			error("malloc()");
#endif

			return NULL;
		}
	}
}

/* Produce a linked list of blocks according to text data */
struct block_ll *parse_blockchain_data(char *data, size_t data_len)
{
	if (data == NULL || data_len == 0) {
		return NULL;
	}

	struct block_ll *bll = malloc(sizeof(struct block_ll));
	struct block_ll *bll_start = bll;
	if (bll == NULL) {

#ifdef ENABLE_DEBUG
		error("malloc()");
#endif
		
		return NULL;
	}

	/*
	 * Block start signal
	 */

	const char *block_start = "block_start";

	/*
	 * Block header
	 */

	const char *block_nonce = "\nnonce:";
	const char *block_utime = "\nutime:";
	const char *block_rnum  = "\nrnum:";
	const char *block_diff  = "\ndiff:";
	const char *block_mrkle = "\nmrkle:";

	/*
	 * Block transactions:
	 * trans_num symbolizes the number of transactions
	 * *and* the start of transactions.
	 */

	const char *block_trans_num   = "\ntrans_num:";
	const char *block_trans_end   = "\ntrans_end";

	/*
	 * Block end signal
	 */

	const char *block_end = "\nblock_end";

	/*
	 * Data is being transmitted in big endian
	 * format, and we are assuming this machine
	 * is little endian.
	 */

	for (size_t i = 0; i < data_len; i++) {
		if (!strncmp(data, block_start, sizeof(block_start))) {
			data += start_size;
		} else {
			break;
		}

		if (!strncmp(data, block_nonce, sizeof(block_nonce))) {
			data += sizeof(block_nonce);
			bll->b.header.nonce = bytes_to_32(data);
			switch_endian(&(bll->b.header.nonce));
			data += 4; /* get passed uint32 data */
		} else {
			break;
		}

		if (!strncmp(data, block_utime, sizeof(block_utime))) {
			data += sizeof(block_utime);
			bll->b.header.unix_time = bytes_to_32(data);
			switch_endian(&(bll->b.header.unix_time));
			data += 4;
		} else {
			break;
		}

		if (!strncmp(data, block_rnum, sizeof(block_rnum))) {
			data += sizeof(block_rnum);
			bll->b.header.random_num = bytes_to_32(data);
			switch_endian(&(bll->b.header.random_num));
			data += 4;
		} else {
			break;
		}

		if (!strncmp(data, block_diff, sizeof(block_diff))) {
			/*
			 * Just one byte of data
			 */

			data += sizeof(block_diff);
			bll->b.header.difficulty = *data;
			data++;
		} else {
			break;
		}

		if (!strncmp(data, block_mrkle, sizeof(block_mrkle))) {
			memcpy(bll->b.header.merkle_hash, data, 32);
			data += 32;
		} else {
			break;
		}

		if (!strncmp(data, block_trans_num, sizeof(block_trans_num))) {
			data += sizeof(block_trans_num);
			bll->b.num_trans = bytes_to_32(data);
			switch_endian(&(bll->b.num_trans));
			data += 4;

			size_t index = 0;
			if (get_transactions(data, &bll->b, &index) < 0) {

#ifdef ENABLE_DEBUG
				error("get_transactions()");
#endif

				break;
			}

			data += index;
		} else {
			break;
		}
		
		if (!strncmp(data, block_trans_end, sizeof(block_trans_end))) {
			data += sizeof(block_trans_end);
		} else {
			break;
		}

		bll->next = malloc(sizeof(struct block_ll));
		if (bll->next == NULL) {

#ifdef ENABLE_DEBUG
			error("malloc()");
#endif

			break;
		}

		continue;

incomplete:
#ifdef ENABLE_DEBUG
		warn("incomplete block");
#endif
		continue;
	}



failure:
	return NULL;
}
