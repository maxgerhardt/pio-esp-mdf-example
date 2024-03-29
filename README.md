# PlatformIO ESP-MDF example V2

## Description

This project provides a base example for the usage of ESP-MDF within PlatformIO. This example superseedes https://github.com/maxgerhardt/pio-mdf-example.

## Configuring

Refer to [esp-mdf](https://github.com/espressif/esp-mdf/tree/master/examples/get-started). This is the "get started" example, which lets two or more nodes communicate with each other. There is one root node and 1 or more non-root nodes.

To configure a device to be a root or non-root device, execute the menuconfig configuration per [docs](https://docs.platformio.org/en/latest/frameworks/espidf.html#configuration). Also see possible [caveat](https://github.com/platformio/platform-espressif32/issues/423) for menu navigation.

All other configurable settings for the components reside there, too, e.g. for ([mcommon](https://github.com/espressif/esp-mdf/blob/master/components/mcommon/Kconfig) or [mwifi](https://github.com/espressif/esp-mdf/blob/master/components/mwifi/Kconfig))

## Codebase

All ESP-MDF code present is a momentary snapshot at [esp-mdf@354d0bf](https://github.com/espressif/esp-mdf/commit/354d0bf687722570d2c22a71798a72ba17951030) (17 Oct 2023).

## License

All ESP-MDF code has the 'ESPRESSIF MIT License'.
