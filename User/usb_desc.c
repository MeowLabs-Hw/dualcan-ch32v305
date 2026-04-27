#include "usb_desc.h"

/* Total length of the configuration descriptor block:
 *   9 (config) + 9 (interface) + 7 (EP1 IN) + 7 (EP2 OUT) = 32
 */
#define GSUSB_CFG_TOTAL_LEN  32

const uint8_t GSUSB_DeviceDescr[] = {
    0x12,                       /* bLength */
    0x01,                       /* bDescriptorType (Device) */
    0x00, 0x02,                 /* bcdUSB 2.00 */
    0xFF,                       /* bDeviceClass — vendor specific */
    0xFF,                       /* bDeviceSubClass */
    0xFF,                       /* bDeviceProtocol */
    GSUSB_EP0_SIZE,             /* bMaxPacketSize0 */
    (uint8_t)GSUSB_VID, (uint8_t)(GSUSB_VID >> 8),
    (uint8_t)GSUSB_PID, (uint8_t)(GSUSB_PID >> 8),
    (uint8_t)GSUSB_BCD_DEVICE, (uint8_t)(GSUSB_BCD_DEVICE >> 8),
    GSUSB_STR_MFR,              /* iManufacturer */
    GSUSB_STR_PROD,             /* iProduct */
    GSUSB_STR_SERIAL,           /* iSerialNumber */
    0x01                        /* bNumConfigurations */
};

const uint8_t GSUSB_CfgDescr_HS[] = {
    /* Configuration descriptor */
    0x09,                       /* bLength */
    0x02,                       /* bDescriptorType (Configuration) */
    GSUSB_CFG_TOTAL_LEN, 0x00,  /* wTotalLength */
    0x01,                       /* bNumInterfaces */
    0x01,                       /* bConfigurationValue */
    0x00,                       /* iConfiguration */
    0x80,                       /* bmAttributes — bus-powered, no remote wakeup */
    0xFA,                       /* bMaxPower 500 mA (CAN transceiver + LED room) */

    /* Interface descriptor */
    0x09,                       /* bLength */
    0x04,                       /* bDescriptorType (Interface) */
    0x00,                       /* bInterfaceNumber */
    0x00,                       /* bAlternateSetting */
    0x02,                       /* bNumEndpoints */
    0xFF,                       /* bInterfaceClass — vendor specific */
    0xFF,                       /* bInterfaceSubClass */
    0xFF,                       /* bInterfaceProtocol */
    0x00,                       /* iInterface */

    /* Endpoint 1 IN — bulk, device → host CAN frames */
    0x07,                       /* bLength */
    0x05,                       /* bDescriptorType (Endpoint) */
    0x81,                       /* bEndpointAddress (IN, EP1) */
    0x02,                       /* bmAttributes (Bulk) */
    (uint8_t)GSUSB_BULK_HS_PACK_SIZE, (uint8_t)(GSUSB_BULK_HS_PACK_SIZE >> 8),
    0x00,                       /* bInterval (ignored for bulk) */

    /* Endpoint 2 OUT — bulk, host → device CAN frames */
    0x07,
    0x05,
    0x02,                       /* bEndpointAddress (OUT, EP2) */
    0x02,                       /* Bulk */
    (uint8_t)GSUSB_BULK_HS_PACK_SIZE, (uint8_t)(GSUSB_BULK_HS_PACK_SIZE >> 8),
    0x00
};

const uint8_t GSUSB_CfgDescr_FS[] = {
    0x09,
    0x02,
    GSUSB_CFG_TOTAL_LEN, 0x00,
    0x01, 0x01, 0x00, 0x80, 0xFA,

    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0x00,

    0x07, 0x05, 0x81, 0x02,
    (uint8_t)GSUSB_BULK_FS_PACK_SIZE, (uint8_t)(GSUSB_BULK_FS_PACK_SIZE >> 8),
    0x00,

    0x07, 0x05, 0x02, 0x02,
    (uint8_t)GSUSB_BULK_FS_PACK_SIZE, (uint8_t)(GSUSB_BULK_FS_PACK_SIZE >> 8),
    0x00
};

/* Device qualifier descriptor (USB 2.0 HS-capable device must provide this). */
const uint8_t GSUSB_QualifierDescr[] = {
    0x0A, 0x06,
    0x00, 0x02,                 /* bcdUSB 2.00 */
    0xFF, 0xFF, 0xFF,           /* class/subclass/protocol */
    GSUSB_EP0_SIZE,
    0x01,                       /* bNumConfigurations */
    0x00                        /* bReserved */
};

/* Other-speed configuration buffers — first two bytes are the descriptor type
 * marker for "other speed configuration" (0x07); the rest is filled at runtime
 * by memcpy()'ing the body of the opposite-speed config descriptor. */
uint8_t GSUSB_OtherSpeedCfg_HS[sizeof(GSUSB_CfgDescr_FS)] = { 0x09, 0x07 };
uint8_t GSUSB_OtherSpeedCfg_FS[sizeof(GSUSB_CfgDescr_HS)] = { 0x09, 0x07 };

const uint8_t GSUSB_StrLang[]  = { 0x04, 0x03, 0x09, 0x04 }; /* en-US */

const uint8_t GSUSB_StrMfr[]   = {
    0x16, 0x03,
    'M',0, 'u',0, 's',0, 'l',0, 'e',0, ' ',0, 'L',0, 'a',0, 'b',0, 's',0
};

const uint8_t GSUSB_StrProd[]  = {
    0x22, 0x03,
    'C',0, 'H',0, '3',0, '2',0, 'V',0, '3',0, '0',0, '5',0,
    ' ',0, 'g',0, 's',0, '_',0, 'u',0, 's',0, 'b',0
};

const uint8_t GSUSB_StrSerial[] = {
    0x1A, 0x03,
    '0',0, '0',0, '0',0, '0',0, '0',0, '0',0,
    '0',0, '0',0, '0',0, '0',0, '0',0, '1',0
};
