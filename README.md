# Broadlink RM4 Mini — ESPHome via LibreTiny

First known public ESPHome flash of the **Broadlink RM4 Mini** (board `RM4_MINI-MAIN-V4.0.1`).

The RM4 Mini uses a **Realtek RTL8720CF** (ARM Cortex-M33, 100MHz) inside the BL3357-P WiFi module — not an ESP32. This project uses [LibreTiny](https://github.com/libretiny-eu/libretiny) to run ESPHome on it.

See also: [Broadlink RM4 Pro ESPHome](https://github.com/phdindota/broadlink-rm4pro-esphome) (same chip, adds 433MHz RF).

## What Works

- ✅ IR Receive — full protocol decoding (NEC, Pronto, RC5, Samsung, Sony, etc.)
- ✅ IR Transmit — custom PWM carrier via `ir_tx.h` (NEC, raw timing, Pronto hex)
- ✅ Status LED
- ✅ Physical button
- ✅ OTA updates
- ✅ Home Assistant API integration

## Pin Map

Extracted from stock firmware boot log:

| Function | Pin | Notes |
|---|---|---|
| IR TX (carrier) | PA4 | 38kHz PWM via `analogWrite()` |
| IR RX (data) | PA18 | `remote_receiver` with LibreTiny patch |
| Status LED | PA7 | Inverted |
| Keypad Button | PA19 | Inverted, pull-up |
| Network Button | PA15 | Inverted, pull-up |
| SHT30 (temp/humidity) | Soft I2C | Pins TBD — `th_support 1` in firmware |

## Hardware

- **Board:** RM4_MINI-MAIN-V4.0.1 (2021-03-17)
- **WiFi Module:** BL3357-P (Realtek RTL8720CF)
- **Flash:** 2MB
- **Test Points (back of board):** IOA0, IOA13, 3.3V, U2-TX, U2-RX, GND, 5V, T4, T8

## Requirements

- [ESPHome](https://esphome.io/) 2024.2+
- [ltchiptool](https://github.com/libretiny-eu/ltchiptool) for initial UART flash
- 3.3V UART adapter (e.g., DSD TECH SH-U09C5)
- Soldering or test clips for UART pads

## Flashing

### 1. Open the case

Peel the rubber pad on the bottom to reveal screws. Remove screws and separate the case.

### 2. Connect UART

Wire your UART adapter to the test points on the back of the board:

| UART Adapter | Board Pad |
|---|---|
| GND | GND |
| TX | U2-RX |
| RX | U2-TX |

**Do NOT connect the UART adapter's 3.3V to the board.** Power the RM4 Mini from its own USB cable.

### 3. Backup stock firmware

Bridge **IOA0 → 3.3V** (download mode), then power cycle via USB:

```bash
ltchiptool flash read -d /dev/ttyUSB0 rtl8720c rm4mini_stock.bin
```

Keep this backup safe in case you want to restore stock firmware.

### 4. Compile ESPHome

```bash
git clone https://github.com/phdindota/broadlink-rm4mini-esphome.git
cd broadlink-rm4mini-esphome
cp secrets.yaml.example secrets.yaml
# Edit secrets.yaml with your WiFi credentials and API key
esphome compile rm4mini.yaml
```

The included `patch_libretiny.py` will automatically patch LibreTiny during compilation. You should see `*** PATCH: Applied ***` in the compile output.

### 5. Flash

With IOA0 still bridged to 3.3V, power cycle and flash:

```bash
ltchiptool flash write -d /dev/ttyUSB0 .esphome/build/broadlink-rm4mini/.pioenvs/broadlink-rm4mini/image_flash_is.0x000000.bin
```

### 6. Boot

Remove the IOA0 bridge, power cycle. The device should connect to WiFi and appear in Home Assistant.

### 7. Future updates

All subsequent updates are OTA — no need to open the case again:

```bash
esphome upload rm4mini.yaml --device broadlink-rm4mini.local
```

## ESPHome Dashboard Setup

To manage this device from the ESPHome dashboard add-on in Home Assistant:

1. Copy `rm4mini.yaml`, `ir_tx.h`, `patch_libretiny.py`, and `secrets.yaml` to `/config/esphome/` on your HA instance (via Samba, SSH, or File Editor).

2. **Important:** Change the patch script path in the YAML to an absolute path:

```yaml
  platformio_options:
    extra_scripts:
      - pre:/config/esphome/patch_libretiny.py
```

3. The device should appear in the ESPHome dashboard. Use **Clean Build Files** on the first compile to ensure the patch applies.

## LibreTiny Patch — Why It's Needed

LibreTiny's `digitalRead()` has a bug on RTL8720CF that breaks ESPHome's `remote_receiver` component. Two issues:

1. **`pinCheckGetData` only checks `PIN_GPIO`** — but `remote_receiver` uses `attachInterrupt()` which sets the `PIN_IRQ` flag instead. So `digitalRead()` fails the flag check and returns `LOW` without ever reading the pin.

2. **`digitalRead` must use `gpio_irq_read()` in interrupt context** — the standard `gpio_read()` doesn't work inside ISR handlers on this chip.

The included `patch_libretiny.py` fixes both issues automatically during compilation. It modifies `wiring_digital.c` to:
- Accept `PIN_GPIO | PIN_IRQ` in the flag check
- Use `gpio_irq_read(data->irq)` when an IRQ is active on the pin

This fix benefits **all** LibreTiny RTL8720CF users, not just this project.

## IR Transmitter — Why Custom?

ESPHome's built-in `remote_transmitter` component uses software bit-banging for the 38kHz carrier signal. On the RTL8720CF's 100MHz Cortex-M33, this tight loop triggers the hardware watchdog and crashes the MCU.

The included `ir_tx.h` uses hardware PWM via `analogWrite()` instead, which runs in the background without blocking the CPU.

## UART Logging

Hardware UART logging (`baud_rate: 115200`) can interfere with GPIO interrupts on this chip. The YAML sets `baud_rate: 0` to disable hardware UART output. Logs are still available over the network via the ESPHome API (i.e., the dashboard logs and `esphome logs` command work fine).

### Capturing IR Codes

1. The YAML already includes `remote_receiver` with `dump: all`
2. Watch ESPHome logs: `esphome logs rm4mini.yaml --device broadlink-rm4mini.local`
3. Point any IR remote at the RM4 Mini and press buttons
4. Copy the raw/NEC/Pronto codes from the log output
5. Use them with the `ir_tx.h` functions below

### Sending Raw Codes

Use raw timing arrays captured from `remote_receiver` logs. Values are in microseconds — positive for mark (IR on), negative for space (IR off). Uses `int32_t` to support long repeat gaps (e.g. -39712).

```yaml
button:
  - platform: template
    name: "Yamaha Volume Up"
    on_press:
      - lambda: |-
          static const int32_t code[] = {
            9042, -4430, 624, -500, 620, -1618, 624, -498, 624, -1616,
            624, -1616, 624, -1618, 624, -1616, 624, -500, 622, -1616,
            624, -500, 620, -1618, 624, -498, 624, -498, 622, -498,
            622, -498, 622, -1618, 624, -498, 622, -1618, 624, -498,
            622, -1618, 624, -1616, 624, -498, 622, -500, 622, -498,
            622, -1618, 622, -500, 622, -1616, 626, -498, 622, -498,
            622, -1618, 624, -1616, 624, -1618, 624, -39712, 9018,
            -2190, 622
          };
          ir_send_raw(code, sizeof(code) / sizeof(code[0]));
```

### Sending NEC Codes

For devices using the NEC protocol, pass the address and command directly:

```yaml
button:
  - platform: template
    name: "TV Power"
    on_press:
      - lambda: |-
          ir_send_nec(0x04, 0x08);
```

### Sending Pronto Codes

Pronto hex codes can be sent as a string. These are often found in device databases and remote control forums:

```yaml
button:
  - platform: template
    name: "AC Power"
    on_press:
      - lambda: |-
          ir_send_raw_pronto("0000 006D 0022 0000 015A 00AD 0016 0015 ...");
```

## Stock Firmware Boot Log (Reference)

```
== Rtl8710c IoT Platform ==
Chip VID: 5, Ver: 3
ROM Version: v3.0

[APP]==== rmparam in profile ====
[APP]app_network_pin 15
[APP]app_keypad_pin  19
[APP]th_support 1
[APP]energy_support 0

[APP]==== irda in profile ====
[APP]is_support 1
[APP]capin_work_level 0
[APP]carrier_pin 4
[APP]data_pin 18
[APP]data_gpt 8
[APP]carrier_gpt 1
[APP]carrier_gpt_channel 4
[APP]led_pin 7

[APP]==== rf in profile ====
[APP]is_support 0
```

## Known Limitations

- **IR TX** uses a custom PWM implementation (`ir_tx.h`) because ESPHome's `remote_transmitter` crashes the MCU
- **UART logging** must be disabled (`baud_rate: 0`) to avoid interference with IR receiver interrupts
- **SHT30 sensor** — stock firmware shows soft I2C temperature/humidity support, but the I2C pins haven't been mapped yet
- **ESPHome dashboard** may show "unknown board" warnings (cosmetic only)
- **Noise filtering** — `filter: 200us` is needed in `remote_receiver` to reject ambient IR noise

## License

MIT

## Credits

Reverse engineered and flashed with ❤️ for the Home Assistant community.
