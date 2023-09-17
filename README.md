# esptic2udp

Implementation of [tic2json](http://hacks.slashdirt.org/sw/tic2json/) for ESP8266/ESP32.
Gets TIC data on RX pin, sends formatted JSON over UDP.

## License

GPLv2-only - http://www.gnu.org/licenses/gpl-2.0.html

Copyright: (C) 2021-2023 Thibaut VARÈNE

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2,
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See LICENSE.md for details

## Usage

### Configure the project

`idf.py menuconfig`

* Under Component config -> esptic2udp, set the following:
  * UART for receiving TIC frames and TIC baudrate
  * Optional GPIO number for LED heartbeat and LED active state
  * Optional GPIO number for TIC2UART enable and enable active state
  * Target UDP host and port
* Under Component config -> simple_network, configure as needed
* Under Component config -> tic2json, set TIC version and adjust options as needed
* Under Component config - > Common ESP-related, it is advised to move the console IO to a different UART than the one used by this app

### Build and Flash

Build the project and flash it to the board:

`idf.py flash`

See the Getting Started Guide for full steps to configure and use ESP-IDF to build projects.

Tested working on ESP8266 and ESP32.

### Connect

Use e.g. [TIC2UART](http://hacks.slashdirt.org/hw/tic2uart/) or [TICESP](http://hacks.slashdirt.org/hw/ticesp/) to connect the TIC output of the meter to the RX pin of your ESP board.
