/*
 * gs_usb protocol definitions.
 *
 * Mirror of the upstream candleLight_fw / Linux-kernel gs_usb header,
 * trimmed to only the parts needed for a classic-CAN device (no FD,
 * no ELM, no extended termination/filter ops). On-wire layout matches
 * the upstream so the stock Linux gs_usb driver binds without changes.
 *
 * Upstream reference: candle-usb/candleLight_fw include/gs_usb.h (MIT, 2016 Hubert Denkmair)
 *                     and Linux drivers/net/can/usb/gs_usb.c (GPL-2.0).
 * This file contains only protocol constants and packed structs — no derived code.
 */
#ifndef __GS_USB_H__
#define __GS_USB_H__

#include <stdint.h>

/* Endpoint addresses (kernel hard-codes these). */
#define GSUSB_ENDPOINT_IN   0x81  /* EP1 IN  — device → host frames */
#define GSUSB_ENDPOINT_OUT  0x02  /* EP2 OUT — host → device frames */

/* Feature bits returned in struct gs_device_bt_const.feature. */
#define GS_CAN_FEATURE_LISTEN_ONLY              (1u << 0)
#define GS_CAN_FEATURE_LOOP_BACK                (1u << 1)
#define GS_CAN_FEATURE_TRIPLE_SAMPLE            (1u << 2)
#define GS_CAN_FEATURE_ONE_SHOT                 (1u << 3)
#define GS_CAN_FEATURE_HW_TIMESTAMP             (1u << 4)
#define GS_CAN_FEATURE_IDENTIFY                 (1u << 5)
#define GS_CAN_FEATURE_USER_ID                  (1u << 6)
#define GS_CAN_FEATURE_PAD_PKTS_TO_MAX_PKT_SIZE (1u << 7)
#define GS_CAN_FEATURE_FD                       (1u << 8)
#define GS_CAN_FEATURE_BT_CONST_EXT             (1u << 10)
#define GS_CAN_FEATURE_TERMINATION              (1u << 11)
#define GS_CAN_FEATURE_BERR_REPORTING           (1u << 12)
#define GS_CAN_FEATURE_GET_STATE                (1u << 13)

/* Linux-style CAN ID flag bits, packed inside gs_host_frame.can_id. */
#define CAN_EFF_FLAG  0x80000000U  /* extended (29-bit) identifier */
#define CAN_RTR_FLAG  0x40000000U  /* remote-transmission-request frame */
#define CAN_ERR_FLAG  0x20000000U  /* error frame (we don't emit these in phase 1) */
#define CAN_EFF_MASK  0x1FFFFFFFU
#define CAN_SFF_MASK  0x000007FFU

/* host_frame.flags */
#define GS_CAN_FLAG_OVERFLOW 0x01

/* Vendor request codes carried in bRequest of the EP0 SETUP packet. */
enum gs_usb_breq {
    GS_USB_BREQ_HOST_FORMAT     = 0,
    GS_USB_BREQ_BITTIMING       = 1,
    GS_USB_BREQ_MODE            = 2,
    GS_USB_BREQ_BERR            = 3,
    GS_USB_BREQ_BT_CONST        = 4,
    GS_USB_BREQ_DEVICE_CONFIG   = 5,
    GS_USB_BREQ_TIMESTAMP       = 6,
    GS_USB_BREQ_IDENTIFY        = 7,
    GS_USB_BREQ_GET_USER_ID     = 8,
    GS_USB_BREQ_SET_USER_ID     = 9,
    GS_USB_BREQ_DATA_BITTIMING  = 10,
    GS_USB_BREQ_BT_CONST_EXT    = 11,
    GS_USB_BREQ_SET_TERMINATION = 12,
    GS_USB_BREQ_GET_TERMINATION = 13,
    GS_USB_BREQ_GET_STATE       = 14,
};

/* Modes used in struct gs_device_mode.mode. */
enum gs_can_mode {
    GS_CAN_MODE_RESET = 0,
    GS_CAN_MODE_START = 1,
};

/* Same flag bits as in feature, applied to gs_device_mode.flags
 * to enable per-channel options at start time. */
#define GS_CAN_MODE_NORMAL              0
#define GS_CAN_MODE_LISTEN_ONLY         (1u << 0)
#define GS_CAN_MODE_LOOP_BACK           (1u << 1)
#define GS_CAN_MODE_TRIPLE_SAMPLE       (1u << 2)
#define GS_CAN_MODE_ONE_SHOT            (1u << 3)
#define GS_CAN_MODE_HW_TIMESTAMP        (1u << 4)
#define GS_CAN_MODE_PAD_PKTS_TO_MAX_PKT (1u << 7)
#define GS_CAN_MODE_FD                  (1u << 8)
#define GS_CAN_MODE_BERR_REPORTING      (1u << 12)

#define GSUSB_PACKED __attribute__((packed, aligned(4)))

struct gs_host_config {
    uint32_t byte_order;     /* host writes 0x0000beef; we just echo back endianness check */
} GSUSB_PACKED;

struct gs_device_config {
    uint8_t  reserved1;
    uint8_t  reserved2;
    uint8_t  reserved3;
    uint8_t  icount;         /* number of CAN channels minus 1 (0 → 1 channel) */
    uint32_t sw_version;
    uint32_t hw_version;
} GSUSB_PACKED;

struct gs_device_mode {
    uint32_t mode;           /* enum gs_can_mode */
    uint32_t flags;          /* GS_CAN_MODE_* combined */
} GSUSB_PACKED;

struct gs_device_bittiming {
    uint32_t prop_seg;
    uint32_t phase_seg1;
    uint32_t phase_seg2;
    uint32_t sjw;
    uint32_t brp;
} GSUSB_PACKED;

struct gs_can_bittiming_const {
    uint32_t tseg1_min;
    uint32_t tseg1_max;
    uint32_t tseg2_min;
    uint32_t tseg2_max;
    uint32_t sjw_max;
    uint32_t brp_min;
    uint32_t brp_max;
    uint32_t brp_inc;
} GSUSB_PACKED;

struct gs_device_bt_const {
    uint32_t feature;
    uint32_t fclk_can;       /* CAN peripheral input clock in Hz */
    struct gs_can_bittiming_const btc;
} GSUSB_PACKED;

struct gs_identify_mode {
    uint32_t mode;           /* 0 = off, 1 = blink */
} GSUSB_PACKED;

/* Classic-CAN host frame: fixed 20 bytes (8-byte data payload, no timestamp).
 * The kernel inspects the bulk-IN endpoint's wMaxPacketSize and our reported
 * features to know which variant to expect — we advertise none of TIMESTAMP/FD,
 * so this is the layout used. */
struct gs_host_frame {
    uint32_t echo_id;        /* host puts a token here on TX; device echoes on TX-complete.
                                Set to 0xFFFFFFFF for unsolicited RX frames. */
    uint32_t can_id;         /* 11/29-bit ID + CAN_*_FLAG bits */
    uint8_t  can_dlc;
    uint8_t  channel;        /* 0-based CAN channel index */
    uint8_t  flags;          /* GS_CAN_FLAG_OVERFLOW */
    uint8_t  reserved;
    uint8_t  data[8];
} GSUSB_PACKED;

#define GS_HOST_FRAME_SIZE  ((uint32_t)sizeof(struct gs_host_frame))

#endif /* __GS_USB_H__ */
