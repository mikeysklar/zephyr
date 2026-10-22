/*
 * Copyright (c) 2025 Beechwoods Software, Inc.
 * Copyright (c) 2026 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bluetooth HCI driver for the Infineon CYW43439 over the shared gSPI bus.
 *
 * Zephyr's other CYW43 Bluetooth path (hci_uart_infineon.c) assumes the
 * controller's HCI is exposed on a UART. On the Raspberry Pi Pico W and
 * Pico 2 W it is not: HCI/ACL rides the same gSPI bus as WiFi. This driver
 * presents that shared bus as a bt_hci device, with the transport itself
 * provided by pico-sdk's cybt_shared_bus re-homed onto WHD's bus accessors
 * (cyw43_bt_whd_glue.c).
 */

#define DT_DRV_COMPAT infineon_cyw43_bt_hci

#include <zephyr/device.h>
#include <zephyr/drivers/bluetooth.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "cybt_shared_bus_driver.h"
#include "cyw43_bt_whd_glue.h"

#define LOG_LEVEL CONFIG_BT_HCI_DRIVER_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_hci_cyw43);

/* Offsets within the received H:4 packet. */
#define PACKET_TYPE          0
#define PACKET_TYPE_SIZE     1
#define EVT_HEADER_EVENT     1
#define EVT_HEADER_SIZE      2
#define EVT_LE_META_SUBEVENT 3

#define MAX_BT_MSG_SIZE 2048

/*
 * Bound on packets drained from the bt2host ring per process() call. Caps how
 * long the bus lock is held; any remainder is taken on the next poll.
 */
#define CYW43_BT_DRAIN_MAX 32

/*
 * cyw43_bluetooth_hci_read/write carry a 4-byte header ahead of the HCI packet,
 * whose last byte is the H:4 packet-type indicator.
 */
#define CYW43_PACKET_HEADER_SIZE 4

static uint8_t __noinit cyw43_rxbuf[MAX_BT_MSG_SIZE + CYW43_PACKET_HEADER_SIZE];
static uint8_t __noinit cyw43_txbuf[CONFIG_NET_BUF_DATA_SIZE + CYW43_PACKET_HEADER_SIZE];

struct hci_cyw43_data {
	/* Must be first: the host stores its recv callback here via bt_hci_open(). */
	struct bt_hci_driver_data common;
};

static int hci_cyw43_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	/*
	 * Deliberately does no bus traffic. The controller lives behind the
	 * AIROC/WHD WiFi driver, which may not have brought the chip up yet at
	 * POST_KERNEL; touching the backplane here would fail and leave the
	 * device permanently un-ready. Bring-up happens in open(), i.e. from
	 * bt_enable(), by which point WHD is up.
	 */
	return 0;
}

static int hci_cyw43_open(const struct device *dev)
{
	int err;

	ARG_UNUSED(dev);

	/* Downloads the BT patchram (once) and starts the RX poll thread. */
	err = cyw43_bluetooth_hci_init();
	if (err) {
		LOG_ERR("CYW43 Bluetooth transport bring-up failed (err %d)", err);
		return err;
	}

	return 0;
}

static int hci_cyw43_close(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static void hci_cyw43_process_one(void)
{
	struct net_buf *buf = NULL;
	bool discardable = false;
	k_timeout_t timeout = K_FOREVER;
	struct bt_hci_acl_hdr acl_hdr = { .len = 0 };
	uint32_t cyw43_len = 0;
	uint32_t pkt_len;   /* type byte + payload actually received */
	uint32_t avail;     /* payload bytes after the type byte */
	uint32_t len;       /* HCI packet length parsed from its header */
	const struct device *dev = DEVICE_DT_GET(DT_DRV_INST(0));
	uint8_t packet_type;
	uint8_t *rxmsg;
	int ret;

	/*
	 * On a failed read *len is not meaningful, so do not parse whatever the
	 * buffer happens to still hold.
	 */
	ret = cyw43_bluetooth_hci_read(&cyw43_rxbuf[0], MAX_BT_MSG_SIZE, &cyw43_len);
	if (ret) {
		LOG_ERR("cyw43_bluetooth_hci_read failed (ret %d)", ret);
		return;
	}

	/*
	 * cyw43_len counts the whole 4-byte header. Require at least the type
	 * byte, and that it fits the RX buffer — the latter guards the pkt_len
	 * subtraction below against underflow.
	 */
	if (cyw43_len < CYW43_PACKET_HEADER_SIZE || cyw43_len > sizeof(cyw43_rxbuf)) {
		LOG_WRN("bogus cyw43 RX length %u", cyw43_len);
		return;
	}

	rxmsg = &cyw43_rxbuf[CYW43_PACKET_HEADER_SIZE - 1];
	packet_type = rxmsg[PACKET_TYPE];
	pkt_len = cyw43_len - (CYW43_PACKET_HEADER_SIZE - 1);
	avail = pkt_len - PACKET_TYPE_SIZE;

	LOG_HEXDUMP_DBG(rxmsg, pkt_len, "HCI RX data:");

	switch (packet_type) {
	case BT_HCI_H4_EVT:
		if (avail < sizeof(struct bt_hci_evt_hdr)) {
			LOG_WRN("EVT too short: %u", avail);
			return;
		}
		if (rxmsg[EVT_HEADER_EVENT] == BT_HCI_EVT_LE_META_EVENT &&
		    rxmsg[EVT_LE_META_SUBEVENT] == BT_HCI_EVT_LE_ADVERTISING_REPORT) {
			discardable = true;
			timeout = K_NO_WAIT;
		}
		buf = bt_buf_get_evt(rxmsg[EVT_HEADER_EVENT], discardable, timeout);
		len = sizeof(struct bt_hci_evt_hdr) + rxmsg[EVT_HEADER_SIZE];
		break;
	case BT_HCI_H4_ACL:
		if (avail < sizeof(struct bt_hci_acl_hdr)) {
			LOG_WRN("ACL too short: %u", avail);
			return;
		}
		buf = bt_buf_get_rx(BT_BUF_ACL_IN, timeout);
		memcpy(&acl_hdr, &rxmsg[1], sizeof(acl_hdr));
		len = sizeof(struct bt_hci_acl_hdr) + sys_le16_to_cpu(acl_hdr.len);
		break;
#if defined(CONFIG_BT_ISO)
	case BT_HCI_H4_ISO: {
		struct bt_hci_iso_hdr iso_hdr;

		if (avail < sizeof(struct bt_hci_iso_hdr)) {
			LOG_WRN("ISO too short: %u", avail);
			return;
		}
		buf = bt_buf_get_rx(BT_BUF_ISO_IN, timeout);
		memcpy(&iso_hdr, &rxmsg[1], sizeof(iso_hdr));
		len = sizeof(struct bt_hci_iso_hdr) + bt_iso_hdr_len(sys_le16_to_cpu(iso_hdr.len));
		break;
	}
#endif /* CONFIG_BT_ISO */
	case BT_HCI_H4_SCO:
		/*
		 * Classic SCO has a different header and buffer type than ISO,
		 * and the CYW43 BLE path never delivers it. Drop it rather than
		 * mis-parsing it as ISO.
		 */
		LOG_WRN("dropping unsupported SCO packet (cyw43_len %u)", cyw43_len);
		return;
	default:
		LOG_WRN("unknown H4 RX packet type 0x%02x", packet_type);
		return;
	}

	/*
	 * bt_buf_get_evt()/bt_buf_get_rx() return NULL on pool exhaustion, and
	 * bt_buf_get_evt() returns NULL by design for a discardable advertising
	 * report under K_NO_WAIT. Drop rather than dereference.
	 */
	if (buf == NULL) {
		if (discardable) {
			LOG_DBG("dropped discardable advertising report (no buf)");
		} else {
			LOG_WRN("no RX buf for type 0x%02x; dropping", packet_type);
		}
		return;
	}

	/*
	 * The HCI length field must agree with what we actually received and fit
	 * the destination, or we would over-read cyw43_rxbuf / overflow the
	 * net_buf.
	 */
	if (len > avail) {
		LOG_ERR("HCI len %u exceeds received payload %u (type 0x%02x)",
			len, avail, packet_type);
		net_buf_unref(buf);
		return;
	}
	if (len > net_buf_tailroom(buf)) {
		LOG_ERR("HCI len %u exceeds buf tailroom %zu (type 0x%02x)",
			len, net_buf_tailroom(buf), packet_type);
		net_buf_unref(buf);
		return;
	}

	net_buf_add_mem(buf, &rxmsg[1], len);
	bt_hci_recv(dev, buf);
}

/*
 * Is there unread BT data in the controller's bt2host ring?
 *
 * Checks the ring indices rather than the I_HMB_FC_CHANGE interrupt flag: the
 * WiFi SDPCM path reads the same SDIO_INT_STATUS register and clears the whole
 * I_HMB_SW_MASK, which includes that flag. Under concurrent WiFi+BT the WiFi
 * side can therefore clear BT's flow-control flag before BT sees it, leaving an
 * HCI command-complete sitting in the ring and the host reporting "Controller
 * unresponsive". The indices are the source of truth.
 */
bool cyw43_bluetooth_has_pending(void)
{
	cybt_fw_membuf_index_t idx;

	if (!cyw43_bt_state.bt_loaded) {
		return false;
	}
	if (cybt_get_bt_buf_index(&idx) != CYBT_SUCCESS) {
		return false;
	}
	return idx.bt2host_in_val != idx.bt2host_out_val;
}

/*
 * Drain the bt2host ring rather than reading a single packet.
 *
 * If the controller batched several HCI packets (say a connection event plus
 * the command-complete the host is blocked on in bt_hci_cmd_send_sync), reading
 * only one strands the rest until the next assertion, which surfaces as a
 * command timeout. Bounded so a controller streaming without pause cannot hold
 * the bus lock indefinitely.
 */
void cyw43_bluetooth_hci_process(void)
{
	int i = 0;

	do {
		hci_cyw43_process_one();
	} while (cyw43_bluetooth_has_pending() && ++i < CYW43_BT_DRAIN_MAX);
}

static int hci_cyw43_send(const struct device *dev, struct net_buf *buf)
{
	int rv = 0;
	uint8_t packet_type;
	uint32_t cyw43_len;

	ARG_UNUSED(dev);

	/*
	 * The host hands us a buffer whose first byte is the H:4 packet-type
	 * indicator. The shared-bus write wants that same indicator in the 4th
	 * byte of its header followed by the rest of the buffer, so buf->data
	 * can be copied verbatim starting at the indicator slot.
	 */
	if (buf->len < 1) {
		LOG_ERR("zero-length HCI TX buffer");
		rv = -EINVAL;
		goto out;
	}

	packet_type = buf->data[0];

	switch (packet_type) {
	case BT_HCI_H4_CMD:
	case BT_HCI_H4_ACL:
	case BT_HCI_H4_ISO:
		break;
	default:
		LOG_ERR("unknown H4 TX packet type 0x%02x", packet_type);
		rv = -EINVAL;
		goto out;
	}

	if (buf->len + (CYW43_PACKET_HEADER_SIZE - 1) > sizeof(cyw43_txbuf)) {
		LOG_ERR("HCI TX buffer too long: %u", buf->len);
		rv = -EMSGSIZE;
		goto out;
	}

	memcpy(&cyw43_txbuf[CYW43_PACKET_HEADER_SIZE - 1], buf->data, buf->len);
	/* buf already contains H:4, which occupies the last header byte. */
	cyw43_len = buf->len + (CYW43_PACKET_HEADER_SIZE - 1);

	/*
	 * TX runs on the host's bt_tx_processor thread, so it must take the
	 * shared bus lock like every other bus user — cybt does not wrap its
	 * write path itself. The bus code yields mid-transfer, so even
	 * cooperative threads would otherwise interleave and the controller's
	 * ring indices read back corrupt. The mutex is recursive with priority
	 * inheritance, so the higher-priority poll thread is not inverted.
	 */
	whd_bus_lock();
	rv = cyw43_bluetooth_hci_write(cyw43_txbuf, cyw43_len);
	whd_bus_unlock();

	LOG_HEXDUMP_DBG(buf->data, buf->len, "HCI TX data:");

out:
	net_buf_unref(buf);
	return rv;
}

#if defined(CONFIG_BT_HCI_SETUP)
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/addr.h>

/*
 * The controller derives its BD_ADDR from the WiFi MAC + 1. The MAC is a 48-bit
 * big-endian value (mac[0] most significant), so "+1" increments from the last
 * octet with carry; bt_addr_t stores the address little-endian.
 */
static void hci_cyw43_expected_bt_addr(bt_addr_t *out)
{
	uint8_t mac[6];
	int i;

	memcpy(mac, cyw43_bt_state.mac, sizeof(mac));

	for (i = 5; i >= 0; i--) {
		if (++mac[i] != 0U) {
			break;
		}
	}

	for (i = 0; i < 6; i++) {
		out->val[i] = mac[5 - i];
	}
}

/*
 * Controller setup hook, run during bt_enable() after HCI Reset.
 *
 * The patchram is already downloaded by the transport and the controller
 * exposes a valid OTP-derived public address, so there is no vendor command we
 * must issue. This is a read-only sanity check that the address is the expected
 * WiFi-MAC+1, so a zero or mismatched identity is surfaced rather than silently
 * shipped.
 */
static int hci_cyw43_setup(const struct device *dev, const struct bt_hci_setup_params *param)
{
	struct bt_hci_rp_read_bd_addr *rp;
	struct net_buf *rsp = NULL;
	bt_addr_t expected;
	char got_s[BT_ADDR_STR_LEN];
	char exp_s[BT_ADDR_STR_LEN];
	int err;

	ARG_UNUSED(dev);
	ARG_UNUSED(param);

	err = bt_hci_cmd_send_sync(BT_HCI_OP_READ_BD_ADDR, NULL, &rsp);
	if (err) {
		LOG_ERR("HCI Read_BD_ADDR failed (err %d)", err);
		return err;
	}

	rp = (void *)rsp->data;
	if (rp->status) {
		LOG_ERR("HCI Read_BD_ADDR status 0x%02x", rp->status);
		net_buf_unref(rsp);
		return -EIO;
	}

	hci_cyw43_expected_bt_addr(&expected);
	bt_addr_to_str(&rp->bdaddr, got_s, sizeof(got_s));
	bt_addr_to_str(&expected, exp_s, sizeof(exp_s));

	if (bt_addr_eq(&rp->bdaddr, BT_ADDR_ANY) || bt_addr_eq(&rp->bdaddr, BT_ADDR_NONE)) {
		LOG_ERR("controller reported invalid public BD_ADDR %s", got_s);
		net_buf_unref(rsp);
		return -EIO;
	}

	if (!bt_addr_eq(&rp->bdaddr, &expected)) {
		LOG_WRN("controller public BD_ADDR %s != expected WiFi-MAC+1 %s", got_s, exp_s);
	} else {
		LOG_INF("controller public BD_ADDR %s verified (= WiFi MAC + 1)", got_s);
	}

	net_buf_unref(rsp);
	return 0;
}
#endif /* CONFIG_BT_HCI_SETUP */

static DEVICE_API(bt_hci, hci_cyw43_api) = {
	.open = hci_cyw43_open,
	.close = hci_cyw43_close,
	.send = hci_cyw43_send,
#if defined(CONFIG_BT_HCI_SETUP)
	.setup = hci_cyw43_setup,
#endif
};

#define HCI_DEVICE_INIT(inst)                                                                      \
	static struct hci_cyw43_data hci_cyw43_data_##inst;                                        \
	DEVICE_DT_INST_DEFINE(inst, hci_cyw43_init, NULL, &hci_cyw43_data_##inst, NULL,             \
			      POST_KERNEL, CONFIG_BT_HCI_INIT_PRIORITY, &hci_cyw43_api)

DT_INST_FOREACH_STATUS_OKAY(HCI_DEVICE_INIT)
