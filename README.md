# dualcan-ch32v305

Dual-channel CAN to USB interface firmware for the CH32V305RBT6.

Implements the [gs_usb](https://github.com/candle-usb/candleLight_fw) protocol,
making the device appear as a standard `gs_usb` interface to the Linux SocketCAN
stack — compatible with `can-utils`, SavvyCAN, python-can, and any other tool
that supports candleLight-style USB-CAN adapters.

## Features

- 2× independent CAN channels
- Classic CAN, up to 1 Mbit/s
- USB Full-Speed, gs_usb compatible
- SocketCAN integration on Linux (no driver install required on modern kernels)
- Configurable bitrates per channel
- Hardware error frame reporting

## Hardware

- **MCU:** CH32V305RBT6 (RISC-V Qingke V4F, 144 MHz)
- **Transceivers:** 2× SN65HVD230 (or compatible 3.3V CAN transceivers)
- **USB:** Full-Speed device, USB-C
- **Crystal:** 8 MHz HSE

Hardware design files will be published in the future.

## Building

Project is built with [MounRiver Studio](http://www.mounriver.com/) (MRS), the
official WCH RISC-V IDE.

1. Clone the repository
2. Open the project in MRS (`File → Open Project`)
3. Build (`Project → Build Project`)
4. Flash with WCH-LinkE via `Run → Download`

## Usage on Linux

Plug the device in. Two `can` interfaces appear:

```sh
ip link
# can0, can1

sudo ip link set can0 up type can bitrate 500000
sudo ip link set can1 up type can bitrate 500000

candump can0
cansend can1 123#DEADBEEF
```

## Status

- **Firmware:** Complete and tested
- **Hardware:** Design files in progress, to be released

## License

Firmware: MIT
Hardware design files (when released): CERN-OHL-P-2.0
