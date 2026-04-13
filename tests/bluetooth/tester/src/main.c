/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/toolchain.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>
#include <uv.h>

#include <zephyr/logging/log.h>
#define LOG_MODULE_NAME bttester_main
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_BTTESTER_LOG_LEVEL);

#include "btp/btp.h"

/*
 * Following bttool's pattern exactly:
 * 1. main() creates uv_loop
 * 2. bt_ipc_thread: creates BT instance, registers callbacks, runs uv_run()
 * 3. All BT API calls from cmd_handler are dispatched to bt_ipc_thread
 *    via uv_async_queue (same as bttool dispatches commands)
 * 4. cmd_handler blocks on semaphore until bt_ipc_thread completes the call
 */

/* Exported from z_api_manager.c */
extern int z_bt_manager_init(void);
extern void *z_bt_svc_ins_get(void);

/* Exported from z_api_gap.c */
extern void *local_bt_ins;

/* Global uv_loop for BT IPC thread */
uv_loop_t *g_bt_loop = NULL;
static volatile bool g_bt_ready = false;

/*
 * z_api dispatch mechanism:
 * cmd_handler thread calls z_api_dispatch() which:
 * 1. Packages the function pointer + args into a request struct
 * 2. Sends it to bt_ipc_thread via uv_async
 * 3. Blocks on semaphore until bt_ipc_thread executes it
 * 4. Returns the result
 */
typedef int (*z_api_func_t)(void *arg);

typedef struct {
	z_api_func_t func;
	void *arg;
	int result;
	sem_t done;
} z_api_request_t;

static uv_async_t g_dispatch_async;
static z_api_request_t *g_pending_req = NULL;
static pthread_mutex_t g_dispatch_mutex = PTHREAD_MUTEX_INITIALIZER;

static void dispatch_cb(uv_async_t *handle)
{
	z_api_request_t *req = g_pending_req;
	if (!req) return;

	LOG_INF("dispatch_cb: executing func=%p in bt_ipc_thread", req->func);
	req->result = req->func(req->arg);
	LOG_INF("dispatch_cb: func returned %d", req->result);

	/* Wake up the caller */
	sem_post(&req->done);
}

/*
 * Dispatch a z_api call to the bt_ipc_thread.
 * Called from cmd_handler thread. Blocks until completion.
 */
int z_api_dispatch(z_api_func_t func, void *arg)
{
	z_api_request_t req;
	int ret;

	if (!g_bt_ready) {
		LOG_ERR("z_api_dispatch: BT not ready");
		return -EAGAIN;
	}

	req.func = func;
	req.arg = arg;
	req.result = 0;
	sem_init(&req.done, 0, 0);

	/* Serialize dispatches (only one at a time) */
	pthread_mutex_lock(&g_dispatch_mutex);
	g_pending_req = &req;

	/* Signal bt_ipc_thread to execute */
	uv_async_send(&g_dispatch_async);

	/* Wait for completion with timeout */
	pthread_mutex_unlock(&g_dispatch_mutex);

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 5; /* 5 second timeout */

	ret = sem_timedwait(&req.done, &ts);
	if (ret != 0) {
		LOG_ERR("z_api_dispatch: TIMEOUT waiting for IPC (func=%p)", func);
		sem_destroy(&req.done);
		return -ETIMEDOUT;
	}

	ret = req.result;
	sem_destroy(&req.done);

	return ret;
}

static void bt_ipc_thread(void *arg)
{
	int ret;

	LOG_INF("bt_ipc_thread: started (tid=%p)", (void *)pthread_self());

	/* Set thread priority same as bttool does */
	pthread_setschedprio(pthread_self(),
		CONFIG_BLUETOOTH_SERVICE_LOOP_THREAD_PRIORITY);

	/* Init dispatch async handle BEFORE creating BT instance */
	uv_async_init(g_bt_loop, &g_dispatch_async, dispatch_cb);

	/* Initialize BT manager (creates instance, registers callback, enables adapter) */
	ret = z_bt_manager_init();
	LOG_INF("bt_ipc_thread: z_bt_manager_init returned %d", ret);

	if (ret == 0) {
		/* Store the instance pointer for z_api_gap.c to use */
		local_bt_ins = z_bt_svc_ins_get();
		LOG_INF("bt_ipc_thread: local_bt_ins=%p", local_bt_ins);
	}

	g_bt_ready = true;
	LOG_INF("bt_ipc_thread: BT ready, entering uv_run");

	/* Run uv loop to process IPC callbacks + dispatched z_api calls */
	uv_run(g_bt_loop, UV_RUN_DEFAULT);
	LOG_INF("bt_ipc_thread: uv_loop exited");
}

int main(void)
{
	pthread_t bt_tid;
	pthread_attr_t attr;

	LOG_INF("bttester: starting");

	/* Create uv loop for BT IPC (same as bttool) */
	g_bt_loop = zalloc(sizeof(uv_loop_t));
	if (!g_bt_loop) {
		LOG_ERR("Failed to allocate uv_loop");
		return -1;
	}
	uv_loop_init(g_bt_loop);

	/* Start BT IPC thread */
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 8192);
	pthread_create(&bt_tid, &attr, (void *(*)(void *))bt_ipc_thread, NULL);
	pthread_attr_destroy(&attr);

	/* Wait for BT to be ready */
	LOG_INF("bttester: waiting for BT init...");
	while (!g_bt_ready) {
		k_sleep(K_MSEC(100));
	}
	LOG_INF("bttester: BT ready, local_bt_ins=%p", local_bt_ins);

	/* Start BTP tester (creates cmd_handler thread + TCP server) */
	tester_init();

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
