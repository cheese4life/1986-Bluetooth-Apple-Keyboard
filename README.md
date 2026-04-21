# 1986 Apple Keyboard to ESP32 Mapper

This project replaces or bypasses the original 8048-family keyboard controller in a vintage Apple keyboard and lets an ESP32 scan the matrix directly.

The current firmware in `src/main.cpp` is an interactive key mapper. You press a key on the Apple keyboard, then type that key's name in the serial monitor from your computer. Once enough keys are mapped, the firmware can export a C table that you can reuse in a final USB or Bluetooth keyboard firmware.

This repo was developed against an Apple M0116 that originally used a NEC 8048-family controller, but the same general approach can be used on other Apple keyboards built around an Intel MCS-48 / 8048-family chip in a 40-pin DIP package.

## What This Repo Contains

- `src/main.cpp`: the active ESP32 key-mapper firmware
- `platformio.ini`: PlatformIO config for the ESP32 dev board target
- `WIRING.md`: the current known-good pin assignment for the tested keyboard in this repo
- `backups/`: older bring-up and diagnostic firmware snapshots

## What This Software Is For

Use this project when you want to:

- revive a dead or removed 8048-family keyboard controller
- discover the matrix on a vintage Apple keyboard
- map every physical key before writing final HID firmware
- build a USB or Bluetooth conversion around an ESP32

This repo is currently a bring-up and mapping tool first. It is not yet a polished end-user Bluetooth keyboard firmware.

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

After flashing, open the serial monitor at 115200 baud.

The mapper will wait for a key press from the Apple keyboard. Then:

1. Press one key on the Apple keyboard.
2. Read the detected row and column in the serial output.
3. Type that key's name from your computer keyboard.
4. Repeat until enough of the matrix is mapped.

Built-in serial commands:

- `map`: show current mappings
- `export`: print generated C arrays for later firmware
- `undo`: remove the most recent mapping
- `count`: show how many keys are mapped
- `help`: print the help text again

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

This repo currently contains a working matrix mapper and a known-good wiring map for the tested board. The natural next step after mapping is to build final USB or BLE HID firmware around the discovered matrix.