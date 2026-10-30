.. zephyr:board:: adafruit_metro_m7_airlift

Overview
********

The Adafruit Metro M7 with AirLift is an Arduino-shaped board built around the
NXP i.MX RT1011 Cortex-M7 MCU, with 8 MB of QSPI flash, an ESP32 WiFi
co-processor (AirLift) and a `STEMMA QT`_ connector for the I2C bus. It is the
:zephyr:board:`adafruit_metro_m7` with the ESP32 in place of the microSD slot.

Hardware
********

- i.MX RT1011 Arm Cortex-M7 at up to 500 MHz
- 128 KB of on-chip RAM (FlexRAM: 64 KB OCRAM, 32 KB ITCM, 32 KB DTCM)
- 8 MB Winbond W25Q64JV QSPI flash, used for execute-in-place
- ESP32 WiFi co-processor on the SPI bus
- 1 red LED (D13)
- 1 NeoPixel RGB LED
- USB device port
- STEMMA QT I2C connector

Supported Features
==================

.. zephyr:board-supported-hw::

Zephyr has no driver for the AirLift's ESP32 firmware; the ESP32 control pins
are not described in the devicetree.

Connections and IOs
===================

The `Metro M7 with AirLift Learn Guide`_ has the `pinouts`_ and the
`schematic`_.

==========  ============  ============================
Pin         i.MX pad      Function
==========  ============  ============================
D0, D1      GPIO_09, 10   LPUART1 RX, TX (console)
D13         GPIO_03       Red LED
SDA, SCL    GPIO_01, 02   LPI2C1
SCK         GPIO_AD_06    LPSPI1 (2x3 SPI header)
MOSI        GPIO_AD_04    LPSPI1
MISO        GPIO_AD_03    LPSPI1
NeoPixel    GPIO_00
ESP_CS      GPIO_AD_14    ESP32 chip select
ESP_BUSY    GPIO_AD_11    ESP32 busy
ESP_RESET   GPIO_AD_07    ESP32 reset
ESP_GPIO0   GPIO_SD_05    ESP32 GPIO0
==========  ============  ============================

D0 and D1 are also the ESP32's UART. The analog inputs A0-A5 are ADC1
channels 2, 1, 0, 5, 10 and 8. LPSPI1 has no hardware chip select: its PCS0
pad, GPIO_AD_05, is A3.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

The board ships with a UF2 bootloader. To run Zephyr, connect an SWD debug
probe such as a J-Link and flash with the ``jlink`` or ``pyocd`` runner. This
replaces the UF2 bootloader.

The two boot-mode switches select between booting from the QSPI flash and the
ROM serial downloader. Set them to boot from flash to run Zephyr.

Flashing
========

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: adafruit_metro_m7_airlift
   :goals: flash

Connect a 3.3 V USB-to-serial adapter to D0 (RX), D1 (TX) and GND, open a
terminal at 115200 baud, and reset the board. You should see:

.. code-block:: console

   Hello World! adafruit_metro_m7_airlift/mimxrt1011

References
**********

.. target-notes::

.. _STEMMA QT:
   https://learn.adafruit.com/introducing-adafruit-stemma-qt

.. _Metro M7 with AirLift Learn Guide:
   https://learn.adafruit.com/adafruit-metro-m7-with-airlift

.. _pinouts:
   https://learn.adafruit.com/adafruit-metro-m7-with-airlift/pinouts

.. _schematic:
   https://learn.adafruit.com/adafruit-metro-m7-with-airlift/downloads
