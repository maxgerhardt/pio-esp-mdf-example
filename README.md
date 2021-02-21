# PlatformIO ESP-MDF example V2

## Description 

This project provides a base example for the usage of ESP-MDF within PlatformIO. This example superseedes https://github.com/maxgerhardt/pio-mdf-example. 

## Configuring 

Refer to [esp-mdf](https://github.com/espressif/esp-mdf/tree/master/examples/get-started). This is the "get started" example, which lets two or more nodes communicate with each other. There is one root node and 1 or more non-root nodes. 

To configure a device to be a root or non-root device, change the `build_flags` in the `platformio.ini` for the respective environment.

All other configurable settings for the components reside there, too, e.g. for [mcommon](https://github.com/espressif/esp-mdf/blob/master/components/mcommon/Kconfig) or [mwifi](https://github.com/espressif/esp-mdf/blob/master/components/mwifi/Kconfig)

## Codebase

All ESP-MDF code present is a momentary snapshot at [esp-mdf@f77e318](https://github.com/espressif/esp-mdf/commit/f77e31855ed7448f5f5212937e5a33485a76a932) (20 Feb 2021).

## License

All ESP-MDF code has the 'ESPRESSIF MIT License'.