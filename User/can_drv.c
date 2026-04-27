#include "can_drv.h"
#include "ch32v30x.h"
#include "led.h"

/* Default bittiming = 500 kbps with APB1=72 MHz:
 *   bit-time = 1 + (prop_seg + phase_seg1) + phase_seg2 = 1 + 12 + 3 = 16 TQ
 *   BRP=9 → 72e6 / (9*16) = 500000
 * The host normally rewrites this before opening, so this is just the
 * fallback if someone opens before sending GS_USB_BREQ_BITTIMING. */
static const struct gs_device_bittiming s_bt_default = {
    .prop_seg   = 6,
    .phase_seg1 = 6,
    .phase_seg2 = 3,
    .sjw        = 1,
    .brp        = 9,
};

static struct gs_device_bittiming s_bt   [CAN_DRV_NUM_CHANNELS];
static volatile uint8_t           s_open [CAN_DRV_NUM_CHANNELS];

static CAN_TypeDef * const s_can[CAN_DRV_NUM_CHANNELS] = { CAN1, CAN2 };

/* CAN1 owns filter banks 0..6, CAN2 owns banks 7..13. Plenty of room for
 * either side to grow if we ever expose programmable filters via gs_usb. */
#define CAN_FILTER_BANK_CAN1   0
#define CAN_FILTER_BANK_CAN2   7
#define CAN_SLAVE_START_BANK   7

static void can_drv_pins_can1(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* PB8 = CAN1_RX, PB9 = CAN1_TX (Remap1). Default PA11/PA12 collide with
     * USBFS — even though we use USBHS here, PB8/9 keeps the USBFS pins free
     * and matches the WCH CAN/Networking example. */
    GPIO_PinRemapConfig(GPIO_Remap1_CAN1, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_9;                /* TX = AF push-pull */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    gpio.GPIO_Pin  = GPIO_Pin_8;                 /* RX = input pull-up */
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);
}

static void can_drv_pins_can2(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* PB12 = CAN2_RX, PB13 = CAN2_TX (default mapping, no remap). */
    gpio.GPIO_Pin   = GPIO_Pin_13;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    gpio.GPIO_Pin  = GPIO_Pin_12;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);
}

void can_drv_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1 | RCC_APB1Periph_CAN2, ENABLE);

    can_drv_pins_can1();
    can_drv_pins_can2();

    /* Filter bank split — must be done while both CANs are still in INIT mode
     * (which they are, since CAN_Init hasn't been called yet on either). After
     * this, banks < CAN_SLAVE_START_BANK belong to CAN1, ≥ to CAN2. */
    CAN_SlaveStartBank(CAN_SLAVE_START_BANK);

    for (uint8_t i = 0; i < CAN_DRV_NUM_CHANNELS; i++) {
        s_bt[i]   = s_bt_default;
        s_open[i] = 0;
    }
}

void can_drv_set_bittiming(uint8_t ch, const struct gs_device_bittiming *bt)
{
    if (ch >= CAN_DRV_NUM_CHANNELS) return;
    s_bt[ch] = *bt;
}

uint8_t can_drv_is_open(uint8_t ch)
{
    if (ch >= CAN_DRV_NUM_CHANNELS) return 0;
    return s_open[ch];
}

static uint8_t any_channel_open(void)
{
    for (uint8_t i = 0; i < CAN_DRV_NUM_CHANNELS; i++)
        if (s_open[i]) return 1;
    return 0;
}

static void apply_filter_accept_all(uint8_t ch)
{
    /* One 32-bit mask filter per channel, ID=0/MASK=0 → accept everything into
     * that channel's FIFO0. Filter bank index is in CAN1's allocated region for
     * ch=0 and CAN2's region for ch=1. */
    CAN_FilterInitTypeDef f = {0};
    f.CAN_FilterNumber           = (ch == 0) ? CAN_FILTER_BANK_CAN1 : CAN_FILTER_BANK_CAN2;
    f.CAN_FilterMode             = CAN_FilterMode_IdMask;
    f.CAN_FilterScale            = CAN_FilterScale_32bit;
    f.CAN_FilterIdHigh           = 0x0000;
    f.CAN_FilterIdLow            = 0x0000;
    f.CAN_FilterMaskIdHigh       = 0x0000;
    f.CAN_FilterMaskIdLow        = 0x0000;
    f.CAN_FilterFIFOAssignment   = CAN_Filter_FIFO0;
    f.CAN_FilterActivation       = ENABLE;
    CAN_FilterInit(&f);
}

void can_drv_open(uint8_t ch, uint32_t mode_flags)
{
    if (ch >= CAN_DRV_NUM_CHANNELS) return;

    CAN_InitTypeDef init = {0};
    uint32_t tseg1 = s_bt[ch].prop_seg + s_bt[ch].phase_seg1;
    uint32_t tseg2 = s_bt[ch].phase_seg2;
    uint32_t sjw   = s_bt[ch].sjw;
    uint32_t brp   = s_bt[ch].brp;

    /* Clamp to bxCAN register widths. */
    if (tseg1 < CAN_DRV_TSEG1_MIN) tseg1 = CAN_DRV_TSEG1_MIN;
    if (tseg1 > CAN_DRV_TSEG1_MAX) tseg1 = CAN_DRV_TSEG1_MAX;
    if (tseg2 < CAN_DRV_TSEG2_MIN) tseg2 = CAN_DRV_TSEG2_MIN;
    if (tseg2 > CAN_DRV_TSEG2_MAX) tseg2 = CAN_DRV_TSEG2_MAX;
    if (sjw   > CAN_DRV_SJW_MAX  ) sjw   = CAN_DRV_SJW_MAX;
    if (sjw   < 1                ) sjw   = 1;
    if (brp   < CAN_DRV_BRP_MIN  ) brp   = CAN_DRV_BRP_MIN;
    if (brp   > CAN_DRV_BRP_MAX  ) brp   = CAN_DRV_BRP_MAX;

    init.CAN_TTCM = DISABLE;
    init.CAN_ABOM = ENABLE;     /* automatic bus-off recovery — keeps RE silent */
    init.CAN_AWUM = DISABLE;
    init.CAN_NART = (mode_flags & GS_CAN_MODE_ONE_SHOT) ? ENABLE : DISABLE;
    init.CAN_RFLM = DISABLE;
    init.CAN_TXFP = ENABLE;     /* FIFO order TX so frames go on wire in submit order */

    if (mode_flags & GS_CAN_MODE_LOOP_BACK)
        init.CAN_Mode = (mode_flags & GS_CAN_MODE_LISTEN_ONLY) ? CAN_Mode_Silent_LoopBack
                                                               : CAN_Mode_LoopBack;
    else if (mode_flags & GS_CAN_MODE_LISTEN_ONLY)
        init.CAN_Mode = CAN_Mode_Silent;
    else
        init.CAN_Mode = CAN_Mode_Normal;

    init.CAN_SJW       = (uint8_t)(sjw   - 1);
    init.CAN_BS1       = (uint8_t)(tseg1 - 1);
    init.CAN_BS2       = (uint8_t)(tseg2 - 1);
    init.CAN_Prescaler = (uint16_t)brp;     /* StdPeriph subtracts 1 internally */

    CAN_DeInit(s_can[ch]);
    /* CAN_DeInit(CAN1) — and CAN_Init(CAN1) on chippackid 4..7 — pulses the
     * APB1 reset for the whole CAN1 register block, which is where the SHARED
     * filter banks and CAN2SB live. So after touching CAN1 we have to re-seat
     * the slave-start-bank AND restore any peer filter that was wiped. */
    CAN_SlaveStartBank(CAN_SLAVE_START_BANK);
    CAN_Init(s_can[ch], &init);
    apply_filter_accept_all(ch);
    if (ch == 0) {
        for (uint8_t i = 1; i < CAN_DRV_NUM_CHANNELS; i++)
            if (s_open[i]) apply_filter_accept_all(i);
    }

    CAN_ITConfig(s_can[ch], CAN_IT_FMP0, ENABLE);
    NVIC_EnableIRQ((ch == 0) ? USB_LP_CAN1_RX0_IRQn : CAN2_RX0_IRQn);

    s_open[ch] = 1;
    led_set_can_open(any_channel_open());
}

void can_drv_close(uint8_t ch)
{
    if (ch >= CAN_DRV_NUM_CHANNELS) return;
    if (!s_open[ch]) return;
    NVIC_DisableIRQ((ch == 0) ? USB_LP_CAN1_RX0_IRQn : CAN2_RX0_IRQn);
    CAN_ITConfig(s_can[ch], CAN_IT_FMP0, DISABLE);
    CAN_DeInit(s_can[ch]);
    s_open[ch] = 0;
    /* Closing CAN1 wipes the shared filter master + CAN2SB. Re-seat the slave
     * start bank and reinstall any peer filter so CAN2 keeps receiving. */
    if (ch == 0) {
        CAN_SlaveStartBank(CAN_SLAVE_START_BANK);
        for (uint8_t i = 1; i < CAN_DRV_NUM_CHANNELS; i++)
            if (s_open[i]) apply_filter_accept_all(i);
    }
    led_set_can_open(any_channel_open());
}

uint8_t can_drv_send(uint8_t ch, const struct gs_host_frame *frame)
{
    if (ch >= CAN_DRV_NUM_CHANNELS) return CAN_TxStatus_NoMailBox;
    if (!s_open[ch])                return CAN_TxStatus_NoMailBox;

    CanTxMsg msg = {0};
    uint32_t id  = frame->can_id;

    if (id & CAN_EFF_FLAG) {
        msg.IDE   = CAN_Id_Extended;
        msg.ExtId = id & CAN_EFF_MASK;
    } else {
        msg.IDE   = CAN_Id_Standard;
        msg.StdId = id & CAN_SFF_MASK;
    }
    msg.RTR = (id & CAN_RTR_FLAG) ? CAN_RTR_Remote : CAN_RTR_Data;
    msg.DLC = frame->can_dlc > 8 ? 8 : frame->can_dlc;
    for (uint8_t i = 0; i < msg.DLC; i++)
        msg.Data[i] = frame->data[i];

    return CAN_Transmit(s_can[ch], &msg);
}

/* Common ISR body — read everything pending from FIFO0 on the given peripheral
 * and stage host_frames into the gs_usb IN ring with the channel field set. */
static void can_rx0_isr_body(uint8_t ch)
{
    CAN_TypeDef *cx = s_can[ch];

    if (CAN_GetITStatus(cx, CAN_IT_FMP0) == RESET)
        return;

    while (CAN_MessagePending(cx, CAN_FIFO0) > 0) {
        CanRxMsg rx;
        struct gs_host_frame f = {0};

        CAN_Receive(cx, CAN_FIFO0, &rx);

        f.echo_id = 0xFFFFFFFFu;            /* unsolicited RX, not a TX echo */
        if (rx.IDE == CAN_Id_Extended)
            f.can_id = (rx.ExtId & CAN_EFF_MASK) | CAN_EFF_FLAG;
        else
            f.can_id = rx.StdId & CAN_SFF_MASK;
        if (rx.RTR == CAN_RTR_Remote)
            f.can_id |= CAN_RTR_FLAG;

        f.can_dlc = rx.DLC > 8 ? 8 : rx.DLC;
        f.channel = ch;
        f.flags   = 0;
        for (uint8_t i = 0; i < f.can_dlc; i++)
            f.data[i] = rx.Data[i];

        can_drv_on_rx(&f);
    }

    CAN_ClearITPendingBit(cx, CAN_IT_FMP0);
}

void USB_LP_CAN1_RX0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    can_rx0_isr_body(0);
}

void CAN2_RX0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void CAN2_RX0_IRQHandler(void)
{
    can_rx0_isr_body(1);
}
