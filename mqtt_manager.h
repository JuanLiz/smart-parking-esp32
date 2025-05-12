#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// MQTT Configuration passed from main .ino
struct MQTTConfig {
    const char* broker_host;
    uint16_t broker_port;
    const char* client_id_prefix; // e.g., "juanliz-sparking-" (ESP32 will append unique part)
    const char* base_topic_prefix; // e.g., "juanliz-sparking-esp32/"
    // Add user/password if your broker requires them
    // const char* mqtt_user;
    // const char* mqtt_password;
};

// Public Function Declarations
/**
 * @brief Initializes WiFi and MQTT client.
 * Connects to WiFi and then to the MQTT broker.
 * @param config MQTTConfig struct with broker details, topics, etc.
 * @param wifi_ssid SSID of the WiFi network.
 * @param wifi_password Password for the WiFi network.
 */
void setupMQTTManager(const MQTTConfig& config, const char* wifi_ssid, const char* wifi_password);

/**
 * @brief Keeps the MQTT connection alive and processes incoming messages.
 * Should be called regularly in the main loop().
 */
void loopMQTTManager();

/**
 * @brief Publishes a message to a given MQTT topic.
 * @param sub_topic The part of the topic after the base_topic_prefix.
 * @param payload The message payload string.
 * @param retained Whether the message should be retained by the broker.
 * @return true if publish was successful (queued), false otherwise.
 */
bool publishMQTTMessage(const char* sub_topic, const char* payload, bool retained = false);

// --- Helper function for converting iButton ID to String ---
String ibuttonBytesToHexString(const byte* id_bytes);

// --- Specific publishing functions for convenience ---
void publishStatus(bool online, uint32_t occupancy, uint32_t total_spaces);
void publishIButtonScanned(const byte* ibutton_id, bool is_registered, uint32_t associated_id);
void publishPairingReady(const char* pairing_session_id);
void publishPairingSuccess(const char* pairing_session_id, const byte* ibutton_id, uint32_t associated_id);
void publishPairingFailure(const char* pairing_session_id, const char* reason);
void publish2FARequest(const byte* ibutton_id, uint32_t associated_id, const char* device_id_esp32);

// --- Getters for state needed by main .ino ---
bool isMQTTConnected();
bool isPairingModeActive();
const char* getCurrentPairingSessionId();
void clearPairingMode(); // To be called by main .ino after processing iButton for pairing

bool isWaitingFor2FA();
const char* get2FA_iButtonId_Str(); // Gets iButton ID (string) waiting for 2FA
void clear2FA_WaitingState(); // Clears the 2FA waiting state
bool get2FA_GrantStatus(); // Gets true if 2FA was granted, false if denied or timed out
void reset2FA_GrantStatus(); // Resets grant status for next 2FA


#endif // MQTT_MANAGER_H