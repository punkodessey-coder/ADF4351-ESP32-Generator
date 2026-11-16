# ADF4351 ESP32 Generator

Responsive Wi-Fi web controller for ADF4351 on **ESP32 DevKit V1 (38-pin)**.

## Connect from phone or PC
1. Power the ESP32.
2. Join Wi-Fi access point **ADF4351-ESP32** (password **adf4351rf**).
3. Open **http://192.168.4.2** in a browser.

The firmware configures a static SoftAP address with `WiFi.softAPConfig()`: IP and gateway `192.168.4.2`, subnet `255.255.255.0`. No router or internet connection is required.

## Wiring

| ADF4351 | ESP32 DevKit V1 |
|---|---|
| CLK | GPIO18 |
| DAT | GPIO23 |
| LE | GPIO27 |
| CE | GPIO17 |
| LD | GPIO16 |
| MUX | GPIO34 (input-only) |
| GND | GND |
| PDR | Leave unconnected |

## Hardware precautions
- Verify the module supply input, voltage and polarity before powering it.
- Do not assume the header marked `3V3` is a supply input; it may be an output. Do not connect it to ESP32 3V3 without checking the carrier schematic.
- Join ESP32 and ADF4351 grounds.
- Confirm the carrier board accepts 3.3 V logic on CLK/DAT/LE/CE. Bare ADF4351 digital pins use a lower-voltage domain; carrier boards differ in level translation.
- Never connect RF outputs to ESP32 GPIO. Use a suitable 50-ohm load or RF test equipment.
- The exact carrier-board electrical design has not been verified from its reverse side/schematic.

## Features
- Browser UI sized for mobile and desktop.
- Frequency setting (35–4400 MHz).
- RF output A enable/disable.
- Four nominal power codes (-4, -1, +2, +5 dBm; not calibrated).
- Sweep start/stop/step/interval.
- LD and MUX input status.
- Frequency and power saved in ESP32 NVS.

## Reference frequency
The board photo appears to show a 25.000 MHz reference oscillator. Firmware uses `REF_HZ=25000000.0`; verify the fitted reference before relying on frequency accuracy.

## Flash on Windows
Install Arduino CLI: https://arduino.github.io/arduino-cli/ and ensure it is in PATH. Install the appropriate USB-UART driver (often CP210x or CH340), connect the ESP32, run `FLASH.bat`, and enter the COM port. Select FQBN `esp32:esp32:esp32` (ESP32 Dev Module). If upload hangs at Connecting, hold BOOT as upload begins and release when writing starts.

## Validation status
Initial firmware, not bench-tested on the exact carrier board. Confirm supply, logic levels, reference routing, loop-filter and RF output design. Verify register settings and measured output using suitable RF test equipment. UI power values are nominal register settings, not calibrated measurements.
