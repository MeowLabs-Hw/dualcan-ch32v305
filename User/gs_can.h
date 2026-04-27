/*
 * gs_usb protocol glue. The USBHS device layer calls into here when:
 *   - a vendor SETUP packet arrives on EP0 (gs_can_handle_vendor_setup_*)
 *   - a bulk OUT packet arrives on EP2 (gs_can_handle_bulk_out)
 *   - a bulk IN finishes on EP1 (gs_can_handle_bulk_in_complete)
 *
 * The CAN driver calls can_drv_on_rx() (defined here) when a frame arrives
 * on the CAN bus — we stage it into the bulk-IN queue.
 */
#ifndef __GS_CAN_H__
#define __GS_CAN_H__

#include <stdint.h>
#include "gs_usb.h"

void gs_can_init(void);

/* EP0 vendor request dispatchers.
 * `*resp_ptr` is set to the buffer the EP0 stack should return to the host.
 * `*resp_len` is set to the number of bytes in that buffer. Return 0 on
 * success, non-zero to stall EP0. For OUT-direction requests, the data
 * payload arrives later via gs_can_handle_vendor_setup_out_data(). */
uint8_t gs_can_handle_vendor_setup_in(uint8_t bRequest, uint16_t wValue,
                                      uint16_t wIndex, uint16_t wLength,
                                      const uint8_t **resp_ptr,
                                      uint16_t *resp_len);

/* For OUT-direction vendor requests, the SETUP arrives first (cached) and
 * then the data payload comes in chunks; this is called once the full payload
 * has been assembled in `data` (`len` bytes). */
uint8_t gs_can_handle_vendor_setup_out(uint8_t bRequest, uint16_t wValue,
                                       uint16_t wIndex,
                                       const uint8_t *data, uint16_t len);

/* Called from the bulk-OUT EP2 handler with a complete host_frame from the host. */
void gs_can_handle_bulk_out(const struct gs_host_frame *frame);

/* Called from the bulk-IN EP1 handler when the previous TX completes — this
 * lets us push the next queued frame onto the wire. */
void gs_can_handle_bulk_in_complete(void);

/* Called by the USB stack right after enumeration finishes (SET_CONFIGURATION).
 * Resets gs_usb state. */
void gs_can_on_enum(void);

#endif /* __GS_CAN_H__ */
