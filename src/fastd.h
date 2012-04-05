/*
  Copyright (c) 2012, Matthias Schiffer <mschiffer@universe-factory.net>
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#ifndef _FASTD_FASTD_H_
#define _FASTD_FASTD_H_

#include "types.h"
#include "queue.h"

#include <errno.h>
#include <linux/if_ether.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <time.h>


struct _fastd_buffer {
	void *base;
	size_t base_len;

	void *data;
	size_t len;
};

struct _fastd_eth_addr {
	uint8_t data[ETH_ALEN];
};

struct _fastd_protocol {
	const char *name;

	fastd_protocol_config* (*init)(fastd_context *ctx);
	void (*peer_configure)(fastd_context *ctx, fastd_peer_config *peer_conf);
	void (*peer_config_purged)(fastd_context *ctx, fastd_peer_config *peer_conf);

	size_t (*max_packet_size)(fastd_context *ctx);
	size_t (*min_encrypt_head_space)(fastd_context *ctx);
	size_t (*min_decrypt_head_space)(fastd_context *ctx);

	void (*handshake_init)(fastd_context *ctx, fastd_peer *peer);
	void (*handshake_handle)(fastd_context *ctx, fastd_peer *peer, const fastd_handshake *handshake);

	void (*handle_recv)(fastd_context *ctx, fastd_peer *peer, fastd_buffer buffer);
	void (*send)(fastd_context *ctx, fastd_peer *peer, fastd_buffer buffer);

	void (*free_peer_state)(fastd_context *ctx, fastd_peer *peer);

	void (*generate_key)(fastd_context *ctx);
};

struct _fastd_config {
	fastd_loglevel loglevel;

	unsigned keepalive_interval;
	unsigned peer_stale_time;
	unsigned peer_stale_time_temp;
	unsigned eth_addr_stale_time;

	char *ifname;

	struct sockaddr_in bind_addr_in;
	struct sockaddr_in6 bind_addr_in6;

	uint16_t mtu;
	fastd_mode mode;

	bool peer_to_peer;

	const fastd_protocol *protocol;
	char *secret;
	unsigned key_valid;
	unsigned key_refresh;

	fastd_string_stack *peer_dirs;
	fastd_peer_config *peers;

	unsigned n_floating;
	unsigned n_v4;
	unsigned n_v6;

	fastd_protocol_config *protocol_config;

	char *on_up;
	char *on_up_dir;

	char *on_down;
	char *on_down_dir;
};

struct _fastd_context {
	const fastd_config *conf;

	char *ifname;

	struct timespec now;

	fastd_peer *peers;
	fastd_queue task_queue;

	int tunfd;
	int sockfd;
	int sock6fd;

	size_t eth_addr_size;
	size_t n_eth_addr;
	fastd_peer_eth_addr *eth_addr;

	unsigned int randseed;
};

struct _fastd_string_stack {
	fastd_string_stack *next;
	char str[];
};

void fastd_printf(const fastd_context *ctx, const char *format, ...);

void fastd_read_peer_dir(fastd_context *ctx, fastd_config *conf, const char *dir);
bool fastd_read_config(fastd_context *ctx, fastd_config *conf, const char *filename, bool peer_config, int depth);
void fastd_configure(fastd_context *ctx, fastd_config *conf, int argc, char *const argv[]);
void fastd_reconfigure(fastd_context *ctx, fastd_config *conf);
void fastd_config_release(fastd_context *ctx, fastd_config *conf);

void fastd_random_bytes(fastd_context *ctx, void *buffer, size_t len, bool secure);

static inline int fastd_rand(fastd_context *ctx, int min, int max) {
	unsigned int r = (unsigned int)rand_r(&ctx->randseed);
	return (r%(max-min) + min);
}

#define pr_log(ctx, level, prefix, args...) do { \
		if ((ctx)->conf == NULL || (level) <= (ctx)->conf->loglevel) { \
			char timestr[100]; \
			time_t t; \
			struct tm tm; \
			\
			t = time(NULL); \
			if (localtime_r(&t, &tm) != NULL) { \
				if (strftime(timestr, sizeof(timestr), "%F %T %z --- ", &tm) > 0) \
					fputs(timestr, stderr); \
			} \
			\
			fputs(prefix, stderr); \
			fastd_printf(ctx, args); \
			fputs("\n", stderr); \
		} \
	} while(0)

#define is_error(ctx) ((ctx)->conf == NULL || LOG_ERROR <= (ctx)->conf->loglevel)
#define is_warn(ctx) ((ctx)->conf == NULL || LOG_WARN <= (ctx)->conf->loglevel)
#define is_info(ctx) ((ctx)->conf == NULL || LOG_INFO <= (ctx)->conf->loglevel)
#define is_verbose(ctx) ((ctx)->conf == NULL || LOG_VERBOSE <= (ctx)->conf->loglevel)
#define is_debug(ctx) ((ctx)->conf == NULL || LOG_DEBUG <= (ctx)->conf->loglevel)

#define pr_fatal(ctx, args...) pr_log(ctx, LOG_FATAL, "Fatal: ", args)
#define pr_error(ctx, args...) pr_log(ctx, LOG_ERROR, "Error: ", args)
#define pr_warn(ctx, args...) pr_log(ctx, LOG_WARN, "Warning: ", args)
#define pr_info(ctx, args...) pr_log(ctx, LOG_INFO, "Info: ", args)
#define pr_verbose(ctx, args...) pr_log(ctx, LOG_VERBOSE, "Verbose: ", args)
#define pr_debug(ctx, args...) pr_log(ctx, LOG_DEBUG, "DEBUG: ", args)

#define warn_errno(ctx, message) pr_warn(ctx, "%s: %s", message, strerror(errno))
#define exit_fatal(ctx, args...) do { pr_fatal(ctx, args); abort(); } while(0)
#define exit_bug(ctx, message) exit_fatal(ctx, "BUG: %s", message)
#define exit_error(ctx, args...) do { pr_error(ctx, args); exit(1); } while(0)
#define exit_errno(ctx, message) exit_error(ctx, "%s: %s", message, strerror(errno))


#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})


static inline fastd_buffer fastd_buffer_alloc(size_t len, size_t head_space, size_t tail_space) {
	size_t base_len = head_space+len+tail_space;
	uint8_t *ptr = malloc(base_len);
	return (fastd_buffer){ .base = ptr, .base_len = base_len, .data = ptr+head_space, .len = len };
}

static inline void fastd_buffer_free(fastd_buffer buffer) {
	free(buffer.base);
}

static inline void fastd_buffer_pull_head(fastd_buffer *buffer, size_t len) {
	buffer->data -= len;
	buffer->len += len;

	if (buffer->data < buffer->base)
		abort();
}

static inline void fastd_buffer_push_head(fastd_buffer *buffer, size_t len) {
	if (buffer->len < len)
		abort();

	buffer->data += len;
	buffer->len -= len;
}

static inline size_t fastd_max_packet_size(const fastd_context *ctx) {
	switch (ctx->conf->mode) {
	case MODE_TAP:
		return ctx->conf->mtu+ETH_HLEN;
	case MODE_TUN:
		return ctx->conf->mtu;
	default:
		exit_bug(ctx, "invalid mode");
	}
}

static inline fastd_string_stack* fastd_string_stack_dup(const char *str) {
	fastd_string_stack *ret = malloc(sizeof(fastd_string_stack) + strlen(str) + 1);
	ret->next = NULL;
	strcpy(ret->str, str);

	return ret;
}

static inline fastd_string_stack* fastd_string_stack_push(fastd_string_stack *stack, const char *str) {
	fastd_string_stack *ret = malloc(sizeof(fastd_string_stack) + strlen(str) + 1);
	ret->next = stack;
	strcpy(ret->str, str);

	return ret;
}

static inline void fastd_string_stack_free(fastd_string_stack *str) {
	while(str) {
		fastd_string_stack *next = str->next;
		free(str);
		str = next;
	}
}

static inline bool timespec_after(const struct timespec *tp1, const struct timespec *tp2) {
	return (tp1->tv_sec > tp2->tv_sec ||
		(tp1->tv_sec == tp2->tv_sec && tp1->tv_nsec > tp2->tv_nsec));
}

/* returns (tp1 - tp2) in milliseconds  */
static inline int timespec_diff(const struct timespec *tp1, const struct timespec *tp2) {
	return ((tp1->tv_sec - tp2->tv_sec))*1000 + (tp1->tv_nsec - tp2->tv_nsec)/1e6;
}

static inline bool strequal(const char *str1, const char *str2) {
	if (str1 && str2)
		return (!strcmp(str1, str2));
	else
		return (str1 == str2);
}

#endif /* _FASTD_FASTD_H_ */
