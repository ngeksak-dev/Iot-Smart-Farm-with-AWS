/*
  Smart Farm ESP32 Firmware
  =========================

  FLASH ONCE. Never again.

  There is nothing to edit in this file for a normal setup. No WiFi name, no
  password, no server address. You set those from your phone the first time
  the board powers on, and the board remembers them.

  ------------------------------------------------------------------
  FIRST TIME YOU POWER IT ON

    1. On your phone, open the WiFi list
    2. Connect to the network called  SmartFarm-Setup
       (password:  smartfarm)
    3. A setup page opens by itself. If it doesn't, open a browser
       and go to  192.168.4.1
    4. Choose your WiFi, type its password, and type your server's
       address (the number from step 9 of the README)
    5. Press Save

  The board restarts and connects. It remembers all of this, so from now on
  it just works — after a power cut, after being unplugged and moved, after
  anything.

  ------------------------------------------------------------------
  TO CHANGE THE SETTINGS LATER

  Hold the BOOT button while powering the board on, and keep holding for
  3 seconds. The setup page comes back. Nothing else is lost.

  ------------------------------------------------------------------
  ADVANCED: encrypted connection (TLS)

  Only for a server on the internet. See ADVANCED.md. Remove the // from
  #define USE_TLS below, then paste your CA certificate into CA_CERT and
  set MIN_VALID_EPOCH. Everything else switches over on its own.
  ------------------------------------------------------------------

  What is stored on the board, and what is not
  --------------------------------------------
  Stored on the board : WiFi name and password, server address, MQTT login.
                        These describe how to reach the server, so the board
                        cannot be told them over the network.
  NOT stored          : which sensors and relays are attached. That still
                        arrives from the server as a retained MQTT message,
                        so adding a sensor is done from the dashboard and
                        never needs a reflash.

  Libraries (Arduino IDE -> Tools -> Manage Libraries)
  ---------------------------------------------------
    WiFiManager (by tzapu)          <-- new, needed for the setup page
    PubSubClient (knolleary)
    ArduinoJson (bblanchon)
    DHT sensor library (Adafruit) + Adafruit Unified Sensor
*/

// ===================================================================
//  ADVANCED: remove the // on the next line to switch TLS on
// ===================================================================
// #define USE_TLS
// ===================================================================

#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

#if defined(USE_TLS)
  #include <WiFiClientSecure.h>
  #include <time.h>
#endif

// ---- Setup page ----
// The network the board creates when it has nothing saved.
const char* SETUP_AP_NAME = "SmartFarm-Setup";
const char* SETUP_AP_PASS = "smartfarm";      // at least 8 characters

// Hold this button during power-on to wipe the saved settings.
// GPIO 0 is the BOOT button on almost every ESP32 dev board.
#define RESET_BUTTON_PIN 0
#define RESET_HOLD_MS    3000

// ---- Defaults offered on the setup page ----
// These are only suggestions shown in the form. Whatever is typed there is
// what gets saved, so there is no need to change them here.
#define DEFAULT_MQTT_HOST "192.168.1.126"
#define DEFAULT_MQTT_USER "mqtt"
#define DEFAULT_MQTT_PASS "mqtt123"

#if defined(USE_TLS)
  const int MQTT_PORT = 8883;          // encrypted
#else
  const int MQTT_PORT = 1883;          // plain
#endif


#if defined(USE_TLS)
// ============ ADVANCED SETTINGS (TLS only) ============

// Earliest clock the board will accept before attempting TLS.
// The ESP32 has no battery clock, so it boots thinking the year is 1970.
// TLS checks a certificate's date range, so a 1970 clock fails every
// handshake with what looks like a certificate error. Waiting for a real
// date past the certificate's start avoids that.
//
// Find your certificate's start date:  openssl x509 -in ca.crt -noout -dates
// Convert it:                          date -d "<that date>" +%s
const time_t MIN_VALID_EPOCH = 1786250856UL;   // 2026-08-09 04:47:36 UTC

// The CA certificate that signed your broker's server certificate.
// Paste the whole file between the markers, including BEGIN and END.
//
// Your broker's SERVER certificate must contain the address you type on the
// setup page in its subjectAltName. Connecting by IP means the SAN needs an
// entry of type IP:<address>, or the handshake fails on a name mismatch.
static const char CA_CERT[] PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
PASTE YOUR CA CERTIFICATE HERE
-----END CERTIFICATE-----
)CERT";

// ======================================================
#endif


#define MAX_SENSORS 8
#define MAX_OUTPUTS 8
#define ANNOUNCE_INTERVAL_MS    30000UL
#define SENSOR_READ_INTERVAL_MS  5000UL

#if defined(USE_TLS)
  WiFiClientSecure espClient;
#else
  WiFiClient espClient;
#endif
PubSubClient mqtt(espClient);
Preferences prefs;

// Settings loaded from flash at boot, filled in from the setup page.
String mqttHost, mqttUser, mqttPass;

char macStr[18];
char topicConfig[40];
char topicStatus[36];

struct SensorSlot {
  bool    active = false;
  String  module;          // must match the catalog name, e.g. "DHT22"
  int     pin = -1;
  DHT*    dht = nullptr;
  volatile long pulseCount = 0;
  float   totalVolume = 0;
};
SensorSlot sensors[MAX_SENSORS];
int sensorCount = 0;
unsigned long lastRead = 0;

int  knownOutputPins[MAX_OUTPUTS];
bool knownOutputActiveLow[MAX_OUTPUTS];
bool knownOutputOn[MAX_OUTPUTS];      // survives a config reload
int  knownOutputCount = 0;

String lastConfigPayload = "";        // used to ignore unchanged configs
bool   haveConfig = false;

void IRAM_ATTR pulseISR0() { sensors[0].pulseCount++; }
void IRAM_ATTR pulseISR1() { sensors[1].pulseCount++; }
void IRAM_ATTR pulseISR2() { sensors[2].pulseCount++; }
void IRAM_ATTR pulseISR3() { sensors[3].pulseCount++; }
void (*pulseISRs[4])() = { pulseISR0, pulseISR1, pulseISR2, pulseISR3 };

unsigned long lastAnnounce = 0;
bool shouldSaveSettings = false;

// Forward declarations, so setup() can call these regardless of file order.
void loadSettings();
void checkFactoryReset();
void startPortalIfNeeded();
void connectMqtt();
void publishAnnounce();
void mqttCallback(char* topic, byte* payload, unsigned int len);
void applyConfig(byte* payload, unsigned int len);
void applyControl(byte* payload, unsigned int len);
void readAndPublishSensors(unsigned long now);
#if defined(USE_TLS)
void syncTime();
void printTlsError();
#endif

// ------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

#if defined(USE_TLS)
  Serial.println("\nSmart Farm board — TLS mode (port 8883)");
#else
  Serial.println("\nSmart Farm board — plain mode (port 1883)");
#endif

  loadSettings();
  checkFactoryReset();      // BOOT held at power-on wipes saved settings
  startPortalIfNeeded();    // connects, or opens the setup page

#if defined(USE_TLS)
  syncTime();               // TLS needs a real date before it can verify
  espClient.setCACert(CA_CERT);
  espClient.setTimeout(15000);
#endif

  String mac = WiFi.macAddress();
  mac.toCharArray(macStr, 18);
  Serial.println("Board ID (MAC): " + mac);

  snprintf(topicConfig, sizeof(topicConfig), "esp32/config/%s", macStr);
  snprintf(topicStatus, sizeof(topicStatus), "esp32/status/%s", macStr);

  mqtt.setServer(mqttHost.c_str(), MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1536);          // readings[] grows with sensor count

#if defined(USE_TLS)
  // Try the TLS layer on its own first. If this fails the problem is the
  // certificate or the clock, not MQTT — far easier to act on than
  // PubSubClient's generic rc=-2.
  Serial.println("Testing raw TLS handshake...");
  if (espClient.connect(mqttHost.c_str(), MQTT_PORT)) {
    Serial.println("TLS handshake OK");
    espClient.stop();
  } else {
    printTlsError();
    Serial.println("TLS failed. Check: clock synced, CA matches the broker,");
    Serial.println("and the server cert's SAN contains " + mqttHost);
  }
#endif

  connectMqtt();
}

void loop() {
  // WiFi credentials live in the ESP32's own storage, so reconnecting after
  // a dropout needs no arguments and no reflash.
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    WiFi.reconnect();
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
      delay(300);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi back: " + WiFi.localIP().toString());
    } else {
      // Still nothing. Restart rather than sit here forever — this recovers
      // a board that was moved while powered, or whose router rebooted.
      Serial.println("\nStill no WiFi. Restarting.");
      ESP.restart();
    }
  }

  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();

  unsigned long now = millis();
  if (now - lastAnnounce >= ANNOUNCE_INTERVAL_MS) {
    lastAnnounce = now;
    publishAnnounce();
  }
  readAndPublishSensors(now);
}

// ------------------------------------------------------------------
//  Settings stored on the board
// ------------------------------------------------------------------
void loadSettings() {
  prefs.begin("smartfarm", true);          // read-only
  mqttHost = prefs.getString("host", DEFAULT_MQTT_HOST);
  mqttUser = prefs.getString("user", DEFAULT_MQTT_USER);
  mqttPass = prefs.getString("pass", DEFAULT_MQTT_PASS);
  prefs.end();
  Serial.println("Server address: " + mqttHost);
}

void saveSettings() {
  prefs.begin("smartfarm", false);         // read-write
  prefs.putString("host", mqttHost);
  prefs.putString("user", mqttUser);
  prefs.putString("pass", mqttPass);
  prefs.end();
  Serial.println("Settings saved to the board.");
}

// Hold the BOOT button at power-on to wipe everything and start over.
void checkFactoryReset() {
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(RESET_BUTTON_PIN) != LOW) return;   // not held

  Serial.print("BOOT held — keep holding to erase settings");
  unsigned long start = millis();
  while (digitalRead(RESET_BUTTON_PIN) == LOW) {
    delay(200);
    Serial.print(".");
    if (millis() - start >= RESET_HOLD_MS) {
      Serial.println("\nErasing WiFi and server settings...");
      WiFiManager wm;
      wm.resetSettings();                  // clears saved WiFi
      prefs.begin("smartfarm", false);
      prefs.clear();                       // clears server address
      prefs.end();
      Serial.println("Done. Restarting into setup mode.");
      delay(500);
      ESP.restart();
    }
  }
  Serial.println("\nReleased too early — carrying on normally.");
}

void saveSettingsCallback() { shouldSaveSettings = true; }

void startPortalIfNeeded() {
  WiFiManager wm;
  wm.setSaveConfigCallback(saveSettingsCallback);

  // Extra boxes on the setup page, beyond WiFi name and password.
  WiFiManagerParameter pHost("host", "Server address (e.g. 192.168.1.126)",
                             mqttHost.c_str(), 40);
  WiFiManagerParameter pUser("user", "MQTT username", mqttUser.c_str(), 32);
  WiFiManagerParameter pPass("pass", "MQTT password", mqttPass.c_str(), 32);
  wm.addParameter(&pHost);
  wm.addParameter(&pUser);
  wm.addParameter(&pPass);

  // If the setup page is opened but nobody fills it in, restart rather than
  // sit as an access point forever. A board on a pole must recover on its own.
  wm.setConfigPortalTimeout(300);          // 5 minutes

  Serial.println("Connecting to saved WiFi...");
  Serial.println("(If none is saved, join the WiFi network '" +
                 String(SETUP_AP_NAME) + "' to set this board up.)");

  if (!wm.autoConnect(SETUP_AP_NAME, SETUP_AP_PASS)) {
    Serial.println("Setup timed out. Restarting.");
    delay(1000);
    ESP.restart();
  }

  if (shouldSaveSettings) {
    mqttHost = pHost.getValue();
    mqttUser = pUser.getValue();
    mqttPass = pPass.getValue();
    mqttHost.trim();
    mqttUser.trim();
    mqttPass.trim();
    saveSettings();
  }

  Serial.println("WiFi connected: " + WiFi.localIP().toString());
}

#if defined(USE_TLS)
void syncTime() {
  // The offset only affects how local time is printed. Certificate checking
  // uses UTC, which NTP sets correctly either way.
  configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  Serial.print("Syncing time via NTP");

  time_t now = time(nullptr);
  unsigned long start = millis();
  unsigned long total = millis();

  while (now < MIN_VALID_EPOCH) {
    delay(200);
    Serial.print(".");
    now = time(nullptr);

    if (millis() - start > 30000) {
      Serial.println("\nNTP slow, retrying...");
      configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
      start = millis();
    }
    if (millis() - total > 180000) {
      Serial.println("\nWARNING: could not reach a time later than the CA's start date.");
      Serial.println("TLS will very likely fail. Check internet access and NTP reachability.");
      break;
    }
  }

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  Serial.printf("\nTime synced: %04d-%02d-%02d %02d:%02d:%02d (local)\n",
                timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

// Prints the underlying mbedTLS error so failures are diagnosable.
void printTlsError() {
  char err[128] = {0};
  int code = espClient.lastError(err, sizeof(err));
  if (code != 0) Serial.printf("TLS error %d: %s\n", code, err);
}
#endif

void connectMqtt() {
  int attempts = 0;
  while (!mqtt.connected()) {
#if defined(USE_TLS)
    Serial.println("Connecting to MQTT (TLS) at " + mqttHost + "...");
#else
    Serial.println("Connecting to MQTT at " + mqttHost + "...");
#endif
    // Last will: the broker publishes "offline" if we drop unexpectedly
    if (mqtt.connect(macStr, mqttUser.c_str(), mqttPass.c_str(),
                     topicStatus, 1, true, "offline")) {
      Serial.println("MQTT connected");
      mqtt.publish(topicStatus, "online", true);
      mqtt.subscribe(topicConfig);
      mqtt.subscribe("esp32/control");   // flat topic; every board listens
      publishAnnounce();
      return;
    }

    // rc=5 means the username/password was refused.
    // rc=-2 means the connection itself failed: wrong address, server not
    // running, or (with TLS) a certificate or clock problem.
    Serial.printf("MQTT connect failed, rc=%d, free heap=%u\n",
                  mqtt.state(), ESP.getFreeHeap());
#if defined(USE_TLS)
    printTlsError();
#endif

    // After many failures the saved address is probably wrong. Reopening the
    // setup page lets it be corrected without a computer or a reflash.
    if (++attempts >= 20) {
      Serial.println("Cannot reach the server after 20 tries.");
      Serial.println("Hold BOOT at power-on to change the address.");
      attempts = 0;
    }
    Serial.println("Retrying in 3s...");
    delay(3000);
  }
}

void publishAnnounce() {
  StaticJsonDocument<128> doc;
  doc["mac"] = macStr;
  doc["ip"]  = WiFi.localIP().toString();
  char buf[128];
  size_t n = serializeJson(doc, buf);
  mqtt.publish("esp32/announce", buf, n);
}

void mqttCallback(char* topic, byte* payload, unsigned int len) {
  if (strcmp(topic, topicConfig) == 0)          applyConfig(payload, len);
  else if (strcmp(topic, "esp32/control") == 0) applyControl(payload, len);
}

// ------------------------------------------------------------------
void applyConfig(byte* payload, unsigned int len) {
  // The dashboard republishes this retained config every ~20s. Re-applying an
  // IDENTICAL config would reset every relay and rebuild the DHT objects each
  // time, so a relay switched on would drop out moments later.
  String incoming;
  incoming.reserve(len);
  for (unsigned int i = 0; i < len; i++) incoming += (char)payload[i];
  if (haveConfig && incoming == lastConfigPayload) return;
  lastConfigPayload = incoming;
  haveConfig = true;

  // Remember relay states so an unrelated edit doesn't switch off a pump.
  int  prevPins[MAX_OUTPUTS];
  bool prevOn[MAX_OUTPUTS];
  int  prevCount = knownOutputCount;
  for (int i = 0; i < prevCount && i < MAX_OUTPUTS; i++) {
    prevPins[i] = knownOutputPins[i];
    prevOn[i]   = knownOutputOn[i];
  }

  // A zero-length payload is how MQTT deletes a retained message: treat it as
  // "this board has no devices", not as a parse error.
  if (len == 0) {
    for (int i = 0; i < MAX_SENSORS; i++) {
      if (sensors[i].dht) { delete sensors[i].dht; sensors[i].dht = nullptr; }
      sensors[i].active = false;
    }
    sensorCount = 0;
    Serial.println("Config cleared: 0 sensors");
    return;
  }

  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, payload, len)) {
    Serial.println("Config JSON parse failed");
    return;
  }
  if (!doc.is<JsonArray>()) {
    Serial.println("Config was not a bare array as expected");
    return;
  }

  for (int i = 0; i < MAX_SENSORS; i++) {
    if (sensors[i].dht) { delete sensors[i].dht; sensors[i].dht = nullptr; }
    sensors[i].active = false;
  }

  JsonArray arr = doc.as<JsonArray>();
  int idx = 0;
  knownOutputCount = 0;

  for (JsonObject s : arr) {
    const char* entryType = s["type"] | "sensor";

    // ---- relay channel ----
    if (strcmp(entryType, "relay") == 0) {
      int pin = s["pin"] | -1;
      if (pin < 0 || knownOutputCount >= MAX_OUTPUTS) continue;
      bool activeLow = s["active_low"] | true;

      bool wasOn = false;
      for (int p = 0; p < prevCount && p < MAX_OUTPUTS; p++) {
        if (prevPins[p] == pin) { wasOn = prevOn[p]; break; }
      }

      knownOutputPins[knownOutputCount]      = pin;
      knownOutputActiveLow[knownOutputCount] = activeLow;
      knownOutputOn[knownOutputCount]        = wasOn;
      knownOutputCount++;

      // active-LOW: ON drives the pin LOW. Setting the OFF level here matters:
      // pins idle LOW, so an active-LOW relay would close on every boot.
      pinMode(pin, OUTPUT);
      digitalWrite(pin, (wasOn != activeLow) ? HIGH : LOW);
      Serial.printf("Relay pin %d registered (%s), state %s\n",
                    pin, activeLow ? "active-LOW" : "active-HIGH", wasOn ? "ON" : "OFF");
      continue;
    }

    // ---- sensor ----
    if (idx >= MAX_SENSORS) continue;
    const char* moduleName = s["module"] | "";
    int pin = s["pin"] | -1;
    if (pin < 0 || strlen(moduleName) == 0) continue;

    sensors[idx].active = true;
    sensors[idx].module = String(moduleName);
    sensors[idx].pin = pin;
    sensors[idx].pulseCount = 0;
    sensors[idx].totalVolume = 0;

    if (sensors[idx].module == "DHT11") {
      sensors[idx].dht = new DHT(pin, DHT11);
      sensors[idx].dht->begin();
    } else if (sensors[idx].module == "DHT22") {
      sensors[idx].dht = new DHT(pin, DHT22);
      sensors[idx].dht->begin();
    } else if (sensors[idx].module == "PIR Motion Sensor") {
      // PULLDOWN, not plain INPUT: a floating pin (loose wire, wrong GPIO)
      // reads HIGH and looks like permanent motion. With a pulldown, a
      // disconnected sensor reads Clear, so bad wiring is obvious.
      pinMode(pin, INPUT_PULLDOWN);
    } else if (sensors[idx].module == "Soil Moisture Sensor" ||
               sensors[idx].module == "LDR Photoresistor") {
      pinMode(pin, INPUT);
    } else if (sensors[idx].module == "Water Flow Sensor") {
      pinMode(pin, INPUT_PULLUP);
      if (idx < 4) attachInterrupt(digitalPinToInterrupt(pin), pulseISRs[idx], FALLING);
    }
    idx++;
  }
  sensorCount = idx;
  Serial.printf("Config applied: %d sensors, %d relay channels\n", sensorCount, knownOutputCount);
}

// Control message on the flat topic: {"pin":26,"state":"ON"}
void applyControl(byte* payload, unsigned int len) {
  StaticJsonDocument<200> doc;
  if (deserializeJson(doc, payload, len)) return;
  int pin = doc["pin"] | -1;
  if (pin < 0) return;

  const char* stateStr = doc["state"] | "off";
  bool on = (strcasecmp(stateStr, "on") == 0);

  bool activeLow = true;      // safe default: most relay boards are active-LOW
  bool known = false;
  for (int i = 0; i < knownOutputCount; i++) {
    if (knownOutputPins[i] == pin) { activeLow = knownOutputActiveLow[i]; known = true; break; }
  }

  if (!known && knownOutputCount < MAX_OUTPUTS) {
    // Commanded before the config arrived — register it so it still works.
    pinMode(pin, OUTPUT);
    knownOutputPins[knownOutputCount] = pin;
    knownOutputActiveLow[knownOutputCount] = activeLow;
    knownOutputOn[knownOutputCount] = on;
    knownOutputCount++;
    Serial.printf("Pin %d commanded before config, assuming active-LOW\n", pin);
  } else {
    for (int i = 0; i < knownOutputCount; i++) {
      if (knownOutputPins[i] == pin) { knownOutputOn[i] = on; break; }
    }
  }

  digitalWrite(pin, (on != activeLow) ? HIGH : LOW);
  Serial.printf("Pin %d -> %s (%s)\n", pin, on ? "ON" : "OFF",
                activeLow ? "active-LOW" : "active-HIGH");
}

// ------------------------------------------------------------------
void readAndPublishSensors(unsigned long now) {
  if (now - lastRead < SENSOR_READ_INTERVAL_MS) return;
  lastRead = now;

  DynamicJsonDocument doc(1536);
  doc["device_id"] = macStr;
  JsonArray readings = doc.createNestedArray("readings");
  bool anything = false;

  for (int i = 0; i < sensorCount; i++) {
    if (!sensors[i].active) continue;

    JsonObject o = readings.createNestedObject();
    o["pin"] = sensors[i].pin;
    o["module"] = sensors[i].module;
    bool got = false;

    if (sensors[i].module == "DHT11" || sensors[i].module == "DHT22") {
      float t = sensors[i].dht->readTemperature();
      float h = sensors[i].dht->readHumidity();
      if (!isnan(t) && !isnan(h)) {
        o["temperature"] = t;
        o["humidity"] = h;
        if (!doc.containsKey("Temperature")) { doc["Temperature"] = t; doc["humidity"] = h; }
        got = true;
      }
    } else if (sensors[i].module == "Soil Moisture Sensor") {
      int v = map(analogRead(sensors[i].pin), 4095, 0, 0, 100);
      o["soil_percent"] = v;
      if (!doc.containsKey("soil_percent")) doc["soil_percent"] = v;
      got = true;
    } else if (sensors[i].module == "LDR Photoresistor") {
      int v = map(analogRead(sensors[i].pin), 0, 4095, 0, 100);
      o["light_percent"] = v;
      if (!doc.containsKey("light_percent")) doc["light_percent"] = v;
      got = true;
    } else if (sensors[i].module == "PIR Motion Sensor") {
      int v = digitalRead(sensors[i].pin);
      o["motion"] = v;
      if (!doc.containsKey("motion")) doc["motion"] = v;
      got = true;
    } else if (sensors[i].module == "Water Flow Sensor") {
      noInterrupts();
      long pulses = sensors[i].pulseCount;
      sensors[i].pulseCount = 0;
      interrupts();
      // ~7.5 pulses/sec per L/min for a YF-S201; check your sensor's datasheet
      float litersPerSec = pulses / 7.5 / 60.0;
      float rate = litersPerSec * 60.0;
      sensors[i].totalVolume += litersPerSec * (SENSOR_READ_INTERVAL_MS / 1000.0);
      o["flow_rate"] = rate;
      o["total_volume"] = sensors[i].totalVolume;
      if (!doc.containsKey("flow_rate")) {
        doc["flow_rate"] = rate;
        doc["total_volume"] = sensors[i].totalVolume;
      }
      got = true;
    }

    if (!got) readings.remove(readings.size() - 1);   // failed read: drop it
    else anything = true;
  }

  if (anything) {
    char buf[1024];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    mqtt.publish("esp32/read_all", buf, n);
    Serial.printf("Published: %s\n", buf);
  }
}
