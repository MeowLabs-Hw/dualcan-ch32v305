#include <string.h>
#include "usbhs_dev.h"
#include "usb_desc.h"
#include "gs_can.h"
#include "ch32v30x_usb.h"
#include "debug.h"

/* ---------------- Endpoint buffers ----------------
 * UEP0 is shared TX/RX (DMA == &EP0_buf[0], 64 bytes is enough for our largest
 * SETUP response chunk, since EP0 max packet = 64 in HS *and* FS for us).
 * EP1 IN  buffer holds one outgoing host_frame at a time (20 B for classic CAN,
 * but we size to GSUSB_BULK_HS_PACK_SIZE so future framings still fit).
 * EP2 OUT buffer holds one incoming host_frame.
 *
 * USBHS DMA addresses are direct SRAM pointers so no special section is
 * needed — only 4-byte alignment, which `__attribute__((aligned(4)))` ensures.
 */
__attribute__((aligned(4))) static uint8_t s_ep0_buf[GSUSB_EP0_SIZE];
__attribute__((aligned(4))) static uint8_t s_ep1_in_buf[GSUSB_BULK_HS_PACK_SIZE];
__attribute__((aligned(4))) static uint8_t s_ep2_out_buf[GSUSB_BULK_HS_PACK_SIZE];

/* ---------------- Device state ---------------- */
static volatile uint8_t  s_dev_addr;
static volatile uint8_t  s_dev_config;
static volatile uint8_t  s_dev_speed;          /* USBHS_SPEED_HIGH / FULL */
static volatile uint8_t  s_dev_configured;     /* set true after SET_CONFIGURATION */
static volatile uint8_t  s_ep1_in_busy;        /* one IN at a time */

/* SETUP-state cache, valid only between SETUP and STATUS stages of one ctl xfer. */
static volatile uint8_t  s_setup_bmReqType;
static volatile uint8_t  s_setup_bRequest;
static volatile uint16_t s_setup_wValue;
static volatile uint16_t s_setup_wIndex;
static volatile uint16_t s_setup_wLength;

/* For OUT vendor requests we accumulate the payload in EP0 buf until the
 * STATUS stage; the largest gs_usb OUT payload is sizeof(struct gs_device_bittiming)
 * = 20 bytes, so EP0_SIZE is plenty. */
static volatile uint16_t s_setup_out_received;

/* For IN responses (descriptors or vendor data) we may need to fragment
 * across multiple EP0 IN transactions; track what's left to send. */
static const uint8_t * s_setup_in_ptr;
static          uint16_t s_setup_in_remaining;

/* --------------------------------------------------------------------- */

void usbhs_dev_rcc_init(void)
{
    /* Sequence verified against WCH CH372Device EVT example. The CKREF=4M
     * setting works on 8 MHz HSE — there's an implicit /2 ahead of the PLL. */
    RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);
    RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
    RCC_USBHSConfig(RCC_USBPLL_Div2);
    RCC_USBHSPLLCKREFCLKConfig(RCC_USBHSPLLCKREFCLK_4M);
    RCC_USBHSPHYPLLALIVEcmd(ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
}

static void usbhs_endp_init(void)
{
    /* Enable EP1 TX (IN) and EP2 RX (OUT). EP0 is implicit. */
    USBHSD->ENDP_CONFIG = USBHS_UEP1_T_EN | USBHS_UEP2_R_EN;

    USBHSD->UEP0_MAX_LEN = GSUSB_EP0_SIZE;
    USBHSD->UEP1_MAX_LEN = GSUSB_BULK_HS_PACK_SIZE;
    USBHSD->UEP2_MAX_LEN = GSUSB_BULK_HS_PACK_SIZE;

    USBHSD->UEP0_DMA    = (uint32_t)s_ep0_buf;
    USBHSD->UEP1_TX_DMA = (uint32_t)s_ep1_in_buf;
    USBHSD->UEP2_RX_DMA = (uint32_t)s_ep2_out_buf;

    USBHSD->UEP0_TX_LEN  = 0;
    USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_ACK;

    USBHSD->UEP1_TX_LEN  = 0;
    USBHSD->UEP1_TX_CTRL = USBHS_UEP_T_RES_NAK;

    USBHSD->UEP2_RX_CTRL = USBHS_UEP_R_RES_ACK;

    s_ep1_in_busy = 0;
}

void usbhs_dev_init(void)
{
    USBHSD->CONTROL = USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
    Delay_Us(10);
    USBHSD->CONTROL &= ~USBHS_UC_RESET_SIE;
    USBHSD->HOST_CTRL = USBHS_UH_PHY_SUSPENDM;
    USBHSD->CONTROL = USBHS_UC_DMA_EN | USBHS_UC_INT_BUSY | USBHS_UC_SPEED_HIGH;
    USBHSD->INT_EN  = USBHS_UIE_SETUP_ACT | USBHS_UIE_TRANSFER |
                      USBHS_UIE_DETECT    | USBHS_UIE_SUSPEND;

    usbhs_endp_init();

    /* Enable D+ pull-up *last* — once this fires the host begins enumeration. */
    USBHSD->CONTROL |= USBHS_UC_DEV_PU_EN;
    NVIC_EnableIRQ(USBHS_IRQn);
}

uint8_t usbhs_dev_is_configured(void)
{
    return s_dev_configured;
}

uint8_t usbhs_dev_speed(void)
{
    return s_dev_speed;
}

uint8_t usbhs_dev_send_frame(const struct gs_host_frame *frame)
{
    if (!s_dev_configured) return 1;
    if (s_ep1_in_busy)     return 1;

    memcpy(s_ep1_in_buf, frame, GS_HOST_FRAME_SIZE);
    USBHSD->UEP1_TX_LEN  = (uint16_t)GS_HOST_FRAME_SIZE;
    USBHSD->UEP1_TX_CTRL = (USBHSD->UEP1_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK;
    s_ep1_in_busy = 1;
    return 0;
}

void usbhs_dev_rearm_bulk_out(void)
{
    USBHSD->UEP2_RX_CTRL = (USBHSD->UEP2_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK;
}

/* --------------------------------------------------------------------- */
/*                       EP0 standard request handler                    */
/* --------------------------------------------------------------------- */

static void ep0_stall(void)
{
    USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_STALL;
    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_STALL;
}

/* Stage `len` bytes from `s_setup_in_ptr` into the EP0 TX buffer and arm IN.
 * Caller must have set s_setup_in_ptr / s_setup_in_remaining. */
static void ep0_tx_chunk(void)
{
    uint16_t n = s_setup_in_remaining > GSUSB_EP0_SIZE
                     ? GSUSB_EP0_SIZE
                     : s_setup_in_remaining;
    if (n) memcpy(s_ep0_buf, s_setup_in_ptr, n);
    s_setup_in_ptr       += n;
    s_setup_in_remaining -= n;
    USBHSD->UEP0_TX_LEN   = n;
    USBHSD->UEP0_TX_CTRL  = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
}

static uint8_t handle_get_descriptor(void)
{
    uint8_t  type = (uint8_t)(s_setup_wValue >> 8);
    uint16_t len  = 0;
    const uint8_t *src = NULL;

    switch (type) {
    case USB_DESCR_TYP_DEVICE:
        src = GSUSB_DeviceDescr;
        len = GSUSB_DEV_DESC_LEN;
        break;

    case USB_DESCR_TYP_CONFIG:
        if ((USBHSD->SPEED_TYPE & 0x03) == 0x01) {
            s_dev_speed = 0x01;             /* HS */
            src = GSUSB_CfgDescr_HS;
            len = GSUSB_CFG_HS_DESC_LEN;
        } else {
            s_dev_speed = 0x00;             /* FS (or LS, but we never enumerate LS) */
            src = GSUSB_CfgDescr_FS;
            len = GSUSB_CFG_FS_DESC_LEN;
        }
        break;

    case USB_DESCR_TYP_STRING:
        switch ((uint8_t)(s_setup_wValue & 0xFF)) {
        case GSUSB_STR_LANG:   src = GSUSB_StrLang;   len = GSUSB_STR_LANG_LEN;   break;
        case GSUSB_STR_MFR:    src = GSUSB_StrMfr;    len = GSUSB_STR_MFR_LEN;    break;
        case GSUSB_STR_PROD:   src = GSUSB_StrProd;   len = GSUSB_STR_PROD_LEN;   break;
        case GSUSB_STR_SERIAL: src = GSUSB_StrSerial; len = GSUSB_STR_SERIAL_LEN; break;
        default: return 1;
        }
        break;

    case USB_DESCR_TYP_QUALIF:
        src = GSUSB_QualifierDescr;
        len = GSUSB_QUALIFIER_DESC_LEN;
        break;

    case USB_DESCR_TYP_SPEED:
        /* Other-speed configuration: serve the body of the *opposite* speed's
         * config descriptor with the descriptor type byte rewritten to 0x07. */
        if (s_dev_speed == 0x01) {
            memcpy(&GSUSB_OtherSpeedCfg_HS[2], &GSUSB_CfgDescr_FS[2],
                   GSUSB_CFG_FS_DESC_LEN - 2);
            src = GSUSB_OtherSpeedCfg_HS;
            len = GSUSB_CFG_FS_DESC_LEN;
        } else {
            memcpy(&GSUSB_OtherSpeedCfg_FS[2], &GSUSB_CfgDescr_HS[2],
                   GSUSB_CFG_HS_DESC_LEN - 2);
            src = GSUSB_OtherSpeedCfg_FS;
            len = GSUSB_CFG_HS_DESC_LEN;
        }
        break;

    default:
        return 1;
    }

    if (s_setup_wLength < len) len = s_setup_wLength;
    s_setup_in_ptr       = src;
    s_setup_in_remaining = len;
    ep0_tx_chunk();
    return 0;
}

/* --------------------------------------------------------------------- */
/*                              SETUP handler                            */
/* --------------------------------------------------------------------- */

static void handle_setup(void)
{
    PUSB_SETUP_REQ pkt = (PUSB_SETUP_REQ)s_ep0_buf;
    s_setup_bmReqType = pkt->bRequestType;
    s_setup_bRequest  = pkt->bRequest;
    s_setup_wValue    = pkt->wValue;
    s_setup_wIndex    = pkt->wIndex;
    s_setup_wLength   = pkt->wLength;
    s_setup_out_received = 0;
    s_setup_in_ptr = NULL;
    s_setup_in_remaining = 0;

    uint8_t fail = 0;

    if ((s_setup_bmReqType & USB_REQ_TYP_MASK) == USB_REQ_TYP_VENDOR) {
        /* gs_usb requests. IN-direction returns a buffer immediately;
         * OUT-direction is deferred until the data payload finishes. */
        if (s_setup_bmReqType & USB_REQ_TYP_IN) {
            const uint8_t *resp = NULL;
            uint16_t resp_len = 0;
            if (gs_can_handle_vendor_setup_in(s_setup_bRequest, s_setup_wValue,
                                              s_setup_wIndex, s_setup_wLength,
                                              &resp, &resp_len) != 0) {
                fail = 1;
            } else {
                if (resp_len > s_setup_wLength) resp_len = s_setup_wLength;
                s_setup_in_ptr       = resp;
                s_setup_in_remaining = resp_len;
            }
        }
        /* OUT direction with payload: do nothing here, EP0 OUT handler will
         * accumulate `wLength` bytes and then call gs_can_handle_vendor_setup_out().
         * OUT direction with wLength==0: complete in the STATUS stage below. */
    } else if ((s_setup_bmReqType & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD) {
        switch (s_setup_bRequest) {
        case USB_GET_DESCRIPTOR:
            if (handle_get_descriptor() != 0) {
                fail = 1;
                break;
            }
            return;                              /* ep0_tx_chunk already armed TX */

        case USB_SET_ADDRESS:
            s_dev_addr = (uint8_t)(s_setup_wValue & 0x7F);
            /* Address takes effect after the STATUS-IN of this transfer. */
            break;

        case USB_GET_CONFIGURATION:
            s_ep0_buf[0] = s_dev_config;
            s_setup_in_ptr       = s_ep0_buf;
            s_setup_in_remaining = 1;
            break;

        case USB_SET_CONFIGURATION:
            s_dev_config     = (uint8_t)(s_setup_wValue & 0xFF);
            s_dev_configured = (s_dev_config != 0);
            if (s_dev_configured) gs_can_on_enum();
            break;

        case USB_GET_INTERFACE:
            s_ep0_buf[0] = 0;
            s_setup_in_ptr       = s_ep0_buf;
            s_setup_in_remaining = 1;
            break;

        case USB_SET_INTERFACE:
            break;

        case USB_GET_STATUS:
            s_ep0_buf[0] = 0;
            s_ep0_buf[1] = 0;
            s_setup_in_ptr       = s_ep0_buf;
            s_setup_in_remaining = 2;
            break;

        case USB_CLEAR_FEATURE:
        case USB_SET_FEATURE:
            /* Endpoint halts: clear stall on EP1 IN / EP2 OUT if requested. */
            if ((s_setup_bmReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP &&
                (uint8_t)(s_setup_wValue & 0xFF) == USB_REQ_FEAT_ENDP_HALT) {
                uint8_t ep = (uint8_t)(s_setup_wIndex & 0x8F);
                if (ep == 0x81) {                    /* EP1 IN */
                    USBHSD->UEP1_TX_CTRL =
                        (s_setup_bRequest == USB_SET_FEATURE)
                            ? ((USBHSD->UEP1_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_STALL)
                            : USBHS_UEP_T_RES_NAK;
                } else if (ep == 0x02) {             /* EP2 OUT */
                    USBHSD->UEP2_RX_CTRL =
                        (s_setup_bRequest == USB_SET_FEATURE)
                            ? ((USBHSD->UEP2_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_STALL)
                            : USBHS_UEP_R_RES_ACK;
                } else {
                    fail = 1;
                }
            }
            break;

        default:
            fail = 1;
            break;
        }
    } else {
        fail = 1;       /* CLASS or RESERVED — gs_usb doesn't use these */
    }

    if (fail) { ep0_stall(); return; }

    /* Common reply path. */
    if (s_setup_bmReqType & USB_REQ_TYP_IN) {
        /* IN direction: send first chunk (or zero-length data). */
        ep0_tx_chunk();
    } else {
        /* OUT direction. */
        if (s_setup_wLength == 0) {
            /* Status stage: send zero-length DATA1 IN. */
            USBHSD->UEP0_TX_LEN  = 0;
            USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
        } else {
            /* Wait for OUT data to come in on EP0; arm RX. */
            USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;
        }
    }
}

/* --------------------------------------------------------------------- */
/*                          IRQ handler                                  */
/* --------------------------------------------------------------------- */

void USBHS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USBHS_IRQHandler(void)
{
    uint8_t intflag = USBHSD->INT_FG;
    uint8_t intst   = USBHSD->INT_ST;

    if (intflag & USBHS_UIF_TRANSFER) {
        uint8_t token = intst & USBHS_UIS_TOKEN_MASK;
        uint8_t ep    = intst & USBHS_UIS_ENDP_MASK;

        if (token == USBHS_UIS_TOKEN_IN) {
            if (ep == 0) {
                /* EP0 IN finished. If more descriptor data to send, send it.
                 * If this was a SET_ADDRESS status stage, latch the address. */
                if (s_setup_in_remaining > 0) {
                    ep0_tx_chunk();
                } else {
                    /* End of data IN — switch to STATUS-OUT (RX) */
                    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;
                }
                if ((s_setup_bmReqType & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD &&
                     s_setup_bRequest == USB_SET_ADDRESS) {
                    USBHSD->DEV_AD = s_dev_addr;
                }
            } else if (ep == 1) {
                /* EP1 IN bulk frame done — let gs_usb queue the next one. */
                USBHSD->UEP1_TX_CTRL = (USBHSD->UEP1_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;
                USBHSD->UEP1_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
                s_ep1_in_busy = 0;
                gs_can_handle_bulk_in_complete();
            }
        } else if (token == USBHS_UIS_TOKEN_OUT) {
            if (ep == 0) {
                if (intst & USBHS_UIS_TOG_OK) {
                    uint16_t rxlen = USBHSD->RX_LEN;
                    if ((s_setup_bmReqType & USB_REQ_TYP_MASK) == USB_REQ_TYP_VENDOR &&
                        !(s_setup_bmReqType & USB_REQ_TYP_IN)) {
                        /* All gs_usb classic-CAN OUT payloads are ≤20 bytes
                         * (gs_device_bittiming), well under EP0_SIZE=64, so they
                         * land in a single DATA1 packet at the start of s_ep0_buf.
                         * If a host ever sends >64 bytes we just stall — accumulating
                         * across chunks would need a separate buffer. */
                        s_setup_out_received += rxlen;
                        if (s_setup_wLength > sizeof(s_ep0_buf)) {
                            ep0_stall();
                            USBHSD->INT_FG = USBHS_UIF_TRANSFER;
                            return;
                        }
                        if (s_setup_out_received >= s_setup_wLength) {
                            (void)gs_can_handle_vendor_setup_out(
                                s_setup_bRequest, s_setup_wValue, s_setup_wIndex,
                                s_ep0_buf, s_setup_wLength);
                            /* Status IN */
                            USBHSD->UEP0_TX_LEN  = 0;
                            USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
                        } else {
                            USBHSD->UEP0_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
                        }
                    } else {
                        /* Standard request OUT data (rare for our descriptor set). */
                        USBHSD->UEP0_TX_LEN  = 0;
                        USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
                    }
                }
            } else if (ep == 2) {
                if (intst & USBHS_UIS_TOG_OK) {
                    uint16_t rxlen = USBHSD->RX_LEN;
                    USBHSD->UEP2_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
                    if (rxlen >= GS_HOST_FRAME_SIZE) {
                        /* NAK further OUTs until the gs_usb layer has consumed
                         * this one and re-armed the endpoint. */
                        USBHSD->UEP2_RX_CTRL = (USBHSD->UEP2_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_NAK;
                        gs_can_handle_bulk_out((const struct gs_host_frame *)s_ep2_out_buf);
                    }
                }
            }
        }

        USBHSD->INT_FG = USBHS_UIF_TRANSFER;
    }
    else if (intflag & USBHS_UIF_SETUP_ACT) {
        USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_NAK;
        USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_NAK;
        handle_setup();
        USBHSD->INT_FG = USBHS_UIF_SETUP_ACT;
    }
    else if (intflag & USBHS_UIF_BUS_RST) {
        s_dev_addr       = 0;
        s_dev_config     = 0;
        s_dev_configured = 0;
        USBHSD->DEV_AD   = 0;
        usbhs_endp_init();
        USBHSD->INT_FG = USBHS_UIF_BUS_RST;
    }
    else if (intflag & USBHS_UIF_SUSPEND) {
        USBHSD->INT_FG = USBHS_UIF_SUSPEND;
    }
    else {
        USBHSD->INT_FG = intflag;
    }
}
