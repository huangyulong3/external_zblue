/* bttester.c - Bluetooth Tester */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (c) 2022 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <zephyr/types.h>
#include <zephyr/toolchain.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
#define LOG_MODULE_NAME bttester
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_BTTESTER_LOG_LEVEL);

#include "btp/btp.h"

#define STACKSIZE 8192
static K_THREAD_STACK_DEFINE(stack, STACKSIZE);
static struct k_thread cmd_thread;

#define CMD_QUEUED 2
struct btp_buf {
	intptr_t _reserved;
	union {
		uint8_t data[BTP_MTU];
		struct btp_hdr hdr;
	};
	uint8_t rsp[BTP_MTU];
};

static struct btp_buf cmd_buf[CMD_QUEUED];

static K_FIFO_DEFINE(cmds_queue);
static K_FIFO_DEFINE(avail_queue);

static struct btp_buf *delayed_cmd;

static struct {
	const struct btp_handler *handlers;
	size_t num;
} service_handler[BTP_SERVICE_ID_MAX + 1];

static struct net_buf_simple *rsp_buf = NET_BUF_SIMPLE(BTP_MTU);
static K_MUTEX_DEFINE(rsp_buf_mutex);

static void tester_send_with_index(uint8_t service, uint8_t opcode, uint8_t index,
				   const uint8_t *data, size_t len);
static void tester_rsp_with_index(uint8_t service, uint8_t opcode, uint8_t index,
				  uint8_t status);

void tester_register_command_handlers(uint8_t service,
				      const struct btp_handler *handlers,
				      size_t num)
{
	__ASSERT_NO_MSG(service <= BTP_SERVICE_ID_MAX);
	__ASSERT_NO_MSG(service_handler[service].handlers == NULL);

	service_handler[service].handlers = handlers;
	service_handler[service].num = num;
}

static const struct btp_handler *find_btp_handler(uint8_t service, uint8_t opcode)
{
	if ((service > BTP_SERVICE_ID_MAX) ||
	    (service_handler[service].handlers == NULL)) {
		return NULL;
	}

	for (uint8_t i = 0; i < service_handler[service].num; i++) {
		if (service_handler[service].handlers[i].opcode == opcode) {
			return &service_handler[service].handlers[i];
		}
	}

	return NULL;
}

static void cmd_handler(void *p1, void *p2, void *p3)
{
	while (1) {
		const struct btp_handler *btp;
		struct btp_buf *cmd;
		uint8_t status;
		uint16_t rsp_len = 0;
		uint16_t len;

		cmd = k_fifo_get(&cmds_queue, K_FOREVER);

		/* Check for NULL pointer - should not happen with K_FOREVER but be safe */
		if (cmd == NULL) {
			LOG_ERR("k_fifo_get returned NULL, retrying...");
			k_msleep(100);
			continue;
		}

		LOG_DBG("cmd service 0x%02x opcode 0x%02x index 0x%02x", cmd->hdr.service,
			cmd->hdr.opcode, cmd->hdr.index);

		len = sys_le16_to_cpu(cmd->hdr.len);

		btp = find_btp_handler(cmd->hdr.service, cmd->hdr.opcode);
		if (btp) {
			if (btp->index != cmd->hdr.index) {
				status = BTP_STATUS_FAILED;
			} else if ((btp->expect_len >= 0) && (btp->expect_len != len)) {
				status = BTP_STATUS_FAILED;
			} else {
				status = btp->func(cmd->hdr.data, len,
						   cmd->rsp, &rsp_len);
			}

			__ASSERT_NO_MSG((rsp_len + sizeof(struct btp_hdr)) <= BTP_MTU);
		} else {
			status = BTP_STATUS_UNKNOWN_CMD;
		}
		/* Allow to delay only 1 command. This is for convenience only
		 * of using cmd data without need of copying those in async
		 * functions. Should be not needed eventually.
		 */
		if (status == BTP_STATUS_DELAY_REPLY) {
			__ASSERT_NO_MSG(delayed_cmd == NULL);
			delayed_cmd = cmd;
			continue;
		}

		if ((status == BTP_STATUS_SUCCESS) && rsp_len > 0) {
			tester_send_with_index(cmd->hdr.service, cmd->hdr.opcode,
					       cmd->hdr.index, cmd->rsp, rsp_len);
		} else {
			tester_rsp_with_index(cmd->hdr.service, cmd->hdr.opcode,
					      cmd->hdr.index, status);
		}

		(void)memset(cmd, 0, sizeof(*cmd));
		k_fifo_put(&avail_queue, cmd);
	}
}

static uint8_t *recv_cb(uint8_t *buf, size_t *off)
{
	struct btp_hdr *cmd = (void *) buf;
	struct btp_buf *new_buf;
	uint16_t len;

	if (*off < sizeof(*cmd)) {
		return buf;
	}

	len = sys_le16_to_cpu(cmd->len);
	if (len > BTP_MTU - sizeof(*cmd)) {
		LOG_ERR("BT tester: invalid packet length");
		*off = 0;
		return buf;
	}

	if (*off < sizeof(*cmd) + len) {
		return buf;
	}

	new_buf =  k_fifo_get(&avail_queue, K_NO_WAIT);
	if (!new_buf) {
		LOG_ERR("BT tester: RX overflow");
		*off = 0;
		return buf;
	}

	k_fifo_put(&cmds_queue, CONTAINER_OF(buf, struct btp_buf, data[0]));

	*off = 0;
	return new_buf->data;
}

static uint8_t *recv_buf;
static size_t recv_off;

/* TCP socket for BTP communication */
static int btp_server_fd = -1;  /* Server socket */
static int btp_client_fd = -1;  /* Connected client socket */
static pthread_t btp_rx_thread;
static volatile bool btp_running = false;

/* TCP port for BTP communication */
#ifndef CONFIG_BT_TESTER_TCP_PORT
#define CONFIG_BT_TESTER_TCP_PORT 9876
#endif

static void *btp_rx_thread_func(void *arg)
{
	struct pollfd fds;

	while (btp_running) {
		/* No client connected yet */
		if (btp_client_fd < 0) {
			/* Check for new connection */
			fds.fd = btp_server_fd;
			fds.events = POLLIN;
			if (poll(&fds, 1, 100) > 0 && (fds.revents & POLLIN)) {
				btp_client_fd = accept(btp_server_fd, NULL, NULL);
				if (btp_client_fd >= 0) {
					LOG_INF("BTP client connected (fd=%d)", btp_client_fd);
				}
			}
			continue;
		}

		fds.fd = btp_client_fd;
		fds.events = POLLIN;

		/* Wait for data with timeout */
		int ret = poll(&fds, 1, 100);
		if (ret > 0 && (fds.revents & POLLIN)) {
			uint8_t byte;
			ssize_t n = read(btp_client_fd, &byte, 1);
			if (n == 1) {
				recv_buf[recv_off++] = byte;
				recv_buf = recv_cb(recv_buf, &recv_off);
			} else if (n == 0) {
				/* Client disconnected */
				LOG_INF("BTP client disconnected");
				close(btp_client_fd);
				btp_client_fd = -1;
			} else {
				if (errno != EAGAIN && errno != EWOULDBLOCK) {
					LOG_ERR("BTP read error: %d", errno);
					close(btp_client_fd);
					btp_client_fd = -1;
				}
			}
		}
	}

	return NULL;
}

static void uart_init(uint8_t *data)
{
	struct sockaddr_in addr;
	pthread_attr_t attr;
	int opt = 1;

	recv_buf = data;

	/* Create TCP socket */
	btp_server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (btp_server_fd < 0) {
		LOG_ERR("Failed to create socket: %d", errno);
		return;
	}

	/* Set socket options */
	setsockopt(btp_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	/* Bind to TCP port */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(CONFIG_BT_TESTER_TCP_PORT);

	if (bind(btp_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("Failed to bind socket: %d", errno);
		close(btp_server_fd);
		btp_server_fd = -1;
		return;
	}

	/* Listen for connections */
	if (listen(btp_server_fd, 1) < 0) {
		LOG_ERR("Failed to listen: %d", errno);
		close(btp_server_fd);
		btp_server_fd = -1;
		return;
	}

	LOG_INF("BTP TCP server listening on port %d", CONFIG_BT_TESTER_TCP_PORT);

	/* Start RX thread */
	btp_running = true;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 4096);
	pthread_create(&btp_rx_thread, &attr, btp_rx_thread_func, NULL);
	pthread_attr_destroy(&attr);
}

static void uart_send(const uint8_t *data, size_t len)
{
	if (btp_client_fd < 0) {
		LOG_WRN("No BTP client connected, dropping %zu bytes", len);
		return;
	}

	ssize_t ret = write(btp_client_fd, data, len);
	if (ret < 0) {
		if (errno == EPIPE || errno == ECONNRESET) {
			LOG_INF("BTP client disconnected during write");
			close(btp_client_fd);
			btp_client_fd = -1;
		} else {
			LOG_ERR("BTP write error: %d", errno);
		}
	}
}

void tester_init(void)
{
	int i;
	struct btp_buf *buf;

	LOG_DBG("Initializing tester");

	for (i = 0; i < CMD_QUEUED; i++) {
		k_fifo_put(&avail_queue, &cmd_buf[i]);
	}

	k_thread_create(&cmd_thread, stack, STACKSIZE, cmd_handler,
			NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);

	buf = k_fifo_get(&avail_queue, K_NO_WAIT);

	uart_init(buf->data);

	/* core service is always available */
	tester_init_core();

	tester_send_with_index(BTP_SERVICE_ID_CORE, BTP_CORE_EV_IUT_READY,
			      BTP_INDEX_NONE, NULL, 0);
}

int tester_rsp_buffer_lock(void)
{
	if (k_mutex_lock(&rsp_buf_mutex, Z_FOREVER) != 0) {
		LOG_ERR("Cannot lock rsp_ring_buf");

		return -EACCES;
	}

	return 0;
}

void tester_rsp_buffer_unlock(void)
{
	k_mutex_unlock(&rsp_buf_mutex);
}

void tester_rsp_buffer_free(void)
{
	net_buf_simple_init(rsp_buf, 0);
}

void tester_rsp_buffer_allocate(size_t len, uint8_t **data)
{
	tester_rsp_buffer_free();

	*data = net_buf_simple_add(rsp_buf, len);
}

static void tester_send_with_index(uint8_t service, uint8_t opcode, uint8_t index,
				   const uint8_t *data, size_t len)
{
	struct btp_hdr msg;

	msg.service = service;
	msg.opcode = opcode;
	msg.index = index;
	msg.len = sys_cpu_to_le16(len);

	uart_send((uint8_t *)&msg, sizeof(msg));
	if (data && len) {
		uart_send(data, len);
	}
}

static void tester_rsp_with_index(uint8_t service, uint8_t opcode, uint8_t index,
				  uint8_t status)
{
	struct btp_status s;

	LOG_DBG("service 0x%02x opcode 0x%02x index 0x%02x status 0x%02x", service, opcode, index,
		status);

	if (status == BTP_STATUS_SUCCESS) {
		tester_send_with_index(service, opcode, index, NULL, 0);
		return;
	}

	s.code = status;
	tester_send_with_index(service, BTP_STATUS, index, (uint8_t *) &s, sizeof(s));
}

void tester_event(uint8_t service, uint8_t opcode, const void *data, size_t len)
{
	__ASSERT_NO_MSG(opcode >= 0x80);

	LOG_DBG("service 0x%02x opcode 0x%02x", service, opcode);

	tester_send_with_index(service, opcode, BTP_INDEX, data, len);
}

void tester_rsp_full(uint8_t service, uint8_t opcode, const void *rsp, size_t len)
{
	struct btp_buf *cmd;

	__ASSERT_NO_MSG(opcode < 0x80);
	__ASSERT_NO_MSG(delayed_cmd != NULL);

	LOG_DBG("service 0x%02x opcode 0x%02x", service, opcode);

	tester_send_with_index(service, opcode, BTP_INDEX, rsp, len);

	cmd = delayed_cmd;
	delayed_cmd = NULL;

	(void)memset(cmd, 0, sizeof(*cmd));
	k_fifo_put(&avail_queue, cmd);
}

void tester_rsp(uint8_t service, uint8_t opcode, uint8_t status)
{
	struct btp_buf *cmd;

	__ASSERT_NO_MSG(opcode < 0x80);
	__ASSERT_NO_MSG(delayed_cmd != NULL);

	LOG_DBG("service 0x%02x opcode 0x%02x status 0x%02x", service, opcode, status);

	tester_rsp_with_index(service, opcode, BTP_INDEX, status);

	cmd = delayed_cmd;
	delayed_cmd = NULL;

	(void)memset(cmd, 0, sizeof(*cmd));
	k_fifo_put(&avail_queue, cmd);
}
