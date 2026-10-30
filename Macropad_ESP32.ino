#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <Keypad.h>
#include <Wire.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <time.h>
#include <ESPmDNS.h>
#include "mbedtls/sha256.h"

const String css = R"rawliteral(
    <!DOCTYPE html><html><head><meta charset='utf-8'>
    <meta name='viewport' content='width=device-width,initial-scale=1'>
    <style>
            body {
                background-color: #282828;
            }
            header {
                width: 85%;
                margin: 0 auto;
                margin-bottom: 20px;
                text-align: left;
                background-color: #444444;
                border: 3px solid #555555;
                border-radius: 10px;    
            }
            div#main {
                width: 85%;
                margin: 20px auto;
                text-align: center;
                background-color: #444444;
                border: 3px solid #555555;
                border-radius: 10px;
            }
            div.main {
                width: 85%;
                margin: 0 auto;
                text-align: left;
                background-color: #444444;
                border: 3px solid #555555;
                border-radius: 10px;
            }
            a {
                color: #ffffff;
                text-decoration: underline;
            }
            a:hover {
                color: #ffffff;
                text-decoration: none;
            }
            p {
                margin: 10px;
                color: #ffffff;
            }
            h1, h2, h3 {
                margin: 10px;
                color: #ffffff;
            }
            label {
                color: #e1dfdf;
                margin: 10px;
            }
            input {
                background-color: #555555;
                color: #ffffff;
                border: 2px solid #333333;
                margin: 10px;
                padding: 3px;
                border-radius: 5px;     
            }
            input[type="submit"] {
                background-color: #666666;
                color: #ffffff;
                border: 2px solid #333333;
                margin-top: 5px;
                padding: 5px 10px;
                border-radius: 10px;
            }
            input[type="submit"]:hover {
                background-color: #555555;
                color: #ffffff;
                border: 2px solid #333333;
                margin-top: 5px;
                padding: 5px 10px;
                border-radius: 10px;
            }
            footer {
                width: 100%;
                position: fixed;
                bottom: 0;
                left: 0;
                right: 0;
                border-top: 2px solid #444444;
                border-radius: 5px;
                background-color: #282828;
                color: #888888;
                text-align: center;
                padding: 10px 0;
            }
            ul {
                list-style-type: none;
                margin: 0;
                padding: 0;
                display: flex;
            }
            ul li {
                float: left;
                border-right: 1px solid #bbb;
            }
            ul li a {
                display: block;
                color: white;
                text-align: center;
                padding: 20px 16px;
                text-decoration: none;
            }
            ul li a:hover {
                background-color: #555555;
            }
            ul li a.active {
                background-color: #555555;
            }
            #matrix {
                display: grid;
                gap: 6px;
                width: auto;
                margin: 10px;
                padding: 0;
                background: none;
                border: none;
            }
            #matrix input {
                width: 100%;
                box-sizing: border-box;
                margin: 0;
                text-align: center;
            }
                        .msg {
                width: 85%;
                margin: 0 auto 20px auto;
                padding: 12px 16px;
                border-radius: 10px;
                color: #ffffff;
                box-sizing: border-box;
            }
            .msg.ok  { background-color: #2e5d34; border: 3px solid #3f7d47; }
            .msg.err { background-color: #6b2b2b; border: 3px solid #8a3a3a; }

            body { font-family: system-ui, -apple-system, sans-serif; margin: 0; padding: 10px 0; }
            header, div#main, div.main { box-sizing: border-box; }
            div.main { margin-bottom: 20px; }
            ul { flex-wrap: wrap; }
            ul li { flex: 1 1 auto; }
            input[type="text"], input[type="number"], input[type="password"], select { font-size: 16px; }
            #matrix { overflow-x: auto; }

            @media (max-width: 600px) {
                header, div#main, div.main, .msg { width: 94%; }
                ul li a { padding: 14px 10px; }
                input[type="text"], input[type="number"], input[type="password"], select {
                    width: calc(100% - 20px);
                    box-sizing: border-box;
                }
                input[type="checkbox"], input[type="radio"] { width: 20px; height: 20px; }
                input[type="submit"] { padding: 10px 18px; font-size: 16px; }
            }
          </style>
      )rawliteral";

const String nav= R"rawliteral(
          <header>
            <nav>
              <h1>Macropad</h1>
              <ul>
                  <li><a href="/">home</a></li>
                  <li><a href="/config">config</a></li>
                  <li><a href="/settings">settings</a></li>
                  <li><a href="/logout">logout</a></li>
              </ul>
            </nav>
        </header>
)rawliteral";

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306* display = nullptr;
USBHIDKeyboard Keyboard;

const char* AP_NAME   =     "ESP32-Setup";
const char* AP_PASS   =     "";
const int   RESET_PIN =     0;
bool Can_Connect =          false;
bool HasKeypad =            false;
bool mqttConfigured =       false;
bool HasDisplay =           false;
bool DisplayConfigured =    false;
bool isPressed =            false;
bool HasRGB =               false;
bool HasSpi =               false;
bool i2cConfigured =        false;

long lastReconnectAttempt = 0;
String mqtt_server, MQTT_USER, MQTT_PASS;
const char* MQTT_ID = "macropad";

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

String UserUName, UserSalt, UserHash;
const int MAX_SESSIONS = 4;
const unsigned long SESSION_TIMEOUT = 30UL * 60UL * 1000UL;

struct Session {
  bool used;
  char token[33];
  unsigned long lastSeen;
};
Session sessions[MAX_SESSIONS];
int failedLogins;
unsigned long lockUntil = 0;

Preferences prefs;
WebServer server(80);
DNSServer dns;

unsigned long lastWifiCheck = 0;
bool wifiWasDown = false;

String savedSsid, savedPass, networkOptions, readstr;
int i2cSda = -1, i2cScl = -1;
SPIClass* spiBus = nullptr;
bool spiConfigured = false;
int DisplayType = -1;
int spiMosi = -1, spiSck = -1, spiMiso = -1;
int DisplayCs = -1, spiDc = -1, spiRst = -1;  
String pinVal(int p) { return p >= 0 ? String(p) : String(""); }
bool validPin(int p) { return p >= 0 && p <= 48; }
bool busPin(int p) { return p >= 0 && (p == spiMosi || p == spiSck || p == spiMiso); }

String tzString = "CET-1CEST,M3.5.0,M10.5.0/3";

const char* TZ_NAMES[] = {
  "Amsterdam / Berlin / Paris / Rome",
  "London",
  "Lisbon",
  "Helsinki / Athens",
  "Moscow",
  "New York",
  "Chicago",
  "Denver",
  "Los Angeles",
  "India",
  "Tokyo",
  "Sydney",
  "UTC"
};
const char* TZ_VALUES[] = {
  "CET-1CEST,M3.5.0,M10.5.0/3",
  "GMT0BST,M3.5.0/1,M10.5.0",
  "WET0WEST,M3.5.0/1,M10.5.0",
  "EET-2EEST,M3.5.0/3,M10.5.0/4",
  "MSK-3",
  "EST5EDT,M3.2.0,M11.1.0",
  "CST6CDT,M3.2.0,M11.1.0",
  "MST7MDT,M3.2.0,M11.1.0",
  "PST8PDT,M3.2.0,M11.1.0",
  "IST-5:30",
  "JST-9",
  "AEST-10AEDT,M10.1.0,M4.1.0/3",
  "UTC0"
};
const int TZ_COUNT = 13;

const byte MAX_PINS = 8;
byte rowPins[MAX_PINS];
byte colPins[MAX_PINS];
int rowCount;
int colCount;
char keys[MAX_PINS*MAX_PINS];
Keypad* keypad = nullptr;

const int MAX_RGB_LEDS = 6;
int RGB_Leds =0;
int rgbAttached;
bool RGBReady = 0;
const bool RGB_COMMON_ANODE = false;
int RGB_Common;
byte rgb_Pins[MAX_RGB_LEDS*3];
bool ledOn[MAX_RGB_LEDS];
byte ledR[MAX_RGB_LEDS], ledG[MAX_RGB_LEDS], ledB[MAX_RGB_LEDS], ledBri[MAX_RGB_LEDS];
bool rgbDirty = false;
unsigned long rgbDirtyAt = 0;

void applyLed(byte i);
void publishLedState(byte i);
void publishKeyDiscovery();

const int MAX_POTS = 4;
bool HasPot = false;
int potCount = 0;
byte potPins[MAX_POTS];
int potLast[MAX_POTS];
float potSmooth[MAX_POTS];
unsigned long lastPotRead = 0;

unsigned long lastDraw = 0;

byte    keyType[MAX_PINS * MAX_PINS];
String  keyText[MAX_PINS * MAX_PINS];
int     keyInt[MAX_PINS * MAX_PINS];

const uint8_t KP_CODES[10] = {KEY_KP_0, KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4, KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9};

char lastPressed;

WiFiClient espClient;
PubSubClient client(espClient);


String haId(byte i) {
  return "macropad_key_" + String(i / colCount + 1) + "_" + String(i % colCount + 1);
}

void sendError(int code, const String& text) {
  String html = css;
  html += "<title>Error</title></head><body>"
          "<div id='main'><h2>Something went wrong</h2>"
          "<div class='msg err' style='width:auto;margin:10px'>" + esc(text) + "</div>"
          "<p><a href='javascript:history.back()'>Go back</a> | <a href='/'>Home</a></p>"
          "</div></body></html>";
  server.send(code, "text/html", html);
}

String flashText(const String& c) {
  if (c == "keymap")    return "Keypad saved.";
  if (c == "mqtt")      return "MQTT settings saved.";
  if (c == "i2c")       return "I2C pins saved.";
  if (c == "spi")       return "SPI bus saved.";
  if (c == "screen")    return "Display settings saved.";
  if (c == "rgbled")    return "RGB LED settings saved.";
  if (c == "pots")      return "Potentiometers saved.";
  if (c == "timezone")  return "Time zone saved.";
  if (c == "login")     return "You are logged in.";
  if (c == "register")  return "Account saved.";
  if (c == "loggedout") return "You are logged out.";
  return "";
}

String takeFlash() {
  String cookie = server.header("Cookie");
  int p = cookie.indexOf("flash=");
  if (p < 0) return "";
  int e = cookie.indexOf(';', p);
  String code = cookie.substring(p + 6, e < 0 ? cookie.length() : e);
  server.sendHeader("Set-Cookie", "flash=; Path=/; Max-Age=0; SameSite=Strict");
  String text = flashText(code);
  if (text.isEmpty()) return "";
  return "<div class='msg ok'>" + text + "</div>";
}

void finishSave(const String& redirect, const char* code) {
  const char* to = redirect == "config" ? "/config" : redirect == "settings" ? "/settings" : "/";
  server.sendHeader("Set-Cookie", String("flash=") + code + "; Path=/; Max-Age=15; SameSite=Strict");
  redirectTo(to);
}

String jsEsc(const String& s) {
  String o;
  for (char c : s) {
    if (c == '\\') o += "\\\\";
    else if (c == '"') o += "\\\"";
    else if (c == '\n' || c == '\r') o += ' ';
    else if (c == '<') o += "\\x3C";
    else o += c;
  }
  return o;
}

String randomHex(int bytes) {
  String s;
  for (int i = 0; i < bytes; i++) {
    char b[3];
    sprintf(b, "%02x", (uint8_t)(esp_random() & 0xFF));
    s += b;
  }
  return s;
}

bool sameText(const String& a, const String& b) {
  if (a.length() != b.length()) return false;
  uint diff = 0;
  for (unsigned int i = 0; i < a.length(); i++) diff |= a[i] ^ b[i];
  return diff == 0;
}

int currentSession() {
  String cookie = server.header("Cookie");
  int p = cookie.indexOf("sid=");
  if (p < 0) return -1;
  String t = cookie.substring(p + 4, p + 4 + 32);
  if (t.length() != 32) return -1;

  unsigned long now = millis();
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].used) continue;
    if (now - sessions[i].lastSeen > SESSION_TIMEOUT) {
      sessions[i].used = false;
      continue;
    }
    if (sameText(t, String(sessions[i].token))) {
      sessions[i].lastSeen = now;
      return i;
    }
  }
  return -1;
}

void startSession() {
  unsigned long now = millis();
  int slot = 0;
  unsigned long oldestage = 0;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].used || now - sessions[i].lastSeen > SESSION_TIMEOUT) {slot = i; break;}
    unsigned long age = now - sessions[i].lastSeen;
    if (age >= oldestage) {oldestage = age; slot = i;}
  }
  String t = randomHex(16);
  strcpy(sessions[slot].token, t.c_str());
  sessions[slot].lastSeen = now;
  sessions[slot].used = true;
  server.sendHeader("Set-Cookie", "sid=" + t + "; Path=/; HttpOnly; SameSite=Strict");
}

void clearSessions() {
  for (int i = 0; i < MAX_SESSIONS; i++) sessions[i].used = false;
}

bool requireLogin() {
  if (currentSession() >= 0) return true;
  if (!Can_Connect) redirectTo("/");
  else if (UserHash.isEmpty()) redirectTo("/register");
  else redirectTo("/login");
  return false;
}

String sha256Hex(const String& in) {
  uint8_t out[32];
  mbedtls_sha256((const unsigned char*)in.c_str(), in.length(), out, 0);
  char hex[65];
  for (int i = 0; i<32; i++) sprintf(hex + i * 2, "%02x", out[i]);
  return String(hex);
}

void setCredentials(const String& user, const String& pass) {
  String salt = randomHex(16);
  String hash = sha256Hex(salt + pass);

  prefs.begin("credentials", false);
  prefs.putString("username", user);
  prefs.putString("salt", salt);
  prefs.putString("hash", hash);
  prefs.end();

  UserUName = user;
  UserSalt = salt;
  UserHash = hash;
}

void loadCredentials() {
  prefs.begin("credentials", false);
  UserUName = prefs.getString("username", "");
  UserSalt  = prefs.getString("salt", "");
  UserHash  = prefs.getString("hash", "");
  prefs.end();
}

bool checkPassword(const String& user, const String& pass) {
  if (UserHash.isEmpty()) return false;
  String hash = sha256Hex(UserSalt + pass);
  bool okPass = sameText(hash, UserHash);
  bool okUser = sameText(user, UserUName);
  return okUser && okPass;
}

void handleLogout() {
  int s = currentSession();
  if (s >= 0) sessions[s].used = false;
  server.sendHeader("Set-Cookie", "sid=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
  server.sendHeader("Set-Cookie", "flash=loggedout; Path=/; Max-Age=15; SameSite=Strict");
  redirectTo("/login");
}

void callback(char* topic, byte* payload, unsigned int length) {
  String t(topic), p;
  for (unsigned int i = 0; i < length; i++) p += (char)payload[i];
  p.trim();
  Serial.println("MQTT [" + t + "] " + p);

  if (!t.startsWith("macropad/led/")) return;
  int slash = t.indexOf('/', 13);
  if (slash < 0) return;
  int idx = t.substring(13, slash).toInt() - 1;
  if (idx < 0 || idx >= RGB_Leds) return;
  String rest = t.substring(slash);

  if (rest == "/set") {
    ledOn[idx] = p.equalsIgnoreCase("ON");
  } else if (rest == "/brightness/set") {
    ledBri[idx] = constrain(p.toInt(), 0, 255);
  } else if (rest == "/rgb/set") {
    int r, g, b;
    if (sscanf(p.c_str(), "%d,%d,%d", &r, &g, &b) != 3) return;
    ledR[idx] = constrain(r, 0, 255);
    ledG[idx] = constrain(g, 0, 255);
    ledB[idx] = constrain(b, 0, 255);
  } else {
    return;
  }
  applyLed(idx);
  publishLedState(idx);
  rgbDirty = true;
  rgbDirtyAt = millis();
}

void mqttSetup() {
  prefs.begin("MQTT", true);
  mqtt_server = prefs.getString("ip", "");
  MQTT_USER = prefs.getString("username", "");
  MQTT_PASS = prefs.getString("password", "");
  prefs.end();
  
  mqttConfigured = !mqtt_server.isEmpty();
  if (mqttConfigured){
    client.setServer(mqtt_server.c_str(), 1883);
    client.setBufferSize(1024);
    client.setCallback(callback);
  }
}

bool i2cSetup() {
  if (i2cConfigured) {
    Serial.println("I2c already configured");
    return 1;
  }

  prefs.begin("i2c", true);
  i2cSda     = prefs.getInt("sda", -1);
  i2cScl     = prefs.getInt("scl", -1);
  prefs.end();

  if (i2cSda < 0 || i2cScl < 0) {
    Serial.println("No I2C config found");
    return 0;
  }

  Wire.begin(i2cSda, i2cScl);
  return 1;
}

void rgbWrite(byte pin, byte v) {
  ledcWrite(pin, RGB_Common == 1 ? 255 - v : v);
}

void rgbSetColor(byte led, byte r, byte g, byte b) {
  if (!RGBReady || led >= RGB_Leds) return;
  rgbWrite(rgb_Pins[led * 3],     r);
  rgbWrite(rgb_Pins[led * 3 + 1], g);
  rgbWrite(rgb_Pins[led * 3 + 2], b);
}

void rgbSetAll(byte r, byte g, byte b) {
  for (byte l = 0; l < RGB_Leds; l++) rgbSetColor(l, r, g, b);
}

void rgbTest() {
  const byte c[3][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
  for (byte k = 0; k < 3; k++) { rgbSetAll(c[k][0], c[k][1], c[k][2]); delay(400); }
  rgbSetAll(0, 0, 0);
}

String ledTopic(byte i, const char* suffix) {
  return "macropad/led/" + String(i + 1) + suffix;
}

void applyLed(byte i) {
  if (!ledOn[i]) { rgbSetColor(i, 0, 0, 0); return; }
  rgbSetColor(i, ledR[i] * ledBri[i] / 255,
                 ledG[i] * ledBri[i] / 255,
                 ledB[i] * ledBri[i] / 255);
}

void publishLedState(byte i) {
  if (!client.connected()) return;
  client.publish(ledTopic(i, "/state").c_str(), ledOn[i] ? "ON" : "OFF");
  client.publish(ledTopic(i, "/brightness").c_str(), String(ledBri[i]).c_str());
  String rgb = String(ledR[i]) + "," + String(ledG[i]) + "," + String(ledB[i]);
  client.publish(ledTopic(i, "/rgb").c_str(), rgb.c_str());
}

void publishRgbDiscovery() {
  if (!mqttConfigured || !client.connected()) return;
  for (byte i = 0; i < MAX_RGB_LEDS; i++) {
    String n = String(i + 1);
    String cfg = "homeassistant/light/macropad_led_" + n + "/config";

    if (!(HasRGB && RGBReady && i < RGB_Leds)) {
      client.publish(cfg.c_str(), "", true);
      continue;
    }
    String p = "{\"name\":\"Macropad LED " + n + "\","
               "\"unique_id\":\"macropad_led_" + n + "\","
               "\"command_topic\":\"" + ledTopic(i, "/set") + "\","
               "\"state_topic\":\"" + ledTopic(i, "/state") + "\","
               "\"brightness_command_topic\":\"" + ledTopic(i, "/brightness/set") + "\","
               "\"brightness_state_topic\":\"" + ledTopic(i, "/brightness") + "\","
               "\"rgb_command_topic\":\"" + ledTopic(i, "/rgb/set") + "\","
               "\"rgb_state_topic\":\"" + ledTopic(i, "/rgb") + "\","
               "\"availability_topic\":\"macropad/status\","
               "\"device\":{\"identifiers\":[\"macropad\"],\"name\":\"Macropad\",\"manufacturer\":\"DIY\"}}";
    client.publish(cfg.c_str(), p.c_str(), true);
  }
}

void saveRgbState() {
  byte buf[MAX_RGB_LEDS * 5];
  for (byte i = 0; i < MAX_RGB_LEDS; i++) {
    buf[i * 5]     = ledOn[i] ? 1 : 0;
    buf[i * 5 + 1] = ledR[i];
    buf[i * 5 + 2] = ledG[i];
    buf[i * 5 + 3] = ledB[i];
    buf[i * 5 + 4] = ledBri[i];
  }
  prefs.begin("rgbstate", false);
  prefs.putBytes("state", buf, sizeof(buf));
  prefs.end();
  Serial.println("RGB state saved");
}

void loadRgbState() {
  byte buf[MAX_RGB_LEDS * 5];
  prefs.begin("rgbstate", true);
  size_t len = prefs.getBytesLength("state");
  if (len == sizeof(buf)) prefs.getBytes("state", buf, len);
  prefs.end();
  if (len != sizeof(buf)) return;

  for (byte i = 0; i < MAX_RGB_LEDS; i++) {
    ledOn[i]  = buf[i * 5] == 1;
    ledR[i]   = buf[i * 5 + 1];
    ledG[i]   = buf[i * 5 + 2];
    ledB[i]   = buf[i * 5 + 3];
    ledBri[i] = buf[i * 5 + 4];
  }
}

void rgbStateLoop() {
  if (rgbDirty && millis() - rgbDirtyAt > 2000) {
    rgbDirty = false;
    saveRgbState();
  }
}

void rgbSetup() {
  for (byte i = 0; i < MAX_RGB_LEDS; i++) {
    ledOn[i] = false;
    ledR[i] = ledG[i] = ledB[i] = 255;
    ledBri[i] = 255;
  }
  for (int i = 0; i < rgbAttached; i++) ledcDetach(rgb_Pins[i]);
  rgbAttached = 0;
  RGBReady = false;

  prefs.begin("rgbled", true);
  HasRGB   = prefs.getBool("hasrgb", false);
  RGB_Leds = prefs.getInt("rgbleds", 0);
  RGB_Common = prefs.getInt("common", 0);
  size_t len = prefs.getBytesLength("pins");
  bool ok = RGB_Leds >= 1 && RGB_Leds <= MAX_RGB_LEDS && len == (size_t)RGB_Leds * 3;
  if (ok) prefs.getBytes("pins", rgb_Pins, len); else RGB_Leds = 0;
  prefs.end();

  if (!HasRGB || !ok) return;

  for (int i = 0; i < RGB_Leds * 3; i++) {
    if (!ledcAttach(rgb_Pins[i], 5000, 8)) {
      Serial.println("RGB: could not attach pin " + String(rgb_Pins[i]));
      return;
    }
    rgbAttached++;
  }
  RGBReady = true;
  loadRgbState();
  for (byte i = 0; i < RGB_Leds; i++) applyLed(i);
  Serial.println("RGB configured: " + String(RGB_Leds) + " LED(s)");
  publishRgbDiscovery();
  for (byte i = 0; i < RGB_Leds; i++) publishLedState(i);
}

String potTopic(byte i) {
  return "macropad/pot/" + String(i + 1);
}

void publishPotDiscovery() {
  if (!mqttConfigured || !client.connected()) return;
  for (byte i = 0; i < MAX_POTS; i++) {
    String n = String(i + 1);
    String cfg = "homeassistant/sensor/macropad_pot_" + n + "/config";

    if (i >= potCount) {
      client.publish(cfg.c_str(), "", true);
      continue;
    }
    String p = "{\"name\":\"Macropad Pot " + n + "\","
               "\"unique_id\":\"macropad_pot_" + n + "\","
               "\"state_topic\":\"" + potTopic(i) + "\","
               "\"unit_of_measurement\":\"%\","
               "\"availability_topic\":\"macropad/status\","
               "\"device\":{\"identifiers\":[\"macropad\"],\"name\":\"Macropad\",\"manufacturer\":\"DIY\"}}";
    client.publish(cfg.c_str(), p.c_str(), true);
  }
}

void potSetup() {
  potCount = 0;

  prefs.begin("pots", true);
  HasPot = prefs.getBool("haspot", false);
  int n = prefs.getInt("potcount", 0);
  size_t len = prefs.getBytesLength("pins");
  bool ok = n >= 1 && n <= MAX_POTS && len == (size_t)n;
  if (ok) prefs.getBytes("pins", potPins, len);
  prefs.end();

  if (HasPot && ok) {
    potCount = n;
    analogReadResolution(12);
    for (int i = 0; i < potCount; i++) {
      pinMode(potPins[i], INPUT);
      analogSetPinAttenuation(potPins[i], ADC_11db);
      potSmooth[i] = analogRead(potPins[i]);
      potLast[i] = -1;
    }
    Serial.println("Potentiometers configured: " + String(potCount));
  }
  publishPotDiscovery();
}

void potLoop() {
  if (potCount == 0) return;
  if (millis() - lastPotRead < 20) return;
  lastPotRead = millis();

  for (int i = 0; i < potCount; i++) {
    int raw = analogRead(potPins[i]);
    potSmooth[i] += (raw - potSmooth[i]) * 0.2;

    int pct = map(constrain((int)potSmooth[i], 30, 4065), 30, 4065, 0, 100);
    int diff = abs(pct - potLast[i]);

    if (potLast[i] < 0 || diff >= 2 || ((pct == 0 || pct == 100) && diff > 0)) {
      potLast[i] = pct;
      Serial.println("Pot " + String(i + 1) + ": " + String(pct) + "%");
      if (mqttConfigured && client.connected()) {
        client.publish(potTopic(i).c_str(), String(pct).c_str(), true);
      }
    }
  }
}

void drawTime() {
  if (!display) return;
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo, 100)){
    Serial.println("Failed to obtain time");
    return;
  }

  display->clearDisplay();
  display->setTextColor(WHITE);
  display->setTextSize(4);
  display->setCursor(0,0);
  display->print(&timeinfo, "%H");
  display->print(":");
  display->print(&timeinfo, "%M");
  display->display();
}

void freeDisplay() {
  delete display;
  display = nullptr;
}

bool SpiSetup() {
  if (spiConfigured) return true;
  prefs.begin("spi", true);
  spiMosi = prefs.getInt("mosi", -1);
  spiSck  = prefs.getInt("sck", -1);
  spiMiso = prefs.getInt("miso", -1);
  prefs.end();

  if (spiMosi < 0 || spiSck < 0) {
    Serial.println("No SPI bus config found");
    return false;
  }
  spiBus = new SPIClass(FSPI);
  spiBus->begin(spiSck, spiMiso, spiMosi, -1);
  spiConfigured = true;
  return true;
}

void spiBusReset() {
  if (spiBus) { spiBus->end(); delete spiBus; spiBus = nullptr; }
  spiConfigured = false;
}

void displaySetup() {
  DisplayConfigured = false;
  freeDisplay();

  prefs.begin("display", true);
  HasDisplay  = prefs.getBool("hasdisplay", false);
  DisplayType = prefs.getInt("displaytype", 1);
  DisplayCs   = prefs.getInt("cs", -1);
  spiDc       = prefs.getInt("dc", -1);
  spiRst      = prefs.getInt("rst", -1);
  prefs.end();
  if (!HasDisplay) return;

  if (DisplayType == 1) {
    if (!i2cSetup()) return;
    display = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
    if (!display->begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS, true, false)) {
      Serial.println("I2C display not found");
      freeDisplay();
      return;
    }
  }else if (DisplayType == 2) {
    if (!SpiSetup()) return;
    if (DisplayCs < 0 || spiDc < 0) { Serial.println("SPI display needs CS and DC"); return; }
    display = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, spiBus, spiDc, spiRst, DisplayCs);
    if (!display->begin(SSD1306_SWITCHCAPVCC, 0, true, false)) {
      Serial.println("SPI display not found");
      freeDisplay();
      return;
    }
  } else {
    return;
  }

  DisplayConfigured = true;
  lastDraw = 0;
  lastDraw = 0;
  Serial.println("Display configured");
}

boolean reconnect() {
  if (!mqttConfigured) return false;
  if (client.connect(MQTT_ID, MQTT_USER.c_str(), MQTT_PASS.c_str(),
                     "macropad/status", 0, true, "offline")) {
    Serial.println("MQTT connected");
    client.publish("macropad/status", "online", true);
    client.subscribe("macropad/led/+/set");
    client.subscribe("macropad/led/+/brightness/set");
    client.subscribe("macropad/led/+/rgb/set");
    publishRgbDiscovery();
    publishPotDiscovery();
     publishKeyDiscovery();
    for (byte i = 0; i < RGB_Leds; i++) publishLedState(i);
  } else {
    Serial.print("MQTT failed, rc=");
    Serial.println(client.state());
  }
  return client.connected();
}

String esc(const String& s) {
  String o;
  for (char c : s) {
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else if (c == '&') o += "&amp;";
    else o += c;
  }
  return o;
}

bool connectSaved(uint32_t timeoutMs = 30000) {
  if (savedSsid.isEmpty()) return false;
    WiFi.setHostname("macropad");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);  
  WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  uint32_t start = millis();
  Serial.println("connecting to network: " + savedSsid);
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

void applyTimezone(bool first) {
  if (first) {
    configTzTime(tzString.c_str(), "pool.ntp.org");
  } else {
    setenv("TZ", tzString.c_str(), 1);
    tzset();
  }
  Serial.println("Time zone: " + tzString);
}

void timeSetup() {
  prefs.begin("time", true);
  tzString = prefs.getString("tz", tzString);
  prefs.end();
  applyTimezone(true);
}

void wifiKeepAlive() {
  if (!Can_Connect) return;

  if (WiFi.status() == WL_CONNECTED) {
    if (wifiWasDown) {
      wifiWasDown = false;
      Serial.println("WiFi back, IP: " + WiFi.localIP().toString());
    }
    return;
  }

  if (!wifiWasDown) {
    wifiWasDown = true;
    Serial.println("WiFi lost");
  }

  if (millis() - lastWifiCheck < 10000) return;
  lastWifiCheck = millis();
  Serial.println("WiFi: trying to reconnect...");
  WiFi.disconnect();
  WiFi.begin(savedSsid.c_str(), savedPass.c_str());
}

void handleRoot() {
  String html = css;
    if (Can_Connect == true){
      if (!requireLogin()) return;
      html += R"rawliteral(
        <title> home </title>
      </head><body>)rawliteral";
      html += nav;
      html += takeFlash();
      html += R"rawliteral(
      <div id="main">
        <h1>Welcome to Macropad</h1>
        <p><a href="/config">update your config</a></p></br>
        <p>or <a href="/settings">update your settings</a></p>
      </div></body></html>
      )rawliteral";
    }
    else{
      html += R"rawliteral(<title>WiFi Setup</title>
      </head><body><div class="main">
      <h2>WiFi Setup</h2>
      <form method='POST' action='/save'>
      <input name='ssid' list='nets' placeholder='Network name' required>
      <input name='pass' type='password' placeholder='Password'>
      <button type='submit'>Save &amp; connect</button>
      <input type='hidden' name='form' value='wifi'>
      </form></div></body></html>)rawliteral";
    }
    server.send(200, "text/html", html);
}

bool readPins(const char* prefix, int n, byte* out, bool* used) {
  for (int i = 0; i < n; i++) {
    String name = String(prefix) + "_" + String(i);
    if (!server.hasArg(name)) return false;
    int p = server.arg(name).toInt();
    if (p < 0 || p > 48 || used[p]) return false;
    used[p] = true;
    out[i] = p;
  }
  return true;
}

void handleRegister() {
  if (!UserHash.isEmpty() && currentSession() < 0) { redirectTo("/login"); return; }
  String html = css;
  html += R"rawliteral(<title> regiser </title>
  </head><body>)rawliteral";
  html+= nav;
  html += takeFlash();
  html+= R"rawliteral(<div id='login-form'>
  <h1>Register</h1>
  <div class="main">
    <form action='/save' method='post'>
      <input type="hidden" name="form" value="register">
      <input type="hidden" name="redirect" value="home">
      <label for='username'>Username:</label><br>
      <input type='text' id='uname' name='uname'><br>
      <label for='pass'>Password:</label><br>
      <input type='password' id='pass' name='pass'><br>
      <label for='confirmpass'>Confirm Password:</label><br>
      <input type='password' id='confirmpass' name='confirmpass'><br>
      <input type='submit' value='Register'>
  </form></div></body></html>)rawliteral";
  server.send(200, "text/html", html);
}

void handleLogin() {
  if (UserHash.isEmpty()) { redirectTo("/register"); return; }
  String html = css;
  html +=  R"rawliteral(
    <title> Login </title>
  </head><body>)rawliteral";
  html += nav;
  html += takeFlash();
  html += R"rawliteral(<div class="main">
  <h2>Login</h2>
    <form action='/save' method='post'>
      <input type="hidden" name="form" value="login">
      <input type="hidden" name="redirect" value="home">
      <label for='username'>Username:</label><br>
      <input type='text' id='username' name='username'><br>
      <label for='password'>Password:</label><br>
      <input type='password' id='password' name='password'><br>
      <input type='submit' value='Login'>
  </form></div></body></html>)rawliteral";
  server.send(200, "text/html", html);
}

void handleConfig() {
  if (!requireLogin()) return;
  String html = css;
  html += R"rawliteral(
    <style>
      .pins, #matrix { display: grid; gap: 6px; margin: 10px; }
      .pins { grid-template-columns: repeat(auto-fit, minmax(70px, 1fr)); }
      .pins input, #matrix select, #matrix input { width: 100%; box-sizing: border-box; margin: 0; }
      .cell { display: flex; flex-direction: column; gap: 4px; }
      .cell small { color: #e1dfdf; }
      select { background-color: #555555; color: #ffffff; border: 2px solid #333333; border-radius: 5px; padding: 3px; }
    </style>
    <title>Config</title></head><body>
    )rawliteral";
      html += nav;
      html += takeFlash();
      html += R"rawliteral(
    <div id="main">
      <h2>Config</h2>
    </div>
    <div class="main">
      <form action="/save" method="post">
      <h2>Keypad</h2>
      <input type="hidden" name="form" value="keymap">
      <input type="hidden" name="redirect" value="config">
      <label for="rows">Rows:</label>
      <input type="number" id="rows" name="rows" min="1" max="8" value="4">
      <label for="columns">Columns:</label>
      <input type="number" id="columns" name="columns" min="1" max="8" value="4">
      <h3>Row pins</h3>
      <div id="rowpins" class="pins"></div>
      <h3>Column pins</h3>
      <div id="colpins" class="pins"></div>
      <h3>Keys</h3>
      <div id="matrix"></div> 
      <input type="submit" value="Save Config">
    </form>
    </div>
    <div class="main">
      <form action="/save" method="post">
      <h2>MQTT</h2>
      <input type="hidden" name="form" value="mqtt">
      <input type="hidden" name="redirect" value="config">
      <label for="ip">MQTT server IP:</label>
      <input type="text" id="ip" name="ip" value=")rawliteral"+ mqtt_server +R"rawliteral(">
      <label for="user">MQTT username</label>
      <input type="text" id="user" name="user" value=")rawliteral"+ MQTT_USER +R"rawliteral(">
      <label for="pass">MQTT password </label>
      <input type="password" id="pass" name="pass">
      <input type="submit" value="Save Config">
    </form>
    </div>
    <div class="main">
      <form action="/save" method="post">
      <h2>Display</h2>
      <input type="hidden" name="form" value="screen">
      <input type="hidden" name="redirect" value="config">
      <label for="hasscreen">Has screen</label>
      <input type="checkbox" id="hasscreen" name="hasscreen" data-toggles="displaytype" )rawliteral" + String(HasDisplay ? "checked" : "") + R"rawliteral(>
      <div id="displaytype">
      <p>DisplayType</p>
        <input type="radio" id="i2c" name="displaytype" value="1" data-toggles="i2cconfig" )rawliteral" + String(DisplayType == 1 ? "checked" : "") + R"rawliteral(>
        <label for="i2c">I2c</label><br>
        <input type="radio" id="spi" name="displaytype" value="2" data-toggles="spidisplay spiconfig")rawliteral" + String(DisplayType == 2 ? "checked" : "") + R"rawliteral(>
        <label for="spi">SPI</label><br>
        <div id="spidisplay">
          <label for="cs">CS pin</label>
          <input type="number" id="cs" name="cs" min="0" max="48" required value=")rawliteral" + pinVal(DisplayCs) + R"rawliteral(">
          <label for="dc">DC pin</label>
          <input type="number" id="dc" name="dc" min="0" max="48" required value=")rawliteral" + pinVal(spiDc) + R"rawliteral(">
          <label for="rst">RST pin (empty if wired to reset)</label>
          <input type="number" id="rst" name="rst" min="0" max="48" value=")rawliteral" + pinVal(spiRst) + R"rawliteral(">
        </div>
        <div id="displayaddress">
        </div>
      </div>
      <input type="submit" value="Save config">
    </form></div>
    <div class="main" id="spiconfig">
      <form action="/save" method="post">
      <h2>SPI bus</h2>
      <input type="hidden" name="form" value="spi">
      <input type="hidden" name="redirect" value="config">
      <label for="mosi">MOSI pin:</label>
      <input type="number" id="mosi" name="mosi" min="0" max="48" required value=")rawliteral" + pinVal(spiMosi) + R"rawliteral(">
      <label for="sck">SCK pin:</label>
      <input type="number" id="sck" name="sck" min="0" max="48" required value=")rawliteral" + pinVal(spiSck) + R"rawliteral(">
      <label for="miso">MISO pin (optional):</label>
      <input type="number" id="miso" name="miso" min="0" max="48" value=")rawliteral" + pinVal(spiMiso) + R"rawliteral(">
      <input type="submit" value="Save Config">
    </form></div>
    <div class="main" id="i2cconfig">
      <form action="/save" method="post">
      <h2>I2C</h2>
      <input type="hidden" name="form" value="i2c">
      <input type="hidden" name="redirect" value="config">
      <label for="sda">SDA pin: </label>
      <input type="number" id="sda" name="sda" min="1" max="48" value=")rawliteral"+ String(i2cSda) +R"rawliteral(">
      <label for="scl">SCL pin: </label>
      <input type="number" id="scl" name="scl" min="1" max="48" value=")rawliteral"+ String(i2cScl) +R"rawliteral(">
      <input type="submit" value="Save Config">
    </form></div>
    <div class="main">
      <form action="/save" method="post">
      <h2>RGB Leds</h2>
      <input type="hidden" name="form" value="rgbled">
      <input type="hidden" name="redirect" value="config">
      <label for="hasrgb">Has RGB Led(s)</label>
      <input type="checkbox" id="hasrgb" name="hasrgb" data-toggles="rgbconfig" )rawliteral" + String(HasRGB ? "checked" : "") + R"rawliteral(>
      <div id="rgbconfig">
        <h3>common anode/cathode</h3>
        <label for="annode">annode</label>
        <input type="radio" id="annode" name="common" value="1" )rawliteral" + String(RGB_Common == 1 ? "checked" : "") + R"rawliteral(>
        <label for="cathode"> cathode </label>
        <input type="radio" id="cathode" name="common" value="2" )rawliteral" + String(RGB_Common == 2 ? "checked" : "") + R"rawliteral(>
        </br>
        <label for="rgbleds">Number of LEDs:</label>
        <input type="number" id="rgbleds" name="rgbleds" min="1" max=")rawliteral" + String(MAX_RGB_LEDS) + R"rawliteral(" value=")rawliteral" + String(RGB_Leds > 0 ? RGB_Leds : 1) + R"rawliteral(">
        <div id="rgbpins" class="pins"></div>
      </div>
      <input type="submit" value="Save config">
    </form></div>
    <div class="main">
      <form action="/save" method="post">
      <h2>Potentiometers</h2>
      <input type="hidden" name="form" value="pots">
      <input type="hidden" name="redirect" value="config">
      <label for="haspot">Has potentiometer(s)</label>
      <input type="checkbox" id="haspot" name="haspot" data-toggles="potconfig" )rawliteral" + String(HasPot ? "checked" : "") + R"rawliteral(>
      <div id="potconfig">
        <p>Use ADC1 pins: 1, 2, 4, 5, 6, 7, 8, 9 or 10</p>
        <label for="potcount">Number of pots:</label>
        <input type="number" id="potcount" name="potcount" min="1" max=")rawliteral" + String(MAX_POTS) + R"rawliteral(" value=")rawliteral" + String(potCount > 0 ? potCount : 1) + R"rawliteral(">
        <div id="potpins" class="pins"></div>
      </div>
      <input type="submit" value="Save config">
    </form></div>
    )rawliteral";

      html += "<script>var saved={rows:" + String(rowCount) + ",cols:" + String(colCount) + ",rowPins:[";
      for (byte r = 0; r < rowCount; r++) html += String(rowPins[r]) + ",";
      html += "],colPins:[";
      for (byte c = 0; c < colCount; c++) html += String(colPins[c]) + ",";
      html += "],keys:{";
      for (byte r = 0; r < rowCount; r++)
        for (byte c = 0; c < colCount; c++) {
          byte i = r * colCount + c;
          String pos = String(r) + "_" + String(c);
          const char* tn = keyType[i] == 1 ? "serial" : keyType[i] == 2 ? "keypad" : "ha";
          html += "\"type_" + pos + "\":\"" + tn + "\",";
          html += "\"text_" + pos + "\":\"" + jsEsc(keyText[i]) + "\",";
          html += "\"key_" + pos + "\":\"" + String(keyInt[i]) + "\",";
        }
      html += "},rgbPins:[";
      for (int i = 0; i < RGB_Leds * 3; i++) html += String(rgb_Pins[i]) + ",";
      html += "],potPins:[";
      for (int i = 0; i < potCount; i++) html += String(potPins[i]) + ",";
      html += "]};</script>";

      html += R"rawliteral(
    <script>
      const rowsInput = document.getElementById('rows');
      const colsInput = document.getElementById('columns');
      const rowPinsBox = document.getElementById('rowpins');
      const colPinsBox = document.getElementById('colpins');
      const matrix = document.getElementById('matrix');
      const hasScreen = document.getElementById('hasscreen');
      const screen = document.getElementById('screen');

      function clamp(value, min, max) {
        value = parseInt(value, 10);
        if (isNaN(value)) value = min;
        return Math.min(Math.max(value, min), max);
      }

      function buildPins(box, prefix, label, count, savedPins) {
        const old = {};
        box.querySelectorAll('input').forEach(i => old[i.name] = i.value);
        box.innerHTML = '';
        for (let i = 0; i < count; i++) {
          const input = document.createElement('input');
          input.type = 'number';
          input.min = 0;
          input.max = 48;
          input.required = true;
          input.name = `${prefix}_${i}`;
          input.placeholder = `${label} ${i + 1}`;
          input.value = old[input.name] ?? savedPins[i] ?? '';
          box.appendChild(input);
        }
      }

      function buildMatrix() {
        const rows = clamp(rowsInput.value, 1, 8);
        const cols = clamp(colsInput.value, 1, 8);

        const old = {};
        matrix.querySelectorAll('select, input').forEach(i => old[i.name] = i.value);

        matrix.innerHTML = '';
        matrix.style.gridTemplateColumns = `repeat(${cols}, minmax(130px, 1fr)`;

        for (let r = 0; r < rows; r++) {
          for (let c = 0; c < cols; c++) {
            const pos = `${r}_${c}`;
            const cell = document.createElement('div');
            cell.className = 'cell';

            const title = document.createElement('small');
            title.textContent = `Key ${r + 1},${c + 1}`;

            const select = document.createElement('select');
            select.name = `type_${pos}`;
            select.innerHTML = '<option value="ha">Home Assistant (MQTT)</option>' +
                              '<option value="serial">Serial terminal</option>' +
                              '<option value="keypad">Keyboard numpad key</option>';
            select.value = old[select.name] ?? saved.keys[select.name] ?? 'ha';

            const text = document.createElement('input');
            text.type = 'text';
            text.name = `text_${pos}`;
            text.maxLength = 64;
            text.placeholder = 'Text to print';
            text.value = old[text.name] ?? saved.keys[text.name] ?? '';

            const num = document.createElement('select');
            num.name = `key_${pos}`;
            for (let n = 0; n <= 9; n++) num.innerHTML += `<option value="${n}">Numpad ${n}</option>`;
            num.value = old[num.name] ?? saved.keys[num.name] ?? '0';

            const id = document.createElement('small');
            id.textContent = `ID: macropad_key_${r + 1}_${c + 1}`;

            const update = () => {
              const t = select.value;
              text.style.display = t === 'serial' ? '' : 'none';
              text.disabled = t !== 'serial';
              num.style.display = t === 'keypad' ? '' : 'none';
              num.disabled = t !== 'keypad';
              id.style.display = t === 'ha' ? '' : 'none';
            };

            select.addEventListener('change', update);
            update();

            cell.append(title, select, text, num, id);
            matrix.appendChild(cell);
          }
        }
      }

      function rebuild() {
        const rows = clamp(rowsInput.value, 1, 8);
        const cols = clamp(colsInput.value, 1, 8);
        buildPins(rowPinsBox, 'rowpin', 'Row', rows, saved.rowPins);
        buildPins(colPinsBox, 'colpin', 'Col', cols, saved.colPins);
        buildMatrix();
      }

      if (saved.rows > 0) {
        rowsInput.value = saved.rows;
        colsInput.value = saved.cols;
      }
      rowsInput.addEventListener('input', rebuild);
      colsInput.addEventListener('input', rebuild);
      rebuild();

      const rgbCount = document.getElementById('rgbleds');
      const rgbBox = document.getElementById('rgbpins');

      function buildRgb() {
        const n = clamp(rgbCount.value, 1, Number(rgbCount.max));
        const old = {};
        rgbBox.querySelectorAll('input').forEach(i => old[i.name] = i.value);
        rgbBox.innerHTML = '';
        rgbBox.style.gridTemplateColumns = 'repeat(3, 1fr)';
        const ch = ['R', 'G', 'B'];
        for (let l = 0; l < n; l++) {
          for (let c = 0; c < 3; c++) {
            const input = document.createElement('input');
            input.type = 'number';
            input.min = 0;
            input.max = 48;
            input.required = true;
            input.name = `rgbpin_${l}_${c}`;
            input.placeholder = `LED ${l + 1} ${ch[c]}`;
            input.value = old[input.name] ?? saved.rgbPins[l * 3 + c] ?? '';
            rgbBox.appendChild(input);
          }
        }
      }
      rgbCount.addEventListener('input', buildRgb);
      buildRgb();

            const potCountInput = document.getElementById('potcount');
      const potBox = document.getElementById('potpins');

      function buildPots() {
        const n = clamp(potCountInput.value, 1, Number(potCountInput.max));
        const old = {};
        potBox.querySelectorAll('input').forEach(i => old[i.name] = i.value);
        potBox.innerHTML = '';
        for (let i = 0; i < n; i++) {
          const input = document.createElement('input');
          input.type = 'number';
          input.min = 1;
          input.max = 10;
          input.required = true;
          input.name = `potpin_${i}`;
          input.placeholder = `Pot ${i + 1}`;
          input.value = old[input.name] ?? saved.potPins[i] ?? '';
          potBox.appendChild(input);
        }
      }
      potCountInput.addEventListener('input', buildPots);
      buildPots();

      function refreshToggles() {
      for (let pass = 0; pass < 5; pass++) {
        const state = new Map(); // id -> should this box be on?

        document.querySelectorAll('input[data-toggles]').forEach(ctl => {
          const on = ctl.checked && !ctl.disabled;
          ctl.dataset.toggles.split(/\s+/).filter(Boolean).forEach(id => {
            state.set(id, state.get(id) || on); // any "on" checkbox wins
          });
        });

        let changed = false;
        state.forEach((on, id) => {
          const box = document.getElementById(id);
          if (!box) return;
          box.style.display = on ? '' : 'none';
          box.querySelectorAll('input, select, textarea, button').forEach(el => {
            if (el.disabled === on) { el.disabled = !on; changed = true; }
          });
        });

        if (!changed) break;
      }
    }
    document.addEventListener('change', refreshToggles);
    refreshToggles();
    </script></body></html>
    )rawliteral";

  server.send(200, "text/html", html);
}

void handleSettings() {
  if (!requireLogin()) return;
  String html = css;
  html += R"rawliteral(
    <title> Settings </title>
    </head><body>)rawliteral";
  html += nav;
  html += takeFlash();
  html += R"rawliteral(
    <div class="main">
      <h2> Settings </h2>
    </div>
    <div class="main">
      <form action="/save" method="post">
        <h3>Wifi</h3>
        <input type="hidden" name="form" value="wifi">
        <label for="ssid"> SSID </label>
        <input type="text" id="ssid" name="ssid" value=")rawliteral" + savedSsid + R"rawliteral(">
        <label for="pass"> Password </label>
        <input type="password" name="pass">
        <input type="submit" value="Change wifi config">
      </form>
    </div>
    <div class="main">
      <form action='/save' method='post'>
        <h3>Reset credentials</h3>
        <input type="hidden" name="form" value="register">
        <input type="hidden" name="redirect" value="home">
        <label for='username'>Username:</label><br>
        <input type='text' id='uname' name='uname' value=")rawliteral" + UserUName + R"rawliteral(">
        <label for='pass'>Password:</label>
        <input type='password' id='pass' name='pass'>
        <label for='confirmpass'>Confirm Password:</label>
        <input type='password' id='confirmpass' name='confirmpass'>
        <input type='submit' value='Reset Credentials'>
    </form></div>
    <div class="main">
      <form action="/save" method="post">
        <input type="hidden" name="form" value="reset">
        <input type="hidden" name="redirect" value="settings">
        <h3>Reset all config</h3>
        <label for="reset">Type reset to reset all config</label>
        <input type="text" id="reset" name="reset" required>
        <input type="submit" value="Reset config">
    </form></div>
  )rawliteral";
  html += "<div class='main'><form action='/save' method='post'>"
          "<h3>Time zone</h3>"
          "<input type='hidden' name='form' value='timezone'>"
          "<input type='hidden' name='redirect' value='settings'>"
          "<label for='tzpreset'>Time zone</label>"
          "<select id='tzpreset' name='tzpreset' style='background:#555;color:#fff;border:2px solid #333;border-radius:5px;padding:3px;margin:10px'>";
  bool tzFound = false;
  for (int i = 0; i < TZ_COUNT; i++) {
    bool sel = (tzString == TZ_VALUES[i]);
    if (sel) tzFound = true;
    html += "<option value='" + String(TZ_VALUES[i]) + "'" + (sel ? " selected" : "") + ">" + TZ_NAMES[i] + "</option>";
  }
  html += "<option value='custom'" + String(tzFound ? "" : " selected") + ">Custom (type below)</option></select><br>"
          "<label for='tzcustom'>Custom rule (only used when Custom is chosen)</label><br>"
          "<input type='text' id='tzcustom' name='tzcustom' value='" + esc(tzFound ? String("") : tzString) + "'><br>"
          "<input type='submit' value='Save time zone'>"
          "</form></div>";
  server.send(200, "text/html", html);
}

void redirectTo(const char* to) {
  server.sendHeader("Location", to, true);
  server.send(303, "text/plain", "");
}

String keyTopic(byte r, byte c) {
  return "macropad/key/" + String(r + 1) + "_" + String(c + 1) + "/state";
}

void publishKeyDiscovery() {
  if (!mqttConfigured || !client.connected()) return;
  for (byte r = 0; r < MAX_PINS; r++) {
    for (byte c = 0; c < MAX_PINS; c++) {
      String pos = String(r + 1) + "_" + String(c + 1);
      String cfg = "homeassistant/binary_sensor/macropad_key_" + pos + "/config";

      bool active = HasKeypad && r < rowCount && c < colCount &&
                    keyType[r * colCount + c] == 0;
      if (!active) {
        client.publish(cfg.c_str(), "", true);
        continue;
      }
      String p = "{\"name\":\"Macropad Key " + String(r + 1) + "," + String(c + 1) + "\","
                 "\"unique_id\":\"macropad_key_" + pos + "\","
                 "\"state_topic\":\"" + keyTopic(r, c) + "\","
                 "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
                 "\"availability_topic\":\"macropad/status\","
                 "\"device\":{\"identifiers\":[\"macropad\"],\"name\":\"Macropad\",\"manufacturer\":\"DIY\"}}";
      client.publish(cfg.c_str(), p.c_str(), true);
    }
  }
}

void keyPress(byte i) {
  int d = keyInt[i];
  if (d < 0 || d > 9) return;
  Keyboard.press(KP_CODES[d]);
}

void keypadEvent(char key) {
  byte i = key - 1;
  byte r = i / colCount;
  byte c = i % colCount;
  KeyState state = keypad->getState();
  if (state == PRESSED) {
    if (keyType[i] == 0) {
      String id = haId(i);
      Serial.println("HA: " + id);
      if (mqttConfigured && client.connected()) {
        client.publish(keyTopic(r, c).c_str(), "ON");
      }
    } else if (keyType[i] == 1) {
      Serial.println(keyText[i]);
    } else if (keyType[i] == 2) {
      keyPress(i);
    }
  } else if (state == RELEASED) {
    if (keyType[i] == 0) {
      if (mqttConfigured && client.connected()) {
        client.publish(keyTopic(r, c).c_str(), "OFF");
      }
    } else if (keyType[i] == 2) {
      Keyboard.releaseAll();
    }
  }
}

void buttonsSetup() {
  delete keypad;
  keypad = nullptr;
  HasKeypad = false;
  rowCount = colCount = 0;

  prefs.begin("keymap", true);
  byte rows = prefs.getUChar("rows", 0);
  byte cols = prefs.getUChar("cols", 0);
  if (rows == 0 || cols == 0 || rows > MAX_PINS || cols > MAX_PINS ||
      prefs.getBytesLength("rp") != rows || prefs.getBytesLength("cp") != cols) {
    prefs.end();
    Serial.println("No button config found");
    return;
  }
  prefs.getBytes("rp", rowPins, rows);
  prefs.getBytes("cp", colPins, cols);
  rowCount = rows;
  colCount = cols;

  for (byte r = 0; r < rows; r++)
    for (byte c = 0; c < cols; c++) {
      String pos = String(r) + "_" + String(c);
      byte i = r * cols + c;
      keyType[i] = prefs.getUChar(("t" + pos).c_str(), 0);
      keyText[i] = prefs.getString(("s" + pos).c_str(), "");
      keyInt[i] = prefs.getInt(("k" + pos).c_str(), 1);
      keys[i] = i + 1;
    }
  prefs.end();

  keypad = new Keypad(makeKeymap(keys), rowPins, colPins, rowCount, colCount);
  keypad->addEventListener(keypadEvent);
  HasKeypad = true;
  publishKeyDiscovery();
  Serial.println("keymap loaded");
}

const char* pinOwner(int p, const char* skip = "") {
  if (p < 0) return nullptr;

  if (p == 19 || p == 20) return "reserved for USB";
  if (p == 0 || p == 3 || p == 45 || p == 46) return "a strapping pin (not allowed)";
  if (p >= 26 && p <= 32) return "used by the flash memory";

  if (strcmp(skip, "keypad") != 0) {
    for (byte i = 0; i < rowCount; i++) if (rowPins[i] == p) return "used by the keypad";
    for (byte i = 0; i < colCount; i++) if (colPins[i] == p) return "used by the keypad";
  }
  if (strcmp(skip, "rgb") != 0 && HasRGB) {
    for (int i = 0; i < RGB_Leds * 3; i++) if (rgb_Pins[i] == p) return "used by the RGB LEDs";
  }
  if (strcmp(skip, "pots") != 0 && HasPot) {
    for (int i = 0; i < potCount; i++) if (potPins[i] == p) return "used by a potentiometer";
  }
  if (strcmp(skip, "i2c") != 0 && (p == i2cSda || p == i2cScl)) return "used by I2C";
  if (strcmp(skip, "spi") != 0 && (p == spiMosi || p == spiSck || p == spiMiso)) return "used by the SPI bus";
  if (strcmp(skip, "display") != 0 && HasDisplay && DisplayType == 2) {
    if (p == DisplayCs || p == spiDc || p == spiRst) return "used by the display";
  }
  return nullptr;
}

void pinError(int p, const char* owner) {
  server.send(400,"text/plain" , "GPIO " + String(p) + " is not available: " + String(owner));
}

void handleSave() {
  String redirect = server.arg("redirect");
  String form = server.arg("form");
  if (form == "login"){
    if (millis() < lockUntil) {
      sendError(429, "Too many attempts. Wait a few seconds and try again");
      return;
    }
    String uname = server.arg("username");
    String pass = server.arg("password");

    if (!checkPassword(uname, pass)) {
      failedLogins ++;
      int waitSec = failedLogins * 3;
      if (waitSec > 60) waitSec = 60;
      lockUntil = millis() + waitSec * 1000UL;
      sendError(401, "username or password not correct");
      return;
    }
    failedLogins = 0;
    Serial.println("Logged in with username: " + uname);
    startSession();
    finishSave("", "login");
  }else if (form == "register"){
    if (!UserHash.isEmpty() && currentSession() < 0) {
      sendError(403, "A account alredy exists, log in first");
      return;
    }
    String uname = server.arg("uname");
    String pass = server.arg("pass");
    String confirmpass = server.arg("confirmpass");

    if (uname.isEmpty() || pass.isEmpty() || confirmpass.isEmpty()) {
      sendError(400, "username, password and confirm password cant be empty");
      return;
    }
    if (pass != confirmpass) {
      sendError(400, "password and confirm password need to be the same");
      return;
    }
    setCredentials(uname, pass);
    clearSessions();
    startSession();
    Serial.println("New user with username: " + uname);
    finishSave("", "register");
  }else if(form == "keymap"){
    if (!requireLogin()) return;
    int rows = constrain(server.arg("rows").toInt(), 1, MAX_PINS);
    int cols = constrain(server.arg("columns").toInt(), 1, MAX_PINS);

    byte rp[MAX_PINS], cp[MAX_PINS];
    bool used[49] = {false};
    if (!readPins("rowpin", rows, rp, used) || !readPins("colpin", cols, cp, used)) {
      sendError(400, "Pins must be numbers 0-48 and can't be used twice");
      return;
    }
    for (int i = 0; i < rows; i++) {
      const char* o = pinOwner(rp[i], "keypad");
      if (o) { pinError(rp[i], o); return; }
    }
    for (int i = 0; i < cols; i++) {
      const char* o = pinOwner(cp[i], "keypad");
      if (o) { pinError(cp[i], o); return; }
    }

    prefs.begin("keymap", false);
    prefs.clear();
    prefs.putUChar("rows", rows);
    prefs.putUChar("cols", cols);
    prefs.putBytes("rp", rp, rows);
    prefs.putBytes("cp", cp, cols);
    for (int r = 0; r < rows; r++) {
      for (int c = 0; c < cols; c++) {
        String pos = String(r) + "_" + String(c);
        String type = server.arg("type_" + pos);
        byte t = (type == "serial") ? 1 : (type == "keypad") ? 2 : 0;
        prefs.putUChar(("t" + pos).c_str(), t);
        if (t == 1) {
          prefs.putString(("s" + pos).c_str(), server.arg("text_" + pos).substring(0, 64));
        }
        if (t == 2) {
          int key = constrain(server.arg("key_" + pos).toInt(), 0, 9);
          prefs.putInt(("k" + pos).c_str(), key);
        }
      }
    }
    prefs.end();

    buttonsSetup();
    finishSave(redirect, "keymap");
  }else if(form == "wifi"){
    if (Can_Connect && !requireLogin()) return;
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (ssid.isEmpty()) {
      sendError(400, "SSID required");
      return;
    }
    Serial.println("wifi setup completed, SSID: "+ ssid);
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
    server.send(200, "text/html",
      "<html><body style='font-family:sans-serif'><h3>Saved. Restarting...</h3></body></html>");
    delay(1500);
    ESP.restart();
  }else if (form == "mqtt"){
    if (!requireLogin()) return;
    String uname = server.arg("user");
    String pass = server.arg("pass");
    String ip = server.arg("ip");

    prefs.begin("MQTT", false);
    prefs.putString("username", uname);
    prefs.putString("password", pass);
    prefs.putString("ip", ip);
    prefs.end();

    mqttSetup();
    finishSave(redirect, "mqtt");
  }else if (form == "i2c"){
    if (!requireLogin()) return;
    int sda = server.arg("sda").toInt();
    int scl = server.arg("scl").toInt();
    if (sda == scl) { sendError(400, "SDA and SCL must be different"); return; }
    const char* o1 = pinOwner(sda, "i2c");
    if (o1) { pinError(sda, o1); return; }
    const char* o2 = pinOwner(scl, "i2c");
    if (o2) { pinError(scl, o2); return; }
    
    prefs.begin("i2c", false);
    prefs.putInt("sda", sda);
    prefs.putInt("scl", scl);
    prefs.end();

    Wire.end();
    i2cConfigured = false;
    i2cSetup();
    finishSave(redirect, "i2c");
  }
  else if (form == "spi") {
    if (!requireLogin()) return;
    int mosi = server.arg("mosi").toInt();
    int sck  = server.arg("sck").toInt();
    int miso = server.arg("miso").length() ? server.arg("miso").toInt() : -1;

    if (!validPin(mosi) || !validPin(sck) || mosi == sck ||
        (miso >= 0 && (!validPin(miso) || miso == mosi || miso == sck))) {
      sendError(400, "Invalid SPI bus pins");
      return;
    }
    int sp[3] = {mosi, sck, miso};
    for (int i = 0; i < 3; i++) {
      const char* o = pinOwner(sp[i], "spi");
      if (o) { pinError(sp[i], o); return; }
    }
    prefs.begin("spi", false);
    prefs.putInt("mosi", mosi);
    prefs.putInt("sck", sck);
    prefs.putInt("miso", miso);
    prefs.end();

    spiBusReset();
    SpiSetup();
    displaySetup();
    finishSave(redirect, "spi");
  }else if (form == "screen") {
  if (!requireLogin()) return;
  bool has = server.hasArg("hasscreen");
  int type = server.arg("displaytype").toInt();
  int cs = -1, dc = -1, rst = -1;

  if (has && type == 2) {
    cs  = server.arg("cs").toInt();
    dc  = server.arg("dc").toInt();
    rst = server.arg("rst").length() ? server.arg("rst").toInt() : -1;
    if (!validPin(cs) || !validPin(dc) || cs == dc ||
        (rst >= 0 && (!validPin(rst) || rst == cs || rst == dc))) {
      sendError(400, "Invalid display pins (CS, DC and RST must be different)");
      return;
    }
    int dp[3] = {cs, dc, rst};
    for (int i = 0; i < 3; i++) {
      const char* o = pinOwner(dp[i], "display");
      if (o) { pinError(dp[i], o); return; }
    }

    }else if (has && type != 1) {
      sendError(400, "Choose a display type");
      return;
    }

    prefs.begin("display", false);
    prefs.putBool("hasdisplay", has);
    if (has) {
      prefs.putInt("displaytype", type);
      if (type == 2) {
        prefs.putInt("cs", cs);
        prefs.putInt("dc", dc);
        prefs.putInt("rst", rst);
      }
    }
    prefs.end();

    displaySetup();
    finishSave(redirect, "screen");
  }else if (form == "rgbled") {
    if (!requireLogin()) return;
    bool has = server.hasArg("hasrgb");
    int n = 0;
    byte pins[MAX_RGB_LEDS * 3];

    if (has) {
      n = constrain(server.arg("rgbleds").toInt(), 1, MAX_RGB_LEDS);
      RGB_Common = server.arg("common").toInt();
      bool used[49] = {false};
      for (int i = 0; i < n * 3; i++) {
        String name = "rgbpin_" + String(i / 3) + "_" + String(i % 3);
        int p = server.arg(name).toInt();
        if (!server.hasArg(name) || p < 0 || p > 48 || used[p]) {
          sendError(400, "RGB pins must be 0-48 and unique");
          return;
        }
        const char* o = pinOwner(p, "rgb");
        if (o) { pinError(p, o); return; }
        used[p] = true;
        pins[i] = p;
      }
    }

    prefs.begin("rgbled", false);
    prefs.putBool("hasrgb", has);
    if (has) {
      prefs.putInt("rgbleds", n);
      prefs.putBytes("pins", pins, n * 3);
      prefs.putInt("common", RGB_Common);
    }
    prefs.end();

    rgbSetup();
    finishSave(redirect, "rgbled");
  } else if (form == "pots") {
    if (!requireLogin()) return;

    bool has = server.hasArg("haspot");
    int n = 0;
    byte pins[MAX_POTS];

    if (has) {
      n = constrain(server.arg("potcount").toInt(), 1, MAX_POTS);
      bool used[49] = {false};
      for (int i = 0; i < n; i++) {
        String name = "potpin_" + String(i);
        int p = server.arg(name).toInt();
        if (!server.hasArg(name) || p < 1 || p > 10 || used[p]) {
          sendError(400, "Pot pins must be 1-10 (ADC1) and unique");
          return;
        }
        const char* o = pinOwner(p, "pots");
        if (o) { pinError(p, o); return; }
        used[p] = true;
        pins[i] = p;
      }
    }

    prefs.begin("pots", false);
    prefs.putBool("haspot", has);
    if (has) {
      prefs.putInt("potcount", n);
      prefs.putBytes("pins", pins, n);
    }
    prefs.end();

    potSetup();
    finishSave(redirect, "pots");
  } else if (form == "reset") {
    if (!requireLogin()) return;
    if (server.arg("reset") != "reset") {
      sendError(400, "type reset to reset all config");
      return;
    }
    prefs.begin("MQTT", false);
    prefs.clear();
    prefs.end();
    prefs.begin("spi", false);
    prefs.clear();
    prefs.end();
    prefs.begin("i2c", false);
    prefs.clear();
    prefs.end();
    prefs.begin("display", false);
    prefs.clear();
    prefs.end();
    prefs.begin("keymap", false);
    prefs.clear();
    prefs.end();
    prefs.begin("rgbled", false);
    prefs.clear();
    prefs.end();
    prefs.begin("pots", false);
    prefs.clear();
    prefs.end();
    prefs.begin("rgbstate", false);
    prefs.clear();
    prefs.end();
    prefs.begin("time", false);
    prefs.clear();
    prefs.end();
    Serial.println("Config reset, restarting now");
    delay(500);
    ESP.restart();
  } else if (form == "timezone") {
    if (!requireLogin()) return;
    String tz = server.arg("tzpreset");
    if (tz == "custom") tz = server.arg("tzcustom");
    tz.trim();
    if (tz.isEmpty() || tz.length() > 60) {
      sendError(400, "Time zone rule is empty or too long");
      return;
    }

    prefs.begin("time", false);
    prefs.putString("tz", tz);
    prefs.end();

    tzString = tz;
    applyTimezone(false);
    finishSave(redirect, "timezone");
  }
}

void startPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_NAME, strlen(AP_PASS) ? AP_PASS : nullptr);
  Serial.print("Portal running. Connect to '");
  Serial.print(AP_NAME);
  Serial.print("' and open http://");
  Serial.println(WiFi.softAPIP());

  dns.start(53, "*", WiFi.softAPIP());
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();

  while (true) { 
    dns.processNextRequest();
    server.handleClient();
    delay(2);
  }
}

void loadBusPins() {
  prefs.begin("i2c", true);
  i2cSda = prefs.getInt("sda", -1);
  i2cScl = prefs.getInt("scl", -1);
  prefs.end();

  prefs.begin("spi", true);
  spiMosi = prefs.getInt("mosi", -1);
  spiSck  = prefs.getInt("sck", -1);
  spiMiso = prefs.getInt("miso", -1);
  prefs.end();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(RESET_PIN, INPUT_PULLUP);
  uint32_t t = millis();
  while (digitalRead(RESET_PIN) == LOW) {
    if (millis() - t > 3000) {
      prefs.begin("wifi", false);
      prefs.clear();
      prefs.end();
      Serial.println("Saved WiFi erased.");
      break;
    }
    delay(50);
  }

  prefs.begin("wifi", true);
  savedSsid = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  prefs.end();

  if (!connectSaved()) {
    Serial.println("No saved network or connection failed. Starting portal.");
    startPortal();
  }

  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Can_Connect = true;
  if (MDNS.begin("macropad")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: http://macropad.local");
  } else {
    Serial.println("mDNS failed to start");
  }

  timeSetup();
  const char* headerKeys[] = {"Cookie"};
  server.collectHeaders(headerKeys, 1);
  server.on("/", handleRoot);
  server.on("/home", handleRoot);
  server.on("/register", handleRegister);
  server.on("/login", handleLogin);
  server.on("/logout", handleLogout);
  server.on("/config", handleConfig);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/settings", handleSettings);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
  server.begin();

  mqttSetup();
  reconnect();

  loadCredentials();

  loadBusPins();
  buttonsSetup();
  displaySetup();
  rgbSetup();
  potSetup();

  Keyboard.begin();
  USB.begin();

  Serial.println("Config loaded");
}


void loop() {
  server.handleClient();
  wifiKeepAlive();
  if (mqttConfigured && WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      long now = millis();
      if (now - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = now;
        if (reconnect()) lastReconnectAttempt = 0;
      }
    } else {
      client.loop();
    }
  }
  while(Serial.available()) {
    readstr = Serial.readString();
    readstr.trim();
    Serial.println("Recieved: " + readstr);
    if(readstr == "rst"){
      Serial.println("Restarting");
      delay(1000);
      ESP.restart();
    }
    if(readstr == "rst_credentials"){
      prefs.begin("credentials", false);
      prefs.clear();
      prefs.end();
      Serial.println("Credentials reset, restarting esp");
      delay(1000);
      ESP.restart();
    }
  }

  if (HasKeypad) keypad->getKey();
  potLoop();
  rgbStateLoop();

  if (DisplayConfigured && (lastDraw == 0 || millis() - lastDraw >= 1000)) {
    lastDraw = millis();
    drawTime();
  }
}