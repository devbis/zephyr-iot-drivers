/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <iot_drivers/tuya_mcu.h>

#define TICK_MS       20
#define IDLE_MS       1000

static void schedule(struct iotdrv_tuya_mcu *mcu, uint32_t ms)
{
	(void)k_work_reschedule(&mcu->work, K_MSEC(ms));
}

static int enqueue(struct iotdrv_tuya_mcu *mcu, uint8_t cmd, uint16_t seq, bool new_seq, bool wait_ack,
		   const uint8_t *data, size_t len)
{
	struct iotdrv_tuya_tx_item item = {.cmd = cmd, .wait_ack = wait_ack, .len = (uint16_t)len};
	int err;

	if (len > IOTDRV_TUYA_MAX_DATA) {
		return -ENOMEM;
	}

	if (len > 0U) {
		memcpy(item.data, data, len);
	}

	item.seq = seq;
	if (new_seq) {
		/* seq 0 is reserved for the first frame; stay clear of the top values */
		mcu->seq = (mcu->seq >= 0xFFF0U) ? 0U : (uint16_t)(mcu->seq + 1U);
		item.seq = mcu->seq;
	}

	/* a reply (the MCU's frame number is kept) has its own queue */
	err = k_msgq_put(new_seq ? &mcu->tx_q : &mcu->rsp_q, &item, K_NO_WAIT);
	if (err != 0) {
		return -ENOMEM;
	}

	schedule(mcu, 0);

	return 0;
}

static int reply(struct iotdrv_tuya_mcu *mcu, const struct iotdrv_tuya_frame *rx, uint8_t cmd,
		 const uint8_t *data, size_t len)
{
	return enqueue(mcu, cmd, rx->seq, false, false, data, len);
}

static int transmit(struct iotdrv_tuya_mcu *mcu, const struct iotdrv_tuya_tx_item *item)
{
	uint8_t buf[IOTDRV_TUYA_FRAME_MAX];
	struct iotdrv_tuya_frame frame = {
		.version = mcu->version,
		.seq = item->seq,
		.cmd = item->cmd,
		.len = item->len,
		.data = item->data,
	};
	int n = iotdrv_tuya_frame_encode(buf, sizeof(buf), &frame);

	if ((n < 0) || (mcu->send == NULL)) {
		return -EIO;
	}

	return mcu->send(mcu->send_ctx, buf, (size_t)n);
}

/*
 * A response completes a request when it carries the same command; DP sends and
 * DP queries are answered by reports.
 */
static bool completes(uint8_t sent, uint8_t received)
{
	if ((sent == IOTDRV_TUYA_CMD_SEND_DP) || (sent == IOTDRV_TUYA_CMD_QUERY_DPS)) {
		return (received == IOTDRV_TUYA_CMD_REPORT_DP) ||
		       (received == IOTDRV_TUYA_CMD_REPORT_DP_ACKED);
	}

	return sent == received;
}

static void time_encode(uint8_t out[8], uint32_t utc, uint32_t local)
{
	for (size_t i = 0; i < 4U; i++) {
		out[i] = (uint8_t)(utc >> (24U - 8U * i));
		out[4U + i] = (uint8_t)(local >> (24U - 8U * i));
	}
}

static void notify_ready(struct iotdrv_tuya_mcu *mcu, const char *product)
{
	struct iotdrv_tuya_listener *l;

	SYS_SLIST_FOR_EACH_CONTAINER(&mcu->listeners, l, node) {
		if (l->on_ready != NULL) {
			l->on_ready(mcu, product, l->user_data);
		}
	}
}

static void handle_dps(struct iotdrv_tuya_mcu *mcu, const struct iotdrv_tuya_frame *f)
{
	struct iotdrv_tuya_listener *l;
	struct iotdrv_tuya_dp dp;
	size_t off = 0U;

	while (iotdrv_tuya_dp_next(f->data, f->len, &off, &dp) == 0) {
		SYS_SLIST_FOR_EACH_CONTAINER(&mcu->listeners, l, node) {
			if (l->on_dp != NULL) {
				l->on_dp(mcu, &dp, l->user_data);
			}
		}
	}
}

static void handle_frame(struct iotdrv_tuya_mcu *mcu, const struct iotdrv_tuya_frame *f)
{
	static const uint8_t one = 1U;
	uint8_t tm[8];
	uint32_t utc;
	uint32_t local;

	if (mcu->has_inflight && completes(mcu->inflight.cmd, f->cmd)) {
		mcu->has_inflight = false;
	}

	switch (f->cmd) {
	case IOTDRV_TUYA_CMD_HEARTBEAT:
		(void)reply(mcu, f, IOTDRV_TUYA_CMD_HEARTBEAT, &one, 1U);
		break;
	case IOTDRV_TUYA_CMD_PRODUCT_INFO: {
		char product[IOTDRV_TUYA_MAX_DATA + 1U];

		size_t n = MIN(f->len, IOTDRV_TUYA_MAX_DATA);

		memcpy(product, f->data, n);
		product[n] = '\0';
		if (!mcu->ready) {
			mcu->ready = true;
			notify_ready(mcu, product);
		}
		break;
	}
	case IOTDRV_TUYA_CMD_WORKING_MODE:
		/* A request has no payload; the module handles the network itself */
		if (f->len == 0U) {
			(void)reply(mcu, f, IOTDRV_TUYA_CMD_WORKING_MODE, NULL, 0U);
		}
		break;
	case IOTDRV_TUYA_CMD_NET_STATUS:
		/* The MCU's own command 3: data 1 is a reset request, the answer is empty */
		if ((f->len >= 1U) && (f->data[0] == 1U)) {
			struct iotdrv_tuya_listener *l;

			(void)reply(mcu, f, IOTDRV_TUYA_CMD_NET_STATUS, NULL, 0U);
			SYS_SLIST_FOR_EACH_CONTAINER(&mcu->listeners, l, node) {
				if (l->on_reset_request != NULL) {
					l->on_reset_request(mcu, l->user_data);
				}
			}
		}
		break;
	case IOTDRV_TUYA_CMD_NET_STATUS_QUERY:
		(void)reply(mcu, f, IOTDRV_TUYA_CMD_NET_STATUS_QUERY, &mcu->net_status, 1U);
		break;
	case IOTDRV_TUYA_CMD_TIME_SYNC:
		if ((mcu->time_fn != NULL) && (mcu->time_fn(mcu->time_ctx, &utc, &local) == 0)) {
			time_encode(tm, utc, local);
			(void)reply(mcu, f, IOTDRV_TUYA_CMD_TIME_SYNC, tm, sizeof(tm));
		}
		break;
	case IOTDRV_TUYA_CMD_REPORT_DP:
	case IOTDRV_TUYA_CMD_REPORT_DP_ACKED:
		handle_dps(mcu, f);
		(void)reply(mcu, f, IOTDRV_TUYA_CMD_REPORT_DP_ACKED, &one, 1U);
		break;
	default:
		break;
	}
}

static void drain_rx(struct iotdrv_tuya_mcu *mcu)
{
	struct iotdrv_tuya_frame frame;
	uint8_t b;

	while (ring_buf_get(&mcu->rx_ring, &b, 1U) == 1U) {
		if (iotdrv_tuya_parser_feed(&mcu->parser, b, &frame) == 1) {
			mcu->heard_at = k_uptime_get();
			mcu->link_lost = false;
			handle_frame(mcu, &frame);
		}
	}
}

static void pump_tx(struct iotdrv_tuya_mcu *mcu, int64_t now)
{
	struct iotdrv_tuya_tx_item item;

	if (((now - mcu->last_tx_at) >= CONFIG_IOTDRV_TUYA_MCU_TX_GAP_MS) &&
	    (k_msgq_get(&mcu->rsp_q, &item, K_NO_WAIT) == 0)) {
		(void)transmit(mcu, &item);
		mcu->last_tx_at = now;
		return;
	}

	if (mcu->has_inflight) {
		if ((now - mcu->sent_at) < CONFIG_IOTDRV_TUYA_MCU_ACK_TIMEOUT_MS) {
			return;
		}

		if (mcu->retries++ >= CONFIG_IOTDRV_TUYA_MCU_MAX_RETRIES) {
			mcu->has_inflight = false;
		} else if ((now - mcu->last_tx_at) >= CONFIG_IOTDRV_TUYA_MCU_TX_GAP_MS) {
			(void)transmit(mcu, &mcu->inflight);
			mcu->sent_at = now;
			mcu->last_tx_at = now;
			return;
		}
	}

	if (mcu->has_inflight || ((now - mcu->last_tx_at) < CONFIG_IOTDRV_TUYA_MCU_TX_GAP_MS) ||
	    (k_msgq_get(&mcu->tx_q, &item, K_NO_WAIT) != 0)) {
		return;
	}

	(void)transmit(mcu, &item);
	mcu->last_tx_at = now;
	if (item.wait_ack) {
		mcu->inflight = item;
		mcu->has_inflight = true;
		mcu->retries = 0U;
		mcu->sent_at = now;
	}
}

static void check_link(struct iotdrv_tuya_mcu *mcu, int64_t now)
{
	struct iotdrv_tuya_listener *l;

	if (!mcu->started || mcu->link_lost ||
	    ((now - mcu->heard_at) < (int64_t)CONFIG_IOTDRV_TUYA_MCU_LINK_TIMEOUT_S * MSEC_PER_SEC)) {
		return;
	}

	mcu->link_lost = true;
	mcu->ready = false;
	SYS_SLIST_FOR_EACH_CONTAINER(&mcu->listeners, l, node) {
		if (l->on_link_lost != NULL) {
			l->on_link_lost(mcu, l->user_data);
		}
	}
	(void)enqueue(mcu, IOTDRV_TUYA_CMD_PRODUCT_INFO, 0U, true, true, NULL, 0U);
}

static void work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct iotdrv_tuya_mcu *mcu = CONTAINER_OF(dwork, struct iotdrv_tuya_mcu, work);
	int64_t now;

	drain_rx(mcu);
	now = k_uptime_get();
	pump_tx(mcu, now);
	check_link(mcu, now);

	schedule(mcu, (mcu->has_inflight || (k_msgq_num_used_get(&mcu->tx_q) > 0U) ||
		       (k_msgq_num_used_get(&mcu->rsp_q) > 0U) ||
		       (ring_buf_size_get(&mcu->rx_ring) > 0U))
			      ? TICK_MS
			      : IDLE_MS);
}

void iotdrv_tuya_mcu_init(struct iotdrv_tuya_mcu *mcu, uint8_t protocol_version)
{
	memset(mcu, 0, sizeof(*mcu));
	mcu->version = protocol_version;
	mcu->net_status = IOTDRV_TUYA_NET_NOT_JOINED;
	iotdrv_tuya_parser_init(&mcu->parser);
	ring_buf_init(&mcu->rx_ring, sizeof(mcu->rx_storage), mcu->rx_storage);
	k_msgq_init(&mcu->tx_q, mcu->tx_storage, sizeof(struct iotdrv_tuya_tx_item),
		    CONFIG_IOTDRV_TUYA_MCU_TX_QUEUE_LEN);
	k_msgq_init(&mcu->rsp_q, mcu->rsp_storage, sizeof(struct iotdrv_tuya_tx_item),
		    IOTDRV_TUYA_RSP_QUEUE_LEN);
	sys_slist_init(&mcu->listeners);
	k_work_init_delayable(&mcu->work, work_handler);
}

void iotdrv_tuya_mcu_set_transport(struct iotdrv_tuya_mcu *mcu, iotdrv_tuya_send_fn send, void *ctx)
{
	mcu->send = send;
	mcu->send_ctx = ctx;
}

void iotdrv_tuya_mcu_set_time_source(struct iotdrv_tuya_mcu *mcu, iotdrv_tuya_time_fn fn, void *ctx)
{
	mcu->time_fn = fn;
	mcu->time_ctx = ctx;
}

void iotdrv_tuya_mcu_add_listener(struct iotdrv_tuya_mcu *mcu, struct iotdrv_tuya_listener *l)
{
	sys_slist_append(&mcu->listeners, &l->node);
}

void iotdrv_tuya_mcu_remove_listener(struct iotdrv_tuya_mcu *mcu, struct iotdrv_tuya_listener *l)
{
	(void)sys_slist_find_and_remove(&mcu->listeners, &l->node);
}

int iotdrv_tuya_mcu_start(struct iotdrv_tuya_mcu *mcu)
{
	mcu->started = true;
	mcu->heard_at = k_uptime_get();

	return enqueue(mcu, IOTDRV_TUYA_CMD_PRODUCT_INFO, 0U, true, true, NULL, 0U);
}

size_t iotdrv_tuya_mcu_rx(struct iotdrv_tuya_mcu *mcu, const uint8_t *buf, size_t len)
{
	size_t n = ring_buf_put(&mcu->rx_ring, buf, len);

	if (n > 0U) {
		schedule(mcu, 0);
	}

	return n;
}

int iotdrv_tuya_mcu_send_dp(struct iotdrv_tuya_mcu *mcu, const uint8_t *dp, size_t len)
{
	return enqueue(mcu, IOTDRV_TUYA_CMD_SEND_DP, 0U, true, true, dp, len);
}

int iotdrv_tuya_mcu_set_net_status(struct iotdrv_tuya_mcu *mcu, enum iotdrv_tuya_net_status status)
{
	uint8_t v = (uint8_t)status;

	mcu->net_status = v;

	return enqueue(mcu, IOTDRV_TUYA_CMD_NET_STATUS, 0U, true, false, &v, 1U);
}

int iotdrv_tuya_mcu_query_dps(struct iotdrv_tuya_mcu *mcu)
{
	return enqueue(mcu, IOTDRV_TUYA_CMD_QUERY_DPS, 0U, true, true, NULL, 0U);
}

int iotdrv_tuya_mcu_sync_time(struct iotdrv_tuya_mcu *mcu)
{
	uint32_t utc;
	uint32_t local;
	uint8_t tm[8];

	if ((mcu->time_fn == NULL) || (mcu->time_fn(mcu->time_ctx, &utc, &local) != 0)) {
		return -ENOTSUP;
	}

	time_encode(tm, utc, local);

	return enqueue(mcu, IOTDRV_TUYA_CMD_TIME_SYNC, 0U, true, false, tm, sizeof(tm));
}

bool iotdrv_tuya_mcu_is_ready(const struct iotdrv_tuya_mcu *mcu)
{
	return mcu->ready;
}
