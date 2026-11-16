/*
 * Copyright (c) 2026 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * USB device controller driver for the WCH USBHS controller on the CH32V4x7
 * (USBHS1 and USBHS2). Register layout and bits are from the CH32V407
 * reference manual, section 22.2.1.
 *
 * EP0 has a single 64 byte DMA buffer shared by SETUP, OUT and IN. Endpoints 1
 * to 7 have separate RX and TX DMA addresses, which must be 4 byte aligned, and
 * move one packet per interrupt.
 */

#include "udc_common.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/usb/udc.h>

#include <hal_ch32fun.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(udc_wch_usbhs2, CONFIG_UDC_DRIVER_LOG_LEVEL);

#define DT_DRV_COMPAT wch_usbhs2

struct usbhs2_ep_ctrl {
	volatile uint16_t tx_len;
	volatile uint8_t tx_ctrl;
	volatile uint8_t rx_ctrl;
};

struct usbhs2_ep_rx {
	volatile uint16_t rx_len;
	volatile uint16_t rx_size;
};

struct usbhs2_regs {
	volatile uint8_t CONTROL;          /* 0x00 */
	volatile uint8_t BASE_MODE;        /* 0x01 */
	volatile uint8_t INT_EN;           /* 0x02 */
	volatile uint8_t DEV_AD;           /* 0x03 */
	volatile uint8_t WAKE_CTRL;        /* 0x04 */
	volatile uint8_t TEST_MODE;        /* 0x05 */
	volatile uint16_t LPM_DATA;        /* 0x06 */
	volatile uint8_t INT_FG;           /* 0x08 */
	volatile uint8_t INT_ST;           /* 0x09 */
	volatile uint8_t MIS_ST;           /* 0x0A */
	uint8_t reserved0;                 /* 0x0B */
	volatile uint16_t FRAME_NO;        /* 0x0C */
	volatile uint16_t BUS;             /* 0x0E */
	volatile uint16_t UEP_TX_EN;       /* 0x10 */
	volatile uint16_t UEP_RX_EN;       /* 0x12 */
	volatile uint16_t UEP_T_TOG_AUTO;  /* 0x14 */
	volatile uint16_t UEP_R_TOG_AUTO;  /* 0x16 */
	volatile uint8_t UEP_T_BURST;      /* 0x18 */
	volatile uint8_t UEP_T_BURST_MODE; /* 0x19 */
	volatile uint8_t UEP_R_BURST;      /* 0x1A */
	volatile uint8_t UEP_R_RES_MODE;   /* 0x1B */
	volatile uint32_t UEP_AF_MODE;     /* 0x1C */
	volatile uint32_t UEP0_DMA;        /* 0x20 */
	volatile uint32_t UEP_RX_DMA[7];   /* 0x24, EP1..EP7 */
	volatile uint32_t UEP_TX_DMA[7];   /* 0x40, EP1..EP7 */
	volatile uint32_t UEP_MAX_LEN[8];  /* 0x5C */
	struct usbhs2_ep_rx UEP_RX[8];     /* 0x7C */
	struct usbhs2_ep_ctrl UEP_CTRL[8]; /* 0x9C */
	volatile uint16_t UEP_T_ISO;       /* 0xBC */
	volatile uint16_t UEP_R_ISO;       /* 0xBE */
};

BUILD_ASSERT(offsetof(struct usbhs2_regs, UEP0_DMA) == 0x20);
BUILD_ASSERT(offsetof(struct usbhs2_regs, UEP_MAX_LEN) == 0x5C);
BUILD_ASSERT(offsetof(struct usbhs2_regs, UEP_CTRL) == 0x9C);
BUILD_ASSERT(sizeof(struct usbhs2_regs) == 0xC0);

/* R8_USB_CTRL */
#define UD_RST_LINK     BIT(0)
#define UD_RST_SIE      BIT(1)
#define UD_CLR_ALL      BIT(2)
#define UD_PHY_SUSPENDM BIT(3)
#define UD_DMA_EN       BIT(4)
#define UD_DEV_EN       BIT(5)
#define UD_LPM_EN       BIT(7)

/* R8_USB_BASE_MODE */
#define UD_SPEED_FULL 0x00
#define UD_SPEED_HIGH 0x01

/* R8_USB_INT_EN / R8_USB_INT_FG */
#define UDI_BUS_RST   BIT(0)
#define UDI_SUSPEND   BIT(1)
#define UDI_BUS_SLEEP BIT(2)
#define UDI_LPM_ACT   BIT(3)
#define UDI_TRANSFER  BIT(4) /* INT_FG: read only, OR of every endpoint DONE */
#define UDI_SOF       BIT(5)
#define UDI_LINK_RDY  BIT(6)
#define UDI_ALL       GENMASK(7, 0) /* INT_FG is write 1 to clear */

/* R8_USB_MIS_ST */
#define UDMS_READY   BIT(0)
#define UDMS_SUSPEND BIT(1)
#define UDMS_HS_MOD  BIT(7)

#define UD_REMOTE_WKUP BIT(0)

/* UEPn_TX_CTRL, UEPn_RX_CTRL. DONE is RW0: writing 1 leaves it alone. */
#define UEP_RES_MASK    0x03
#define UEP_RES_NAK     0x00
#define UEP_RES_STALL   0x01
#define UEP_RES_ACK     0x02
#define UEP_TOG_MASK    0x0C
#define UEP_TOG_DATA1   0x04
#define UEP_R_SETUP_IS  BIT(3) /* EP0 RX only, read only */
#define UEP_R_TOG_MATCH BIT(4) /* read only */
#define UEP_DONE        BIT(7)

/* RCC bits for the USBHS PLL and UTMI PHYs (RM 3.4.1 RCC_CTLR, 3.4.12 RCC_CFGR2) */
#define RCC_CTLR_USBHSPLLON         BIT(20)
#define RCC_CTLR_USBHSPLLRDY        BIT(21)
#define RCC_CFGR2_USBHSPLLSRC_MASK  (BIT(26) | BIT(27)) /* 0 = HSE */
#define RCC_CFGR2_USBHSPLLREF_MASK  (BIT(28) | BIT(29)) /* 0 = 25 MHz */
#define RCC_CFGR2_USBHSPLLREF_SHIFT 28
/* UTMI1ON is RCC_CFGR2 bit 30 and UTMI2ON is bit 31. */

#define USBHS2_EP0_MPS 64U

/* Time to hold the SIE and link in reset */
#define USBHS2_RESET_US 10

struct udc_wch_usbhs2_config {
	struct usbhs2_regs *regs;
	size_t num_of_eps;
	struct udc_ep_config *ep_cfg_in;
	struct udc_ep_config *ep_cfg_out;
	const struct device *clock_dev;
	uint8_t clock_id;
	uint8_t utmi_bit;
	uint8_t pll_ref;
	bool high_speed;
	void (*irq_enable_func)(const struct device *dev);
	void (*irq_disable_func)(const struct device *dev);
	void (*make_thread)(const struct device *dev);
};

enum usbhs2_event_type {
	USBHS2_EVT_SETUP,
	USBHS2_EVT_XFER_NEW,
	USBHS2_EVT_XFER_FINISHED,
};

struct udc_wch_usbhs2_data {
	struct k_thread thread_data;
	struct k_event events;
	/* bits 31..16 IN endpoints, 15..0 OUT endpoints */
	atomic_t xfer_new;
	atomic_t xfer_finished;
	/* expected data toggle per endpoint, [0] OUT, [1] IN */
	bool data1[2][8];
	/* bytes armed by the last IN packet */
	uint16_t in_len[8];
	bool reset_pending;
	uint8_t setup[8];
	uint8_t __aligned(4) ep0_buf[USBHS2_EP0_MPS];
};

static inline int ep_to_bnum(const uint8_t ep)
{
	return USB_EP_DIR_IS_IN(ep) ? 16 + USB_EP_GET_IDX(ep) : USB_EP_GET_IDX(ep);
}

static inline uint8_t pull_ep_from_bmsk(uint32_t *const bitmap)
{
	unsigned int bit = find_lsb_set(*bitmap) - 1;

	*bitmap &= ~BIT(bit);

	return bit >= 16U ? (USB_EP_DIR_IN | (bit - 16U)) : bit;
}

static inline struct usbhs2_regs *get_regs(const struct device *dev)
{
	const struct udc_wch_usbhs2_config *config = dev->config;

	return config->regs;
}

/* Change the response field without acknowledging a DONE raised meanwhile */
static void set_tx_res(struct usbhs2_regs *regs, uint8_t idx, uint8_t res)
{
	regs->UEP_CTRL[idx].tx_ctrl =
		(regs->UEP_CTRL[idx].tx_ctrl & ~UEP_RES_MASK) | UEP_DONE | res;
}

static void set_rx_res(struct usbhs2_regs *regs, uint8_t idx, uint8_t res)
{
	regs->UEP_CTRL[idx].rx_ctrl =
		(regs->UEP_CTRL[idx].rx_ctrl & ~UEP_RES_MASK) | UEP_DONE | res;
}

static void post_finished(const struct device *dev, const uint8_t ep)
{
	struct udc_wch_usbhs2_data *priv = udc_get_private(dev);

	atomic_set_bit(&priv->xfer_finished, ep_to_bnum(ep));
	k_event_post(&priv->events, BIT(USBHS2_EVT_XFER_FINISHED));
}

static void usbhs2_arm_out(const struct device *dev, struct udc_ep_config *const ep_cfg,
			   struct net_buf *const buf)
{
	struct udc_wch_usbhs2_data *priv = udc_get_private(dev);
	struct usbhs2_regs *regs = get_regs(dev);
	const uint8_t idx = USB_EP_GET_IDX(ep_cfg->addr);
	const uint8_t tog = priv->data1[0][idx] ? UEP_TOG_DATA1 : 0;

	if (idx == 0) {
		regs->UEP_CTRL[0].rx_ctrl = tog | UEP_RES_ACK;
		return;
	}

	regs->UEP_RX_DMA[idx - 1] = (uint32_t)(uintptr_t)net_buf_tail(buf);
	regs->UEP_MAX_LEN[idx] = MIN(net_buf_tailroom(buf), udc_mps_ep_size(ep_cfg));
	regs->UEP_CTRL[idx].rx_ctrl = UEP_DONE | tog | UEP_RES_ACK;
}

static void usbhs2_arm_in(const struct device *dev, struct udc_ep_config *const ep_cfg,
			  struct net_buf *const buf)
{
	struct udc_wch_usbhs2_data *priv = udc_get_private(dev);
	struct usbhs2_regs *regs = get_regs(dev);
	const uint8_t idx = USB_EP_GET_IDX(ep_cfg->addr);
	const uint8_t tog = priv->data1[1][idx] ? UEP_TOG_DATA1 : 0; /* stays DATA0 for iso */
	const uint16_t len = MIN(buf->len, udc_mps_ep_size(ep_cfg));

	if (idx == 0) {
		memcpy(priv->ep0_buf, buf->data, len);
	} else {
		regs->UEP_TX_DMA[idx - 1] = (uint32_t)(uintptr_t)buf->data;
	}

	priv->in_len[idx] = len;
	regs->UEP_CTRL[idx].tx_len = len;
	regs->UEP_CTRL[idx].tx_ctrl = UEP_DONE | tog | UEP_RES_ACK;
}

/* A SETUP latched in ep0_buf that the ISR has not serviced yet */
static bool usbhs2_setup_latched(const struct device *dev)
{
	const uint8_t latched = UEP_DONE | UEP_R_SETUP_IS;

	return (get_regs(dev)->UEP_CTRL[0].rx_ctrl & latched) == latched;
}

static void usbhs2_xfer_next(const struct device *dev, struct udc_ep_config *const ep_cfg)
{
	struct net_buf *buf;
	unsigned int key;

next:
	buf = udc_buf_peek(ep_cfg);
	if (buf == NULL) {
		return;
	}

	if (ep_cfg->addr == USB_CONTROL_EP_OUT && udc_get_buf_info(buf)->setup) {
		/* SETUP packets are received without arming */
		return;
	}

	if (USB_EP_GET_IDX(ep_cfg->addr) != 0 && ((uintptr_t)buf->data & 3U) != 0) {
		LOG_ERR("ep 0x%02x buffer %p not 4 byte aligned", ep_cfg->addr, buf->data);
		buf = udc_buf_get(ep_cfg);
		udc_submit_ep_event(dev, buf, -EINVAL);
		goto next;
	}

	key = irq_lock();
	if (USB_EP_GET_IDX(ep_cfg->addr) == 0 && usbhs2_setup_latched(dev)) {
		/*
		 * A new SETUP is waiting in ep0_buf: arming would overwrite it.
		 * The SETUP event cancels this stage anyway.
		 */
		irq_unlock(key);
		return;
	}

	if (USB_EP_DIR_IS_OUT(ep_cfg->addr)) {
		usbhs2_arm_out(dev, ep_cfg, buf);
	} else {
		usbhs2_arm_in(dev, ep_cfg, buf);
	}
	udc_ep_set_busy(ep_cfg, true);
	irq_unlock(key);
}

static void usbhs2_thread_handler(const struct device *const dev)
{
	struct udc_wch_usbhs2_data *priv = udc_get_private(dev);
	struct udc_ep_config *ep_cfg;
	struct net_buf *buf;
	uint32_t evt;
	uint32_t eps;
	uint8_t ep;

	evt = k_event_wait(&priv->events, UINT32_MAX, false, K_FOREVER);
	udc_lock_internal(dev, K_FOREVER);

	if ((evt & BIT(USBHS2_EVT_XFER_FINISHED)) != 0) {
		k_event_clear(&priv->events, BIT(USBHS2_EVT_XFER_FINISHED));
		eps = atomic_clear(&priv->xfer_finished);

		while (eps) {
			ep = pull_ep_from_bmsk(&eps);
			ep_cfg = udc_get_ep_cfg(dev, ep);

			buf = udc_buf_get(ep_cfg);
			udc_ep_set_busy(ep_cfg, false);
			if (buf == NULL) {
				LOG_ERR("No buffer for ep 0x%02x", ep);
				udc_submit_event(dev, UDC_EVT_ERROR, -ENOBUFS);
			} else {
				udc_submit_ep_event(dev, buf, 0);
			}

			usbhs2_xfer_next(dev, ep_cfg);
		}
	}

	if ((evt & BIT(USBHS2_EVT_XFER_NEW)) != 0) {
		k_event_clear(&priv->events, BIT(USBHS2_EVT_XFER_NEW));
		eps = atomic_clear(&priv->xfer_new);

		while (eps) {
			ep = pull_ep_from_bmsk(&eps);
			ep_cfg = udc_get_ep_cfg(dev, ep);

			if (!udc_ep_is_busy(ep_cfg)) {
				usbhs2_xfer_next(dev, ep_cfg);
			}
		}
	}

	if ((evt & BIT(USBHS2_EVT_SETUP)) != 0) {
		k_event_clear(&priv->events, BIT(USBHS2_EVT_SETUP));
		udc_setup_received(dev, priv->setup);
	}

	udc_unlock_internal(dev);
}

static void usbhs2_handle_in(const struct device *dev, const uint8_t idx, const bool abandon)
{
	struct udc_wch_usbhs2_data *priv = udc_get_private(dev);
	struct usbhs2_regs *regs = get_regs(dev);
	const uint8_t ep = USB_EP_DIR_IN | idx;
	struct udc_ep_config *ep_cfg = udc_get_ep_cfg(dev, ep);
	struct net_buf *buf;

	set_tx_res(regs, idx, UEP_RES_NAK);

	if (!udc_ep_is_busy(ep_cfg)) {
		return;
	}

	buf = udc_buf_peek(ep_cfg);
	if (buf == NULL) {
		return;
	}

	if ((ep_cfg->attributes & USB_EP_TRANSFER_TYPE_MASK) != USB_EP_TYPE_ISO) {
		priv->data1[1][idx] = !priv->data1[1][idx];
	}
	net_buf_pull(buf, priv->in_len[idx]);

	if (buf->len != 0U) {
		if (!abandon) {
			usbhs2_arm_in(dev, ep_cfg, buf);
		}
		return;
	}

	if (udc_ep_buf_has_zlp(buf)) {
		udc_ep_buf_clear_zlp(buf);
		if (!abandon) {
			usbhs2_arm_in(dev, ep_cfg, buf);
		}
		return;
	}

	post_finished(dev, ep);
}

static void usbhs2_handle_out(const struct device *dev, const uint8_t idx, const uint8_t rx_ctrl)
{
	struct udc_wch_usbhs2_data *priv = udc_get_private(dev);
	struct usbhs2_regs *regs = get_regs(dev);
	struct udc_ep_config *ep_cfg = udc_get_ep_cfg(dev, idx);
	const bool iso = (ep_cfg->attributes & USB_EP_TRANSFER_TYPE_MASK) == USB_EP_TYPE_ISO;
	const uint16_t mps = udc_mps_ep_size(ep_cfg);
	struct net_buf *buf;
	uint16_t len;

	buf = udc_buf_peek(ep_cfg);
	if (buf == NULL || !udc_ep_is_busy(ep_cfg)) {
		return;
	}

	if (!iso && !(rx_ctrl & UEP_R_TOG_MATCH)) {
		/* Retransmission after a lost ACK: drop it and re-arm unchanged */
		usbhs2_arm_out(dev, ep_cfg, buf);
		return;
	}

	len = MIN(regs->UEP_RX[idx].rx_len, net_buf_tailroom(buf));
	if (idx == 0) {
		net_buf_add_mem(buf, priv->ep0_buf, len);
	} else {
		net_buf_add(buf, len);
	}

	if (!iso) {
		priv->data1[0][idx] = !priv->data1[0][idx];
	}

	if (len == mps && net_buf_tailroom(buf) != 0U) {
		usbhs2_arm_out(dev, ep_cfg, buf);
	} else {
		post_finished(dev, idx);
	}
}

static void usbhs2_handle_setup(const struct device *dev)
{
	struct udc_wch_usbhs2_data *priv = udc_get_private(dev);
	struct usbhs2_regs *regs = get_regs(dev);

	if (priv->reset_pending) {
		/* LINK_RDY was missed */
		priv->reset_pending = false;
		udc_submit_event(dev, UDC_EVT_RESET, 0);
	}

	/* The data or status stage that follows a SETUP starts with DATA1 */
	priv->data1[0][0] = true;
	priv->data1[1][0] = true;
	regs->UEP_CTRL[0].tx_ctrl = UEP_TOG_DATA1 | UEP_RES_NAK;
	regs->UEP_CTRL[0].rx_ctrl = UEP_TOG_DATA1 | UEP_RES_NAK;

	k_event_post(&priv->events, BIT(USBHS2_EVT_SETUP));
}

static void usbhs2_bus_reset(const struct device *dev)
{
	const struct udc_wch_usbhs2_config *config = dev->config;
	struct udc_wch_usbhs2_data *priv = udc_get_private(dev);
	struct usbhs2_regs *regs = get_regs(dev);

	regs->DEV_AD = 0;
	for (uint8_t i = 0; i < config->num_of_eps; i++) {
		regs->UEP_CTRL[i].tx_len = 0;
		regs->UEP_CTRL[i].tx_ctrl = UEP_RES_NAK;
		regs->UEP_CTRL[i].rx_ctrl = UEP_RES_NAK;
	}
	memset(priv->data1, 0, sizeof(priv->data1));
	regs->UEP_CTRL[0].rx_ctrl = UEP_RES_ACK;
}

static void usbhs2_isr_handler(const struct device *dev)
{
	const struct udc_wch_usbhs2_config *config = dev->config;
	struct udc_wch_usbhs2_data *priv = udc_get_private(dev);
	struct usbhs2_regs *regs = get_regs(dev);
	const uint8_t intflag = regs->INT_FG;

	if ((intflag & UDI_TRANSFER) != 0) {
		/*
		 * INT_FG TRANSFER is the OR of every endpoint's DONE flag and
		 * INT_ST names only one endpoint. Drain all of them, or the SIE
		 * stops answering, SETUP included.
		 */
		for (uint8_t i = 0; i < config->num_of_eps; i++) {
			const uint8_t rx = regs->UEP_CTRL[i].rx_ctrl;
			const uint8_t tx = regs->UEP_CTRL[i].tx_ctrl;
			const bool setup = (i == 0) && (rx & UEP_DONE) && (rx & UEP_R_SETUP_IS);

			if (setup) {
				/* Save it before an IN packet can reuse ep0_buf */
				memcpy(priv->setup, priv->ep0_buf, sizeof(priv->setup));
			}

			if ((tx & UEP_DONE) != 0) {
				regs->UEP_CTRL[i].tx_ctrl &= ~UEP_DONE;
				usbhs2_handle_in(dev, i, setup);
			}

			if ((rx & UEP_DONE) != 0) {
				regs->UEP_CTRL[i].rx_ctrl &= ~UEP_DONE;
				if (setup) {
					usbhs2_handle_setup(dev);
				} else {
					usbhs2_handle_out(dev, i, rx);
				}
			}
		}
	}

	if ((intflag & UDI_BUS_RST) != 0) {
		regs->INT_FG = UDI_BUS_RST;
		usbhs2_bus_reset(dev);
		if (config->high_speed) {
			/* The speed is known when LINK_RDY ends the reset */
			priv->reset_pending = true;
		} else {
			udc_submit_event(dev, UDC_EVT_RESET, 0);
		}
	}

	if ((intflag & UDI_LINK_RDY) != 0) {
		regs->INT_FG = UDI_LINK_RDY;
		if (priv->reset_pending && (regs->MIS_ST & UDMS_READY)) {
			priv->reset_pending = false;
			udc_submit_event(dev, UDC_EVT_RESET, 0);
		}
	}

	if ((intflag & UDI_SUSPEND) != 0) {
		/* Raised for suspend and resume, many times over: report changes only */
		const bool suspended = (regs->MIS_ST & UDMS_SUSPEND) != 0;

		regs->INT_FG = UDI_SUSPEND;
		if (suspended != udc_is_suspended(dev)) {
			udc_set_suspended(dev, suspended);
			udc_submit_event(dev, suspended ? UDC_EVT_SUSPEND : UDC_EVT_RESUME, 0);
		}
	}

	if ((intflag & UDI_SOF) != 0) {
		/* Set every frame whether enabled or not */
		regs->INT_FG = UDI_SOF;
		if (IS_ENABLED(CONFIG_UDC_ENABLE_SOF)) {
			udc_submit_sof_event(dev);
		}
	}

	if ((intflag & (UDI_BUS_SLEEP | UDI_LPM_ACT)) != 0) {
		regs->INT_FG = intflag & (UDI_BUS_SLEEP | UDI_LPM_ACT);
	}
}

static int udc_wch_usbhs2_ep_enqueue(const struct device *dev, struct udc_ep_config *const ep_cfg,
				     struct net_buf *buf)
{
	struct udc_wch_usbhs2_data *priv = udc_get_private(dev);

	udc_buf_put(ep_cfg, buf);

	if (!ep_cfg->stat.halted) {
		atomic_set_bit(&priv->xfer_new, ep_to_bnum(ep_cfg->addr));
		k_event_post(&priv->events, BIT(USBHS2_EVT_XFER_NEW));
	}

	return 0;
}

static int udc_wch_usbhs2_ep_dequeue(const struct device *dev, struct udc_ep_config *const ep_cfg)
{
	struct usbhs2_regs *regs = get_regs(dev);
	const uint8_t idx = USB_EP_GET_IDX(ep_cfg->addr);
	unsigned int key;

	key = irq_lock();

	if (USB_EP_DIR_IS_IN(ep_cfg->addr)) {
		set_tx_res(regs, idx, UEP_RES_NAK);
	} else {
		set_rx_res(regs, idx, UEP_RES_NAK);
	}

	udc_ep_cancel_queued(dev, ep_cfg);
	udc_ep_set_busy(ep_cfg, false);

	irq_unlock(key);

	return 0;
}

static int udc_wch_usbhs2_ep_enable(const struct device *dev, struct udc_ep_config *const ep_cfg)
{
	struct udc_wch_usbhs2_data *priv = udc_get_private(dev);
	struct usbhs2_regs *regs = get_regs(dev);
	const uint8_t idx = USB_EP_GET_IDX(ep_cfg->addr);
	const bool iso = (ep_cfg->attributes & USB_EP_TRANSFER_TYPE_MASK) == USB_EP_TYPE_ISO;

	if (idx == 0) {
		regs->UEP0_DMA = (uint32_t)(uintptr_t)priv->ep0_buf;
		regs->UEP_MAX_LEN[0] = USBHS2_EP0_MPS;
		regs->UEP_TX_EN |= BIT(0);
		regs->UEP_RX_EN |= BIT(0);
		if (USB_EP_DIR_IS_IN(ep_cfg->addr)) {
			regs->UEP_CTRL[0].tx_len = 0;
			regs->UEP_CTRL[0].tx_ctrl = UEP_RES_NAK;
		} else {
			regs->UEP_CTRL[0].rx_ctrl = UEP_RES_ACK;
		}
		return 0;
	}

	if (USB_EP_DIR_IS_IN(ep_cfg->addr)) {
		priv->data1[1][idx] = false;
		regs->UEP_CTRL[idx].tx_len = 0;
		regs->UEP_CTRL[idx].tx_ctrl = UEP_RES_NAK;
		WRITE_BIT(regs->UEP_T_ISO, idx, iso);
		regs->UEP_TX_EN |= BIT(idx);
	} else {
		priv->data1[0][idx] = false;
		regs->UEP_MAX_LEN[idx] = udc_mps_ep_size(ep_cfg);
		regs->UEP_CTRL[idx].rx_ctrl = UEP_RES_NAK;
		WRITE_BIT(regs->UEP_R_ISO, idx, iso);
		regs->UEP_RX_EN |= BIT(idx);
	}

	LOG_DBG("Enable ep 0x%02x", ep_cfg->addr);

	return 0;
}

static int udc_wch_usbhs2_ep_disable(const struct device *dev, struct udc_ep_config *const ep_cfg)
{
	struct usbhs2_regs *regs = get_regs(dev);
	const uint8_t idx = USB_EP_GET_IDX(ep_cfg->addr);

	if (USB_EP_DIR_IS_IN(ep_cfg->addr)) {
		regs->UEP_CTRL[idx].tx_ctrl = UEP_RES_NAK;
		regs->UEP_CTRL[idx].tx_len = 0;
		if (idx != 0) {
			regs->UEP_T_ISO &= ~BIT(idx);
			regs->UEP_TX_EN &= ~BIT(idx);
		}
	} else {
		regs->UEP_CTRL[idx].rx_ctrl = UEP_RES_NAK;
		if (idx != 0) {
			regs->UEP_R_ISO &= ~BIT(idx);
			regs->UEP_RX_EN &= ~BIT(idx);
		}
	}

	LOG_DBG("Disable ep 0x%02x", ep_cfg->addr);

	return 0;
}

static int udc_wch_usbhs2_ep_set_halt(const struct device *dev, struct udc_ep_config *const ep_cfg)
{
	struct usbhs2_regs *regs = get_regs(dev);
	const uint8_t idx = USB_EP_GET_IDX(ep_cfg->addr);

	if (USB_EP_DIR_IS_IN(ep_cfg->addr)) {
		regs->UEP_CTRL[idx].tx_len = 0;
		set_tx_res(regs, idx, UEP_RES_STALL);
	} else {
		set_rx_res(regs, idx, UEP_RES_STALL);
	}

	if (idx != 0) {
		ep_cfg->stat.halted = true;
	}

	LOG_DBG("Set halt ep 0x%02x", ep_cfg->addr);

	return 0;
}

static int udc_wch_usbhs2_ep_clear_halt(const struct device *dev,
					struct udc_ep_config *const ep_cfg)
{
	struct udc_wch_usbhs2_data *priv = udc_get_private(dev);
	struct usbhs2_regs *regs = get_regs(dev);
	const uint8_t idx = USB_EP_GET_IDX(ep_cfg->addr);

	unsigned int key;

	if (idx == 0) {
		return 0;
	}

	key = irq_lock();

	/* Clearing a halt resets the toggle to DATA0 */
	if (USB_EP_DIR_IS_IN(ep_cfg->addr)) {
		priv->data1[1][idx] = false;
	} else {
		priv->data1[0][idx] = false;
	}

	if (!ep_cfg->stat.halted) {
		/*
		 * A toggle reset on a live endpoint: keep any armed transfer and
		 * only restart its next packet with DATA0.
		 */
		if (USB_EP_DIR_IS_IN(ep_cfg->addr)) {
			regs->UEP_CTRL[idx].tx_ctrl =
				(regs->UEP_CTRL[idx].tx_ctrl & ~UEP_TOG_MASK) | UEP_DONE;
		} else {
			regs->UEP_CTRL[idx].rx_ctrl =
				(regs->UEP_CTRL[idx].rx_ctrl & ~UEP_TOG_MASK) | UEP_DONE;
		}
		irq_unlock(key);
		return 0;
	}

	if (USB_EP_DIR_IS_IN(ep_cfg->addr)) {
		set_tx_res(regs, idx, UEP_RES_NAK);
	} else {
		set_rx_res(regs, idx, UEP_RES_NAK);
	}

	ep_cfg->stat.halted = false;
	udc_ep_set_busy(ep_cfg, false);
	irq_unlock(key);

	if (udc_buf_peek(ep_cfg) != NULL) {
		atomic_set_bit(&priv->xfer_new, ep_to_bnum(ep_cfg->addr));
		k_event_post(&priv->events, BIT(USBHS2_EVT_XFER_NEW));
	}

	LOG_DBG("Clear halt ep 0x%02x", ep_cfg->addr);

	return 0;
}

static int udc_wch_usbhs2_set_address(const struct device *dev, const uint8_t addr)
{
	get_regs(dev)->DEV_AD = addr;

	return 0;
}

static int udc_wch_usbhs2_host_wakeup(const struct device *dev)
{
	/* The hardware drives resume signalling and clears the bit itself */
	get_regs(dev)->WAKE_CTRL |= UD_REMOTE_WKUP;

	return 0;
}

static enum udc_bus_speed udc_wch_usbhs2_device_speed(const struct device *dev)
{
	const struct udc_wch_usbhs2_config *config = dev->config;

	if (config->high_speed && (get_regs(dev)->MIS_ST & UDMS_HS_MOD)) {
		return UDC_BUS_SPEED_HS;
	}

	return UDC_BUS_SPEED_FS;
}

static int usbhs2_clock_init(const struct device *dev)
{
	const struct udc_wch_usbhs2_config *config = dev->config;
	int ret;

	/* One USBHS PLL feeds both UTMI PHYs. Its source and reference can only be set
	 * while it is off (RM 3.4.12).
	 */
	if ((RCC->CTLR & RCC_CTLR_USBHSPLLRDY) == 0U) {
		RCC->CTLR &= ~RCC_CTLR_USBHSPLLON;
		RCC->CFGR2 =
			(RCC->CFGR2 & ~(RCC_CFGR2_USBHSPLLSRC_MASK | RCC_CFGR2_USBHSPLLREF_MASK)) |
			((uint32_t)config->pll_ref << RCC_CFGR2_USBHSPLLREF_SHIFT);
		RCC->CTLR |= RCC_CTLR_USBHSPLLON;
		if (!WAIT_FOR((RCC->CTLR & RCC_CTLR_USBHSPLLRDY) != 0U, 10000, k_busy_wait(1))) {
			LOG_ERR("USBHS PLL did not lock");
			return -ETIMEDOUT;
		}
	}

	RCC->CFGR2 |= BIT(config->utmi_bit);

	ret = clock_control_on(config->clock_dev,
			       (clock_control_subsys_t)(uintptr_t)config->clock_id);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

static int udc_wch_usbhs2_init(const struct device *dev)
{
	const struct udc_wch_usbhs2_config *config = dev->config;
	struct usbhs2_regs *regs = config->regs;
	int ret;

	ret = usbhs2_clock_init(dev);
	if (ret != 0) {
		return ret;
	}

	regs->INT_EN = 0;
	regs->CONTROL = UD_RST_SIE | UD_RST_LINK | UD_CLR_ALL;
	k_busy_wait(USBHS2_RESET_US);
	regs->CONTROL = UD_RST_LINK | UD_PHY_SUSPENDM;
	regs->INT_FG = UDI_ALL;

	regs->UEP_TX_EN = 0;
	regs->UEP_RX_EN = 0;
	regs->UEP_T_ISO = 0;
	regs->UEP_R_ISO = 0;
	regs->UEP_T_TOG_AUTO = 0;
	regs->UEP_R_TOG_AUTO = 0;
	for (uint8_t i = 0; i < config->num_of_eps; i++) {
		regs->UEP_CTRL[i].tx_len = 0;
		regs->UEP_CTRL[i].tx_ctrl = UEP_RES_NAK;
		regs->UEP_CTRL[i].rx_ctrl = UEP_RES_NAK;
		regs->UEP_MAX_LEN[i] = 0;
	}

	regs->BASE_MODE = config->high_speed ? UD_SPEED_HIGH : UD_SPEED_FULL;
	regs->DEV_AD = 0;

	regs->INT_EN = UDI_BUS_RST | UDI_SUSPEND | UDI_BUS_SLEEP | UDI_LPM_ACT | UDI_TRANSFER |
		       UDI_LINK_RDY | (IS_ENABLED(CONFIG_UDC_ENABLE_SOF) ? UDI_SOF : 0);

	if (udc_ep_enable_internal(dev, USB_CONTROL_EP_OUT, USB_EP_TYPE_CONTROL, USBHS2_EP0_MPS,
				   0) != 0) {
		LOG_ERR("Failed to enable control endpoint");
		return -EIO;
	}

	if (udc_ep_enable_internal(dev, USB_CONTROL_EP_IN, USB_EP_TYPE_CONTROL, USBHS2_EP0_MPS,
				   0) != 0) {
		LOG_ERR("Failed to enable control endpoint");
		return -EIO;
	}

	return 0;
}

static int udc_wch_usbhs2_enable(const struct device *dev)
{
	const struct udc_wch_usbhs2_config *config = dev->config;

	config->irq_enable_func(dev);
	/* DEV_EN turns on the D+ pull-up and attaches to the bus */
	config->regs->CONTROL = UD_DEV_EN | UD_DMA_EN | UD_LPM_EN | UD_PHY_SUSPENDM;

	LOG_DBG("Enable device %s", dev->name);

	return 0;
}

static int udc_wch_usbhs2_disable(const struct device *dev)
{
	const struct udc_wch_usbhs2_config *config = dev->config;

	config->regs->CONTROL &= ~UD_DEV_EN;
	config->irq_disable_func(dev);

	LOG_DBG("Disable device %s", dev->name);

	return 0;
}

static int udc_wch_usbhs2_shutdown(const struct device *dev)
{
	const struct udc_wch_usbhs2_config *config = dev->config;

	config->regs->INT_EN = 0;
	config->regs->INT_FG = UDI_ALL;
	config->regs->CONTROL = UD_RST_SIE | UD_RST_LINK;

	if (udc_ep_disable_internal(dev, USB_CONTROL_EP_OUT) != 0) {
		LOG_ERR("Failed to disable control endpoint");
		return -EIO;
	}

	if (udc_ep_disable_internal(dev, USB_CONTROL_EP_IN) != 0) {
		LOG_ERR("Failed to disable control endpoint");
		return -EIO;
	}

	RCC->CFGR2 &= ~BIT(config->utmi_bit);

	return 0;
}

static int udc_wch_usbhs2_driver_preinit(const struct device *dev)
{
	const struct udc_wch_usbhs2_config *config = dev->config;
	struct udc_wch_usbhs2_data *priv = udc_get_private(dev);
	struct udc_data *data = dev->data;
	const uint16_t mps = config->high_speed ? 1024 : 1023;
	int err;

	k_mutex_init(&data->mutex);
	k_event_init(&priv->events);
	atomic_clear(&priv->xfer_new);
	atomic_clear(&priv->xfer_finished);

	data->caps.rwup = true;
	data->caps.hs = config->high_speed;
	data->caps.mps0 = UDC_MPS0_64;

	for (int i = 0; i < config->num_of_eps; i++) {
		config->ep_cfg_out[i].caps.out = 1;
		if (i == 0) {
			config->ep_cfg_out[i].caps.control = 1;
			config->ep_cfg_out[i].caps.mps = USBHS2_EP0_MPS;
		} else {
			config->ep_cfg_out[i].caps.bulk = 1;
			config->ep_cfg_out[i].caps.interrupt = 1;
			config->ep_cfg_out[i].caps.iso = 1;
			config->ep_cfg_out[i].caps.mps = mps;
		}

		config->ep_cfg_out[i].addr = USB_EP_DIR_OUT | i;
		err = udc_register_ep(dev, &config->ep_cfg_out[i]);
		if (err != 0) {
			LOG_ERR("Failed to register endpoint");
			return err;
		}
	}

	for (int i = 0; i < config->num_of_eps; i++) {
		config->ep_cfg_in[i].caps.in = 1;
		if (i == 0) {
			config->ep_cfg_in[i].caps.control = 1;
			config->ep_cfg_in[i].caps.mps = USBHS2_EP0_MPS;
		} else {
			config->ep_cfg_in[i].caps.bulk = 1;
			config->ep_cfg_in[i].caps.interrupt = 1;
			config->ep_cfg_in[i].caps.iso = 1;
			config->ep_cfg_in[i].caps.mps = mps;
		}

		config->ep_cfg_in[i].addr = USB_EP_DIR_IN | i;
		err = udc_register_ep(dev, &config->ep_cfg_in[i]);
		if (err != 0) {
			LOG_ERR("Failed to register endpoint");
			return err;
		}
	}

	config->make_thread(dev);

	return 0;
}

static void udc_wch_usbhs2_lock(const struct device *dev)
{
	k_sched_lock();
	udc_lock_internal(dev, K_FOREVER);
}

static void udc_wch_usbhs2_unlock(const struct device *dev)
{
	udc_unlock_internal(dev);
	k_sched_unlock();
}

static const struct udc_api udc_wch_usbhs2_api = {
	.lock = udc_wch_usbhs2_lock,
	.unlock = udc_wch_usbhs2_unlock,
	.device_speed = udc_wch_usbhs2_device_speed,
	.init = udc_wch_usbhs2_init,
	.enable = udc_wch_usbhs2_enable,
	.disable = udc_wch_usbhs2_disable,
	.shutdown = udc_wch_usbhs2_shutdown,
	.set_address = udc_wch_usbhs2_set_address,
	.host_wakeup = udc_wch_usbhs2_host_wakeup,
	.ep_enable = udc_wch_usbhs2_ep_enable,
	.ep_disable = udc_wch_usbhs2_ep_disable,
	.ep_set_halt = udc_wch_usbhs2_ep_set_halt,
	.ep_clear_halt = udc_wch_usbhs2_ep_clear_halt,
	.ep_enqueue = udc_wch_usbhs2_ep_enqueue,
	.ep_dequeue = udc_wch_usbhs2_ep_dequeue,
};

/* The USBHS PLL runs from HSE; its reference select is 25, 20, 24 or 32 MHz */
#define USBHS2_HSE_FREQ DT_PROP(DT_NODELABEL(clk_hse), clock_frequency)
BUILD_ASSERT(USBHS2_HSE_FREQ == 25000000 || USBHS2_HSE_FREQ == 20000000 ||
		     USBHS2_HSE_FREQ == 24000000 || USBHS2_HSE_FREQ == 32000000,
	     "USBHS PLL needs a 20, 24, 25 or 32 MHz HSE");
#define USBHS2_PLL_REF                                                                             \
	(USBHS2_HSE_FREQ == 20000000   ? 1                                                         \
	 : USBHS2_HSE_FREQ == 24000000 ? 2                                                         \
	 : USBHS2_HSE_FREQ == 32000000 ? 3                                                         \
				       : 0)

/* USBHS1 at 0x40024000 uses UTMI1 (CFGR2 bit 30), USBHS2 at 0x40023400 UTMI2 (bit 31) */
#define USBHS2_UTMI_BIT(n) (DT_INST_REG_ADDR(n) == 0x40024000 ? 30 : 31)
#define USBHS2_HIGH_SPEED(n)                                                                       \
	(!DT_INST_NODE_HAS_PROP(n, maximum_speed) ||                                               \
	 DT_INST_ENUM_HAS_VALUE(n, maximum_speed, high_speed))

#define UDC_WCH_USBHS2_DEVICE_DEFINE(n)                                                            \
	BUILD_ASSERT(DT_INST_PROP(n, num_bidir_endpoints) <= 8, "8 endpoints at most");            \
                                                                                                   \
	K_THREAD_STACK_DEFINE(udc_wch_usbhs2_stack_##n, CONFIG_UDC_WCH_USBHS2_STACK_SIZE);         \
                                                                                                   \
	static void udc_wch_usbhs2_irq_enable_func_##n(const struct device *dev)                   \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), 0, usbhs2_isr_handler, DEVICE_DT_INST_GET(n), 0);     \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}                                                                                          \
                                                                                                   \
	static void udc_wch_usbhs2_irq_disable_func_##n(const struct device *dev)                  \
	{                                                                                          \
		irq_disable(DT_INST_IRQN(n));                                                      \
	}                                                                                          \
                                                                                                   \
	static void udc_wch_usbhs2_thread_##n(void *dev, void *arg1, void *arg2)                   \
	{                                                                                          \
		while (true) {                                                                     \
			usbhs2_thread_handler(dev);                                                \
		}                                                                                  \
	}                                                                                          \
                                                                                                   \
	static void udc_wch_usbhs2_make_thread_##n(const struct device *dev)                       \
	{                                                                                          \
		struct udc_wch_usbhs2_data *priv = udc_get_private(dev);                           \
                                                                                                   \
		k_thread_create(&priv->thread_data, udc_wch_usbhs2_stack_##n,                      \
				K_THREAD_STACK_SIZEOF(udc_wch_usbhs2_stack_##n),                   \
				udc_wch_usbhs2_thread_##n, (void *)dev, NULL, NULL,                \
				K_PRIO_COOP(CONFIG_UDC_WCH_USBHS2_THREAD_PRIORITY), K_ESSENTIAL,   \
				K_NO_WAIT);                                                        \
		k_thread_name_set(&priv->thread_data, dev->name);                                  \
	}                                                                                          \
                                                                                                   \
	static struct udc_ep_config ep_cfg_out_##n[DT_INST_PROP(n, num_bidir_endpoints)];          \
	static struct udc_ep_config ep_cfg_in_##n[DT_INST_PROP(n, num_bidir_endpoints)];           \
                                                                                                   \
	static const struct udc_wch_usbhs2_config udc_wch_usbhs2_config_##n = {                    \
		.regs = (struct usbhs2_regs *)DT_INST_REG_ADDR(n),                                 \
		.num_of_eps = DT_INST_PROP(n, num_bidir_endpoints),                                \
		.ep_cfg_in = ep_cfg_in_##n,                                                        \
		.ep_cfg_out = ep_cfg_out_##n,                                                      \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                                \
		.clock_id = DT_INST_CLOCKS_CELL(n, id),                                            \
		.utmi_bit = USBHS2_UTMI_BIT(n),                                                    \
		.pll_ref = USBHS2_PLL_REF,                                                         \
		.high_speed = USBHS2_HIGH_SPEED(n),                                                \
		.irq_enable_func = udc_wch_usbhs2_irq_enable_func_##n,                             \
		.irq_disable_func = udc_wch_usbhs2_irq_disable_func_##n,                           \
		.make_thread = udc_wch_usbhs2_make_thread_##n,                                     \
	};                                                                                         \
                                                                                                   \
	static struct udc_wch_usbhs2_data udc_priv_##n;                                            \
                                                                                                   \
	static struct udc_data udc_data_##n = {                                                    \
		.mutex = Z_MUTEX_INITIALIZER(udc_data_##n.mutex),                                  \
		.priv = &udc_priv_##n,                                                             \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, udc_wch_usbhs2_driver_preinit, NULL, &udc_data_##n,               \
			      &udc_wch_usbhs2_config_##n, POST_KERNEL,                             \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &udc_wch_usbhs2_api);

DT_INST_FOREACH_STATUS_OKAY(UDC_WCH_USBHS2_DEVICE_DEFINE)
