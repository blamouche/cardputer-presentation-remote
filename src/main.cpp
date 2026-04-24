#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>

// M5Cardputer's Keyboard_def.h defines these as numeric macros, which clashes
// with BleKeyboard's `const uint8_t KEY_* = ...` declarations. We only read the
// Cardputer keyboard via chars (state.word), so dropping the macros is safe.
#undef KEY_LEFT_CTRL
#undef KEY_LEFT_SHIFT
#undef KEY_LEFT_ALT
#undef KEY_FN
#undef KEY_OPT
#undef KEY_BACKSPACE
#undef KEY_TAB
#undef KEY_ENTER

#include <BleKeyboard.h>

// SD pins on Cardputer (ADV uses the same pinout as the original)
#define SD_SCK  40
#define SD_MISO 39
#define SD_MOSI 14
#define SD_CS   12

#define CONFIG_PATH "/cardputer-presentation-remote.json"

struct KeyMapping {
    uint8_t left;
    uint8_t right;
    uint8_t up;
    uint8_t down;
};

struct Config {
    String   deviceName;
    String   manufacturer;
    uint8_t  batteryLevel;
    KeyMapping mapping;
};

Config config;
BleKeyboard* bleKeyboard = nullptr;
String configStatus = "defaults";
String lastKey      = "-";
bool   prevConnected = false;

// Map a human-readable key name (from config.json) to a BleKeyboard code.
uint8_t parseKey(const char* name) {
    String s(name);
    s.toUpperCase();
    if (s == "LEFT"  || s == "LEFT_ARROW")  return KEY_LEFT_ARROW;
    if (s == "RIGHT" || s == "RIGHT_ARROW") return KEY_RIGHT_ARROW;
    if (s == "UP"    || s == "UP_ARROW")    return KEY_UP_ARROW;
    if (s == "DOWN"  || s == "DOWN_ARROW")  return KEY_DOWN_ARROW;
    if (s == "PAGE_UP"   || s == "PAGEUP")   return KEY_PAGE_UP;
    if (s == "PAGE_DOWN" || s == "PAGEDOWN") return KEY_PAGE_DOWN;
    if (s == "HOME")   return KEY_HOME;
    if (s == "END")    return KEY_END;
    if (s == "ESC"    || s == "ESCAPE") return KEY_ESC;
    if (s == "ENTER"  || s == "RETURN") return KEY_RETURN;
    if (s == "TAB")    return KEY_TAB;
    if (s == "SPACE")  return ' ';
    if (s == "F5")     return KEY_F5;
    if (s.length() == 1) return (uint8_t)s[0];
    return 0;
}

void applyDefaults() {
    config.deviceName   = "Cardputer Remote";
    config.manufacturer = "M5Stack";
    config.batteryLevel = 100;
    config.mapping.left  = KEY_LEFT_ARROW;
    config.mapping.right = KEY_RIGHT_ARROW;
    config.mapping.up    = KEY_UP_ARROW;
    config.mapping.down  = KEY_DOWN_ARROW;
}

bool writeDefaultConfig() {
    File f = SD.open(CONFIG_PATH, FILE_WRITE);
    if (!f) return false;

    JsonDocument doc;
    doc["device_name"]  = config.deviceName;
    doc["manufacturer"] = config.manufacturer;
    JsonObject keys = doc["keys"].to<JsonObject>();
    keys["left"]  = "LEFT_ARROW";
    keys["right"] = "RIGHT_ARROW";
    keys["up"]    = "UP_ARROW";
    keys["down"]  = "DOWN_ARROW";

    bool ok = serializeJsonPretty(doc, f) > 0;
    f.close();
    return ok;
}

bool loadConfig() {
    applyDefaults();

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SPI)) {
        configStatus = "no SD";
        return false;
    }
    if (!SD.exists(CONFIG_PATH)) {
        if (writeDefaultConfig()) {
            configStatus = "created";
        } else {
            configStatus = "create err";
        }
        return false;
    }

    File f = SD.open(CONFIG_PATH, FILE_READ);
    if (!f) { configStatus = "open err"; return false; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        configStatus = String("json err: ") + err.c_str();
        return false;
    }

    if (doc["device_name"].is<const char*>())
        config.deviceName = String((const char*)doc["device_name"]);
    if (doc["manufacturer"].is<const char*>())
        config.manufacturer = String((const char*)doc["manufacturer"]);

    JsonObject keys = doc["keys"];
    if (!keys.isNull()) {
        if (keys["left"].is<const char*>())  config.mapping.left  = parseKey(keys["left"]);
        if (keys["right"].is<const char*>()) config.mapping.right = parseKey(keys["right"]);
        if (keys["up"].is<const char*>())    config.mapping.up    = parseKey(keys["up"]);
        if (keys["down"].is<const char*>())  config.mapping.down  = parseKey(keys["down"]);
    }

    configStatus = "loaded";
    return true;
}

void drawUI() {
    auto& d = M5Cardputer.Display;
    d.fillScreen(BLACK);

    d.setTextColor(WHITE);
    d.setTextSize(2);
    d.setCursor(5, 5);
    d.print(config.deviceName);

    d.setTextSize(1);
    d.setCursor(5, 32);
    d.setTextColor(bleKeyboard && bleKeyboard->isConnected() ? GREEN : YELLOW);
    d.printf("BLE: %s", (bleKeyboard && bleKeyboard->isConnected()) ? "CONNECTED" : "WAITING");

    d.setTextColor(WHITE);
    d.setCursor(5, 48);
    d.printf("Config: %s", configStatus.c_str());

    d.setCursor(5, 64);
    d.printf("Last: %s", lastKey.c_str());

    d.setTextColor(DARKGREY);
    d.setCursor(5, 100);
    d.print(",  .  ;  /   =>  L  D  U  R");
    d.setCursor(5, 114);
    d.print("Edit " CONFIG_PATH " on SD");
}

void sendKey(uint8_t code, const char* label) {
    if (!bleKeyboard || !bleKeyboard->isConnected() || code == 0) return;
    bleKeyboard->write(code);
    lastKey = label;
    drawUI();
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(120);
    Serial.begin(115200);

    loadConfig();

    bleKeyboard = new BleKeyboard(
        config.deviceName.c_str(),
        config.manufacturer.c_str(),
        config.batteryLevel
    );
    bleKeyboard->begin();

    drawUI();
}

void loop() {
    M5Cardputer.update();

    bool nowConnected = bleKeyboard->isConnected();
    if (nowConnected != prevConnected) {
        prevConnected = nowConnected;
        drawUI();
    }

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto state = M5Cardputer.Keyboard.keysState();
        for (char c : state.word) {
            switch (c) {
                case ',': sendKey(config.mapping.left,  "LEFT");  break;
                case '.': sendKey(config.mapping.down,  "DOWN");  break;
                case ';': sendKey(config.mapping.up,    "UP");    break;
                case '/': sendKey(config.mapping.right, "RIGHT"); break;
            }
        }
    }

    delay(10);
}
