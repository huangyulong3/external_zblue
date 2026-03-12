/******************************************************************************
 *
 * Copyright (C) 2024 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *****************************************************************************/

#include <zephyr/kernel.h>
#include <nuttx/wdog.h>

/**
 * @brief Timer expiration handler
 *
 * This function is called when a timer expires. It invokes the user-defined
 * expiry function and optionally restarts the timer if a period is set.
 *
 * @param arg  The timer pointer passed as argument.
 */
static void z_timer_wdog_handler(wdparm_t arg)
{
	struct k_timer *timer = (struct k_timer *)arg;

	if (timer->expiry_fn) {
		timer->expiry_fn(timer);
	}

	/* Restart timer if period is set */
	if (!K_TIMEOUT_EQ(timer->period, K_NO_WAIT) &&
	    !K_TIMEOUT_EQ(timer->period, K_FOREVER)) {
		wd_start(&timer->wdog, timer->period.ticks,
			 z_timer_wdog_handler, (wdparm_t)timer);
	}
}

void k_timer_init(struct k_timer *timer,
		  k_timer_expiry_t expiry_fn,
		  k_timer_stop_t stop_fn)
{
	timer->expiry_fn = expiry_fn;
	timer->stop_fn = stop_fn;
	timer->status = 0;
	timer->user_data = NULL;
}

void k_timer_start(struct k_timer *timer, k_timeout_t duration,
		   k_timeout_t period)
{
	if (K_TIMEOUT_EQ(duration, K_FOREVER)) {
		return;
	}

	/* Stop any existing timer */
	wd_cancel(&timer->wdog);

	timer->period = period;
	timer->status = 0;

	wd_start(&timer->wdog, duration.ticks, z_timer_wdog_handler, (wdparm_t)timer);
}

void k_timer_stop(struct k_timer *timer)
{
	wd_cancel(&timer->wdog);

	if (timer->stop_fn) {
		timer->stop_fn(timer);
	}
}

uint32_t k_timer_status_get(struct k_timer *timer)
{
	return timer->status;
}

uint32_t k_timer_status_sync(struct k_timer *timer)
{
	/* Simple implementation - just return current status */
	return timer->status;
}

k_ticks_t k_timer_remaining_ticks(const struct k_timer *timer)
{
	/* Cast away const to match NuttX wd_gettime signature */
	return wd_gettime((struct wdog_s *)&timer->wdog);
}

k_ticks_t k_timer_expires_ticks(const struct k_timer *timer)
{
	/* Cast away const to match NuttX wd_gettime signature */
	return k_uptime_ticks() + wd_gettime((struct wdog_s *)&timer->wdog);
}

void *k_timer_user_data_get(const struct k_timer *timer)
{
	return timer->user_data;
}

void k_timer_user_data_set(struct k_timer *timer, void *user_data)
{
	timer->user_data = user_data;
}
