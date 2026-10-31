/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Tuya MCU session engine.
 *
 * Knows the protocol and nothing else: bytes come in through
 * iotdrv_tuya_mcu_rx(), go out through a transport send callback, and everything
 * that happens on the link is delivered to listeners. It does not know the
 * UART, the Zigbee/BLE stack or the device behind the MCU.
 */

#ifndef IOTDRV_TUYA_MCU_H_
#define IOTDRV_TUYA_MCU_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/slist.h>

#include <iot_drivers/tuya_proto.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iotdrv_tuya_mcu;

enum iotdrv_tuya_net_status {
	IOTDRV_TUYA_NET_NOT_JOINED = 0,
	IOTDRV_TUYA_NET_JOINED = 1,
	IOTDRV_TUYA_NET_JOINING = 2,
	IOTDRV_TUYA_NET_OFFLINE = 3,
};

/* Transmit @p len bytes; return 0 on success or a negative errno. */
typedef int (*iotdrv_tuya_send_fn)(void *ctx, const uint8_t *buf, size_t len);

/* Wall clock for time sync, Unix seconds. Return 0 on success. */
typedef int (*iotdrv_tuya_time_fn)(void *ctx, uint32_t *utc, uint32_t *local);

/* Every callback is optional and runs on the system work queue. */
struct iotdrv_tuya_listener {
	sys_snode_t node;
	/* The MCU answered the product info query; @p product is the raw JSON. */
	void (*on_ready)(struct iotdrv_tuya_mcu *mcu, const char *product, void *user_data);
	/* A datapoint arrived from the MCU. */
	void (*on_dp)(struct iotdrv_tuya_mcu *mcu, const struct iotdrv_tuya_dp *dp, void *user_data);
	/* Nothing was heard from the MCU for CONFIG_IOTDRV_TUYA_MCU_LINK_TIMEOUT_S. */
	void (*on_link_lost)(struct iotdrv_tuya_mcu *mcu, void *user_data);
	void *user_data;
};

struct iotdrv_tuya_tx_item {
	uint16_t seq;
	uint16_t len;
	uint8_t cmd;
	bool wait_ack;
	uint8_t data[IOTDRV_TUYA_MAX_DATA];
};

struct iotdrv_tuya_mcu {
	struct k_work_delayable work;
	struct iotdrv_tuya_parser parser;
	struct ring_buf rx_ring;
	uint8_t rx_storage[CONFIG_IOTDRV_TUYA_MCU_RX_BUF_SIZE];
	struct k_msgq tx_q;
	char tx_storage[CONFIG_IOTDRV_TUYA_MCU_TX_QUEUE_LEN * sizeof(struct iotdrv_tuya_tx_item)];
	struct iotdrv_tuya_tx_item inflight;
	sys_slist_t listeners;

	iotdrv_tuya_send_fn send;
	void *send_ctx;
	iotdrv_tuya_time_fn time_fn;
	void *time_ctx;

	int64_t sent_at;
	int64_t heard_at;
	int64_t last_tx_at;
	uint16_t seq;
	uint8_t version;
	uint8_t retries;
	uint8_t net_status;
	bool has_inflight;
	bool started;
	bool ready;
	bool link_lost;
};

void iotdrv_tuya_mcu_init(struct iotdrv_tuya_mcu *mcu, uint8_t protocol_version);

void iotdrv_tuya_mcu_set_transport(struct iotdrv_tuya_mcu *mcu, iotdrv_tuya_send_fn send, void *ctx);

void iotdrv_tuya_mcu_set_time_source(struct iotdrv_tuya_mcu *mcu, iotdrv_tuya_time_fn fn, void *ctx);

void iotdrv_tuya_mcu_add_listener(struct iotdrv_tuya_mcu *mcu, struct iotdrv_tuya_listener *l);

void iotdrv_tuya_mcu_remove_listener(struct iotdrv_tuya_mcu *mcu, struct iotdrv_tuya_listener *l);

/* Start the handshake by querying the product info. */
int iotdrv_tuya_mcu_start(struct iotdrv_tuya_mcu *mcu);

/* Feed received bytes; safe from an ISR. Returns the number of bytes accepted. */
size_t iotdrv_tuya_mcu_rx(struct iotdrv_tuya_mcu *mcu, const uint8_t *buf, size_t len);

/* Send one datapoint (already encoded with iotdrv_tuya_dp_encode*) to the MCU. */
int iotdrv_tuya_mcu_send_dp(struct iotdrv_tuya_mcu *mcu, const uint8_t *dp, size_t len);

/* Report the module's network state; also used to answer MCU queries. */
int iotdrv_tuya_mcu_set_net_status(struct iotdrv_tuya_mcu *mcu, enum iotdrv_tuya_net_status status);

/* Ask the MCU to report all its datapoints. */
int iotdrv_tuya_mcu_query_dps(struct iotdrv_tuya_mcu *mcu);

/* Push the current time to the MCU. Needs a time source. */
int iotdrv_tuya_mcu_sync_time(struct iotdrv_tuya_mcu *mcu);

bool iotdrv_tuya_mcu_is_ready(const struct iotdrv_tuya_mcu *mcu);

#ifdef __cplusplus
}
#endif

#endif /* IOTDRV_TUYA_MCU_H_ */
