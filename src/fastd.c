/*
  Copyright (c) 2012, Matthias Schiffer <mschiffer@universe-factory.net>
  Partly based on QuickTun Copyright (c) 2010, Ivo Smits <Ivo@UCIS.nl>.
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


#include "fastd.h"
#include "handshake.h"
#include "task.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>


extern fastd_method fastd_method_null;


static void init_tuntap(fastd_context *ctx) {
	struct ifreq ifr;

	pr_debug(ctx, "Initializing tun/tap device...");

	if ((ctx->tunfd = open("/dev/net/tun", O_RDWR)) < 0)
		exit_errno(ctx, "Could not open tun/tap device file");

	memset(&ifr, 0, sizeof(ifr));

	if (ctx->conf->ifname)
		strncpy(ifr.ifr_name, ctx->conf->ifname, IF_NAMESIZE-1);

	ifr.ifr_flags = IFF_TAP;
	ifr.ifr_flags |= IFF_NO_PI;
	if (ioctl(ctx->tunfd, TUNSETIFF, (void *)&ifr) < 0)
		exit_errno(ctx, "TUNSETIFF ioctl failed");

	pr_debug(ctx, "Tun/tap device initialized.");
}

static void init_socket(fastd_context *ctx) {
	pr_debug(ctx, "Initializing UDP socket...");

	if ((ctx->sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		exit_errno(ctx, "socket");

	struct sockaddr_in bindaddr;
	bindaddr.sin_family = AF_INET;
	bindaddr.sin_addr.s_addr = ctx->conf->bind_address;
	bindaddr.sin_port = ctx->conf->bind_port;

	if (bind(ctx->sockfd, (struct sockaddr*)&bindaddr, sizeof(struct sockaddr_in)))
		exit_errno(ctx, "bind");

	pr_debug(ctx, "UDP socket initialized.");
}

static void default_config(fastd_config *conf) {
	conf->loglevel = LOG_DEBUG;
	conf->ifname = NULL;
	conf->bind_address = htonl(INADDR_ANY);
	conf->bind_port = htons(1337);
	conf->mtu = 1500;
	conf->protocol = PROTOCOL_ETHERNET;
	conf->method = &fastd_method_null;
	conf->peers = NULL;
}

static void configure(fastd_context *ctx, fastd_config *conf, int argc, char *argv[]) {
	default_config(conf);

	fastd_peer_config **current_peer = &conf->peers;

	static const struct option long_options[] = {
		{"dev",      required_argument, 0, 'i'},
		{"bind",     required_argument, 0, 'b'},
		{"mtu",      required_argument, 0, 'M'},
		{"protocol", required_argument, 0, 'P'},
		{"method",   required_argument, 0, 'm'},
		{"peer",     required_argument, 0, 'p'},
		{0, 0, 0, 0}
	};

	int c;
	int option_index = 0;
	struct in_addr addr;
	long l;
	char *charptr;
	char *endptr;
	char *addrstr;

	while ((c = getopt_long (argc, argv, "i:b:M:P:m:p:", long_options, &option_index)) != -1) {
		switch(c) {
		case 'i':
			conf->ifname = optarg;
			break;

		case 'b':
			charptr = strchr(optarg, ':');
			if (charptr) {
				addrstr = strndup(optarg, charptr-optarg);
			}
			else {
				addrstr = optarg;
			}

			if (inet_pton(AF_INET, addrstr, &addr) != 1) {
				exit_error(ctx, "invalid bind address `%s'", addrstr);
			}

			conf->bind_address = addr.s_addr;

			if (charptr) {
				l = strtol(charptr+1, &endptr, 10);
				if (*endptr || l > 65535)
					exit_error(ctx, "invalid bind port `%s'", charptr+1);
				conf->bind_port = htons(l);

				free(addrstr);
			}

			break;

		case 'M':
			conf->mtu = strtol(optarg, &endptr, 10);
			if (*endptr || conf->mtu < 576)
				exit_error(ctx, "invalid mtu `%s'", optarg);
			break;

		case 'P':
			if (!strcmp(optarg, "ethernet"))
				conf->protocol = PROTOCOL_ETHERNET;
			else if (!strcmp(optarg, "ip"))
				conf->protocol = PROTOCOL_IP;
			else
				exit_error(ctx, "invalid protocol `%s'", optarg);
			break;

		case 'm':
			if (!strcmp(optarg, "null"))
				conf->method = &fastd_method_null;
			else
				exit_error(ctx, "invalid method `%s'", optarg);
			break;

		case 'p':
			*current_peer = malloc(sizeof(fastd_peer_config));
			(*current_peer)->next = NULL;

			charptr = strchr(optarg, ':');
			if (charptr) {
				addrstr = strndup(optarg, charptr-optarg);
			}
			else {
				addrstr = optarg;
			}

			if (inet_pton(AF_INET, addrstr, &addr) != 1) {
				exit_error(ctx, "invalid bind address `%s'", addrstr);
			}

			(*current_peer)->address = addr.s_addr;

			if (charptr) {
				l = strtol(charptr+1, &endptr, 10);
				if (*endptr || l > 65535)
					exit_error(ctx, "invalid bind port `%s'", charptr+1);
				(*current_peer)->port = htons(l);

				free(addrstr);
			}
			else {
				(*current_peer)->port = htons(1337); // Default port
			}

			current_peer = &(*current_peer)->next;

			break;

		case '?':
			exit(1);

		default:
			abort();
		}
	}

	if (!conf->peers) {
		exit_error(ctx, "no peers have been configured");
	}
}

static void init_peers(fastd_context *ctx) {
	fastd_peer **current_peer = &ctx->peers;
	fastd_peer_config *peer_conf;
	for (peer_conf = ctx->conf->peers; peer_conf; peer_conf = peer_conf->next) {
		*current_peer = malloc(sizeof(fastd_peer));
		(*current_peer)->next = NULL;
		(*current_peer)->config = peer_conf;
		(*current_peer)->address = peer_conf->address;
		(*current_peer)->port = peer_conf->port;
		(*current_peer)->state = STATE_WAIT;
		(*current_peer)->last_req_id = 0;
		(*current_peer)->addresses = NULL;

		fastd_task_schedule_handshake(ctx, *current_peer, 0);

		current_peer = &(*current_peer)->next;
	}
}

static void* get_source_address(const fastd_context *ctx, void *buffer) {
	switch (ctx->conf->protocol) {
	case PROTOCOL_ETHERNET:
		return &((struct ethhdr*)buffer)->h_source;
	case PROTOCOL_IP:
		return NULL;
	default:
		exit_bug(ctx, "invalid protocol");
	}
}

static void* get_dest_address(const fastd_context *ctx, void *buffer) {
	switch (ctx->conf->protocol) {
	case PROTOCOL_ETHERNET:
		return &((struct ethhdr*)buffer)->h_dest;
	case PROTOCOL_IP:
		return NULL;
	default:
		exit_bug(ctx, "invalid protocol");
	}
}

static void handle_tasks(fastd_context *ctx) {
	fastd_task *task;
	while ((task = fastd_task_get(ctx)) != NULL) {
		switch (task->type) {
		case TASK_SEND:
			if (task->send.peer) {
				struct msghdr msg;
				memset(&msg, 0, sizeof(msg));

				struct sockaddr_in sendaddr;
				sendaddr.sin_family = AF_INET;
				sendaddr.sin_addr.s_addr = task->send.peer->address;
				sendaddr.sin_port = task->send.peer->port;
					
				msg.msg_name = &sendaddr;
				msg.msg_namelen = sizeof(sendaddr);

				struct iovec iov[2] = {
					{ .iov_base = &task->send.packet_type, .iov_len = 1 },
					{ .iov_base = task->send.buffer.data, .iov_len = task->send.buffer.len }
				};

				msg.msg_iov = iov;
				msg.msg_iovlen = task->send.buffer.len ? 2 : 1;

				sendmsg(ctx->sockfd, &msg, 0);
			}

			fastd_buffer_free(task->send.buffer);
			break;

		case TASK_HANDLE_RECV:
			// TODO Handle source address
			write(ctx->tunfd, task->handle_recv.buffer.data, task->handle_recv.buffer.len);
			fastd_buffer_free(task->handle_recv.buffer);
			break;

		case TASK_HANDSHAKE:
			if (task->handshake.peer->state != STATE_WAIT)
				break;

			pr_debug(ctx, "Sending handshake...");
			fastd_handshake_send(ctx, task->handshake.peer);

			fastd_task_schedule_handshake(ctx, task->handshake.peer, 20000);
			break;

		default:
			exit_bug(ctx, "invalid task type");
		}

		free(task);
	}
}

static void handle_input(fastd_context *ctx) {
	struct pollfd fds[2];
	fds[0].fd = ctx->tunfd;
	fds[0].events = POLLIN;
	fds[1].fd = ctx->sockfd;
	fds[1].events = POLLIN;

	int ret = poll(fds, 2, fastd_task_timeout(ctx));
	if (ret < 0)
		exit_errno(ctx, "poll");

	if (fds[0].revents & POLLIN) {
		size_t max_len = fastd_max_packet_size(ctx);
		fastd_buffer buffer = fastd_buffer_alloc(max_len, 0, 0);

		ssize_t len = read(ctx->tunfd, buffer.data, max_len);
		if (len < 0)
			exit_errno(ctx, "read");

		uint8_t *src_addr = get_source_address(ctx, buffer.data);
		uint8_t *dest_addr = get_dest_address(ctx, buffer.data);

		pr_debug(ctx, "A packet with length %u is to be sent from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x",
			 (unsigned)len, src_addr[0], src_addr[1], src_addr[2], src_addr[3], src_addr[4], src_addr[5],
			 dest_addr[0], dest_addr[1], dest_addr[2], dest_addr[3], dest_addr[4], dest_addr[5]);

		// TODO find correct peer

		fastd_peer *peer;
		for (peer = ctx->peers; peer; peer = peer->next) {
			if (peer->state == STATE_ESTABLISHED) {
				fastd_buffer send_buffer = fastd_buffer_alloc(len, 0, 0);
				memcpy(send_buffer.data, buffer.data, len);
				ctx->conf->method->method_send(ctx, peer, send_buffer);
			}
		}

		fastd_buffer_free(buffer);
	}
	if (fds[1].revents & POLLIN) {
		size_t max_len = ctx->conf->method->method_max_packet_size(ctx);
		fastd_buffer buffer = fastd_buffer_alloc(max_len, 0, 0);

		uint8_t packet_type;

		struct iovec iov[2] = {
			{ .iov_base = &packet_type, .iov_len = 1 },
			{ .iov_base = buffer.data, .iov_len = max_len }
		};
		struct sockaddr_in recvaddr;
			
		struct msghdr msg;
		memset(&msg, 0, sizeof(msg));

		msg.msg_name = &recvaddr;
		msg.msg_namelen = sizeof(recvaddr);
		msg.msg_iov = iov;
		msg.msg_iovlen = 2;

		ssize_t len = recvmsg(ctx->sockfd, &msg, 0);
		if (len < 0)
			pr_warn(ctx, "recvfrom: %s", strerror(errno));

		fastd_peer *peer;
		for (peer = ctx->peers; peer; peer = peer->next) {
			if (recvaddr.sin_addr.s_addr == peer->address && recvaddr.sin_port == peer->port)
				break;
		}

		if (peer) {
			switch (packet_type) {
			case 0:
				buffer.len = len - 1;
				ctx->conf->method->method_handle_recv(ctx, peer, buffer);
				break;

			case 1:
				fastd_handshake_handle(ctx, peer, buffer);
				break;

			default:
				fastd_buffer_free(buffer);
			}
		}
		else {
			pr_debug(ctx, "received packet from unknown peer");
			fastd_buffer_free(buffer);
		}
	}
}


int main(int argc, char *argv[]) {
	fastd_context ctx;
	memset(&ctx, 0, sizeof(ctx));

	fastd_config conf;
	configure(&ctx, &conf, argc, argv);
	ctx.conf = &conf;

	init_peers(&ctx);

	init_tuntap(&ctx);
	init_socket(&ctx);

	while (1) {
		handle_tasks(&ctx);
		handle_input(&ctx);
	}

	return 0;
}