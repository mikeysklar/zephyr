.. zephyr:board:: adafruit_clue_nrf52840

Overview
********

The Adafruit CLUE nRF52840 Express provides support for the Nordic
Semiconductor nRF52840 ARM Cortex-M4F CPU and the following devices:

* :abbr:`ADC (Analog to Digital Converter)`
* CLOCK
* FLASH
* :abbr:`GPIO (General Purpose Input Output)`
* :abbr:`I2C (Inter-Integrated Circuit)`
* :abbr:`MPU (Memory Protection Unit)`
* :abbr:`NVIC (Nested Vectored Interrupt Controller)`
* :abbr:`PWM (Pulse Width Modulation)`
* RADIO (Bluetooth Low Energy and 802.15.4)
* :abbr:`RTC (nRF RTC System Clock)`
* Segger RTT (RTT Console)
* :abbr:`SPI (Serial Peripheral Interface)`
* :abbr:`UART (Universal asynchronous receiver-transmitter)`
* :abbr:`USB (Universal Serial Bus)`
* :abbr:`WDT (Watchdog Timer)`

Hardware
********

- nRF52840 ARM Cortex-M4F processor at 64 MHz
- 1 MB flash memory and 256 KB of SRAM
- 1.3 inch 240x240 pixel IPS TFT color display (ST7789)
- 2 MB QSPI flash memory
- LSM6DS33 6-DoF accelerometer and gyroscope
- LIS3MDL magnetometer
- APDS9960 proximity, light, color, and gesture sensor
- SHT30 humidity sensor
- BMP280 temperature and barometric pressure sensor
- 2 user buttons (A and B)
- 1 white LED
- 2 NeoPixel LEDs
- Buzzer/speaker
- microSD card slot
- STEMMA QT connector

Supported Features
==================

.. zephyr:board-supported-hw::

Connections and IOs
===================

The `Adafruit CLUE nRF52840 Express Learn site`_ has detailed information
about the board including `pinouts`_ and the `schematic`_.

LED
---

* LED0 (white) = P1.01

Push buttons
------------

* Button A = P1.02
* Button B = P1.10
* RESET = P0.18

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Flashing
========

The CLUE nRF52840 Express ships with the `Adafruit nRF52 Bootloader`_ which
supports flashing using `UF2`_. This allows easy flashing of new images, but
does not support debugging the device.

#. Build the Zephyr kernel and the :zephyr:code-sample:`blinky` sample
   application.

   .. zephyr-app-commands::
      :zephyr-app: samples/basic/blinky
      :board: adafruit_clue_nrf52840/nrf52840/uf2
      :goals: build
      :compact:

#. Connect the board to your host computer using USB.

#. Tap the reset button twice quickly to enter bootloader mode. A mass
   storage device named ``CLUEBOOT`` should appear on the host. Ensure this
   is mounted.

#. Flash the image.

   .. zephyr-app-commands::
      :zephyr-app: samples/basic/blinky
      :board: adafruit_clue_nrf52840/nrf52840/uf2
      :goals: flash
      :compact:

#. You should see the white LED blink.

References
**********

.. target-notes::

.. _Adafruit CLUE nRF52840 Express Learn site:
    https://learn.adafruit.com/adafruit-clue

.. _pinouts:
    https://learn.adafruit.com/adafruit-clue/pinouts

.. _schematic:
    https://learn.adafruit.com/adafruit-clue/downloads

.. _Adafruit nRF52 Bootloader:
    https://github.com/adafruit/Adafruit_nRF52_Bootloader

.. _UF2:
    https://github.com/microsoft/uf2
