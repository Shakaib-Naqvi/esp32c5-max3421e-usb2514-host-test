# ESP32-C5 MAX3421E SPI Initialization Test

Minimal ESP-IDF project that verifies SPI communication between an ESP32-C5
and a MAX3421E USB host controller. It reads the MAX3421E revision register,
resets the controller, waits for its oscillator to become ready, and enables
USB host mode with the D+/D- pull-downs.

The intended hardware path is ESP32-C5 -> MAX3421E -> USB2514 USB hub. The
USB2514 is downstream of the MAX3421E on USB D+/D-; it is not controlled over
SPI by this example.

## Tested wiring

| ESP32-C5 | MAX3421E module | Purpose |
|---|---|---|
| GPIO7 | MOSI | SPI controller-to-peripheral data |
| GPIO1 | MISO | SPI peripheral-to-controller data |
| GPIO14 | SCLK | SPI clock |
| GPIO9 | CS / SS | Active-low chip select |
| GND | GND | Common ground |

Connect the MAX3421E USB D+ and D- lines to the USB2514 upstream port as
required by the hub hardware design. Power the MAX3421E module and USB2514
according to their board schematics; their supply, clock, reset, and USB power
requirements are outside this SPI-only example.

## ESP-IDF compatibility

The project targets ESP32-C5 and uses the ESP-IDF SPI master driver API.
ESP-IDF v5.4 or newer is recommended because ESP32-C5 support is included in
those releases.

## Build, flash, and monitor

From an ESP-IDF terminal:

```sh
idf.py set-target esp32c5
idf.py build
idf.py -p PORT flash monitor
```

Replace `PORT` with the board's serial port. Exit the monitor with `Ctrl+]`.

## Expected output

The usual ESP-IDF boot messages are followed by lines similar to:

```text
I (...) MAX3421E: ESP32-C5 MAX3421E SPI initialization test
I (...) MAX3421E: Initializing SPI bus and MAX3421E device
I (...) MAX3421E: SPI bus/device initialized
I (...) MAX3421E: MAX3421E revision register: 0x13
I (...) MAX3421E: MAX3421E oscillator ready
I (...) MAX3421E: MAX3421E initialized successfully
```

The revision value can vary with the MAX3421E silicon revision. An SPI error or
oscillator timeout is reported with an `E` log line.

## Limitation

This project only initializes the MAX3421E and confirms register-level SPI
communication. It does not enumerate the USB2514 or attached devices, provide
HID support, schedule USB transfers, or implement an equivalent of Arduino
`Usb.Task()`.
