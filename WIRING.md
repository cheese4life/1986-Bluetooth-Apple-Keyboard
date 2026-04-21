# M0116 to ESP32 Wiring

This document reflects the current working wiring used by the active key-mapper firmware.
Pin numbers below refer to the original 8048 DIP socket on the keyboard PCB, viewed from
above with the notch at the top.

## Power

| DIP pin | Signal | ESP32 |
|--------|--------|-------|
| 40 | VCC | 3V3 |
| 26 | VDD | 3V3 |
| 20 | VSS | GND |

## Matrix rows

These are the row outputs driven LOW one at a time by the firmware.

| Firmware row | DIP pin | NEC signal | ESP32 GPIO |
|-------------|--------|------------|------------|
| 0 | 27 | P10 | GPIO 4 |
| 1 | 28 | P11 | GPIO 5 |
| 2 | 29 | P12 | GPIO 13 |
| 3 | 30 | P13 | GPIO 23 |
| 4 | 31 | P14 | GPIO 16 |
| 5 | 32 | P15 | GPIO 17 |
| 6 | 33 | P16 | GPIO 18 |
| 7 | 34 | P17 | GPIO 19 |
| 8 | 21 | P20 | GPIO 26 |

## Matrix columns

These are the column inputs read by the firmware.

| Firmware col | DIP pin | NEC signal | ESP32 GPIO | Pull-up |
|-------------|--------|------------|------------|---------|
| 0 | 19 | DB7 | GPIO 22 | internal |
| 1 | 12 | DB0 | GPIO 27 | internal |
| 2 | 13 | DB1 | GPIO 15 | internal |
| 3 | 14 | DB2 | GPIO 12 | internal |
| 4 | 15 | DB3 | GPIO 34 | external 4.7k |
| 5 | 16 | DB4 | GPIO 35 | external 4.7k |
| 6 | 17 | DB5 | GPIO 39 | external 4.7k |
| 7 | 18 | DB6 | GPIO 36 | external 4.7k |
| 8 | 36 | P25 | GPIO 32 | internal |

## Direct modifier pins

These are not part of the scanned matrix.

| Key | DIP pin | NEC signal | ESP32 GPIO |
|-----|--------|------------|------------|
| Caps Lock | 6 | INT | GPIO 33 |
| Option | 23 | P22 | GPIO 21 |
| Shift | 37 | P26 | GPIO 25 |
| Command | 24 | P23 | GPIO 14 |

## Current notes

- This is the current 9x9 matrix plus 4 direct modifiers used by the mapper firmware.
- GPIO 23 was moved from DIP pin 22 (P21) to DIP pin 30 (P13).
- DIP pin 22 / P21 is not used in the current working firmware.
- GPIO 34, 35, 36, and 39 are input-only on the ESP32, so those column lines need the external pull-ups shown above.
