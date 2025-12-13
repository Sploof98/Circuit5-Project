#include <ArduinoMqttClient.h>
#include <WiFiS3.h>

#include "MqttTelemetry.h"
#include "WiFiProvisioning.h"


// --- 1. MQTT CONFIG FOR UNO R4 (DEVICE SIDE, TLS + AUTH) ---

// Client ID for this device
static const char MQTT_CLIENT_ID[] = "uno-r4-living-room";

// --- 2. GLOBAL MQTT OBJECTS ---

// Use SSL/TLS client for port 8883
static WiFiSSLClient wifiClient;

// ArduinoMqttClient instance (wraps the SSL client)
static MqttClient gMqttClient(wifiClient);

// MQTT broker info (HiveMQ Cloud)
static MqttCredentials gMqttCreds;  // loaded from EEPROM provisioning


// Forward declaration of internal helper
static void connectToMqttBroker();

// --- 3. PUBLIC API IMPLEMENTATIONS ----------------------------------

void mqttSetup() {
  Serial.println("MQTT: Initialising client...");

  // Set client ID and keepalive
  gMqttClient.setId(MQTT_CLIENT_ID);
  gMqttClient.setKeepAliveInterval(60);

  // Set username/password for HiveMQ Cloud
  gMqttClient.setUsernamePassword(gMqttCreds.username, gMqttCreds.password);


  // Load stored MQTT credentials (saved by provisioning portal)
  if (!loadMqttCredentials(gMqttCreds)) {
    Serial.println("MQTT: No stored MQTT credentials. Run provisioning portal.");
    // Option A: just return and let your main sketch decide what to do
    return;

    // Option B (if you want it automatic):
    // runProvisioningPortal(wifiCreds);  // only if you have wifiCreds here
  }

  // If using HiveMQ username/password auth:
  gMqttClient.setUsernamePassword(gMqttCreds.username, gMqttCreds.password);


  // Connect to broker (host + port) in helper
  connectToMqttBroker();
}


void mqttLoop() {
  if (gMqttCreds.magic != MQTT_MAGIC) return; 
  if (!gMqttClient.connected()) connectToMqttBroker();
  gMqttClient.poll();
}


void mqttPublishTelemetry(float temperature, float humidity, const String &status) {
  if (!gMqttClient.connected()) {
    connectToMqttBroker();
    if (!gMqttClient.connected()) {
      Serial.println("MQTT: still not connected, skipping telemetry publish.");
      return;
    }
  }

  // Build JSON payload
  String payload = "{";
  payload += "\"deviceId\":\"uno-r4-living-room\",";
  payload += "\"temperature\":";
  payload += String(temperature, 2);
  payload += ",";
  payload += "\"humidity\":";
  payload += String(humidity, 2);
  payload += ",";
  payload += "\"status\":\"";
  payload += status;
  payload += "\"}";

  Serial.print("MQTT: Publishing to ");
  Serial.print(gMqttCreds.topic);
  Serial.print(" => ");
  Serial.println(payload);

  gMqttClient.beginMessage(gMqttCreds.topic);
  gMqttClient.print(payload);
  gMqttClient.endMessage();
}

// --- 4. INTERNAL HELPER ---------------------------------------------

static void connectToMqttBroker() {
  Serial.print("MQTT: Connecting to broker ");
  Serial.print(gMqttCreds.broker);
  Serial.print(":");
  Serial.println(gMqttCreds.port);

  int attempts = 0;
  while (!gMqttClient.connect(gMqttCreds.broker, gMqttCreds.port)) {
    Serial.print("MQTT connect failed, error code = ");
    Serial.println(gMqttClient.connectError());

    attempts++;
    if (attempts >= 5) {
      Serial.println("MQTT: giving up for now, will retry in loop.");
      return;
    }

    delay(2000);
  }

  Serial.println("MQTT: Connected to HiveMQ Cloud broker.");
}
