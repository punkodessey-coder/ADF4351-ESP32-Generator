# ADF4351 ESP32 Generator

Responsive Wi-Fi web controller for ADF4351 on ESP32 DevKit V1 (38-pin).

## Connect
1. Power the ESP32.
2. Join access point **ADF4351-ESP32**, password **adf4351rf**.
3. Open **http://192.168.4.2**.

Static AP IP and gateway: 192.168.4.2/24. No router or internet is required.

## Wiring

| ADF4351 carrier | ESP32 DevKit V1 |
|---|---|
| CLK | GPIO18 |
| DAT | GPIO23 |
| LE | GPIO27 |
| CE | GPIO17 |
| LD | GPIO16 |
| MUX | GPIO34 (input only) |
| GND | GND |
| PDR | Leave unconnected |

## UI features
- **Generator:** frequency 35–4400 MHz, RF output enable, four nominal power codes, frequency sweep.
- **Presets:** eight non-volatile slots storing frequency and power.
- **Monitoring:** PLL LD state, MUX input, serial write counter, time since last write, LD transitions, Wi-Fi client count, uptime and free heap.
- **Settings:** software serial clock timing and heuristic auto-selection.
- Frequency, power, clock setting and presets are stored in ESP32 NVS.

## Serial clock and diagnostics limitations
ADF4351 uses a write-only 3-wire serial interface in this design. It does not provide a normal SPI ACK or register readback, so firmware cannot prove that the chip received a word merely because GPIO toggled. The monitor reports ESP32-side write activity and external status pins. LD/PLL lock is an indirect indication and also depends on reference clock, power, loop filter, RF configuration and board wiring.

Clock choices are software-timed:
- Fastest: no added delay (GPIO loop speed depends on core/build).
- 500 kHz nominal: 1 us half-period.
- 250 kHz nominal: 2 us half-period.
- 100 kHz nominal: 5 us half-period.

Auto-select tries the listed timings and chooses the fastest trial for which LD is observed high after programming. This is a heuristic, not a communications handshake or a guaranteed timing calibration. A pre-existing/high LD signal can produce a false positive; verify on the physical board and prefer a conservative clock if uncertain. The nominal clock values are approximate.

## Electrical and RF precautions
- Verify the carrier supply input, voltage and polarity before powering it.
- Do not assume the header pin marked 3V3 is a supply input; it may be an output. Check the carrier schematic/reverse side first.
- Join ESP32 and ADF4351 grounds.
- Confirm the carrier accepts 3.3 V logic on CLK/DAT/LE/CE. Bare ADF4351 logic-domain requirements and carrier level translation differ.
- Never connect RF outputs to ESP32 GPIO. Use a suitable 50-ohm load or RF test equipment.
- The exact carrier-board electrical design has not been verified.

## Reference frequency
Firmware assumes a 25.000 MHz reference (REF_HZ=25000000). Verify the fitted oscillator and reference routing before relying on frequency accuracy.

## Flash on Windows
Install Arduino CLI and ensure it is in PATH. Install the appropriate USB-UART driver (often CP210x or CH340), connect ESP32, run FLASH.bat, and enter the COM port. FQBN: esp32:esp32:esp32 (ESP32 Dev Module). If upload hangs at Connecting, hold BOOT as upload begins and release when writing starts.

## Validation status
The firmware changes have not been bench-tested on the exact carrier board. The GitHub Actions compile job should be checked for compile status; a successful compile still does not validate PLL register values, carrier power/logic compatibility or RF output. Verify the actual RF output with suitable test equipment.
