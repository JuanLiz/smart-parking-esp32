#include <Arduino.h>
#include <ESP32Servo.h>
#include "ibutton_manager.h"  // Include our manager

// --- User Configuration ---
// iButton
#define IBUTTON_DATA_PIN 33         // GPIO pin for the OneWire data line
#define MAX_REGISTERED_IBUTTONS 10  // Maximum number of iButtons to store
#define TOTAL_PARKING_SPACES 4      // Total capacity
#define IBUTTON_COOLDOWN_MS 10000   // Seconds cooldown for same iButton

// Servo
#define SERVO_PIN 27             // GPIO pin for the Servo motor
#define SERVO_OPEN_ANGLE 90      // Angle for open gate position
#define SERVO_CLOSE_ANGLE 0      // Angle for closed gate position
#define GATE_OPEN_DELAY_MS 1000  // Time the gate stays open (milliseconds)


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
  gateServo.write(SERVO_OPEN_ANGLE);
}

void closeGate() {
  Serial.println("Closing gate...");
  gateServo.write(SERVO_CLOSE_ANGLE);
}

void handleSerialCommands() {
  if (Serial.available() > 0) {
    char command = Serial.read();
    while (Serial.available() > 0) Serial.read();  // Clear input buffer

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
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n--- ESP32 Smart Parking System ---");

  // Initialize the iButton Manager, passing configuration
  setupIButtonManager(IBUTTON_DATA_PIN, MAX_REGISTERED_IBUTTONS);

  // Initialize Servo
  gateServo.attach(SERVO_PIN);
  closeGate();  // Ensure gate starts closed

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

  if (readIButton(current_ibutton_id)) {
    unsigned long now = millis();
    bool cooldown_active = false;

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


      // Process based on state (Register, Delete, Idle/Authenticate)
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
            // Refresh RAM occupancy count after potential change during delete
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

          // Check if registered and get record
          if (getIButtonRecord(current_ibutton_id, current_record, &record_idx)) {
            // Store associated ID
            last_associated_id = current_record.associated_id;

            Serial.print("iButton AUTHENTICATED. AssocID: ");
            Serial.print(last_associated_id);
            Serial.printf(". Currently Inside: %s\n", current_record.is_inside ? "YES" : "NO");

            if (!current_record.is_inside) {  // --- Attempting ENTRY ---
              Serial.println("Attempting ENTRY.");
              if (current_occupancy < TOTAL_PARKING_SPACES) {
                Serial.println("Space available. Opening gate.");
                // --- Placeholder for MQTT Double Factor ---
                // TODO: Implement MQTT check here BEFORE proceeding
                // -----------------------------------------
                openGate();

                current_occupancy++;
                current_record.is_inside = true;

                // Update EEPROM (Record first, then count - count commit saves both ideally)
                if (updateIButtonRecord(record_idx, current_record)) {
                  if (writeOccupancyCount(current_occupancy)) {
                    Serial.println("Entry successful. Record and count updated.");
                  } else {
                    Serial.println("Error: Failed to update occupancy count after record update.");
                    // State might be inconsistent here! Maybe revert is_inside? Complex.
                  }
                } else {
                  Serial.println("Error: Failed to update iButton record for entry.");
                  current_occupancy--;  // Revert RAM count
                }

                delay(GATE_OPEN_DELAY_MS);
                closeGate();

                // Update last scan info
                memcpy(last_scanned_id, current_ibutton_id, IBUTTON_ID_LEN);
                last_scan_timestamp = now;

              } else {
                Serial.println("Parking FULL. Entry denied.");
                // Deny entry, maybe flash LED etc.
              }
            } else {  // --- Attempting EXIT ---
              Serial.println("Attempting EXIT (assuming scan at entrance means exit).");
              // --- Placeholder for MQTT Double Factor (less common for exit, but possible) ---
              // TODO: Check if any exit validation is needed
              // -----------------------------------------
              openGate();  // Open gate for exit

              if (current_occupancy > 0) {
                current_occupancy--;
              } else {
                Serial.println("Warning: Occupancy already 0, cannot decrement further.");
              }
              current_record.is_inside = false;

              // Update EEPROM
              if (updateIButtonRecord(record_idx, current_record)) {
                if (writeOccupancyCount(current_occupancy)) {
                  Serial.println("Exit successful. Record and count updated.");
                } else {
                  Serial.println("Error: Failed to update occupancy count after record update for exit.");
                }
              } else {
                Serial.println("Error: Failed to update iButton record for exit.");
                // Revert RAM count?
                if (current_occupancy < TOTAL_PARKING_SPACES) current_occupancy++;  // Revert if possible
              }

              delay(GATE_OPEN_DELAY_MS);  // Keep gate open for exit duration
              closeGate();
              
              // Update last scan info
              memcpy(last_scanned_id, current_ibutton_id, IBUTTON_ID_LEN);
              last_scan_timestamp = now;
            }

          } else {  // iButton not registered
            Serial.println("iButton NOT REGISTERED.");
            last_associated_id = INVALID_ASSOCIATED_ID;
            // Optional: Add visual/audio feedback for rejection
          }
          Serial.printf(
            "\nSystem Idle. Occupancy: %u/%d. Present iButton or enter command (r,d,l,c): ",
            current_occupancy,
            TOTAL_PARKING_SPACES);
          break;  // End of IDLE case
      }           // End switch(currentState)

      delay(1500);

    }  // End if (!cooldown_active)

  }  // End if (readIButton)

  // Small delay in the loop to yield CPU time when idle
  delay(50);
}