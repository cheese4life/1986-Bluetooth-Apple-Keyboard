/*
 * Apple M0116 -> BLE HID Keyboard  (v20, dual-mode)
 *
 *   Mapping mode  : interactive serial mapper (same UX as v19).
 *                   On `save`, writes mappings to NVS and reboots into BLE mode.
 *   BLE mode      : presents itself to your computer as "Apple M0116" Bluetooth
 *                   keyboard and translates each matrix/modifier press into HID.
 *
 * Boot logic:
 *   - If saved keymap exists in NVS  -> BLE mode.
 *   - Else                           -> mapping mode.
 *   - Hold CapsLock at boot ~2s      -> wipe NVS, force mapping mode.
 *
 * Hardware: 9x9 matrix + 4 direct modifier pins (unchanged from v19).
 */

#include <Arduino.h>
#include <Preferences.h>
#include <BleKeyboard.h>

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

const int CAPS_PIN   = 33;
const int OPTION_PIN = 21;
const int SHIFT_PIN  = 25;
const int CMD_PIN    = 14;

// ==================== KEY MAPPING STORAGE ====================

struct KeyMapping {
  uint8_t type;      // 0=matrix, 1=direct
  uint8_t row, col;
  uint8_t directId;  // 0=caps, 1=option, 2=shift, 3=cmd
  char    name[32];
};

#define MAX_MAPPINGS 100
KeyMapping mappings[MAX_MAPPINGS];
int mapCount = 0;

// NVS
Preferences prefs;
const char* NVS_NS  = "applemap";
const char* NVS_KEY = "blob";
const uint32_t NVS_MAGIC = 0xA9911604;
struct __attribute__((packed)) NvsHeader {
  uint32_t magic;
  uint16_t count;
};

// ==================== MODE ====================

enum Mode { MODE_MAPPING, MODE_BLE };
Mode mode = MODE_MAPPING;

enum MapState { WAITING_FOR_KEY, WAITING_FOR_NAME };
MapState mapState = WAITING_FOR_KEY;
uint8_t pendType, pendRow, pendCol, pendDirect;

// ==================== DEBOUNCE STATE ====================

bool matrix[9][9]    = {{false}};
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

// ==================== BLE ====================

BleKeyboard bleKeyboard("Apple M0116", "Apple", 100);
bool bleWasConnected = false;

// ==================== PIN SETUP ====================

void setupPins() {
  for (int r = 0; r < NUM_ROWS; r++) {
    pinMode(ROWS[r], OUTPUT);
    digitalWrite(ROWS[r], HIGH);
  }
  for (int c = 0; c < NUM_COLS; c++) {
    pinMode(COLS[c], COL_HAS_INTERNAL_PULLUP[c] ? INPUT_PULLUP : INPUT);
  }
  pinMode(CAPS_PIN,   INPUT_PULLUP);
  pinMode(OPTION_PIN, INPUT_PULLUP);
  pinMode(SHIFT_PIN,  INPUT_PULLUP);
  pinMode(CMD_PIN,    INPUT_PULLUP);
}

void silentScan() {
  for (int row = 0; row < NUM_ROWS; row++) {
    digitalWrite(ROWS[row], LOW);
    delayMicroseconds(settleUs);
    for (int col = 0; col < NUM_COLS; col++) {
      bool p = (digitalRead(COLS[col]) == LOW);
      rawMatrix[row][col] = p;
      matrix[row][col]    = p;
      dbMatrix[row][col]  = millis();
    }
    digitalWrite(ROWS[row], HIGH);
    delayMicroseconds(settleUs);
  }
  capsRaw = capsState = (digitalRead(CAPS_PIN)   == LOW); capsDb = millis();
  optRaw  = optState  = (digitalRead(OPTION_PIN) == LOW); optDb  = millis();
  shftRaw = shftState = (digitalRead(SHIFT_PIN)  == LOW); shftDb = millis();
  cmdRaw  = cmdState  = (digitalRead(CMD_PIN)    == LOW); cmdDb  = millis();
}

int findMapping(uint8_t type, uint8_t row, uint8_t col, uint8_t directId) {
  for (int i = 0; i < mapCount; i++) {
    if (mappings[i].type != type) continue;
    if (type == 0 && mappings[i].row == row && mappings[i].col == col) return i;
    if (type == 1 && mappings[i].directId == directId) return i;
  }
  return -1;
}

// ==================== NVS ====================

bool loadMappings() {
  prefs.begin(NVS_NS, true);
  size_t len = prefs.getBytesLength(NVS_KEY);
  if (len < sizeof(NvsHeader)) { prefs.end(); return false; }

  uint8_t* buf = (uint8_t*)malloc(len);
  if (!buf) { prefs.end(); return false; }
  prefs.getBytes(NVS_KEY, buf, len);
  prefs.end();

  NvsHeader hdr;
  memcpy(&hdr, buf, sizeof(hdr));
  if (hdr.magic != NVS_MAGIC ||
      len != sizeof(NvsHeader) + (size_t)hdr.count * sizeof(KeyMapping) ||
      hdr.count > MAX_MAPPINGS) {
    free(buf);
    return false;
  }
  mapCount = hdr.count;
  memcpy(mappings, buf + sizeof(NvsHeader), (size_t)hdr.count * sizeof(KeyMapping));
  free(buf);
  return mapCount > 0;
}

bool saveMappings() {
  size_t len = sizeof(NvsHeader) + (size_t)mapCount * sizeof(KeyMapping);
  uint8_t* buf = (uint8_t*)malloc(len);
  if (!buf) return false;
  NvsHeader hdr = { NVS_MAGIC, (uint16_t)mapCount };
  memcpy(buf, &hdr, sizeof(hdr));
  memcpy(buf + sizeof(NvsHeader), mappings, (size_t)mapCount * sizeof(KeyMapping));

  prefs.begin(NVS_NS, false);
  size_t written = prefs.putBytes(NVS_KEY, buf, len);
  prefs.end();
  free(buf);
  return written == len;
}

void clearMappings() {
  prefs.begin(NVS_NS, false);
  prefs.clear();
  prefs.end();
  mapCount = 0;
}

// ==================== MAPPING MODE ====================

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
  mappings[idx].type     = pendType;
  mappings[idx].row      = pendRow;
  mappings[idx].col      = pendCol;
  mappings[idx].directId = pendDirect;
  strncpy(mappings[idx].name, name, 31);
  mappings[idx].name[31] = '\0';

  Serial.printf(">>> Registered as \"%s\"%s  (%d/%d mapped)\n",
                name, isUpdate ? " (updated)" : "",
                mapCount, NUM_ROWS * NUM_COLS + 4);
  Serial.println(">>> Ready for next key... (type 'save' when done)\n");
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
    mapState = WAITING_FOR_NAME;
    int existing = findMapping(1, 0, 0, id);
    Serial.printf("\n>>> %s detected (%s)", DIRECT_LABELS[id], DIRECT_PINS[id]);
    if (existing >= 0) Serial.printf("  [currently: \"%s\"]", mappings[existing].name);
    Serial.println();
    Serial.println("    Type key name (or press key on Mac):");
  }
}

void scanForKey_mapping() {
  if (mapState != WAITING_FOR_KEY) return;

  detectDirect(0, CAPS_PIN,   capsRaw, capsState, capsDb); if (mapState != WAITING_FOR_KEY) return;
  detectDirect(1, OPTION_PIN, optRaw,  optState,  optDb);  if (mapState != WAITING_FOR_KEY) return;
  detectDirect(2, SHIFT_PIN,  shftRaw, shftState, shftDb); if (mapState != WAITING_FOR_KEY) return;
  detectDirect(3, CMD_PIN,    cmdRaw,  cmdState,  cmdDb);  if (mapState != WAITING_FOR_KEY) return;

  for (int row = 0; row < NUM_ROWS; row++) {
    digitalWrite(ROWS[row], LOW);
    delayMicroseconds(settleUs);
    for (int col = 0; col < NUM_COLS; col++) {
      bool pressed = (digitalRead(COLS[col]) == LOW);
      if (pressed != rawMatrix[row][col]) {
        rawMatrix[row][col] = pressed;
        dbMatrix[row][col]  = millis();
      }
      if ((millis() - dbMatrix[row][col]) >= DEBOUNCE_MS) {
        if (rawMatrix[row][col] != matrix[row][col]) {
          matrix[row][col] = rawMatrix[row][col];
          if (matrix[row][col]) {
            pendType = 0; pendRow = row; pendCol = col;
            mapState = WAITING_FOR_NAME;
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
  Serial.println("Matrix:");
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      int idx = findMapping(0, r, c, 0);
      if (idx >= 0) Serial.printf("  [%d,%d] %-12s", r, c, mappings[idx].name);
    }
    Serial.println();
  }
  Serial.println("\nDirect pins:");
  for (int d = 0; d < 4; d++) {
    int idx = findMapping(1, 0, 0, d);
    if (idx >= 0) Serial.printf("  %-10s -> \"%s\"\n", DIRECT_LABELS[d], mappings[idx].name);
  }
  Serial.println("=======================\n");
}

void printMapHelp() {
  Serial.println();
  Serial.println("==========================================");
  Serial.println("Apple M0116 Key Mapper -> BLE HID  (v20)");
  Serial.println("==========================================");
  Serial.println();
  Serial.println("Press a key on the Apple keyboard, then");
  Serial.println("type its name on your Mac:");
  Serial.println();
  Serial.println("  Just hit Enter      -> \"Return\"");
  Serial.println("  Space then Enter    -> \"Space\"");
  Serial.println("  Tab then Enter      -> \"Tab\"");
  Serial.println("  Arrow keys          -> \"Up\"/\"Down\"/\"Left\"/\"Right\"");
  Serial.println("  Type a char + Enter -> that character");
  Serial.println("  Type a name + Enter -> e.g. \"F1\", \"Esc\"");
  Serial.println("  Type \"skip\"         -> skip this key");
  Serial.println();
  Serial.println("Commands (while waiting for a key press):");
  Serial.println("  map     - show all mappings so far");
  Serial.println("  undo    - remove last mapping");
  Serial.println("  count   - show mapped key count");
  Serial.println("  save    - save to flash and reboot into BLE mode");
  Serial.println("  clear   - erase saved keymap");
  Serial.println("  help    - this message");
  Serial.println();
  Serial.println("Tip: hold CapsLock at boot for ~2s to force re-mapping.");
  Serial.println("==========================================");
}

void handleSerial_mapping() {
  if (!Serial.available()) return;
  String input = Serial.readStringUntil('\n');

  if (mapState == WAITING_FOR_NAME) {
    String raw = input;
    if (raw.length() > 0 && raw.charAt(raw.length() - 1) == '\r') {
      raw = raw.substring(0, raw.length() - 1);
    }
    String name;
    if (raw.length() == 0) name = "Return";
    else if (raw == " ")   name = "Space";
    else if (raw == "\t")  name = "Tab";
    else if (raw.charAt(0) == 0x1B) {
      if (raw.length() == 1) name = "Escape";
      else if (raw.length() >= 3 && raw.charAt(1) == '[') {
        switch (raw.charAt(2)) {
          case 'A': name = "Up"; break;
          case 'B': name = "Down"; break;
          case 'C': name = "Right"; break;
          case 'D': name = "Left"; break;
          case 'H': name = "Home"; break;
          case 'F': name = "End"; break;
          default:  name = "Escape"; break;
        }
      } else name = "Escape";
    } else if (raw.charAt(0) == 0x7F || raw.charAt(0) == 0x08) {
      name = "Delete";
    } else {
      raw.trim();
      if (raw.equalsIgnoreCase("skip")) {
        Serial.println(">>> Skipped. Ready for next key...\n");
        mapState = WAITING_FOR_KEY;
        silentScan();
        return;
      }
      name = raw;
    }
    registerKey(name.c_str());
    mapState = WAITING_FOR_KEY;
    silentScan();
    return;
  }

  input.trim();
  if (input.equalsIgnoreCase("map"))      printMap();
  else if (input.equalsIgnoreCase("undo")) {
    if (mapCount > 0) {
      mapCount--;
      Serial.printf(">>> Removed \"%s\". %d mapped.\n", mappings[mapCount].name, mapCount);
    } else Serial.println(">>> Nothing to undo.");
  }
  else if (input.equalsIgnoreCase("count"))
    Serial.printf(">>> %d / %d keys mapped\n", mapCount, NUM_ROWS * NUM_COLS + 4);
  else if (input.equalsIgnoreCase("help"))  printMapHelp();
  else if (input.equalsIgnoreCase("save")) {
    if (mapCount == 0) { Serial.println(">>> Nothing mapped. Map some keys first."); return; }
    if (saveMappings()) {
      Serial.printf(">>> Saved %d mappings to flash. Rebooting into BLE mode...\n", mapCount);
      delay(800);
      ESP.restart();
    } else Serial.println("!!! Failed to save to NVS.");
  }
  else if (input.equalsIgnoreCase("clear")) {
    clearMappings();
    Serial.println(">>> Cleared saved keymap.");
  }
  else if (input.length() > 0)
    Serial.println(">>> Waiting for Apple keyboard press. Type 'help' for commands.");
}

// ==================== KEY NAME -> HID ====================
//
// Returns true if `name` resolves to a HID keycode and/or modifier.
// `outKey` is the BleKeyboard-style key code (0 if pure modifier).
// `outMod` is set to one of KEY_LEFT_* if the name represents a modifier.

bool nameToHid(const char* nameRaw, uint8_t* outKey, uint8_t* outMod) {
  *outKey = 0; *outMod = 0;
  if (!nameRaw || !*nameRaw) return false;

  String n(nameRaw);
  String l = n; l.toLowerCase();

  // ---- Modifiers ----
  if (l == "shift" || l == "lshift" || l == "leftshift")           { *outMod = KEY_LEFT_SHIFT; return true; }
  if (l == "rshift" || l == "rightshift")                          { *outMod = KEY_RIGHT_SHIFT; return true; }
  if (l == "ctrl" || l == "control" || l == "lctrl")               { *outMod = KEY_LEFT_CTRL; return true; }
  if (l == "rctrl" || l == "rightctrl")                            { *outMod = KEY_RIGHT_CTRL; return true; }
  if (l == "alt" || l == "option" || l == "opt" || l == "lalt")    { *outMod = KEY_LEFT_ALT; return true; }
  if (l == "ralt" || l == "rightalt")                              { *outMod = KEY_RIGHT_ALT; return true; }
  if (l == "cmd" || l == "command" || l == "gui" || l == "win" ||
      l == "meta" || l == "lgui" || l == "lcmd")                   { *outMod = KEY_LEFT_GUI; return true; }
  if (l == "rgui" || l == "rcmd" || l == "rightcmd")               { *outMod = KEY_RIGHT_GUI; return true; }

  // ---- Named keys ----
  if (l == "return" || l == "enter")          { *outKey = KEY_RETURN;      return true; }
  if (l == "esc" || l == "escape")            { *outKey = KEY_ESC;         return true; }
  if (l == "tab")                             { *outKey = KEY_TAB;         return true; }
  if (l == "space" || l == "spacebar")        { *outKey = ' ';             return true; }
  if (l == "delete" || l == "backspace" ||
      l == "del")                             { *outKey = KEY_BACKSPACE;   return true; }
  if (l == "fwddelete" || l == "forwarddelete" ||
      l == "fdelete")                         { *outKey = KEY_DELETE;      return true; }
  if (l == "up" || l == "uparrow")            { *outKey = KEY_UP_ARROW;    return true; }
  if (l == "down" || l == "downarrow")        { *outKey = KEY_DOWN_ARROW;  return true; }
  if (l == "left" || l == "leftarrow")        { *outKey = KEY_LEFT_ARROW;  return true; }
  if (l == "right" || l == "rightarrow")      { *outKey = KEY_RIGHT_ARROW; return true; }
  if (l == "home")                            { *outKey = KEY_HOME;        return true; }
  if (l == "end")                             { *outKey = KEY_END;         return true; }
  if (l == "pageup" || l == "pgup")           { *outKey = KEY_PAGE_UP;     return true; }
  if (l == "pagedown" || l == "pgdn")         { *outKey = KEY_PAGE_DOWN;   return true; }
  if (l == "insert" || l == "ins")            { *outKey = KEY_INSERT;      return true; }
  if (l == "capslock" || l == "caps")         { *outKey = KEY_CAPS_LOCK;   return true; }

  // ---- Function keys ----
  if (l.length() >= 2 && l.charAt(0) == 'f') {
    int fn = l.substring(1).toInt();
    if (fn >= 1 && fn <= 12) {
      static const uint8_t fkeys[] = {
        KEY_F1, KEY_F2, KEY_F3, KEY_F4,  KEY_F5,  KEY_F6,
        KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12
      };
      *outKey = fkeys[fn - 1];
      return true;
    }
  }

  // ---- Single ASCII printable character ----
  if (n.length() == 1) {
    char c = n.charAt(0);
    if ((uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) { *outKey = (uint8_t)c; return true; }
  }

  return false;
}

// ==================== BLE MODE ====================

// Per-cell pressed state in BLE mode (so we know what to release on edges).
uint8_t bleKey [9][9] = {{0}};
uint8_t bleMod [9][9] = {{0}};
uint8_t bleDirectKey[4] = {0};
uint8_t bleDirectMod[4] = {0};

void blePressMapping(uint8_t type, uint8_t row, uint8_t col, uint8_t did) {
  int idx = findMapping(type, row, col, did);
  if (idx < 0) return;
  uint8_t kc, mod;
  if (!nameToHid(mappings[idx].name, &kc, &mod)) return;
  if (mod) bleKeyboard.press(mod);
  if (kc)  bleKeyboard.press(kc);
  if (type == 0) { bleKey[row][col] = kc; bleMod[row][col] = mod; }
  else           { bleDirectKey[did] = kc; bleDirectMod[did] = mod; }
}

void bleReleaseMapping(uint8_t type, uint8_t row, uint8_t col, uint8_t did) {
  uint8_t kc = 0, mod = 0;
  if (type == 0) { kc = bleKey[row][col];   mod = bleMod[row][col];   bleKey[row][col] = 0;   bleMod[row][col] = 0; }
  else           { kc = bleDirectKey[did];  mod = bleDirectMod[did];  bleDirectKey[did] = 0;  bleDirectMod[did] = 0; }
  if (kc)  bleKeyboard.release(kc);
  if (mod) bleKeyboard.release(mod);
}

void bleEdgeDirect(int id, int pin, bool &raw, bool &st, unsigned long &db) {
  bool pressed = (digitalRead(pin) == LOW);
  if (pressed != raw) { raw = pressed; db = millis(); }
  if ((millis() - db) >= DEBOUNCE_MS && raw != st) {
    st = raw;
    if (st) blePressMapping(1, 0, 0, id);
    else    bleReleaseMapping(1, 0, 0, id);
  }
}

void scanForKey_ble() {
  if (!bleKeyboard.isConnected()) return;

  bleEdgeDirect(0, CAPS_PIN,   capsRaw, capsState, capsDb);
  bleEdgeDirect(1, OPTION_PIN, optRaw,  optState,  optDb);
  bleEdgeDirect(2, SHIFT_PIN,  shftRaw, shftState, shftDb);
  bleEdgeDirect(3, CMD_PIN,    cmdRaw,  cmdState,  cmdDb);

  for (int row = 0; row < NUM_ROWS; row++) {
    digitalWrite(ROWS[row], LOW);
    delayMicroseconds(settleUs);
    for (int col = 0; col < NUM_COLS; col++) {
      bool pressed = (digitalRead(COLS[col]) == LOW);
      if (pressed != rawMatrix[row][col]) {
        rawMatrix[row][col] = pressed;
        dbMatrix[row][col]  = millis();
      }
      if ((millis() - dbMatrix[row][col]) >= DEBOUNCE_MS) {
        if (rawMatrix[row][col] != matrix[row][col]) {
          matrix[row][col] = rawMatrix[row][col];
          if (matrix[row][col]) blePressMapping(0, row, col, 0);
          else                  bleReleaseMapping(0, row, col, 0);
        }
      }
    }
    digitalWrite(ROWS[row], HIGH);
    delayMicroseconds(settleUs);
  }
}

void releaseAllBle() {
  bleKeyboard.releaseAll();
  memset(bleKey, 0, sizeof(bleKey));
  memset(bleMod, 0, sizeof(bleMod));
  memset(bleDirectKey, 0, sizeof(bleDirectKey));
  memset(bleDirectMod, 0, sizeof(bleDirectMod));
}

void handleSerial_ble() {
  if (!Serial.available()) return;
  String input = Serial.readStringUntil('\n');
  input.trim();
  if (input.equalsIgnoreCase("remap") || input.equalsIgnoreCase("clear")) {
    Serial.println(">>> Erasing keymap and rebooting into mapping mode...");
    clearMappings();
    delay(500);
    ESP.restart();
  } else if (input.equalsIgnoreCase("map")) {
    printMap();
  } else if (input.equalsIgnoreCase("status")) {
    Serial.printf(">>> BLE %s, %d mappings\n",
                  bleKeyboard.isConnected() ? "connected" : "advertising",
                  mapCount);
  } else if (input.length() > 0) {
    Serial.println(">>> BLE mode. Commands: status, map, remap (wipes keymap & reboots).");
  }
}

// ==================== SETUP / LOOP ====================

bool capsHeldAtBoot() {
  pinMode(CAPS_PIN, INPUT_PULLUP);
  delay(20);
  if (digitalRead(CAPS_PIN) != LOW) return false;
  unsigned long t0 = millis();
  while (millis() - t0 < 2000) {
    if (digitalRead(CAPS_PIN) != LOW) return false;
    delay(10);
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(800);

  if (capsHeldAtBoot()) {
    Serial.println("\n>>> CapsLock held at boot - wiping keymap, entering mapping mode.");
    clearMappings();
  }

  bool haveMap = loadMappings();
  mode = haveMap ? MODE_BLE : MODE_MAPPING;

  setupPins();
  silentScan();

  if (mode == MODE_BLE) {
    Serial.println("\n==========================================");
    Serial.println("Apple M0116 -> BLE HID  (v20)  BLE MODE");
    Serial.println("==========================================");
    Serial.printf("Loaded %d mappings from flash.\n", mapCount);
    Serial.println("Pair from your computer's Bluetooth menu:");
    Serial.println("  Device name: \"Apple M0116\"");
    Serial.println("Commands: status, map, remap");
    bleKeyboard.begin();
  } else {
    printMapHelp();
    Serial.println("\nReady! Press a key on the Apple keyboard...\n");
  }
}

void loop() {
  if (mode == MODE_BLE) {
    bool now = bleKeyboard.isConnected();
    if (now != bleWasConnected) {
      bleWasConnected = now;
      Serial.println(now ? ">>> BLE connected." : ">>> BLE disconnected, advertising.");
      if (!now) releaseAllBle();
    }
    scanForKey_ble();
    handleSerial_ble();
  } else {
    scanForKey_mapping();
    handleSerial_mapping();
  }
}
