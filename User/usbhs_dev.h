/*
 * USBHS device stack. Handles bus events, EP0 standard requests, and the
 * bulk endpoints used by gs_usb. Vendor (class) requests on EP0 and bulk
 * data are forwarded to the gs_usb glue layer in gs_can.[ch].
 */
#ifndef __USBHS_DEV_H__
#define __USBHS_DEV_H__

#include <stdint.h>
#include "ch32v30x.h"
#include "gs_usb.h"

/* Bring up the USBHS PHY/PLL clocks. Must run before usbhs_dev_init(). */
void usbhs_dev_rcc_init(void);

/* Reset SIE, install descriptors, arm endpoints, enable D+ pull-up. */
void usbhs_dev_init(void);

/* Returns 1 once SET_CONFIGURATION has fired — i.e. the host has finished
 * enumerating us and bulk transfer is allowed. */
uint8_t usbhs_dev_is_configured(void);

/* Queue one host_frame on EP1 IN (device → host). Returns 0 on success,
 * non-zero if the previous IN is still pending (gs_usb layer will retry).
 * The buffer is copied internally so `frame` does not need to outlive the call. */
uint8_t usbhs_dev_send_frame(const struct gs_host_frame *frame);

/* Re-arm EP2 OUT to receive another host_frame from the host. Called from the
 * gs_usb layer after it consumes a delivered frame. */
void usbhs_dev_rearm_bulk_out(void);

/* Returns the negotiated speed (USBHS_SPEED_HIGH / USBHS_SPEED_FULL). */
uint8_t usbhs_dev_speed(void);

#endif /* __USBHS_DEV_H__ */
