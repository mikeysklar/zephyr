.. zephyr:board:: adafruit_metro_ch32v407

Overview
********

The Adafruit Metro CH32V407 is an Arduino-shaped board built around the WCH
CH32V407 RISC-V MCU, with 4 MB of SPI flash and a USB-C port. The
:zephyr:board:`adafruit_metro_ch32v467` is the same board with a CH32V467.

Hardware
********

- CH32V407 QingKe V3V RISC-V core at 200 MHz, from a 25 MHz crystal
- 200 KB of RAM
- 992 KB of on-chip flash, the first 64 KB hold the bootloader
- 4 MB GD25Q32 SPI flash
- 1 red LED (D13)
- 1 user button
- USB-C device port (full speed)

The NeoPixel, the microSD slot and Ethernet are not supported yet.

Supported Features
==================

.. zephyr:board-supported-hw::

Connections and IOs
===================

==========  =========  ==========================
Pin         MCU pin    Function
==========  =========  ==========================
D0, D1      PB7, PB6   USART1 RX, TX (console)
D13         PB12       Red LED
SCK         PA5        SPI1 (2x3 SPI header)
MOSI        PA7        SPI1
MISO        PA6        SPI1
USER        PC15       User button, active low
FLASH_CS    PA15       SPI flash chip select
==========  =========  ==========================

The SPI flash is on SPI3: SCK on PB3, MISO on PB4 and MOSI on PB5.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

The board ships with adaboot, an MCUboot based bootloader with a UF2 drive, in
the first 64 KB of flash. Applications are linked into its single slot at
``0x10000``. To load an application, build an MCUboot image, wrap it in a UF2
and copy it to the bootloader drive.

Flashing
========

Build with the MCUboot single-app settings:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: adafruit_metro_ch32v407
   :goals: build
   :gen-args: -DCONFIG_BOOTLOADER_MCUBOOT=y -DCONFIG_MCUBOOT_BOOTLOADER_MODE_SINGLE_APP=y -DCONFIG_MCUBOOT_GENERATE_UNSIGNED_IMAGE=y

Wrap the image in a UF2 with the adaboot family ID and the slot offset:

.. code-block:: console

   python3 scripts/build/uf2conv.py build/zephyr/zephyr.signed.bin \
       -c -b 0x10000 -f 0xf4b47471 -o hello_world.uf2

Double-tap the reset button. The D13 LED fades in and out and a drive named
``ADABOOT`` appears. Copy ``hello_world.uf2`` to it. The board resets and runs
the application.

``west flash`` with the ``uf2`` runner copies ``build/zephyr/zephyr.uf2``. That
file has no MCUboot header, so the bootloader does not start it.

The console is USART1 on D0 (RX) and D1 (TX) at 115200 baud.
