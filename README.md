# 1986 Apple Keyboard to ESP32 Bluetooth Keyboard

This project replaces or bypasses the original 8048-family keyboard controller in a vintage Apple keyboard and turns it into a Bluetooth Low Energy HID keyboard you can pair with any modern computer.

The firmware in `src/main.cpp` is dual-mode:

1. **Mapping mode** — interactive serial mapper. You press a key on the Apple keyboard, then type that key's name in the serial monitor. When you're done, type `save` and the firmware writes the mapping to flash and reboots.
2. **BLE mode** — once a saved keymap exists in flash, the ESP32 boots straight into Bluetooth mode and advertises itself as `Apple M0116`. Pair it from your computer's Bluetooth menu and start typing.

This repo was developed against an Apple M0116 that originally used a NEC 8048-family controller, but the same general approach can be used on other Apple keyboards built around an Intel MCS-48 / 8048-family chip in a 40-pin DIP package.

## What This Repo Contains

- `src/main.cpp`: the active dual-mode ESP32 firmware (mapper + BLE HID keyboard)
- `platformio.ini`: PlatformIO config for the ESP32 dev board target, including the BLE keyboard library dependency
- `WIRING.md`: the current known-good pin assignment for the tested keyboard in this repo
- `backups/`: older bring-up and diagnostic firmware snapshots

## What This Software Is For

Use this project when you want to:

- revive a dead or removed 8048-family keyboard controller
- discover the matrix on a vintage Apple keyboard
- map every physical key once over serial
- use the keyboard as a Bluetooth HID keyboard with any modern Mac, Windows, Linux, iOS, or Android device

## Supported Hardware

Tested:

- Apple M0116
- NEC 8048-family controller in the original 40-pin socket
- ESP32 dev board using the `esp32dev` PlatformIO target

Likely adaptable:

- other Apple keyboards from the same era that use an Intel or NEC 8048-family controller
- other 40-pin DIP keyboards where the original controller exposes matrix lines on P1, P2, DB0-DB7, INT, or similar MCS-48 pins

Important:

- do not assume another keyboard uses the exact same row, column, or modifier pins as the tested M0116
- copy the process, not blindly the exact wiring map

## Hardware You Need

- ESP32 dev board
- the Apple keyboard PCB
- access to the original 8048-family controller socket or traces
- jumper wires
- external 4.7k pull-up resistors for any ESP32 input-only GPIO used as columns
- USB cable for power, flashing, and serial monitor access
- VS Code with PlatformIO, or the PlatformIO CLI

## Safety And Bring-Up Notes

- Do not let the original 8048 chip and the ESP32 drive the same lines at the same time.
- If the original controller is still present, remove it or fully isolate it first.
- Count DIP pins carefully from the notch orientation before wiring anything.
- ESP32 GPIO 34, 35, 36, and 39 are input-only. Use them only as column inputs.
- If you use those input-only pins, add external pull-ups because they do not have usable internal pull-ups.
- Be careful with ESP32 boot-strap pins and board-specific quirks.

## Reference Material

- Intel 8048 / 8035 family datasheet:
  https://www.bitsavers.org/components/intel/8048/8048_8035_HMOS_Single_Component_8-Bit_Microcomputer_DataSheet_1980.pdf
- Current tested wiring for this repo:
  [WIRING.md](WIRING.md)

The datasheet is useful for understanding the original controller's pin names, especially P10-P17, P20-P27, DB0-DB7, INT, VCC, VDD, and VSS.

## How The Mapper Works

The firmware scans a set of row pins by driving one row LOW at a time and reading a set of column pins as inputs.

When a key press is detected, the serial monitor prompts you to name that key. For example:

- press the physical `A` key on the Apple keyboard
- type `A` in the serial monitor and press Enter on your computer
- the firmware stores that physical row and column location as `A`

It also supports a few direct, non-matrix modifier lines such as Caps Lock, Option, Shift, and Command.

## Current Known-Good Configuration In This Repo

The active firmware is currently set up as:

- 9 matrix rows
- 9 matrix columns
- 4 direct modifier inputs

See [WIRING.md](WIRING.md) for the exact pin assignment that is currently known to work on the tested keyboard.

## Building And Flashing

From the project directory:

```bash
pio run -t upload --upload-port YOUR_SERIAL_PORT
pio device monitor -p YOUR_SERIAL_PORT -b 115200
```

Example on macOS:

```bash
pio run -t upload --upload-port /dev/cu.usbserial-210
pio device monitor -p /dev/cu.usbserial-210 -b 115200
```

If you are not sure which serial device to use, list devices first:

```bash
pio device list
```

## Using The Mapper

After flashing for the first time (or after wiping the keymap), open the serial monitor at 115200 baud. The firmware boots into mapping mode whenever no saved keymap exists in flash.

The mapper will wait for a key press from the Apple keyboard. Then:

1. Press one key on the Apple keyboard.
2. Read the detected row and column in the serial output.
3. Type that key's name from your computer keyboard and press Enter.
4. Repeat until every key you care about is mapped.
5. Type `save` to commit the keymap to flash and reboot into Bluetooth mode.

Key-name conventions used by the BLE layer:

- single printable characters (`a`, `A`, `1`, `;`) → that character
- `Return`, `Space`, `Tab`, `Escape`
- `Delete` (the Mac-style key that deletes left), `FwdDelete` (deletes right)
- `Up`, `Down`, `Left`, `Right`, `Home`, `End`, `PageUp`, `PageDown`, `Insert`
- `F1`–`F12`
- `CapsLock`
- modifiers: `Shift`, `Ctrl`, `Option` (or `Alt`), `Command` (or `Cmd` / `Gui`); `L`/`R` prefix selects left or right side

The shortcuts the prompt accepts:

- just press Enter → `"Return"`
- type a space then Enter → `"Space"`
- press Tab then Enter → `"Tab"`
- press an arrow key then Enter → `"Up"`/`"Down"`/`"Left"`/`"Right"`
- type `skip` to skip the current physical key

Built-in serial commands while waiting for a key press:

- `map` — show current mappings
- `undo` — remove the most recent mapping
- `count` — show how many keys are mapped
- `save` — write keymap to flash and reboot into Bluetooth mode
- `clear` — erase the saved keymap (stays in mapping mode)
- `help` — print the help text again

## Using It Over Bluetooth

Once you have typed `save`, the ESP32 reboots and starts advertising as a Bluetooth Low Energy keyboard.

1. On your computer, open the Bluetooth settings.
2. Look for a device named **`Apple M0116`** and pair with it. No PIN is required.
3. Start typing on the Apple keyboard. Keys go through over BLE just like any other wireless keyboard.

The pairing is remembered by the host. After the first pairing, the ESP32 will reconnect automatically whenever it powers on and the host is in range.

USB on the ESP32 is only used for power and (optionally) the serial console. You do not need to keep it plugged into your computer once you have powered it from any USB source.

### Re-mapping Keys Later

There are two ways to get back into mapping mode:

- **Hold CapsLock at boot for ~2 seconds.** The firmware detects this, wipes the saved keymap, and stays in mapping mode.
- **Send `remap` over the serial monitor while in BLE mode.** This erases the keymap and reboots into mapping mode.

### Serial Commands While In BLE Mode

- `status` — show whether BLE is connected and how many mappings are loaded
- `map` — print the current keymap
- `remap` (or `clear`) — wipe the saved keymap and reboot into mapping mode

### Bluetooth Troubleshooting

- **Device does not appear in your Bluetooth menu.** Make sure the ESP32 is powered, and that the serial monitor shows `BLE MODE` and `advertising`. If it shows mapping mode instead, no keymap is saved yet — map some keys and run `save`.
- **Pairs but nothing types.** Make sure the keys you actually press are mapped. Use `map` over serial to see what was saved. Anything that wasn't mapped is silently ignored in BLE mode.
- **Modifiers do not work.** They must be mapped using one of the modifier names above (`Shift`, `Option`, `Command`, `Ctrl`). A modifier mapped to a regular character will type that character instead of acting as a modifier.
- **Wrong characters appear.** The ESP32 sends standard US-layout HID scan codes. If your computer is set to a different keyboard layout, set its input source to `U.S.` or remap individual keys in mapping mode.

## Adapting This To Another 8048-Based Apple Keyboard

If your keyboard is not the exact board used in this repo:

1. Use the 8048 datasheet to identify the original controller pins on your board.
2. Start with power and ground only, then add a small set of likely row and column lines.
3. Treat P10-P17, P20-P27, DB0-DB7, and INT as candidates, but verify them on the board instead of assuming.
4. Choose ESP32 output-capable pins for rows.
5. Choose ESP32 input pins for columns.
6. Add external pull-ups where required.
7. Update the arrays in `src/main.cpp` to match your board.
8. Reflash and test until the matrix is stable.

On some boards, not every P2 pin will be part of the main scanned matrix. Some may be used for modifiers or other special signals.

## Typical Workflow

1. Remove or isolate the original 8048-family controller.
2. Wire the keyboard socket to the ESP32.
3. Flash the mapper firmware.
4. Map keys one by one over serial.
5. Export the generated key table.
6. Use that table as the basis for your final HID firmware.

## Known ESP32 Constraints

- GPIO 34, 35, 36, and 39 are input-only.
- Some ESP32 pins affect boot mode or onboard peripherals on certain dev boards.
- If a whole group of keys is missing, the usual cause is one missing row or one missing column, not bad debounce logic.

## Project Status

This repo contains a working matrix mapper, a known-good wiring map for the tested board, and a Bluetooth Low Energy HID keyboard layer that uses the saved keymap. After mapping a board once, it behaves like a normal wireless keyboard.