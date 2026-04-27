/*
 * Status LED on PA3 — active low (driven low to light the LED).
 *
 * Behavior:
 *   - Idle (CAN closed, no identify): OFF.
 *   - CAN open:                       ON (steady).
 *   - GS_USB_BREQ_IDENTIFY mode=1:    forced ON regardless of CAN state.
 *   - GS_USB_BREQ_IDENTIFY mode=0:    revert to CAN-open-driven state.
 *
 * TODO(phase 2): driving a real blink for IDENTIFY needs a timer source
 * (e.g. TIM3 @ 4 Hz). For now we just hold solid ON during identify, which
 * still lets the operator pick the device out of a stack visually.
 */
#ifndef __LED_H__
#define __LED_H__

#include <stdint.h>

void led_init(void);
void led_set_can_open(uint8_t open);
void led_set_identify(uint8_t identify);

#endif /* __LED_H__ */
