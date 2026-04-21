/*
 * Apple Keyboard Key Mapper
 *
 * Interactive key mapping tool for the M0116.
 * Press a key on the Apple keyboard, type its name on your laptop.
 * Auto-detects Enter, Space, Tab from raw keystrokes.
 *
 * Hardware: 9x9 matrix + 4 direct modifier pins
 *   GPIO 23 moved from column P21 to row P13 (DIP pin 30)
 *   to restore missing number row keys (7, 8, etc.)
 */

#include <Arduino.h>

// ==================== PIN DEFINITIONS ====================

const int ROWS[] = {4, 5, 13, 23, 16, 17, 18, 19, 26};
const int NUM_ROWS = 9;
const char* ROW_NAMES[] = {
  "P10(4)", "P11(5)", "P12(13)", "P13(23)",
  "P14(16)", "P15(17)", "P16(18)", "P17(19)",
  "P20(26)"
};

const int COLS[] = {22, 27, 15, 12, 34, 35, 39, 36, 32};
const int NUM_COLS = 9;
const char* COL_NAMES[] = {
  "DB7(22)", "DB0(27)", "DB1(15)", "DB2(12)",
  "DB3(34)", "DB4(35)", "DB5(39)", "DB6(36)",
  "P25(32)"
};

const bool COL_HAS_INTERNAL_PULLUP[] = {
  true, true, true, true,
  false, false, false, false,
  true
};

const int CAPS_PIN = 33;
const int OPTION_PIN = 21;
const int SHIFT_PIN = 25;
const int CMD_PIN = 14;

// ==================== STATE MACHINE ====================

enum State { WAITING_FOR_KEY, WAITING_FOR_NAME };
State state = WAITING_FOR_KEY;

// ==================== KEY MAPPING STORAGE ====================

struct KeyMapping {
  uint8_t type;      // 0=matrix, 1=direct
  uint8_t row, col;  // matrix position (type==0)
  uint8_t directId;  // 0=caps, 1=option, 2=shift, 3=cmd (type==1)
  char name[32];
};

#define MAX_MAPPINGS 100
KeyMapping mappings[MAX_MAPPINGS];
int mapCount = 0;

uint8_t pendType, pendRow, pendCol, pendDirect;

// ==================== DEBOUNCE STATE ====================

bool matrix[9][9] = {{false}};
bool rawMatrix[9][9] = {{false}};
unsigned long dbMatrix[9][9] = {{0}};

bool capsState = false, capsRaw = false; unsigned long capsDb = 0;
bool optState  = false, optRaw  = false; unsigned long optDb  = 0;
bool shftState = false, shftRaw = false; unsigned long shftDb = 0;
bool cmdState  = false, cmdRaw  = false; unsigned long cmdDb  = 0;

const unsigned long DEBOUNCE_MS = 10;
int settleUs = 50;

const char* DIRECT_LABELS[] = {"CapsLock", "Option", "Shift", "Command"};
const char* DIRECT_PINS[]   = {"pin6/GPIO33", "pin23/GPIO21", "pin37/GPIO25", "pin24/GPIO14"};

// ==================== FUNCTIONS ====================

void setupPins() {
  for (int r = 0; r < NUM_ROWS; r++) {
    pinMode(ROWS[r], OUTPUT);
    digitalWrite(ROWS[r], HIGH);
  }
  for (int c = 0; c < NUM_COLS; c++) {
    pinMode(COLS[c], COL_HAS_INTERNAL_PULLUP[c] ? INPUT_PULLUP : INPUT);
  }
  pinMode(CAPS_PIN, INPUT_PULLUP);
  pinMode(OPTION_PIN, INPUT_PULLUP);
  pinMode(SHIFT_PIN, INPUT_PULLUP);
  pinMode(CMD_PIN, INPUT_PULLUP);
}

void silentScan() {
  for (int row = 0; row < NUM_ROWS; row++) {
    digitalWrite(ROWS[row], LOW);
    delayMicroseconds(settleUs);
    for (int col = 0; col < NUM_COLS; col++) {
      bool p = (digitalRead(COLS[col]) == LOW);
      rawMatrix[row][col] = p;
      matrix[row][col] = p;
      dbMatrix[row][col] = millis();
    }
    digitalWrite(ROWS[row], HIGH);
    delayMicroseconds(settleUs);
  }
  capsRaw = capsState = (digitalRead(CAPS_PIN) == LOW);   capsDb = millis();
  optRaw  = optState  = (digitalRead(OPTION_PIN) == LOW); optDb  = millis();
  shftRaw = shftState = (digitalRead(SHIFT_PIN) == LOW);  shftDb = millis();
  cmdRaw  = cmdState  = (digitalRead(CMD_PIN) == LOW);    cmdDb  = millis();
}

int findMapping(uint8_t type, uint8_t row, uint8_t col, uint8_t directId) {
  for (int i = 0; i < mapCount; i++) {
    if (mappings[i].type != type) continue;
    if (type == 0 && mappings[i].row == row && mappings[i].col == col) return i;
    if (type == 1 && mappings[i].directId == directId) return i;
  }
  return -1;
}

void registerKey(const char* name) {
  int idx = findMapping(pendType, pendRow, pendCol, pendDirect);
  bool isUpdate = (idx >= 0);
  if (idx < 0) {
    if (mapCount >= MAX_MAPPINGS) {
      Serial.println("!!! Mapping table full!");
      return;
    }
    idx = mapCount++;
  }
  mappings[idx].type = pendType;
  mappings[idx].row = pendRow;
  mappings[idx].col = pendCol;
  mappings[idx].directId = pendDirect;
  strncpy(mappings[idx].name, name, 31);
  mappings[idx].name[31] = '\0';

  Serial.printf(">>> Registered as \"%s\"%s  (%d/%d mapped)\n",
                name, isUpdate ? " (updated)" : "",
                mapCount, NUM_ROWS * NUM_COLS + 4);
  Serial.println(">>> Ready for next key...\n");
}

bool checkDirect(int pin, bool &raw, bool &st, unsigned long &db) {
  bool pressed = (digitalRead(pin) == LOW);
  if (pressed != raw) { raw = pressed; db = millis(); }
  if ((millis() - db) >= DEBOUNCE_MS && raw != st) {
    st = raw;
    return st;
  }
  return false;
}

void detectDirect(int id, int pin, bool &raw, bool &st, unsigned long &db) {
  if (checkDirect(pin, raw, st, db)) {
    pendType = 1; pendDirect = id;
    state = WAITING_FOR_NAME;
    int existing = findMapping(1, 0, 0, id);
    Serial.printf("\n>>> %s detected (%s)", DIRECT_LABELS[id], DIRECT_PINS[id]);
    if (existing >= 0) Serial.printf("  [currently: \"%s\"]", mappings[existing].name);
    Serial.println();
    Serial.println("    Type key name (or press key on Mac):");
  }
}

void scanForKey() {
  if (state != WAITING_FOR_KEY) return;

  detectDirect(0, CAPS_PIN,   capsRaw, capsState, capsDb);
  if (state != WAITING_FOR_KEY) return;
  detectDirect(1, OPTION_PIN, optRaw,  optState,  optDb);
  if (state != WAITING_FOR_KEY) return;
  detectDirect(2, SHIFT_PIN,  shftRaw, shftState, shftDb);
  if (state != WAITING_FOR_KEY) return;
  detectDirect(3, CMD_PIN,    cmdRaw,  cmdState,  cmdDb);
  if (state != WAITING_FOR_KEY) return;

  for (int row = 0; row < NUM_ROWS; row++) {
    digitalWrite(ROWS[row], LOW);
    delayMicroseconds(settleUs);
    for (int col = 0; col < NUM_COLS; col++) {
      bool pressed = (digitalRead(COLS[col]) == LOW);
      if (pressed != rawMatrix[row][col]) {
        rawMatrix[row][col] = pressed;
        dbMatrix[row][col] = millis();
      }
      if ((millis() - dbMatrix[row][col]) >= DEBOUNCE_MS) {
        if (rawMatrix[row][col] != matrix[row][col]) {
          matrix[row][col] = rawMatrix[row][col];
          if (matrix[row][col]) {
            pendType = 0; pendRow = row; pendCol = col;
            state = WAITING_FOR_NAME;
            int existing = findMapping(0, row, col, 0);
            Serial.printf("\n>>> Key detected: row=%d(%s) col=%d(%s)",
                          row, ROW_NAMES[row], col, COL_NAMES[col]);
            if (existing >= 0) Serial.printf("  [currently: \"%s\"]", mappings[existing].name);
            Serial.println();
            Serial.println("    Type key name (or press key on Mac):");
            digitalWrite(ROWS[row], HIGH);
            return;
          }
        }
      }
    }
    digitalWrite(ROWS[row], HIGH);
    delayMicroseconds(settleUs);
  }
}

void printMap() {
  Serial.println("\n=== CURRENT KEY MAP ===");
  Serial.printf("Total mapped: %d / %d\n\n", mapCount, NUM_ROWS * NUM_COLS + 4);

  Serial.println("Matrix (row x col):");
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      int idx = findMapping(0, r, c, 0);
      if (idx >= 0) {
        Serial.printf("  [%d,%d] %-12s", r, c, mappings[idx].name);
      }
    }
    Serial.println();
  }

  Serial.println("\nDirect pins:");
  for (int d = 0; d < 4; d++) {
    int idx = findMapping(1, 0, 0, d);
    if (idx >= 0) {
      Serial.printf("  %-10s → \"%s\"\n", DIRECT_LABELS[d], mappings[idx].name);
    }
  }
  Serial.println("=======================\n");
}

void exportMap() {
  Serial.println("\n// ========== GENERATED KEY MAP ==========");
  Serial.println("// Apple M0116 — paste into BLE HID firmware\n");

  Serial.printf("const char* MATRIX_KEYS[%d][%d] = {\n", NUM_ROWS, NUM_COLS);
  for (int r = 0; r < NUM_ROWS; r++) {
    Serial.printf("  /* row %d */ {", r);
    for (int c = 0; c < NUM_COLS; c++) {
      int idx = findMapping(0, r, c, 0);
      if (idx >= 0) Serial.printf("\"%s\"", mappings[idx].name);
      else Serial.print("NULL");
      if (c < NUM_COLS - 1) Serial.print(", ");
    }
    Serial.println("},");
  }
  Serial.println("};\n");

  Serial.println("// Direct modifier pins:");
  for (int d = 0; d < 4; d++) {
    int idx = findMapping(1, 0, 0, d);
    Serial.printf("// %-10s = \"%s\"\n", DIRECT_LABELS[d],
                  idx >= 0 ? mappings[idx].name : "(unmapped)");
  }
  Serial.println("\n// ========== END KEY MAP ==========\n");
}

void printHelp() {
  Serial.println();
  Serial.println("==========================================");
  Serial.println("Apple Keyboard Key Mapper — v19");
  Serial.println("  9x9 matrix + 4 direct modifier pins");
  Serial.println("  GPIO 23: P21 col → P13 row (DIP 30)");
  Serial.println("==========================================");
  Serial.println();
  Serial.println("Press a key on the Apple keyboard, then");
  Serial.println("type its name on your Mac:");
  Serial.println();
  Serial.println("  Just hit Enter    → \"Return\"");
  Serial.println("  Space then Enter  → \"Space\"");
  Serial.println("  Tab then Enter    → \"Tab\"");
  Serial.println("  Type a char + Enter → that character");
  Serial.println("  Type a name + Enter → e.g. \"F1\", \"Esc\"");
  Serial.println("  Type \"skip\"        → skip this key");
  Serial.println();
  Serial.println("Commands (while waiting for a key press):");
  Serial.println("  map    — show all mappings so far");
  Serial.println("  export — print C code for BLE firmware");
  Serial.println("  undo   — remove last mapping");
  Serial.println("  count  — show mapped key count");
  Serial.println("  help   — this message");
  Serial.println("==========================================");
}

void handleSerial() {
  if (!Serial.available()) return;

  String input = Serial.readStringUntil('\n');

  if (state == WAITING_FOR_NAME) {
    String name;

    String raw = input;
    if (raw.length() > 0 && raw.charAt(raw.length() - 1) == '\r') {
      raw = raw.substring(0, raw.length() - 1);
    }

    if (raw.length() == 0) {
      name = "Return";
    } else if (raw == " ") {
      name = "Space";
    } else if (raw == "\t") {
      name = "Tab";
    } else if (raw.charAt(0) == 0x1B) {
      if (raw.length() == 1) {
        name = "Escape";
      } else if (raw.length() >= 3 && raw.charAt(1) == '[') {
        switch (raw.charAt(2)) {
          case 'A': name = "Up"; break;
          case 'B': name = "Down"; break;
          case 'C': name = "Right"; break;
          case 'D': name = "Left"; break;
          case 'H': name = "Home"; break;
          case 'F': name = "End"; break;
          default:  name = "Escape"; break;
        }
      } else {
        name = "Escape";
      }
    } else if (raw.charAt(0) == 0x7F || raw.charAt(0) == 0x08) {
      name = "Delete";
    } else {
      raw.trim();
      if (raw.equalsIgnoreCase("skip")) {
        Serial.println(">>> Skipped. Ready for next key...\n");
        state = WAITING_FOR_KEY;
        silentScan();
        return;
      }
      name = raw;
    }

    registerKey(name.c_str());
    state = WAITING_FOR_KEY;
    silentScan();
    return;
  }

  input.trim();
  if (input.equalsIgnoreCase("map")) {
    printMap();
  } else if (input.equalsIgnoreCase("export")) {
    exportMap();
  } else if (input.equalsIgnoreCase("undo")) {
    if (mapCount > 0) {
      mapCount--;
      Serial.printf(">>> Removed \"%s\". %d mapped.\n", mappings[mapCount].name, mapCount);
    } else {
      Serial.println(">>> Nothing to undo.");
    }
  } else if (input.equalsIgnoreCase("count")) {
    Serial.printf(">>> %d / %d keys mapped\n", mapCount, NUM_ROWS * NUM_COLS + 4);
  } else if (input.equalsIgnoreCase("help")) {
    printHelp();
  } else if (input.length() > 0) {
    Serial.println(">>> Waiting for Apple keyboard press. Type 'help' for commands.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  setupPins();
  silentScan();
  printHelp();
  Serial.println("\nReady! Press a key on the Apple keyboard...\n");
}

void loop() {
  scanForKey();
  handleSerial();
}
