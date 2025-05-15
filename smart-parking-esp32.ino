#include <Arduino.h>
#include <ESP32Servo.h>
#include "ibutton_manager.h"
#include "mqtt_manager.h"

// --- User Configuration ---
// iButton
#define IBUTTON_DATA_PIN 33         // GPIO pin for the OneWire data line
#define MAX_REGISTERED_IBUTTONS 10  // Maximum number of iButtons to store
#define TOTAL_PARKING_SPACES 3      // Total capacity
#define IBUTTON_COOLDOWN_MS 10000   // Seconds cooldown for same iButton

// Servo
#define SERVO_PIN 27             // GPIO pin for the Servo motor
#define SERVO_OPEN_ANGLE 90      // Angle for open gate position
#define SERVO_CLOSE_ANGLE 0      // Angle for closed gate position
#define GATE_OPEN_DELAY_MS 1000  // Time the gate stays open (milliseconds)

// Buzzer
#define BUZZER_PIN 26                // GPIO pin for the Buzzer
#define BEEP_DURATION_MS 300         // Duration for a single success beep
#define REJECT_BEEP_DURATION_MS 100  // Duration for each rejection beep
#define REJECT_PAUSE_MS 100          // Pause between rejection beeps
#define REJECT_BEEP_COUNT 3          // Number of rejection beeps

// --- WiFi Configuration ---
const char *WIFI_SSID = "jalarras";
const char *WIFI_PASSWORD = "saposapo777";

// --- MQTT Configuration ---
MQTTConfig mqtt_settings = {
  "broker.emqx.io",          // Broker host
  1883,                      // Broker port
  "juanliz-sparking-",       // Client ID prefix (ESP MAC part will be added)
  "juanliz-sparking-esp32/"  // Base topic prefix
};
const char *ESP32_DEVICE_ID = "ESP32_Parking_01";  // Unique ID for this device

// --- Global Objects ---
Servo gateServo;

byte current_ibutton_id[IBUTTON_ID_LEN];              // Buffer for the currently read iButton ID
uint32_t last_associated_id = INVALID_ASSOCIATED_ID;  // Store associated ID of authenticated iButton

uint32_t current_occupancy = 0;                // NEW: RAM variable for current count
byte last_scanned_id[IBUTTON_ID_LEN] = { 0 };  // NEW: Track last scanned ID for cooldown
unsigned long last_scan_timestamp = 0;         // NEW: Track last scan time for cooldown

// --- States for Serial Control ---
enum ControlState {
  IDLE,
  WAITING_FOR_IBUTTON_TO_REGISTER,
  WAITING_FOR_IBUTTON_TO_DELETE
};
ControlState currentState = IDLE;

// --- Helper Functions ---
void openGate() {
  Serial.println("Opening gate...");
  // Single beep for success
  digitalWrite(BUZZER_PIN, HIGH);
  delay(BEEP_DURATION_MS);
  digitalWrite(BUZZER_PIN, LOW);

  gateServo.write(SERVO_OPEN_ANGLE);
}

void closeGate() {
  Serial.println("Closing gate...");
  gateServo.write(SERVO_CLOSE_ANGLE);
}

void intermitentBeep() {
  // Intermittent beep for rejection
  for (int i = 0; i < REJECT_BEEP_COUNT; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(REJECT_BEEP_DURATION_MS);
    digitalWrite(BUZZER_PIN, LOW);
    // Don't pause after the last beep
    if (i < REJECT_BEEP_COUNT - 1) {
      delay(REJECT_PAUSE_MS);
    }
  }
}


void processEntry(IButtonRecord &record, int record_idx) {
  unsigned long entry_time = millis();  // For cooldown
  if (current_occupancy < TOTAL_PARKING_SPACES) {
    Serial.println("Space available. Opening gate for entry.");
    openGate();
    current_occupancy++;
    record.is_inside = true;
    if (updateIButtonRecord(record_idx, record) && writeOccupancyCount(current_occupancy)) {
      Serial.println("Entry successful. Record and count updated.");
      if (isMQTTConnected()) {  // NUEVO: Publicar estado actualizado
        publishStatus(true, current_occupancy, TOTAL_PARKING_SPACES);
      }
    } else {
      Serial.println("Error: Failed to update record/occupancy for entry. Reverting RAM.");
      current_occupancy--;       // Revert RAM
      record.is_inside = false;  // Revert RAM
                                 // Note: EEPROM might be inconsistent if one write failed.
    }
    delay(GATE_OPEN_DELAY_MS);
    closeGate();
    memcpy(last_scanned_id, record.ibutton_id, IBUTTON_ID_LEN);  // Use record's ID
    last_scan_timestamp = entry_time;                            // Use the time of entry attempt
  } else {
    Serial.println("Parking FULL. Entry denied.");
    intermitentBeep();
  }
}


void processExit(IButtonRecord &record, int record_idx) {
  unsigned long exit_time = millis();  // For cooldown
  Serial.println("Attempting EXIT. Opening gate.");
  openGate();

  if (current_occupancy > 0) {
    current_occupancy--;
  } else {
    Serial.println("Warning: Occupancy already 0, cannot decrement further for exit.");
  }
  record.is_inside = false;

  if (updateIButtonRecord(record_idx, record)) {
    if (writeOccupancyCount(current_occupancy)) {
      Serial.println("Exit successful. Record and count updated.");
      if (isMQTTConnected()) {  // NUEVO: Publicar estado actualizado
        publishStatus(true, current_occupancy, TOTAL_PARKING_SPACES);
      }
    } else {
      Serial.println("Error: Failed to update occupancy count after record update for exit. Record updated.");
      // RAM count was already decremented.
    }
  } else {
    Serial.println("Error: Failed to update iButton record for exit. Reverting RAM occupancy.");
    if (current_occupancy < TOTAL_PARKING_SPACES) current_occupancy++;  // Revert RAM count if possible
                                                                        // record.is_inside remains false in RAM, but EEPROM is not updated.
  }
  delay(GATE_OPEN_DELAY_MS);
  closeGate();
  memcpy(last_scanned_id, record.ibutton_id, IBUTTON_ID_LEN);  // Use record's ID
  last_scan_timestamp = exit_time;                             // Use the time of exit attempt
}

void handleSerialCommands() {
  if (Serial.available() > 0) {
    char command = Serial.read();
    while (Serial.available() > 0)
      Serial.read();  // Clear input buffer

    switch (command) {
      case 'r':  // Register new iButton
        Serial.println("\n--- Register Mode ---");
        Serial.println("Please present the iButton you wish to register...");
        currentState = WAITING_FOR_IBUTTON_TO_REGISTER;
        break;

      case 'd':  // Delete an iButton
        Serial.println("\n--- Delete Mode ---");
        Serial.println("Please present the iButton you wish to delete...");
        currentState = WAITING_FOR_IBUTTON_TO_DELETE;
        break;

      case 'l':                        // List registered iButtons
        printAllRegisteredIButtons();  // Call the function from the manager
        break;

      case 'c':  // Cancel current operation
        Serial.println("\nCurrent operation cancelled. Returning to Idle mode.");
        currentState = IDLE;
        break;

      default:
        Serial.println("\nUnknown command.");
        Serial.println("Available commands: 'r' (register), 'd' (delete), 'l' (list), 'c' (cancel).");
        break;
    }
    // Prompt for next action if idle
    if (currentState == IDLE) {
      Serial.print("\nSystem Idle. Present iButton or enter command (r,d,l,c): ");
    }
  }
}

// --- Setup ---
void setup() {
  // NUEVO: Iniciar Serial con timeout
  Serial.begin(115200);
  unsigned long serial_timeout_start = millis();
  while (!Serial && (millis() - serial_timeout_start < 2000)) { // Espera hasta 2 segundos
    // Puedes dejar este bucle vacío o parpadear un LED si tienes uno para indicar espera
    delay(10);
  }
  delay(1500);
  Serial.println("\n--- ESP32 Smart Parking System ---");

  // Initialize the iButton Manager, passing configuration
  setupIButtonManager(IBUTTON_DATA_PIN, MAX_REGISTERED_IBUTTONS);

  // Initialize Servo
  gateServo.attach(SERVO_PIN);
  closeGate();  // Ensure gate starts closed

  // Initialize Buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);  // Ensure buzzer is off initially

  // Initialize WiFi
  setupMQTTManager(mqtt_settings, WIFI_SSID, WIFI_PASSWORD);

  // Read initial occupancy count
  current_occupancy = readOccupancyCount();
  // Optional: Add validation against TOTAL_PARKING_SPACES here
  if (current_occupancy > TOTAL_PARKING_SPACES) {
    Serial.printf("Warning: Stored occupancy (%u) > total spaces (%d). Resetting to 0.\n",
                  current_occupancy,
                  TOTAL_PARKING_SPACES);
    current_occupancy = 0;
    writeOccupancyCount(current_occupancy);  // Save the reset value
  }

  Serial.printf("System ready. Total Spaces: %d, Current Occupancy: %u\n", TOTAL_PARKING_SPACES, current_occupancy);
  printAllRegisteredIButtons();
  Serial.print("\nPresent iButton or enter command (r,d,l,c): ");
}

// --- Main Loop ---
void loop() {
  // 1. Handle commands from Serial Monitor
  handleSerialCommands();
  // 2. Handle MQTT connection and messages
  loopMQTTManager();  // Procesa MQTT. Puede:
                      // - Llamar callback -> setear two_fa_granted (si respuesta llega)
                      // - Expirar timeout -> setear two_fa_granted=false y clear2FA_WaitingState() (si tiempo pasa)


  static bool mqtt_just_connected = false;
  static bool last_mqtt_connected_state = false;

  if (isMQTTConnected() && !last_mqtt_connected_state) {
    mqtt_just_connected = true;  // Marcamos que acabamos de conectar
  }
  last_mqtt_connected_state = isMQTTConnected();

  if (mqtt_just_connected) {
    Serial.println("MQTT just connected (or reconnected). Publishing status...");
    publishStatus(true, current_occupancy, TOTAL_PARKING_SPACES);
    mqtt_just_connected = false;  // Resetear el flag
  }

  // --- PROACTIVE CHECK for 2FA Result ---
  static bool was_waiting_before = false;  // Static variable to track previous waiting state

  if (isWaitingFor2FA()) {
    // Aún estamos esperando. ¿Llegó una respuesta CONCEDIDA?
    if (get2FA_GrantStatus()) {  // Si get2FA_GrantStatus() es true, el callback la puso así.
      Serial.println("PROACTIVE CHECK: 2FA Granted via callback. Proceeding with entry.");
      byte ibutton_id_for_2fa_entry[IBUTTON_ID_LEN];
      String stored_ib_str = get2FA_iButtonId_Str();
      bool conversion_ok = false;

      if (stored_ib_str.length() == IBUTTON_ID_LEN * 2) {
        for (int i = 0; i < IBUTTON_ID_LEN; i++) {
          char hex_pair[3] = { stored_ib_str.charAt(i * 2), stored_ib_str.charAt(i * 2 + 1), '\0' };
          ibutton_id_for_2fa_entry[i] = strtol(hex_pair, nullptr, 16);
        }
        conversion_ok = true;
      }

      if (conversion_ok) {
        IButtonRecord record_for_entry;
        int record_idx_for_entry;
        if (getIButtonRecord(ibutton_id_for_2fa_entry, record_for_entry, &record_idx_for_entry)) {
          if (!record_for_entry.is_inside) {
            Serial.println("Proactive 2FA Grant: Executing entry.");
            processEntry(record_for_entry, record_idx_for_entry);  // Use helper
          } else {
            Serial.println("Proactive 2FA Grant: iButton already inside?");
          }
        } else {
          Serial.println("Proactive 2FA Grant: Could not find record for granted ID!");
        }
      } else {
        Serial.println("Proactive 2FA Grant: Error converting stored ID.");
      }

      clear2FA_WaitingState();
      reset2FA_GrantStatus();
      currentState = IDLE;
    }
    // If still waiting and not granted, do nothing here, let timeout or next scan handle.
    was_waiting_before = true;  // Update static state: we are currently waiting
  } else {
    // This could be due to:
    // 1. Timeout in loopMQTTManager() (set isWaitingFor2FA=false, two_fa_granted=false).
    // 2. Granted logic above executed (set isWaitingFor2FA=false, two_fa_granted=false).
    // 3. 2FA was never initiated.

    if (was_waiting_before) {  // Transition: Was waiting in a previous iteration, now not.
                               // And we didn't enter the get2FA_GrantStatus() block above.
                               // So, this means the 2FA process ended due to DENIAL or TIMEOUT.
      Serial.println("PROACTIVE CHECK: 2FA process finished (DENIED or TIMEOUT). Entry aborted.");
      // intermitentBeep();
      reset2FA_GrantStatus();  // Ensure grant flag is false (timeout already does this)
      currentState = IDLE;
    }
    was_waiting_before = false;  // Update static state: we are not currently waiting
  }
  // --- End PROACTIVE CHECK ---

  // --- Handle MQTT-driven pairing first if active ---
  if (isPairingModeActive()) {
    if (readIButton(current_ibutton_id)) {  // iButton presented during pairing
      Serial.print("\niButton detected during MQTT Pairing Mode for session: ");
      Serial.println(getCurrentPairingSessionId());
      printIButtonID(current_ibutton_id);
      Serial.println();

      if (registerIButton(current_ibutton_id)) {  // registerIButton now also returns the associated_id
        uint32_t new_assoc_id;
        // Need to re-fetch the record to get the new associated ID
        IButtonRecord temp_rec;
        if (getIButtonRecord(current_ibutton_id, temp_rec)) {
          new_assoc_id = temp_rec.associated_id;
          publishPairingSuccess(getCurrentPairingSessionId(), current_ibutton_id, new_assoc_id);
          Serial.println("iButton registered via MQTT successfully.");
          printAllRegisteredIButtons();
        } else {
          publishPairingFailure(getCurrentPairingSessionId(), "failed_to_get_assoc_id_after_reg");
          Serial.println("Error: Registered but could not retrieve new associated ID.");
        }
      } else {
        // registerIButton prints its own errors ("already exists" or "no space")
        // Determine a more specific reason if possible for the MQTT message
        // For now, a generic "registration_failed"
        publishPairingFailure(getCurrentPairingSessionId(), "El botón ya existe");
        Serial.println("Failed to register iButton via MQTT.");
      }
      clearPairingMode();   // Important: clear pairing mode in MQTT manager
      currentState = IDLE;  // Ensure local state machine is also IDLE

      delay(1500);
    }
    // No iButton yet, or pairing timed out (handled in mqtt_manager.loopMQTTManager)
    delay(50);  // Small delay when in pairing mode waiting for iButton
    return;     // Don't process further if in MQTT pairing mode
  }
  // --- END MQTT Pairing Logic ---

  if (readIButton(current_ibutton_id)) {
    unsigned long now = millis();
    bool cooldown_active = false;

    // Publish every scan event (before cooldown or other logic)
    // We need to know if it's registered to send complete info
    IButtonRecord temp_scan_record;
    bool scanned_is_registered = getIButtonRecord(current_ibutton_id, temp_scan_record);
    publishIButtonScanned(current_ibutton_id, scanned_is_registered, scanned_is_registered ? temp_scan_record.associated_id : 0);

    // --- Cooldown Check ---
    // If the same button is scanned within the cooldown period
    // after a successful entrance or exit, ignore the scan.
    // This prevents double scans from being processed for occupancy counter
    if (memcmp(current_ibutton_id, last_scanned_id, IBUTTON_ID_LEN) == 0) {  // Same iButton?
      if (now - last_scan_timestamp < IBUTTON_COOLDOWN_MS) {
        cooldown_active = true;
        Serial.print("\nCooldown active for iButton: ");
        printIButtonID(current_ibutton_id);
        Serial.println(". Scan ignored.");
      }
    }

    if (!cooldown_active) {
      Serial.print("\niButton detected: ");
      printIButtonID(current_ibutton_id);
      Serial.println();

      switch (currentState) {
        case WAITING_FOR_IBUTTON_TO_REGISTER:
          if (registerIButton(current_ibutton_id)) {
            Serial.println("iButton registered successfully.");
            printAllRegisteredIButtons();  // Show updated list
          } else {
            Serial.println("Failed to register iButton (maybe already exists, EEPROM full, or ID generation issue?).");
          }
          currentState = IDLE;  // Return to idle state
          Serial.print("\nSystem Idle. Present iButton or enter command (r,d,l,c): ");
          break;

        case WAITING_FOR_IBUTTON_TO_DELETE:
          if (deleteIButton(current_ibutton_id)) {
            Serial.println("iButton deleted successfully.");
            current_occupancy = readOccupancyCount();
            printAllRegisteredIButtons();
          } else {
            Serial.println("Failed to delete iButton (was not found).");
          }
          currentState = IDLE;
          Serial.printf(
            "\nSystem Idle. Occupancy: %u/%d. Present iButton or enter command (r,d,l,c): ",
            current_occupancy,
            TOTAL_PARKING_SPACES);
          break;

        case IDLE:  // Normal operation: Authenticate & Handle Entry/Exit
        default:
          IButtonRecord current_record;
          int record_idx;

          if (getIButtonRecord(current_ibutton_id, current_record, &record_idx)) {
            last_associated_id = current_record.associated_id;

            Serial.print("iButton AUTHENTICATED. AssocID: ");
            Serial.print(last_associated_id);
            Serial.printf(". Currently Inside: %s\n", current_record.is_inside ? "YES" : "NO");

            bool is_2fa_globally_required = true;  // TODO: Make this configurable

            if (!current_record.is_inside) {   // Attempting ENTRY
              if (is_2fa_globally_required) {  // 2FA is required for entry
                if (!isWaitingFor2FA()) {      // And no 2FA request is pending for ANY iButton
                  Serial.println("Attempting ENTRY, 2FA required. Sending request...");
                  publish2FARequest(current_ibutton_id, current_record.associated_id, ESP32_DEVICE_ID);
                  // publish2FARequest sets isWaitingFor2FA()=true and starts timer
                  Serial.println("Awaiting 2FA confirmation. Gate remains closed. Scan again or wait for proactive check.");
                  // Do not proceed with action here, wait for proactive check or next scan
                } else {  // Attempting entry, 2FA required, BUT another 2FA is already pending
                  Serial.println("Attempting ENTRY, 2FA required, but another 2FA request is already pending. Please wait.");
                  intermitentBeep();  // Indicate busy or waiting for other 2FA
                }
              } else {  // 2FA is NOT required for entry
                Serial.println("Attempting DIRECT ENTRY (2FA not globally required).");
                processEntry(current_record, record_idx);  // Call helper function for direct entry
              }
            } else {  // Attempting EXIT (assuming scan means exit if inside)
              Serial.println("Attempting EXIT.");
              processExit(current_record, record_idx);  // Call helper function for exit
            }
            // No 'proceed_with_action' flag needed here anymore as logic is handled by states/helpers

          } else {  // iButton not registered
            Serial.println("iButton NOT REGISTERED.");
            last_associated_id = INVALID_ASSOCIATED_ID;
            intermitentBeep();
          }
          Serial.printf(
            "\nSystem Idle. Occupancy: %u/%d. Present iButton or enter command (r,d,l,c): ",
            current_occupancy,
            TOTAL_PARKING_SPACES);
          break;
      }

      delay(1500);  // Keep this delay after any non-cooldown iButton processing

    }  // End if (!cooldown_active)

  }  // End if (readIButton)

  // Small delay in the loop to yield CPU time when idle
  delay(50);
}