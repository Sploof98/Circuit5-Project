#pragma once

#include <Arduino.h>


static const uint8_t WIFI_MAGIC = 0x42;
static const uint8_t MQTT_MAGIC = 0x43;

// Simple struct for storing Wi-Fi credentials in EEPROM
struct WifiCredentials {
  uint8_t magic;        // marker to know if credentials are valid
  char ssid[32];        // 31 chars + null
  char password[64];    // 63 chars + null
};

struct MqttCredentials {
  uint8_t magic;
  char broker[80];
  uint16_t port;
  char username[64];
  char password[64];
  char topic[128];
};

bool loadMqttCredentials(MqttCredentials &creds);

void saveMqttCredentials(const MqttCredentials &creds);

void clearMqttCredentials();

// Load credentials from EEPROM. 
// Returns true if valid credentials exist.
bool loadWifiCredentials(WifiCredentials &creds);

// Save credentials to EEPROM.
void saveWifiCredentials(const WifiCredentials &creds);

// Clear credentials from EEPROM (factory reset helper).
void clearWifiCredentials();

// Try to connect to Wi-Fi using stored credentials.
// Returns true on success, false on timeout/failure.
bool connectWithStoredCredentials(WifiCredentials &creds, uint32_t timeoutMs);

// Run the provisioning portal: start AP, HTTP server, form handler.
// Blocks forever until you reset the board.
void runProvisioningPortal(WifiCredentials &creds);
