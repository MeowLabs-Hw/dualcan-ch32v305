/*
 * CH32V305RBT6 SocketCAN / gs_usb adapter — phase 1 (single-channel, classic CAN, Linux only).
 *
 * Boot sequence:
 *   1. Sysclock 144 MHz from 8 MHz HSE (set in system_ch32v30x.c).
 *   2. Bring up CAN1 GPIOs (PB8 RX / PB9 TX via Remap1).
 *   3. Bring up USBHS PHY/PLL clocks, then enumerate as VID 0x1d50 PID 0x606f.
 *   4. Idle in main loop — everything else runs in interrupt context.
 *
 * Linux side:
 *   $ dmesg | grep gs_usb        # confirm enumeration
 *   $ ip -d link show can0       # confirm interface
 *   $ ip link set can0 type can bitrate 500000
 *   $ ip link set can0 up
 *   $ candump can0
 */
#include "debug.h"
#include "ch32v30x.h"
#include "can_drv.h"
#include "usbhs_dev.h"
#include "gs_can.h"
#include "led.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);

    printf("CH32V305 gs_usb — sysclk %u Hz, ChipID %08x\r\n",
           (unsigned)SystemCoreClock, (unsigned)DBGMCU_GetCHIPID());

    led_init();
    can_drv_init();
    gs_can_init();

    usbhs_dev_rcc_init();
    usbhs_dev_init();

    while (1) {
        /* All real work happens in USBHS_IRQHandler and USB_LP_CAN1_RX0_IRQHandler.
         * Could WFI here for power, but USBHS device-mode wakeup behavior on this
         * silicon needs verification first — stick with a busy idle for phase 1. */
        __NOP();
    }
}
