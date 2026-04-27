#include <string.h>
#include "gs_can.h"
#include "can_drv.h"
#include "usbhs_dev.h"
#include "led.h"

/* ---------------- Static response buffers for IN vendor requests ---------------- */
/* Aligned + sized large enough for the biggest reply we serve. */
__attribute__((aligned(4))) static struct gs_device_config   s_dev_cfg = {
    .reserved1 = 0, .reserved2 = 0, .reserved3 = 0,
    .icount = CAN_DRV_NUM_CHANNELS - 1,     /* gs_usb encodes "channels - 1" */
    .sw_version = 1,
    .hw_version = 1,
};

__attribute__((aligned(4))) static struct gs_device_bt_const s_bt_const = {
    .feature  = GS_CAN_FEATURE_LISTEN_ONLY |
                GS_CAN_FEATURE_LOOP_BACK   |
                GS_CAN_FEATURE_ONE_SHOT    |
                GS_CAN_FEATURE_IDENTIFY,
    .fclk_can = CAN_DRV_FCLK_HZ,
    .btc = {
        .tseg1_min = CAN_DRV_TSEG1_MIN,
        .tseg1_max = CAN_DRV_TSEG1_MAX,
        .tseg2_min = CAN_DRV_TSEG2_MIN,
        .tseg2_max = CAN_DRV_TSEG2_MAX,
        .sjw_max   = CAN_DRV_SJW_MAX,
        .brp_min   = CAN_DRV_BRP_MIN,
        .brp_max   = CAN_DRV_BRP_MAX,
        .brp_inc   = CAN_DRV_BRP_INC,
    },
};

__attribute__((aligned(4))) static struct gs_host_config s_host_cfg = {
    .byte_order = 0x0000beef,               /* echoed to confirm endianness */
};

/* ---------------- TX-IN ring (RX-from-bus + TX-complete echoes) ---------------- */
/* The CAN1 RX0 ISR and the bulk-OUT path both produce frames destined for the
 * host (RX frames and TX-completion echoes). The bulk-IN endpoint can carry
 * one frame at a time, so we queue here and pump from the IN-complete callback. */
#define GS_TX_QUEUE_LEN 32   /* power of two not required; small + cheap */
__attribute__((aligned(4))) static struct gs_host_frame s_tx_q[GS_TX_QUEUE_LEN];
static volatile uint8_t s_tx_q_head;        /* producer */
static volatile uint8_t s_tx_q_tail;        /* consumer */

static uint8_t tx_q_empty(void) { return s_tx_q_head == s_tx_q_tail; }
static uint8_t tx_q_full(void)  { return ((uint8_t)(s_tx_q_head + 1) % GS_TX_QUEUE_LEN) == s_tx_q_tail; }

static void tx_q_push(const struct gs_host_frame *f)
{
    if (tx_q_full()) return;                /* drop on overflow — RX overrun is recoverable */
    s_tx_q[s_tx_q_head] = *f;
    s_tx_q_head = (uint8_t)((s_tx_q_head + 1) % GS_TX_QUEUE_LEN);
}

static const struct gs_host_frame *tx_q_peek(void)
{
    if (tx_q_empty()) return NULL;
    return &s_tx_q[s_tx_q_tail];
}

static void tx_q_pop(void)
{
    if (tx_q_empty()) return;
    s_tx_q_tail = (uint8_t)((s_tx_q_tail + 1) % GS_TX_QUEUE_LEN);
}

/* Try to push the next queued frame onto the wire. Called from the IN-complete
 * callback (re-arms after each finished IN) and from producers (RX ISR / bulk-OUT)
 * to kick-start the pump when EP1 IN was idle. */
static void pump_tx_in(void)
{
    const struct gs_host_frame *f = tx_q_peek();
    if (!f) return;
    if (usbhs_dev_send_frame(f) == 0) tx_q_pop();
}

/* --------------------------------------------------------------------- */

void gs_can_init(void)
{
    s_tx_q_head = 0;
    s_tx_q_tail = 0;
}

void gs_can_on_enum(void)
{
    /* On re-enumeration, drop any stale queued frames and close every channel
     * so the host doesn't receive frames it never asked for. */
    s_tx_q_head = 0;
    s_tx_q_tail = 0;
    for (uint8_t ch = 0; ch < CAN_DRV_NUM_CHANNELS; ch++)
        can_drv_close(ch);
}

uint8_t gs_can_handle_vendor_setup_in(uint8_t bRequest, uint16_t wValue,
                                      uint16_t wIndex, uint16_t wLength,
                                      const uint8_t **resp_ptr,
                                      uint16_t *resp_len)
{
    (void)wIndex; (void)wLength;

    /* For per-channel requests, wValue carries the channel index (0..icount).
     * Reject out-of-range channels up front. DEVICE_CONFIG / HOST_FORMAT /
     * TIMESTAMP are device-wide and ignore wValue. */
    uint8_t ch = (uint8_t)(wValue & 0xFF);

    switch (bRequest) {
    case GS_USB_BREQ_DEVICE_CONFIG:
        *resp_ptr = (const uint8_t *)&s_dev_cfg;
        *resp_len = sizeof(s_dev_cfg);
        return 0;

    case GS_USB_BREQ_BT_CONST:
        if (ch >= CAN_DRV_NUM_CHANNELS) return 1;
        /* Both channels share the same bxCAN feature set + APB1 clock, so the
         * response is identical for either channel. */
        *resp_ptr = (const uint8_t *)&s_bt_const;
        *resp_len = sizeof(s_bt_const);
        return 0;

    case GS_USB_BREQ_TIMESTAMP: {
        /* We don't claim HW_TIMESTAMP feature, but the kernel still issues
         * this request during probe. Return a constant zero — the kernel
         * accepts it and skips timestamping logic. */
        static const uint32_t zero = 0;
        *resp_ptr = (const uint8_t *)&zero;
        *resp_len = sizeof(zero);
        return 0;
    }

    case GS_USB_BREQ_GET_STATE: {
        /* Stubbed (we don't advertise GET_STATE); returning success with zero
         * payload is harmless if the host probes anyway. */
        static const uint32_t zero[3] = {0, 0, 0};
        *resp_ptr = (const uint8_t *)zero;
        *resp_len = sizeof(zero);
        return 0;
    }

    default:
        return 1;       /* unknown vendor request → STALL */
    }
}

uint8_t gs_can_handle_vendor_setup_out(uint8_t bRequest, uint16_t wValue,
                                       uint16_t wIndex,
                                       const uint8_t *data, uint16_t len)
{
    (void)wIndex;

    uint8_t ch = (uint8_t)(wValue & 0xFF);

    switch (bRequest) {
    case GS_USB_BREQ_HOST_FORMAT:
        if (len < sizeof(struct gs_host_config)) return 1;
        /* Ignore byte_order content — both sides are little-endian on RISC-V
         * and on every Linux host the kernel currently runs on. */
        s_host_cfg = *(const struct gs_host_config *)data;
        return 0;

    case GS_USB_BREQ_BITTIMING: {
        if (ch >= CAN_DRV_NUM_CHANNELS) return 1;
        if (len < sizeof(struct gs_device_bittiming)) return 1;
        can_drv_set_bittiming(ch, (const struct gs_device_bittiming *)data);
        return 0;
    }

    case GS_USB_BREQ_MODE: {
        if (ch >= CAN_DRV_NUM_CHANNELS) return 1;
        if (len < sizeof(struct gs_device_mode)) return 1;
        const struct gs_device_mode *m = (const struct gs_device_mode *)data;
        if (m->mode == GS_CAN_MODE_RESET) {
            can_drv_close(ch);
        } else if (m->mode == GS_CAN_MODE_START) {
            can_drv_open(ch, m->flags);
        }
        return 0;
    }

    case GS_USB_BREQ_IDENTIFY: {
        if (ch >= CAN_DRV_NUM_CHANNELS) return 1;
        if (len < sizeof(struct gs_identify_mode)) return 1;
        const struct gs_identify_mode *im = (const struct gs_identify_mode *)data;
        /* Single shared LED — track per channel, light if EITHER asks. */
        static uint8_t identify_mask;
        if (im->mode) identify_mask |=  (1u << ch);
        else          identify_mask &= ~(1u << ch);
        led_set_identify(identify_mask != 0);
        return 0;
    }

    default:
        return 1;
    }
}

void gs_can_handle_bulk_out(const struct gs_host_frame *frame)
{
    /* Host gave us a frame to TX on the channel encoded in frame->channel.
     * If that channel is open, send it; either way echo the frame back (with
     * the host's echo_id intact) so the kernel can complete its TX request.
     * The Linux gs_usb driver requires this echo. */
    struct gs_host_frame echo = *frame;
    uint8_t ch = frame->channel;

    if (ch < CAN_DRV_NUM_CHANNELS && can_drv_is_open(ch)) {
        (void)can_drv_send(ch, frame);
        /* TODO(phase 2): defer the echo until the TX mailbox actually empties
         * so timing-sensitive applications see real on-bus completion. */
    }

    tx_q_push(&echo);
    pump_tx_in();

    /* We're done with the OUT buffer — let the EP receive another frame. */
    usbhs_dev_rearm_bulk_out();
}

void gs_can_handle_bulk_in_complete(void)
{
    pump_tx_in();
}

/* CAN1 RX0 ISR feeds frames in here. Stage onto the IN ring + try to push. */
void can_drv_on_rx(const struct gs_host_frame *frame)
{
    tx_q_push(frame);
    pump_tx_in();
}
