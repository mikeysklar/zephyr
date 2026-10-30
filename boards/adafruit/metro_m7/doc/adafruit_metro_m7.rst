.. zephyr:board:: adafruit_metro_m7

Overview
********

The Adafruit Metro M7 with microSD is an Arduino-shaped board built around the
NXP i.MX RT1011 Cortex-M7 MCU, with 8 MB of QSPI flash, a microSD card slot and
a `STEMMA QT`_ connector for the I2C bus. The :zephyr:board:`adafruit_metro_m7_airlift`
is the same board with an ESP32 WiFi co-processor in place of the microSD slot.

Hardware
********

- i.MX RT1011 Arm Cortex-M7 at up to 500 MHz
- 128 KB of on-chip RAM (FlexRAM: 64 KB OCRAM, 32 KB ITCM, 32 KB DTCM)
- 8 MB Winbond W25Q64JV QSPI flash, used for execute-in-place
- microSD card slot on the SPI bus
- 1 red LED (D13)
- 1 NeoPixel RGB LED
- USB device port
- STEMMA QT I2C connector

Supported Features
==================

.. zephyr:board-supported-hw::

Connections and IOs
===================

The `Metro M7 with microSD Learn Guide`_ has the `pinouts`_ and the
`schematic`_.

=========  ============  ============================
Pin        i.MX pad      Function
=========  ============  ============================
D0, D1     GPIO_09, 10   LPUART1 RX, TX (console)
D13        GPIO_03       Red LED
SDA, SCL   GPIO_01, 02   LPI2C1
SCK        GPIO_AD_06    LPSPI1 (2x3 SPI header)
MOSI       GPIO_AD_04    LPSPI1
MISO       GPIO_AD_03    LPSPI1
NeoPixel   GPIO_00
SD_CS      GPIO_AD_14    microSD chip select
SD_CD      GPIO_AD_11    microSD card detect
=========  ============  ============================

The analog inputs A0-A5 are ADC1 channels 2, 1, 0, 5, 10 and 8. LPSPI1 has no
hardware chip select: its PCS0 pad, GPIO_AD_05, is A3.

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
   :board: adafruit_metro_m7
   :goals: flash

Connect a 3.3 V USB-to-serial adapter to D0 (RX), D1 (TX) and GND, open a
terminal at 115200 baud, and reset the board. You should see:

.. code-block:: console

   Hello World! adafruit_metro_m7/mimxrt1011

References
**********

.. target-notes::

.. _STEMMA QT:
   https://learn.adafruit.com/introducing-adafruit-stemma-qt

.. _Metro M7 with microSD Learn Guide:
   https://learn.adafruit.com/adafruit-metro-m7-microsd

.. _pinouts:
   https://learn.adafruit.com/adafruit-metro-m7-microsd/pinouts

.. _schematic:
   https://learn.adafruit.com/adafruit-metro-m7-microsd/downloads
