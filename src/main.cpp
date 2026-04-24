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
#include <FastLED.h>

// SD pins on Cardputer (ADV uses the same pinout as the original)
#define SD_SCK  40
#define SD_MISO 39
#define SD_MOSI 14
#define SD_CS   12

// On-board WS2812 RGB LED
#define LED_PIN        21
#define LED_NUM        1
#define LED_BRIGHTNESS 40
#define LED_FLASH_MS   180

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
bool   prevConnected = false;

enum Direction { DIR_NONE, DIR_LEFT, DIR_RIGHT, DIR_UP, DIR_DOWN };
Direction currentDir = DIR_NONE;
uint32_t  pressTimeMs = 0;
uint32_t  lastDrawMs  = 0;

CRGB     leds[LED_NUM];
CRGB     prevLedColor = CRGB::Black;
uint32_t ledFlashStartMs = 0;

// 16x16 pixel-art critter. Two variants: happy (connected) and sad (waiting).
// Letters map to colors via pixelColor():
// .=transparent  B=body  W=eye white  M=mouth/nose  C=cheek  E=inner ear
static const char* PET_HAPPY[16] = {
    "...B........B...",
    "..BBB......BBB..",
    ".BEBBB....BBEBB.",
    "BBBBBBBBBBBBBBB.",
    "BBBBBBBBBBBBBBB.",
    "BBWWWBBBBBWWWBB.",
    "BBWWWBBBBBWWWBB.",
    "BBWWWBBBBBWWWBB.",
    "BBBBBBBBBBBBBBB.",
    "BCBBBBBMBBBBBCB.",
    "BBBBBMBBBMBBBBB.",
    "BBBBBBMMMBBBBBB.",
    "BBBBBBBBBBBBBBB.",
    ".BBBBBBBBBBBBB..",
    "..BBBBBBBBBBB...",
    "................",
};
static const char* PET_SAD[16] = {
    "...B........B...",
    "..BBB......BBB..",
    ".BEBBB....BBEBB.",
    "BBBBBBBBBBBBBBB.",
    "BBBBBBBBBBBBBBB.",
    "BBWWWBBBBBWWWBB.",
    "BBWWWBBBBBWWWBB.",
    "BBWWWBBBBBWWWBB.",
    "BBBBBBBBBBBBBBB.",
    "BCBBBBBMBBBBBCB.",
    "BBBBBBMMMBBBBBB.",
    "BBBBBMBBBMBBBBB.",
    "BBBBBBBBBBBBBBB.",
    ".BBBBBBBBBBBBB..",
    "..BBBBBBBBBBB...",
    "................",
};
static const int PET_W = 16;
static const int PET_H = 16;
static const int PET_SCALE = 3;

uint16_t COL_BODY = 0, COL_CHEEK = 0, COL_MOUTH = 0, COL_INNER = 0;

M5Canvas canvas(&M5Cardputer.Display);

uint16_t pixelColor(char ch) {
    switch (ch) {
        case 'B': return COL_BODY;
        case 'W': return WHITE;
        case 'M': return COL_MOUTH;
        case 'C': return COL_CHEEK;
        case 'E': return COL_INNER;
        default:  return BLACK;
    }
}

const char* dirLabel(Direction d) {
    switch (d) {
        case DIR_LEFT:  return "LEFT";
        case DIR_RIGHT: return "RIGHT";
        case DIR_UP:    return "UP";
        case DIR_DOWN:  return "DOWN";
        default:        return "";
    }
}

void updateLed() {
    CRGB target;
    if (millis() - ledFlashStartMs < LED_FLASH_MS) {
        target = CRGB::Blue;
    } else if (bleKeyboard && bleKeyboard->isConnected()) {
        target = CRGB::Green;
    } else {
        target = CRGB::Red;
    }
    if (target != prevLedColor) {
        leds[0] = target;
        FastLED.show();
        prevLedColor = target;
    }
}

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
    config.deviceName   = "Slides Remote";
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

void drawScene() {
    canvas.fillScreen(BLACK);
    canvas.setTextSize(1);

    // Header: device name (left) + BLE state (right)
    canvas.setTextColor(WHITE);
    canvas.setCursor(0, 1);
    canvas.print(config.deviceName);

    bool connected = bleKeyboard && bleKeyboard->isConnected();
    const char* statusStr = connected ? "CONNECTED" : "WAITING";
    const int ICON_W = 7;
    const int ICON_PAD = 2;
    int totalW = ICON_W + ICON_PAD + 6 * (int)strlen(statusStr);
    int startX = 240 - totalW;
    int cx = startX + ICON_W / 2;
    int cy = 1 + 3;
    uint16_t color = connected ? GREEN : YELLOW;
    if (connected) {
        canvas.fillCircle(cx, cy, 3, color);
    } else {
        canvas.drawCircle(cx, cy, 3, color);
        canvas.drawLine(cx, cy, cx,     cy - 2, color);
        canvas.drawLine(cx, cy, cx + 2, cy,     color);
    }
    canvas.setTextColor(color);
    canvas.setCursor(startX + ICON_W + ICON_PAD, 1);
    canvas.print(statusStr);

    // Divider
    canvas.drawFastHLine(0, 11, 240, DARKGREY);

    // Hop offset on key press: 7 → 0 over ~320ms with quadratic ease-out
    int offX = 0, offY = 0;
    if (currentDir != DIR_NONE) {
        uint32_t elapsed = millis() - pressTimeMs;
        if (elapsed < 320) {
            float t = elapsed / 320.0f;
            float k = (1.0f - t) * (1.0f - t);
            int mag = (int)(7 * k);
            switch (currentDir) {
                case DIR_LEFT:  offX = -mag; break;
                case DIR_RIGHT: offX =  mag; break;
                case DIR_UP:    offY = -mag; break;
                case DIR_DOWN:  offY =  mag; break;
                default: break;
            }
        }
    }

    // Idle breathing: 1 pixel up/down cycling ~1.4s
    int bob = ((millis() / 700) & 1) ? -1 : 0;

    // Render the critter centered in the animation area
    const char** sprite = connected ? PET_HAPPY : PET_SAD;
    const int renderW = PET_W * PET_SCALE;
    const int renderH = PET_H * PET_SCALE;
    const int CENTER_X = 120;
    const int CENTER_Y = 72;
    int spriteX = CENTER_X - renderW / 2 + offX;
    int spriteY = CENTER_Y - renderH / 2 + offY + bob;

    for (int row = 0; row < PET_H; row++) {
        for (int col = 0; col < PET_W; col++) {
            char ch = sprite[row][col];
            if (ch == '.') continue;
            canvas.fillRect(spriteX + col * PET_SCALE,
                            spriteY + row * PET_SCALE,
                            PET_SCALE, PET_SCALE, pixelColor(ch));
        }
    }

    // Pupils follow the current direction (±1 inside the 3x3 eye white)
    int dx = 0, dy = 0;
    switch (currentDir) {
        case DIR_LEFT:  dx = -1; break;
        case DIR_RIGHT: dx =  1; break;
        case DIR_UP:    dy = -1; break;
        case DIR_DOWN:  dy =  1; break;
        default: break;
    }
    canvas.fillRect(spriteX + (3 + dx) * PET_SCALE,
                    spriteY + (6 + dy) * PET_SCALE,
                    PET_SCALE, PET_SCALE, BLACK);
    canvas.fillRect(spriteX + (11 + dx) * PET_SCALE,
                    spriteY + (6 + dy) * PET_SCALE,
                    PET_SCALE, PET_SCALE, BLACK);

    // Footer: the currently selected key
    const char* label = dirLabel(currentDir);
    if (label[0]) {
        canvas.setTextColor(DARKGREY);
        int lx = (240 - 6 * (int)strlen(label)) / 2;
        canvas.setCursor(lx, 126);
        canvas.print(label);
    }

    canvas.pushSprite(0, 0);
}

void sendKey(uint8_t code, Direction dir, const char* /*label*/) {
    if (!bleKeyboard || !bleKeyboard->isConnected() || code == 0) return;
    // write() does press+release back-to-back which can leave phantom
    // modifier bits stuck on BLE hosts (macOS sees Ctrl-held → trackpad
    // clicks become secondary clicks; browsers see Cmd+Arrow → jump to
    // end of document). Explicit press/release with a small gap plus a
    // defensive releaseAll() clears the HID report reliably.
    bleKeyboard->press(code);
    delay(10);
    bleKeyboard->release(code);
    bleKeyboard->releaseAll();
    currentDir = dir;
    pressTimeMs = millis();
    ledFlashStartMs = pressTimeMs;
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(120);
    Serial.begin(115200);

    loadConfig();

    COL_BODY  = M5Cardputer.Display.color565(0xD8, 0xA2, 0x5C);
    COL_CHEEK = M5Cardputer.Display.color565(0xFF, 0x9A, 0xB1);
    COL_MOUTH = M5Cardputer.Display.color565(0x4A, 0x2A, 0x0A);
    COL_INNER = COL_CHEEK;

    canvas.createSprite(240, 135);

    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_NUM);
    FastLED.setBrightness(LED_BRIGHTNESS);
    leds[0] = CRGB::Red;
    FastLED.show();
    prevLedColor = CRGB::Red;

    bleKeyboard = new BleKeyboard(
        config.deviceName.c_str(),
        config.manufacturer.c_str(),
        config.batteryLevel
    );
    bleKeyboard->begin();

    drawScene();
}

void loop() {
    M5Cardputer.update();

    bool nowConnected = bleKeyboard->isConnected();
    if (nowConnected != prevConnected) {
        prevConnected = nowConnected;
    }

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto state = M5Cardputer.Keyboard.keysState();
        for (char c : state.word) {
            switch (c) {
                case ',': sendKey(config.mapping.left,  DIR_LEFT,  "LEFT");  break;
                case '.': sendKey(config.mapping.down,  DIR_DOWN,  "DOWN");  break;
                case ';': sendKey(config.mapping.up,    DIR_UP,    "UP");    break;
                case '/': sendKey(config.mapping.right, DIR_RIGHT, "RIGHT"); break;
            }
        }
    }

    // Redraw at ~30fps for smooth hop + breathing animation
    uint32_t now = millis();
    if (now - lastDrawMs >= 33) {
        lastDrawMs = now;
        drawScene();
    }

    updateLed();

    delay(5);
}
