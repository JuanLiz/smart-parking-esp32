#include "mqtt_manager.h"
#include "ibutton_manager.h"  // To use printIButtonID if needed for debug

// --- Module Variables ---
WiFiClient espWiFiClient;
PubSubClient mqttClient(espWiFiClient);
MQTTConfig mqtt_config;
String full_client_id;
char char_buffer[256];  // General purpose buffer for payloads, topics

// Pairing state
bool pairing_mode_active = false;
String current_pairing_session_id_str = "";
unsigned long pairing_timeout_start_ms = 0;
const unsigned long PAIRING_TIMEOUT_DURATION_MS = 60000;  // 60 seconds

// 2FA state
bool waiting_for_2fa_response = false;
String two_fa_ibutton_id_str = "";  // Store iButton ID as string for 2FA
unsigned long two_fa_timeout_start_ms = 0;
const unsigned long TWO_FA_TIMEOUT_DURATION_MS = 30000;  // 30 seconds
bool two_fa_granted = false;

// For deletion
bool delete_ibutton_mode_active = false;
unsigned long delete_ibutton_timeout_start_ms = 0;
const unsigned long DELETE_IBUTTON_TIMEOUT_DURATION_MS = 60000;


// Forward declaration for callback
void mqttCallback(char* topic, byte* payload, unsigned int length);

void setupWiFi(const char* ssid, const char* password) {
  delay(10);
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 30) {  // Retry for ~15 seconds
    delay(500);
    Serial.print(".");
    retries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi. Please check credentials or signal.");
    // ESP.restart(); // Or handle error differently
  }
}

void reconnectMQTT() {
  if (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a unique client ID
    uint64_t chipid = ESP.getEfuseMac();
    uint16_t unique_part = (uint16_t)(chipid >> 32);  // Higher part of MAC
    full_client_id = String(mqtt_config.client_id_prefix) + String(unique_part, HEX);

    Serial.print("Client ID: ");
    Serial.println(full_client_id);

    // Attempt to connect
    if (mqttClient.connect(full_client_id.c_str())) {
      Serial.println("MQTT connected!");
      // Subscribe to command topics
      String cmd_topic_base = String(mqtt_config.base_topic_prefix) + "cmd/";
      mqttClient.subscribe((cmd_topic_base + "initiate_pairing").c_str());
      Serial.println("Subscribed to: " + cmd_topic_base + "initiate_pairing");
      mqttClient.subscribe((cmd_topic_base + "cancel_pairing").c_str());
      Serial.println("Subscribed to: " + cmd_topic_base + "cancel_pairing");
      mqttClient.subscribe((cmd_topic_base + "auth/2fa_response").c_str());
      Serial.println("Subscribed to: " + cmd_topic_base + "auth/2fa_response");
      // For deletion
      mqttClient.subscribe((cmd_topic_base + "ibutton/initiate_delete_mode").c_str());
      Serial.println("Subscribed to: " + cmd_topic_base + "ibutton/initiate_delete_mode");
      mqttClient.subscribe((cmd_topic_base + "ibutton/cancel_delete_mode").c_str());
      Serial.println("Subscribed to: " + cmd_topic_base + "ibutton/cancel_delete_mode");

    } else {
      Serial.print("MQTT connect failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
    }
  }
}

void setupMQTTManager(const MQTTConfig& config, const char* wifi_ssid, const char* wifi_password) {
  mqtt_config = config;  // Store config

  setupWiFi(wifi_ssid, wifi_password);

  if (WiFi.status() == WL_CONNECTED) {
    mqttClient.setServer(mqtt_config.broker_host, mqtt_config.broker_port);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(512);  // Increase if payloads are larger
    reconnectMQTT();                // Initial connection attempt
  } else {
    Serial.println("MQTT setup skipped due to WiFi connection failure.");
  }
}

void loopMQTTManager() {
  if (WiFi.status() != WL_CONNECTED) {
    // Attempt to reconnect WiFi if lost? Or let main handle restart.
    // For now, just don't loop MQTT if WiFi is down.
    // Serial.println("WiFi disconnected. MQTT loop paused.");
    // setupWiFi(stored_ssid, stored_password) // Need to store ssid/pass for this
    return;
  }

  if (!mqttClient.connected()) {
    static unsigned long last_mqtt_reconnect_attempt = 0;
    if (millis() - last_mqtt_reconnect_attempt > 5000) {  // Try reconnecting every 5s
      last_mqtt_reconnect_attempt = millis();
      reconnectMQTT();
    }
  } else {
    mqttClient.loop();  // Process MQTT messages (may call mqttCallback and set two_fa_granted)
  }

  // Handle pairing timeout
  if (pairing_mode_active && (millis() - pairing_timeout_start_ms > PAIRING_TIMEOUT_DURATION_MS)) {
    Serial.println("Pairing mode timed out.");
    publishPairingFailure(current_pairing_session_id_str.c_str(), "timeout");
    clearPairingMode();
  }

  // Handle 2FA timeout
  // Check if we are waiting AND the timer has expired
  if (waiting_for_2fa_response && (millis() - two_fa_timeout_start_ms >= TWO_FA_TIMEOUT_DURATION_MS)) {
    Serial.println("2FA response timed out.");  // <<-- ESTO ES LO QUE VES
    two_fa_granted = false;                     // Explicitly mark as not granted on timeout
    clear2FA_WaitingState();                    // <<-- ESTO LIMPIA EL ESTADO
    // El .ino detectará que isWaitingFor2FA() es false y two_fa_granted es false.
  }

  // Handle Delete iButton Mode Timeout ---
  if (delete_ibutton_mode_active
      && (millis() - delete_ibutton_timeout_start_ms > DELETE_IBUTTON_TIMEOUT_DURATION_MS)) {
    Serial.println("MQTT: Delete iButton mode timed out.");
    publishDeleteFailure("timeout");
    clearDeleteIButtonMode();
  }
}

bool publishMQTTMessage(const char* sub_topic, const char* payload, bool retained) {
  if (!mqttClient.connected()) {
    Serial.println("MQTT not connected. Cannot publish.");
    return false;
  }
  String full_topic = String(mqtt_config.base_topic_prefix) + sub_topic;
  Serial.printf("Publishing to %s: %s\n", full_topic.c_str(), payload);
  return mqttClient.publish(full_topic.c_str(), payload, retained);
}

// Helper to convert byte array iButton ID to hex string
String ibuttonBytesToHexString(const byte* id_bytes) {
  String hex_str = "";
  for (int i = 0; i < IBUTTON_ID_LEN; i++) {
    char hex_pair[3];
    sprintf(hex_pair, "%02X", id_bytes[i]);
    hex_str += hex_pair;
  }
  return hex_str;
}

void mqttCallback(char* topic, byte* payload_bytes, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  String payload_str;
  for (int i = 0; i < length; i++) {
    payload_str += (char)payload_bytes[i];
  }
  Serial.println(payload_str);

  String topic_str(topic);
  String cmd_topic_base = String(mqtt_config.base_topic_prefix) + "cmd/";

  // --- Handle initiate_pairing command ---
  if (topic_str.equals(cmd_topic_base + "initiate_pairing")) {
    Serial.print("DEBUG: Payload for initiate_pairing: '");  // Debug print
    Serial.print(payload_str);
    Serial.println("'");

    // Basic JSON parsing - Look for "pairing_session_id":" or "pairing_session_id": "
    const char* key_psid_no_space = "\"pairing_session_id\":\"";
    const char* key_psid_with_space = "\"pairing_session_id\": \"";
    int id_start_pos = payload_str.indexOf(key_psid_with_space);  // Try with space first
    int key_len = strlen(key_psid_with_space);

    if (id_start_pos == -1) {  // If not found with space, try without space
      id_start_pos = payload_str.indexOf(key_psid_no_space);
      key_len = strlen(key_psid_no_space);
    }

    String received_session_id = "";  // Buffer for the parsed ID

    if (id_start_pos > -1) {  // If the key was found (with or without space)
      int val_start = id_start_pos + key_len;
      int val_end = payload_str.indexOf("\"", val_start);  // Find the ending quote
      if (val_end > -1) {
        received_session_id = payload_str.substring(val_start, val_end);
      } else {
        Serial.println("DEBUG: Failed to find end quote for 'pairing_session_id' value.");
      }
    } else {
      Serial.println("DEBUG: Failed to find 'pairing_session_id' key.");
    }

    // Check if parsing was successful and the extracted ID is not empty
    if (!received_session_id.isEmpty()) {
      current_pairing_session_id_str = received_session_id;  // Store the received ID
      pairing_mode_active = true;
      pairing_timeout_start_ms = millis();
      Serial.print("Pairing mode activated. Session ID: ");
      Serial.println(current_pairing_session_id_str);
      publishPairingReady(current_pairing_session_id_str.c_str());
    } else {
      Serial.println("Invalid or empty pairing_session_id in payload.");
    }
  }
  // --- Handle cancel_pairing command ---
  else if (topic_str.equals(cmd_topic_base + "cancel_pairing")) {
    Serial.print("DEBUG: Payload for cancel_pairing: '");  // Debug print
    Serial.print(payload_str);
    Serial.println("'");

    // Basic JSON parsing - Look for "pairing_session_id":" or "pairing_session_id": "
    const char* key_psid_no_space = "\"pairing_session_id\":\"";
    const char* key_psid_with_space = "\"pairing_session_id\": \"";
    int id_start_pos = payload_str.indexOf(key_psid_with_space);  // Try with space first
    int key_len = strlen(key_psid_with_space);

    if (id_start_pos == -1) {  // If not found with space, try without space
      id_start_pos = payload_str.indexOf(key_psid_no_space);
      key_len = strlen(key_psid_no_space);
    }

    String session_to_cancel = "";  // Buffer for the parsed ID

    if (id_start_pos > -1) {  // If the key was found
      int val_start = id_start_pos + key_len;
      int val_end = payload_str.indexOf("\"", val_start);  // Find the ending quote
      if (val_end > -1) {
        session_to_cancel = payload_str.substring(val_start, val_end);
      } else {
        Serial.println("DEBUG: Failed to find end quote for 'pairing_session_id' value.");
      }
    } else {
      Serial.println("DEBUG: Failed to find 'pairing_session_id' key.");
    }


    // Check if parsing was successful and the extracted ID is not empty
    if (!session_to_cancel.isEmpty()) {
      if (pairing_mode_active && session_to_cancel.equals(current_pairing_session_id_str)) {
        Serial.println("Pairing cancelled by remote command.");
        publishPairingFailure(current_pairing_session_id_str.c_str(), "cancelled_by_app");
        clearPairingMode();  // This also clears current_pairing_session_id_str
      } else {
        Serial.print("Pairing cancellation request for non-active or mismatched session: ");
        Serial.println(session_to_cancel);
      }
    } else {
      Serial.println("Invalid or empty pairing_session_id in cancel payload.");
    }
  }
  // --- Handle 2FA response ---
  else if (topic_str.equals(cmd_topic_base + "auth/2fa_response")) {
    if (waiting_for_2fa_response) {
      String received_ib_id = "";
      bool allow_entry_val = false;     // Para almacenar el valor booleano
      bool parsed_allow_entry = false;  // Flag para saber si se parseó allow_entry

      // --- Parse "ibutton_id" ---
      const char* key_ib_id_no_space = "\"ibutton_id\":\"";
      const char* key_ib_id_with_space = "\"ibutton_id\": \"";
      int id_start_pos = payload_str.indexOf(key_ib_id_with_space);  // Intenta con espacio primero
      int key_len = strlen(key_ib_id_with_space);

      if (id_start_pos == -1) {  // Si no se encontró con espacio, intenta sin espacio
        id_start_pos = payload_str.indexOf(key_ib_id_no_space);
        key_len = strlen(key_ib_id_no_space);
      }

      if (id_start_pos > -1) {
        int val_start = id_start_pos + key_len;
        int val_end = payload_str.indexOf("\"", val_start);
        if (val_end > -1) {
          received_ib_id = payload_str.substring(val_start, val_end);
        } else {
          Serial.println("DEBUG: Failed to find end quote for 'ibutton_id' value.");
        }
      } else {
        Serial.println("DEBUG: Failed to find 'ibutton_id' key.");
      }


      // --- Parse "allow_entry" ---
      if (payload_str.indexOf("\"allow_entry\":true") > -1 || payload_str.indexOf("\"allow_entry\": true") > -1) {
        allow_entry_val = true;
        parsed_allow_entry = true;
      } else if (payload_str.indexOf("\"allow_entry\":false") > -1 || payload_str.indexOf("\"allow_entry\": false") > -1) {
        allow_entry_val = false;
        parsed_allow_entry = true;
      }

      if (received_ib_id.isEmpty()) {
        Serial.println("DEBUG: 'ibutton_id' could not be parsed from 2FA response.");
      }
      if (!parsed_allow_entry) {
        Serial.println("DEBUG: 'allow_entry' could not be parsed from 2FA response. Defaulting to DENY.");
        allow_entry_val = false;  // Default seguro
      }

      if (!received_ib_id.isEmpty() && received_ib_id.equalsIgnoreCase(two_fa_ibutton_id_str)) {
        if (parsed_allow_entry) {  // Solo actuar si parseamos 'allow_entry'
          if (allow_entry_val) {
            two_fa_granted = true;  // Flag para que el .ino actúe
            Serial.println("2FA: Entry GRANTED by remote.");
          } else {
            two_fa_granted = false;  // Flag para que el .ino actúe
            Serial.println("2FA: Entry DENIED by remote.");
          }
          // NO LLAMAR clear2FA_WaitingState() ni modificar waiting_for_2fa_response AQUI.
          // El .ino procesará el resultado (two_fa_granted) y luego limpiará el estado.
        } else {
          // No se pudo parsear 'allow_entry', se denegó por defecto en el .ino
          // two_fa_granted ya es false.
          Serial.println("2FA: 'allow_entry' field missing or invalid in remote response.");
        }
      } else {
        // Mismatch de iButton ID
        Serial.println("2FA: Received response for mismatched iButton ID. Ignored.");
        // No hacemos nada con el estado de 2FA pendiente si el ID no coincide.
        // El timeout original sigue corriendo para la solicitud correcta.
      }
    } else {
      Serial.println("Received 2FA response, but not waiting for one. Ignored.");
    }
  }
  // --- Handle initiate_delete_mode command ---
  else if (topic_str.equals(cmd_topic_base + "ibutton/initiate_delete_mode")) {
    // Solo activar si no hay otra operación MQTT en curso
    if (!delete_ibutton_mode_active && !isPairingModeActive() && !isWaitingFor2FA()) {
      Serial.println("MQTT: Delete iButton mode activated by remote command.");
      delete_ibutton_mode_active = true;
      delete_ibutton_timeout_start_ms = millis();
      publishDeleteReady();  // Opcional: notificar a la app que estamos listos
    } else {
      Serial.println("MQTT: Cannot activate delete iButton mode, another operation is active or already in delete mode.");
      // Opcional: Enviar un mensaje de error a la app si se intenta activar mientras otra cosa está activa
      // publishGenericError("delete_mode_activation_failed", "another_operation_active");
    }
  }
  // --- Handle cancel_delete_mode command ---
    else if (topic_str.equals(cmd_topic_base + "ibutton/cancel_delete_mode")) {
        if (delete_ibutton_mode_active) {
            Serial.println("MQTT: Delete iButton mode cancelled by remote command.");
            clearDeleteIButtonMode(); // Esta función ya resetea el flag y el timer
        } else {
            Serial.println("MQTT: Received cancel_delete_mode, but delete mode was not active.");
        }
    }
}

// --- Specific Publishing Functions ---
void publishStatus(bool online, uint32_t occupancy, uint32_t total_spaces) {
  snprintf(char_buffer, sizeof(char_buffer), "{\"online\":%s, \"occupancy\":%u, \"total_spaces\":%u, \"ip\":\"%s\"}",
           online ? "true" : "false", occupancy, total_spaces, WiFi.localIP().toString().c_str());
  publishMQTTMessage("status", char_buffer, true);
}

void publishIButtonScanned(const byte* ibutton_id, bool is_registered, uint32_t associated_id) {
  String ib_id_str = ibuttonBytesToHexString(ibutton_id);
  if (is_registered) {
    snprintf(char_buffer, sizeof(char_buffer), "{\"ibutton_id\":\"%s\", \"is_registered\":true, \"associated_id\":%u}",
             ib_id_str.c_str(), associated_id);
  } else {
    snprintf(char_buffer, sizeof(char_buffer), "{\"ibutton_id\":\"%s\", \"is_registered\":false}",
             ib_id_str.c_str());
  }
  publishMQTTMessage("ibutton/scanned", char_buffer);
}

void publishPairingReady(const char* pairing_session_id) {
  snprintf(char_buffer, sizeof(char_buffer), "{\"pairing_session_id\":\"%s\"}", pairing_session_id);
  publishMQTTMessage("pairing/ready_for_ibutton", char_buffer);
}

void publishPairingSuccess(const char* pairing_session_id, const byte* ibutton_id, uint32_t associated_id) {
  String ib_id_str = ibuttonBytesToHexString(ibutton_id);
  snprintf(char_buffer, sizeof(char_buffer), "{\"pairing_session_id\":\"%s\", \"ibutton_id\":\"%s\", \"associated_id\":%u}",
           pairing_session_id, ib_id_str.c_str(), associated_id);
  publishMQTTMessage("pairing/success", char_buffer);
}

void publishPairingFailure(const char* pairing_session_id, const char* reason) {
  snprintf(char_buffer, sizeof(char_buffer), "{\"pairing_session_id\":\"%s\", \"reason\":\"%s\"}",
           pairing_session_id, reason);
  publishMQTTMessage("pairing/failure", char_buffer);
}

void publish2FARequest(const byte* ibutton_id, uint32_t associated_id, const char* device_id_esp32) {
  String ib_id_str = ibuttonBytesToHexString(ibutton_id);
  two_fa_ibutton_id_str = ib_id_str;  // Store for matching response
  waiting_for_2fa_response = true;
  two_fa_granted = false;              // Reset grant status
  two_fa_timeout_start_ms = millis();  // <<-- AQUI EMPIEZA EL TEMPORIZADOR

  Serial.printf("2FA: Request sent. Timer started at %lu ms for %u ms timeout.\n", two_fa_timeout_start_ms, TWO_FA_TIMEOUT_DURATION_MS);  // DEBUG TIMER

  snprintf(char_buffer, sizeof(char_buffer), "{\"ibutton_id\":\"%s\", \"associated_id\":%u, \"device_id\":\"%s\"}",
           ib_id_str.c_str(), associated_id, device_id_esp32);
  publishMQTTMessage("auth/2fa_request", char_buffer);
}

// --- Deletion publish implementations ---
void publishDeleteReady() {
  // Payload simple, o incluso vacío si el tópico es suficiente
  publishMQTTMessage("ibutton/delete_ready", "{\"status\":\"ready_for_delete\"}");
}

void publishDeleteSuccess(const byte* ibutton_id) {
  String ib_id_str = ibuttonBytesToHexString(ibutton_id);
  snprintf(char_buffer, sizeof(char_buffer), "{\"ibutton_id\":\"%s\", \"status\":\"deleted\"}", ib_id_str.c_str());
  publishMQTTMessage("ibutton/delete_success", char_buffer);
}

void publishDeleteFailure(const char* reason, const byte* ibutton_id_attempted) {
  if (ibutton_id_attempted != nullptr) {
    String ib_id_str = ibuttonBytesToHexString(ibutton_id_attempted);
    snprintf(char_buffer, sizeof(char_buffer), "{\"reason\":\"%s\", \"ibutton_id_attempted\":\"%s\", \"status\":\"delete_failed\"}", reason, ib_id_str.c_str());
  } else {
    snprintf(char_buffer, sizeof(char_buffer), "{\"reason\":\"%s\", \"status\":\"delete_failed\"}", reason);
  }
  publishMQTTMessage("ibutton/delete_failure", char_buffer);
}


// --- Getters for state ---
bool isMQTTConnected() {
  return mqttClient.connected();
}

bool isPairingModeActive() {
  return pairing_mode_active;
}

const char* getCurrentPairingSessionId() {
  return current_pairing_session_id_str.c_str();
}

void clearPairingMode() {
  pairing_mode_active = false;
  current_pairing_session_id_str = "";
  pairing_timeout_start_ms = 0;
}


bool isWaitingFor2FA() {
  return waiting_for_2fa_response;
}

// This is a bit clunky, main loop will convert it back to bytes if needed for comparison.
// Or modify to store byte array. For now, string for simplicity in matching response.
const char* get2FA_iButtonId_Str() {
  return two_fa_ibutton_id_str.c_str();
}

void clear2FA_WaitingState() {
  Serial.println("2FA: Clearing waiting state.");  // DEBUG CLEAR
  waiting_for_2fa_response = false;
  two_fa_ibutton_id_str = "";   // Limpiar ID almacenado
  two_fa_timeout_start_ms = 0;  // <<-- RESETEAR EL TEMPORIZADOR
  // two_fa_granted is NOT cleared here, main loop reads it then resets it.
}

bool get2FA_GrantStatus() {
  return two_fa_granted;
}

void reset2FA_GrantStatus() {
  Serial.println("2FA: Resetting grant status flag.");  // DEBUG RESET
  two_fa_granted = false;
}

bool isDeleteIButtonModeActive() {
  return delete_ibutton_mode_active;
}

void clearDeleteIButtonMode() {
  Serial.println("MQTT: Clearing Delete iButton mode.");
  delete_ibutton_mode_active = false;
  delete_ibutton_timeout_start_ms = 0;
}
