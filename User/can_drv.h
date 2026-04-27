/*
 * CAN1 + CAN2 driver — thin wrapper around the WCH StdPeriph CAN API.
 *
 * Two independent channels (ch=0 → CAN1, ch=1 → CAN2). Each maintains its own
 * bittiming + open state; the gs_usb layer drives them independently based on
 * the `wValue` channel index in vendor SETUP packets.
 *
 * Pinout (defaults — change in can_drv_init_pins() if your board needs it):
 *   CAN1: PB8 RX, PB9 TX (Remap1)
 *   CAN2: PB12 RX, PB13 TX (default mapping, no remap)
 */
#ifndef __CAN_DRV_H__
#define __CAN_DRV_H__

#include <stdint.h>
#include "ch32v30x.h"
#include "gs_usb.h"

#define CAN_DRV_NUM_CHANNELS    2

/* CAN1 + CAN2 are both on APB1. With sysclock=144 MHz and PPRE1=/2 (WCH
 * SetSysClockTo144_HSE), APB1 = 72 MHz. Reported to the host as fclk_can in
 * struct gs_device_bt_const. CH32V305 datasheet allows APB1 up to 144 MHz with
 * no extra divider before bxCAN, so 72 MHz is comfortably within spec. */
#define CAN_DRV_FCLK_HZ        72000000U

/* bxCAN bit-timing register field widths: BS1 4-bit (1..16 TQ), BS2 3-bit
 * (1..8 TQ), SJW 2-bit (1..4 TQ), BRP 10-bit (1..1024). */
#define CAN_DRV_TSEG1_MIN      1
#define CAN_DRV_TSEG1_MAX      16
#define CAN_DRV_TSEG2_MIN      1
#define CAN_DRV_TSEG2_MAX      8
#define CAN_DRV_SJW_MAX        4
#define CAN_DRV_BRP_MIN        1
#define CAN_DRV_BRP_MAX        1024
#define CAN_DRV_BRP_INC        1

/* Bring up GPIOs + RCC clocks for both channels and seat the CAN1/CAN2 filter
 * bank split. Call once at boot before any open/close. */
void can_drv_init(void);

void can_drv_set_bittiming(uint8_t ch, const struct gs_device_bittiming *bt);
void can_drv_open (uint8_t ch, uint32_t mode_flags);
void can_drv_close(uint8_t ch);
uint8_t can_drv_send (uint8_t ch, const struct gs_host_frame *frame);
uint8_t can_drv_is_open(uint8_t ch);

/* Implemented by the gs_usb glue layer; called from each CAN's RX0 ISR with a
 * staged host_frame (channel field already populated by the ISR). */
extern void can_drv_on_rx(const struct gs_host_frame *frame);

#endif /* __CAN_DRV_H__ */
