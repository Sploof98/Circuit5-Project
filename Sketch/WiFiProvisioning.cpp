#include "WiFiProvisioning.h"

#include <WiFiS3.h>
#include <EEPROM.h>

/* ===================== CONFIG =====================
===     Portal Address: http://192.168.4.1        === 
*/


// AP used during provisioning
static const char AP_SSID[]     = "UNO-R4-SETUP";
static const char AP_PASSWORD[] = "configureme";
static const int  AP_CHANNEL    = 1;

// EEPROM layout
static const int     EEPROM_ADDR = 0;

// HTTP server for config
static WiFiServer configServer(80);

// Forward-declare internal helper functions
static String getFormField(const String &body, const String &name);
static String urlDecode(const String &src);
static void   handleConfigClient(WiFiClient &client, WifiCredentials &creds);

static const int EEPROM_WIFI_ADDR = 0;
static const int EEPROM_MQTT_ADDR = EEPROM_WIFI_ADDR + sizeof(WifiCredentials);

// ===================== PUBLIC API =====================

bool loadWifiCredentials(WifiCredentials &creds) {
  EEPROM.get(EEPROM_ADDR, creds);
  if (creds.magic != WIFI_MAGIC) return false;
  if (creds.ssid[0] == '\0')     return false;
  return true;
}

void saveWifiCredentials(const WifiCredentials &creds) {
  EEPROM.put(EEPROM_ADDR, creds);
  // On UNO R4 WiFi, EEPROM writes are committed immediately.
}

void clearWifiCredentials() {
  WifiCredentials empty;
  memset(&empty, 0, sizeof(empty));
  EEPROM.put(EEPROM_ADDR, empty);
}

// Try to connect to Wi-Fi using stored credentials
bool connectWithStoredCredentials(WifiCredentials &creds, uint32_t timeoutMs) {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("ERROR: WiFi module not found.");
    return false;
  }

  Serial.print("Connecting to ");
  Serial.print(creds.ssid);
  Serial.println(" ...");

  unsigned long start = millis();
  int status = WL_IDLE_STATUS;

  while ((millis() - start) < timeoutMs) {
    status = WiFi.begin(creds.ssid, creds.password);
    if (status == WL_CONNECTED) {
      Serial.println("Connected to Wi-Fi!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      return true;
    }
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\nWi-Fi connect timed out.");
  return false;
}

// Start AP + HTTP server and handle config requests
void runProvisioningPortal(WifiCredentials &creds) {
  WiFi.end();  // ensure client mode is off

  Serial.println("Starting Wi-Fi Config AP...");
  int status = WiFi.beginAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);
  if (status != WL_AP_LISTENING && status != WL_AP_CONNECTED) {
    Serial.println("ERROR: Failed to start AP.");
    while (true) { delay(1000); }  // fatal
  }

  IPAddress apIP = WiFi.localIP();
  Serial.print("Config AP SSID: "); Serial.println(AP_SSID);
  Serial.print("Password: ");       Serial.println(AP_PASSWORD);
  Serial.print("Open: http://");    Serial.println(apIP);

  configServer.begin();

  while (true) {
    WiFiClient client = configServer.available();
    if (client) {
      handleConfigClient(client, creds);
      client.stop();
    }
    delay(10);
  }
}

// MQTT credentials load/save/clear

bool loadMqttCredentials(MqttCredentials &creds) {
  EEPROM.get(EEPROM_MQTT_ADDR, creds);
  if (creds.magic != MQTT_MAGIC) return false;
  if (creds.broker[0] == '\0') return false;
  if (creds.topic[0] == '\0') return false;
  return true;
}

void saveMqttCredentials(const MqttCredentials &creds) {
  EEPROM.put(EEPROM_MQTT_ADDR, creds);
}

void clearMqttCredentials() {
  MqttCredentials empty;
  memset(&empty, 0, sizeof(empty));
  EEPROM.put(EEPROM_MQTT_ADDR, empty);
}


// ===================== INTERNAL HELPERS =====================

static void handleConfigClient(WiFiClient &client, WifiCredentials &creds) {
  String requestLine = "";
  String headers     = "";
  String body        = "";
  bool   isPost      = false;
  bool   inHeaders   = true;

  unsigned long start = millis();
  while (client.connected() && (millis() - start) < 5000) {
    if (!client.available()) {
      delay(1);
      continue;
    }
    String line = client.readStringUntil('\n');
    if (inHeaders) {
      if (requestLine.length() == 0) {
        requestLine = line;
        if (requestLine.startsWith("POST")) isPost = true;
      } else if (line == "\r") {
        // headers finished
        inHeaders = false;
        if (!isPost) break;
      } else {
        headers += line;
      }
    } else {
      body += line;
    }
  }
  body.trim();

  Serial.println("=== HTTP Request ===");
  Serial.println(requestLine);

  // POST /save → store Wi-Fi + MQTT credentials
  if (requestLine.startsWith("POST /save")) {
    // ---- Wi-Fi fields ----
    String ssidField = urlDecode(getFormField(body, "ssid"));
    String passField = urlDecode(getFormField(body, "password"));
    ssidField.trim();
    passField.trim();

    // ---- MQTT fields ----
    String brokerField   = urlDecode(getFormField(body, "mqtt_broker"));
    String portField     = urlDecode(getFormField(body, "mqtt_port"));
    String userField     = urlDecode(getFormField(body, "mqtt_user"));
    String mqttPassField = urlDecode(getFormField(body, "mqtt_pass"));
    String topicField    = urlDecode(getFormField(body, "mqtt_topic"));

    brokerField.trim();
    portField.trim();
    userField.trim();
    mqttPassField.trim();
    topicField.trim();

    // Default port if empty / invalid
    int port = portField.toInt();
    if (port <= 0) port = 8883;

    // --- Save Wi-Fi ---
    WifiCredentials newWifi;
    memset(&newWifi, 0, sizeof(newWifi));
    newWifi.magic = WIFI_MAGIC;

    ssidField.substring(0, sizeof(newWifi.ssid) - 1)
        .toCharArray(newWifi.ssid, sizeof(newWifi.ssid));
    passField.substring(0, sizeof(newWifi.password) - 1)
        .toCharArray(newWifi.password, sizeof(newWifi.password));

    saveWifiCredentials(newWifi);
    creds = newWifi;  // update caller’s copy

    // --- Save MQTT ---
    // Requires you to add MqttCredentials + saveMqttCredentials() in WiFiProvisioning.h/.cpp
    MqttCredentials newMqtt;
    memset(&newMqtt, 0, sizeof(newMqtt));
    newMqtt.magic = MQTT_MAGIC;
    newMqtt.port  = (uint16_t)port;

    brokerField.substring(0, sizeof(newMqtt.broker) - 1)
        .toCharArray(newMqtt.broker, sizeof(newMqtt.broker));
    userField.substring(0, sizeof(newMqtt.username) - 1)
        .toCharArray(newMqtt.username, sizeof(newMqtt.username));
    mqttPassField.substring(0, sizeof(newMqtt.password) - 1)
        .toCharArray(newMqtt.password, sizeof(newMqtt.password));
    topicField.substring(0, sizeof(newMqtt.topic) - 1)
        .toCharArray(newMqtt.topic, sizeof(newMqtt.topic));

    saveMqttCredentials(newMqtt);

    // --- Response page ---
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=utf-8");
    client.println("Connection: close");
    client.println();
    client.println(F("<h2>UNO R4 Provisioning</h2>"
                      "<p>Enter Wi-Fi + MQTT settings for this device.</p>"
                      "<form method='POST' action='/save'>"

                      "<h3>Wi-Fi</h3>"
                      "SSID:<br><input type='text' name='ssid' required><br><br>"
                      "Password:<br><input type='password' name='password'><br><br>"

                      "<h3>MQTT</h3>"
                      "Broker Host:<br><input type='text' name='mqtt_broker' placeholder='xxxx.s1.eu.hivemq.cloud' required><br><br>"
                      "Port:<br><input type='number' name='mqtt_port' value='8883' min='1' max='65535' required><br><br>"
                      "Username:<br><input type='text' name='mqtt_user' required><br><br>"
                      "Password:<br><input type='password' name='mqtt_pass' required><br><br>"
                      "Telemetry Topic:<br><input type='text' name='mqtt_topic' placeholder='telemetry/device123' required><br><br>"

                      "<button type='submit'>Save</button>"
                      "</form>"

                      "<p style='font-size:0.9em;color:#666;'>"
                      "Credentials are stored on the device (EEPROM emulation). "
                      "To wipe them later, call clearWifiCredentials() / clearMqttCredentials()."
                      "</p>"
                      "</body></html>"));
    return;
}

// If not POST /save, show the config page (GET / or anything else)
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=utf-8");
  client.println("Connection: close");
  client.println();
  client.println(F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                   "<title>Provisioning</title></head><body>"));

  // (same form HTML you used before, or a simpler placeholder for now)
  client.println(F("<h2>Provisioning</h2>"
                   "<form method='POST' action='/save'>"
                   "SSID:<br><input name='ssid'><br>"
                   "WiFi Password:<br><input type='password' name='password'><br><br>"
                   "Broker:<br><input name='mqtt_broker'><br>"
                   "Port:<br><input name='mqtt_port' value='8883'><br>"
                   "MQTT User:<br><input name='mqtt_user'><br>"
                   "MQTT Pass:<br><input type='password' name='mqtt_pass'><br>"
                   "Topic:<br><input name='mqtt_topic'><br><br>"
                   "<button type='submit'>Save</button>"
                   "</form></body></html>"));
}

// Parse form field from x-www-form-urlencoded body
static String getFormField(const String &body, const String &name) {
  String key = name + "=";
  int start = body.indexOf(key);
  if (start < 0) return "";
  start += key.length();
  int end = body.indexOf('&', start);
  if (end < 0) end = body.length();
  return body.substring(start, end);
}

// URL decode: + and %xx
static String urlDecode(const String &src) {
  String out;
  out.reserve(src.length());
  for (size_t i = 0; i < src.length(); i++) {
    char c = src[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < src.length()) {
      char h1 = src[i + 1];
      char h2 = src[i + 2];
      int hi = (h1 >= 'A') ? ((h1 & ~0x20) - 'A' + 10) : (h1 - '0');
      int lo = (h2 >= 'A') ? ((h2 & ~0x20) - 'A' + 10) : (h2 - '0');
      char decoded = (char)((hi << 4) | (lo & 0x0F));
      out += decoded;
      i += 2;
    } else {
      out += c;
    }
  }
  return out;
}
