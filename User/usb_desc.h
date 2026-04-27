/*
 * USB descriptor table for the gs_usb / candleLight-compatible CAN adapter.
 *
 * Single configuration, single interface, vendor class (0xFF/0xFF/0xFF),
 * two bulk endpoints:
 *   EP1 IN  — device → host CAN frames (and TX-complete echoes)
 *   EP2 OUT — host → device CAN frames
 *
 * VID/PID 0x1d50:0x606f is the OpenMoko-allocated candleLight pair, which
 * is in the stock Linux gs_usb match table — kernel binds with no quirks.
 */
#ifndef __USB_DESC_H__
#define __USB_DESC_H__

#include <stdint.h>

#define GSUSB_VID                 0x1D50
#define GSUSB_PID                 0x606F
#define GSUSB_BCD_DEVICE          0x0100   /* 1.00 */

#define GSUSB_EP0_SIZE            64
#define GSUSB_BULK_HS_PACK_SIZE   512
#define GSUSB_BULK_FS_PACK_SIZE   64

/* String descriptor indices */
#define GSUSB_STR_LANG            0
#define GSUSB_STR_MFR             1
#define GSUSB_STR_PROD            2
#define GSUSB_STR_SERIAL          3

extern const uint8_t  GSUSB_DeviceDescr[];
extern const uint8_t  GSUSB_CfgDescr_HS[];
extern const uint8_t  GSUSB_CfgDescr_FS[];
extern const uint8_t  GSUSB_QualifierDescr[];
extern       uint8_t  GSUSB_OtherSpeedCfg_HS[]; /* filled at runtime by copying CfgDescr_FS body */
extern       uint8_t  GSUSB_OtherSpeedCfg_FS[]; /* filled at runtime by copying CfgDescr_HS body */
extern const uint8_t  GSUSB_StrLang[];
extern const uint8_t  GSUSB_StrMfr[];
extern const uint8_t  GSUSB_StrProd[];
extern const uint8_t  GSUSB_StrSerial[];

#define GSUSB_DEV_DESC_LEN        ((uint16_t)GSUSB_DeviceDescr[0])
#define GSUSB_CFG_HS_DESC_LEN     ((uint16_t)((GSUSB_CfgDescr_HS[3] << 8) | GSUSB_CfgDescr_HS[2]))
#define GSUSB_CFG_FS_DESC_LEN     ((uint16_t)((GSUSB_CfgDescr_FS[3] << 8) | GSUSB_CfgDescr_FS[2]))
#define GSUSB_QUALIFIER_DESC_LEN  ((uint16_t)GSUSB_QualifierDescr[0])
#define GSUSB_STR_LANG_LEN        ((uint16_t)GSUSB_StrLang[0])
#define GSUSB_STR_MFR_LEN         ((uint16_t)GSUSB_StrMfr[0])
#define GSUSB_STR_PROD_LEN        ((uint16_t)GSUSB_StrProd[0])
#define GSUSB_STR_SERIAL_LEN      ((uint16_t)GSUSB_StrSerial[0])

#endif /* __USB_DESC_H__ */
