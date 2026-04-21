/*
 * Apple Keyboard Matrix Mapper — v17 (diagnostics)
 *
 * Full matrix scanner: 8 rows × 10 columns
 * Rows: P10-P12, P14-P17 + P20  (P13/GPIO14 repurposed for Command)
 * Cols: DB0-DB7 + P21, P25
 * All modifiers on direct pins (Caps Lock, Option, Shift, Command)
 * Press each key one at a time — prints row/col on press and release.
 */

#include <Arduino.h>

// ==================== PIN DEFINITIONS ====================

const int ROWS[] = {4, 5, 13, 16, 17, 18, 19, 26};
const int NUM_ROWS = 8;
const char* ROW_NAMES[] = {
  "P10(4)", "P11(5)", "P12(13)",
  "P14(16)", "P15(17)", "P16(18)", "P17(19)",
  "P20(26)"
};

const int COLS[] = {22, 27, 15, 12, 34, 35, 39, 36, 23, 32};
const int NUM_COLS = 10;

const char* COL_NAMES[] = {
  "DB7(22)",
  "DB0(27)", "DB1(15)", "DB2(12)", "DB3(34)", "DB4(35)", "DB5(39)", "DB6(36)",
  "P21(23)", "P25(32)"
};

const bool COL_HAS_INTERNAL_PULLUP[] = {
  true,  // GPIO 22 — DB7
  true,  // GPIO 27 — DB0
  true,  // GPIO 15 — DB1
  true,  // GPIO 12 — DB2
  false, // GPIO 34 — DB3 (input-only, external pull-up)
  false, // GPIO 35 — DB4 (input-only, external pull-up)
  false, // GPIO 39 — DB5 (input-only, VN)
  false, // GPIO 36 — DB6 (input-only, VP)
  true,  // GPIO 23 — P21
  true,  // GPIO 32 — P25
};

// ==================== DIRECT PINS ====================

const int CAPS_PIN = 33;
bool capsState = false;
bool capsRaw = false;
unsigned long capsDebounce = 0;

const int OPTION_PIN = 21;
bool optionState = false;
bool optionRaw = false;
unsigned long optionDebounce = 0;

const int SHIFT_PIN = 25;
bool shiftState = false;
bool shiftRaw = false;
unsigned long shiftDebounce = 0;

const int CMD_PIN = 14;
bool cmdState = false;
bool cmdRaw = false;
unsigned long cmdDebounce = 0;

// ==================== STATE ======================================

int settleUs = 50;
bool matrix[8][10] = {{false}};
bool rawMatrix[8][10] = {{false}};
unsigned long debounce[8][10] = {{0}};
const unsigned long DEBOUNCE_MS = 10;

int keyCount = 0;
bool seenPositions[8][10] = {{false}};

// ==================== FUNCTIONS ====================

void setupPins() {
  for (int r = 0; r < NUM_ROWS; r++) {
    pinMode(ROWS[r], OUTPUT);
    digitalWrite(ROWS[r], HIGH);
  }
  for (int c = 0; c < NUM_COLS; c++) {
    if (COL_HAS_INTERNAL_PULLUP[c]) {
      pinMode(COLS[c], INPUT_PULLUP);
    } else {
      pinMode(COLS[c], INPUT);
    }
  }
  pinMode(CAPS_PIN, INPUT_PULLUP);
  pinMode(OPTION_PIN, INPUT_PULLUP);
  pinMode(SHIFT_PIN, INPUT_PULLUP);
  pinMode(CMD_PIN, INPUT_PULLUP);
}

void scanDirectPins() {
  // Caps Lock
  bool capsPressed = (digitalRead(CAPS_PIN) == LOW);
  if (capsPressed != capsRaw) { capsRaw = capsPressed; capsDebounce = millis(); }
  if ((millis() - capsDebounce) >= DEBOUNCE_MS) {
    if (capsRaw != capsState) {
      capsState = capsRaw;
      Serial.printf("%s CAPSLOCK (pin6/GPIO33)\n", capsState ? "PRESS  " : "RELEASE");
    }
  }

  // Option
  bool optionPressed = (digitalRead(OPTION_PIN) == LOW);
  if (optionPressed != optionRaw) { optionRaw = optionPressed; optionDebounce = millis(); }
  if ((millis() - optionDebounce) >= DEBOUNCE_MS) {
    if (optionRaw != optionState) {
      optionState = optionRaw;
      Serial.printf("%s OPTION (pin23/GPIO21)\n", optionState ? "PRESS  " : "RELEASE");
    }
  }

  // Shift
  bool shiftPressed = (digitalRead(SHIFT_PIN) == LOW);
  if (shiftPressed != shiftRaw) { shiftRaw = shiftPressed; shiftDebounce = millis(); }
  if ((millis() - shiftDebounce) >= DEBOUNCE_MS) {
    if (shiftRaw != shiftState) {
      shiftState = shiftRaw;
      Serial.printf("%s SHIFT (pin37/GPIO25)\n", shiftState ? "PRESS  " : "RELEASE");
    }
  }

  // Command
  bool cmdPressed = (digitalRead(CMD_PIN) == LOW);
  if (cmdPressed != cmdRaw) { cmdRaw = cmdPressed; cmdDebounce = millis(); }
  if ((millis() - cmdDebounce) >= DEBOUNCE_MS) {
    if (cmdRaw != cmdState) {
      cmdState = cmdRaw;
      Serial.printf("%s COMMAND (pin24/GPIO14)\n", cmdState ? "PRESS  " : "RELEASE");
    }
  }
}

void scanMatrix() {
  for (int row = 0; row < NUM_ROWS; row++) {
    digitalWrite(ROWS[row], LOW);
    delayMicroseconds(settleUs);

    for (int col = 0; col < NUM_COLS; col++) {
      bool pressed = (digitalRead(COLS[col]) == LOW);

      if (pressed != rawMatrix[row][col]) {
        rawMatrix[row][col] = pressed;
        debounce[row][col] = millis();
      }

      if ((millis() - debounce[row][col]) >= DEBOUNCE_MS) {
        if (rawMatrix[row][col] != matrix[row][col]) {
          matrix[row][col] = rawMatrix[row][col];

          if (matrix[row][col]) {
            if (!seenPositions[row][col]) {
              seenPositions[row][col] = true;
              keyCount++;
            }
            Serial.printf("PRESS   row=%d(%s)  col=%d(%s)  [position %d/%d]\n",
                          row, ROW_NAMES[row],
                          col, COL_NAMES[col],
                          keyCount, NUM_ROWS * NUM_COLS);
          } else {
            Serial.printf("RELEASE row=%d(%s)  col=%d(%s)\n",
                          row, ROW_NAMES[row],
                          col, COL_NAMES[col]);
          }
        }
      }
    }

    digitalWrite(ROWS[row], HIGH);
    delayMicroseconds(settleUs);
  }
}

void printStatus() {
  Serial.println();
  Serial.println("=== MATRIX STATUS ===");
  Serial.printf("Keys found: %d   Settle: %dus\n", keyCount, settleUs);
  Serial.println("\nDiscovered positions:");

  if (keyCount == 0) {
    Serial.println("  (none yet — press some keys!)");
  } else {
    Serial.print("     ");
    for (int c = 0; c < NUM_COLS; c++) {
      Serial.printf(" %7s", COL_NAMES[c]);
    }
    Serial.println();
    for (int r = 0; r < NUM_ROWS; r++) {
      Serial.printf("R%d(%2d) ", r, ROWS[r]);
      for (int c = 0; c < NUM_COLS; c++) {
        if (seenPositions[r][c]) {
          Serial.printf("    X   ");
        } else {
          Serial.printf("    .   ");
        }
      }
      Serial.println();
    }
  }
  Serial.println("=====================");
}

void printHelp() {
  Serial.println();
  Serial.println("==========================================");
  Serial.println("Apple Keyboard Diagnostics — v17");
  Serial.println("  8x10 matrix + 4 direct modifier pins");
  Serial.println("  + Caps Lock on GPIO33 (pin 6)");
  Serial.println("  + Option on GPIO21 (pin 23)");
  Serial.println("  + Shift on GPIO25 (pin 37)");
  Serial.println("  + Command on GPIO14 (pin 24)");
  Serial.println("==========================================");
  Serial.println();
  Serial.println("Press keys ONE AT A TIME on the keyboard.");
  Serial.println("Each key press prints its row/col position.");
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  status  — show discovered key positions");
  Serial.println("  settle <N> — set settle time in us");
  Serial.println("  reset   — clear all discovered positions");
  Serial.println("  raw     — show one raw matrix scan");
  Serial.println("  help    — this message");
  Serial.println("==========================================");
}

void printRawScan() {
  Serial.println("\n=== RAW MATRIX SCAN ===");
  for (int row = 0; row < NUM_ROWS; row++) {
    digitalWrite(ROWS[row], LOW);
    delayMicroseconds(settleUs);

    Serial.printf("Row %d (GPIO%d): ", row, ROWS[row]);
    for (int col = 0; col < NUM_COLS; col++) {
      int val = digitalRead(COLS[col]);
      Serial.printf("%s=%d ", COL_NAMES[col], val);
    }
    Serial.println();

    digitalWrite(ROWS[row], HIGH);
    delayMicroseconds(settleUs);
  }
  Serial.println("=======================");
}

void handleSerial() {
  if (!Serial.available()) return;
  String input = Serial.readStringUntil('\n');
  input.trim();
  if (input.length() == 0) return;

  if (input.equalsIgnoreCase("status")) {
    printStatus();
  } else if (input.equalsIgnoreCase("raw")) {
    printRawScan();
  } else if (input.equalsIgnoreCase("reset")) {
    memset(seenPositions, 0, sizeof(seenPositions));
    keyCount = 0;
    Serial.println(">>> Cleared all discovered positions");
  } else if (input.startsWith("settle")) {
    int val = input.substring(7).toInt();
    if (val > 0 && val < 100000) {
      settleUs = val;
      Serial.printf(">>> Settle time set to %d us\n", settleUs);
    }
  } else if (input.equalsIgnoreCase("help")) {
    printHelp();
  } else {
    Serial.println("Unknown command. Type 'help'.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  setupPins();
  printHelp();

  Serial.println("\nReady! Press keys one at a time...\n");
}

void loop() {
  scanMatrix();
  scanDirectPins();
  handleSerial();
}
