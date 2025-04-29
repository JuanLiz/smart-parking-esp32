#include "ibutton_manager.h"
#include <limits.h>  // Required for UINT32_MAX


// --- Module Variables ---
OneWire* ds = nullptr;
int max_managed_ibuttons = 0;    // Maximum number of records
int calculated_eeprom_size = 0;  // Total EEPROM size needed


// --- Helpers ---
// Helper function to calculate EEPROM address for a given index
int getRecordAddress(int index) {
  return EEPROM_CONFIG_OFFSET + (index * sizeof(IButtonRecord));
}

// Helper function to generate the next associated ID
uint32_t generateNextAssociatedID() {
  uint32_t max_id = INVALID_ASSOCIATED_ID;  // Start assuming 0 is the max (or no valid IDs exist yet)
  IButtonRecord record;

  if (max_managed_ibuttons <= 0) {
    Serial.println("Error: Cannot generate ID, manager not initialized.");
    return INVALID_ASSOCIATED_ID;  // Return invalid ID
  }

  // Scan all records to find the highest current associated_id
  for (int i = 0; i < max_managed_ibuttons; ++i) {
    EEPROM.get(getRecordAddress(i), record);
    if (record.is_valid && record.associated_id > max_id) {
      max_id = record.associated_id;
    }
  }

  // Check for potential overflow (highly unlikely, but good practice)
  if (max_id == UINT32_MAX) {
    Serial.println("Error: Cannot generate new Associated ID, maximum value reached.");
    return INVALID_ASSOCIATED_ID;  // Return invalid ID to indicate failure
  }

  // The next ID is the highest found + 1
  uint32_t next_id = max_id + 1;

  // Ensure the generated ID is not the reserved invalid ID (shouldn't happen if max_id starts at 0)
  if (next_id == INVALID_ASSOCIATED_ID) {
    next_id++;  // Skip 0 if it somehow resulted from max_id + 1
  }

  Serial.printf("Generated next Associated ID: %u\n", next_id);
  return next_id;
}


// --- Function Implementations ---

void setupIButtonManager(uint8_t pin, int max_records) {
  // Store configuration parameters
  max_managed_ibuttons = max_records;

  // Initialize OneWire object
  if (ds == nullptr) {
    ds = new OneWire(pin);  // Allocate OneWire object dynamically
    Serial.printf("OneWire initialized on pin %d.\n", pin);
  } else {
    // This case should ideally not happen if setup is called only once,
    // but it's good practice to avoid re-allocating if ds already exists.
    // Note: OneWire library doesn't have a simple 'change pin' method.
    // Re-initialization would require deleting the old 'ds' and creating a new one.
    Serial.println("Warning: OneWire object already exists. Re-using existing instance.");
  }


  // Calculate required EEPROM size including the offset
  calculated_eeprom_size = EEPROM_CONFIG_OFFSET + (sizeof(IButtonRecord) * max_managed_ibuttons);

  // Initialize EEPROM
  if (!EEPROM.begin(calculated_eeprom_size)) {
    Serial.println("FATAL: Failed to initialize EEPROM!");
    // Consider halting or restarting
    delay(5000);
    ESP.restart();
  }
  Serial.printf("EEPROM initialized. Total Size: %d bytes. Config Offset: %d bytes. Record Capacity: %d\n",
                calculated_eeprom_size, EEPROM_CONFIG_OFFSET, max_managed_ibuttons);

  // --- Check if EEPROM needs formatting ---
  uint32_t current_signature = 0;
  EEPROM.get(EEPROM_SIGNATURE_ADDR, current_signature);  // Read signature from address 0

  if (current_signature != EEPROM_INIT_SIGNATURE) {
    Serial.println("EEPROM signature not found or invalid. Formatting iButton storage area...");

    IButtonRecord emptyRecord;
    emptyRecord.is_valid = false;
    emptyRecord.associated_id = INVALID_ASSOCIATED_ID;  // Or 0
    emptyRecord.is_inside = false;                      // Initialize is_inside flag
    memset(emptyRecord.ibutton_id, 0, IBUTTON_ID_LEN);  // Clear the ID field too

    // Write empty records to all slots
    for (int i = 0; i < max_managed_ibuttons; ++i) {
      EEPROM.put(getRecordAddress(i), emptyRecord);
      if (i % 10 == 0) {  // Print progress dot occasionally
        Serial.print(".");
      }
    }
    Serial.println();  // Newline after dots

    // Write the signature to mark initialization as complete
    EEPROM.put(EEPROM_SIGNATURE_ADDR, EEPROM_INIT_SIGNATURE);
    EEPROM.put(EEPROM_OCCUPANCY_COUNT_ADDR, (uint32_t)0);  // Initialize count

    // Commit all changes (formatting + signature)
    if (EEPROM.commit()) {
      Serial.println("EEPROM formatting, signature, and initial count write successful.");
    } else {
      Serial.println("Error: EEPROM commit failed after formatting!");
      // Handle this critical error - maybe halt?
    }
  } else {
    Serial.println("Valid EEPROM signature found. Skipping format.");
    // Optional: Verify/Recalculate occupancy count here for robustness
    uint32_t stored_count = readOccupancyCount();
    Serial.printf("Stored occupancy count found: %u\n", stored_count);
  }
}


bool readIButton(byte* id_buffer) {
  if (ds == nullptr) {
    Serial.println("Error: OneWire not initialized. Call setupIButtonManager first.");
    return false;
  }

  if (!ds->search(id_buffer)) {  // Use -> for pointer access
    ds->reset_search();
    delay(50);  // Small delay to prevent rapid ghost reads
    return false;
  }

  // Verify CRC
  if (OneWire::crc8(id_buffer, 7) != id_buffer[7]) {
    Serial.println("CRC Error reading iButton.");
    return false;
  }

  // Verify if it's a DS1990A (Family Code 0x01)
  if (id_buffer[0] != 0x01) {
    Serial.print("OneWire device is not DS1990A. Family Code: 0x");
    Serial.println(id_buffer[0], HEX);
    return false;
  }

  // All checks passed
  return true;
}


bool getIButtonRecord(const byte* ibutton_id, IButtonRecord &record_out, int* record_index) {
   if (max_managed_ibuttons <= 0) return false; // Not initialized

   for (int i = 0; i < max_managed_ibuttons; ++i) {
       IButtonRecord temp_record; // Use a temporary to avoid modifying record_out if not found
       EEPROM.get(getRecordAddress(i), temp_record);
       // Check if the slot is valid and if the IDs match
       if (temp_record.is_valid && memcmp(temp_record.ibutton_id, ibutton_id, IBUTTON_ID_LEN) == 0) {
           record_out = temp_record; // Copy the found record
           if (record_index != nullptr) {
               *record_index = i; // Store the index if requested
           }
           return true; // Found and valid
       }
   }
   // Optional: Clear record_out if not found? Depends on usage.
   // record_out.is_valid = false;
   if (record_index != nullptr) {
        *record_index = -1; // Indicate not found
   }
   return false; // Not found
}


bool registerIButton(const byte* ibutton_id) {
  if (max_managed_ibuttons <= 0) {
    Serial.println("Error: iButton manager not properly initialized (max_records=0).");
    return false;
  }

  int first_free_slot = -1;
  IButtonRecord record;

  // 1. Check for duplicates and find the first free slot
  for (int i = 0; i < max_managed_ibuttons; ++i) {
    EEPROM.get(getRecordAddress(i), record);  // Read from calculated address

    if (record.is_valid) {
      // Check if the ID is already registered
      if (memcmp(record.ibutton_id, ibutton_id, IBUTTON_ID_LEN) == 0) {
        Serial.println("Error: iButton is already registered.");
        return false;  // Already exists
      }
    } else if (first_free_slot == -1) {
      // Found the first free slot
      first_free_slot = i;
    }
  }

  // 2. Check if EEPROM is full
  if (first_free_slot == -1) {
    Serial.println("Error: No free space in EEPROM to register.");
    return false;
  }

  // 3. Generate the next associated ID
  uint32_t new_associated_id = generateNextAssociatedID();
  if (new_associated_id == INVALID_ASSOCIATED_ID) {
    Serial.println("Error: Failed to generate a valid Associated ID.");
    return false;  // Failed to generate ID
  }

  // 4. Create the new record and write it to the free slot
  record.is_valid = true;
  record.associated_id = new_associated_id;  // Use the generated ID
  record.is_inside = false; // Initialize as 'outside'
  memcpy(record.ibutton_id, ibutton_id, IBUTTON_ID_LEN);

  EEPROM.put(getRecordAddress(first_free_slot), record);  // Write to calculated address

  // 5. Commit changes to EEPROM
  if (EEPROM.commit()) {
    Serial.print("iButton registered in slot ");
    Serial.print(first_free_slot);
    Serial.print(" (Address: ");
    Serial.print(getRecordAddress(first_free_slot));
    Serial.print(") with automatically generated Associated ID: ");  // Updated message
    Serial.println(new_associated_id);                               // Use the generated ID
    return true;
  } else {
    Serial.println("Error: EEPROM commit failed during registration.");
    return false;
  }
}


bool updateIButtonRecord(int index, const IButtonRecord& record) {
    if (index < 0 || index >= max_managed_ibuttons) {
        Serial.println("Error: Invalid index for updateIButtonRecord.");
        return false;
    }
    EEPROM.put(getRecordAddress(index), record);
    // Note: We might commit later, after updating the occupancy count too,
    // but committing here ensures the record is saved immediately.
    // Consider the trade-off. Let's commit here for simplicity for now.
    if (!EEPROM.commit()) {
         Serial.println("Error: EEPROM commit failed during record update.");
         return false;
    }
    return true;
}


bool deleteIButton(const byte* ibutton_id) {
    IButtonRecord record;
    int slot_to_delete = -1;
    bool was_inside = false; // Track if we need to decrement occupancy

    // 1. Find the iButton and its current state
    if (!getIButtonRecord(ibutton_id, record, &slot_to_delete)) {
         Serial.println("Error: iButton to delete was not found.");
         return false; // Not found using the helper function
    }

    // Check if it was inside before deleting
    was_inside = record.is_inside;

    // 2. Mark as invalid
    record.is_valid = false;
    record.is_inside = false; // Ensure it's marked as outside
    // Optional: Clear other fields
    // record.associated_id = 0;
    // memset(record.ibutton_id, 0, IBUTTON_ID_LEN);

    EEPROM.put(getRecordAddress(slot_to_delete), record);

    // 3. Adjust occupancy count if necessary
    bool count_updated = false;
    if (was_inside) {
        Serial.println("Deleted iButton was marked as 'inside'. Decrementing occupancy.");
        uint32_t current_count = readOccupancyCount();
        if (current_count > 0) {
            current_count--;
            if (writeOccupancyCount(current_count)) { // writeOccupancyCount handles its own commit
               count_updated = true; // Count was successfully updated
            } else {
               Serial.println("Error: Failed to update occupancy count during deletion.");
               // Proceed with deletion commit anyway? Or return error? Let's proceed.
            }
        } else {
            Serial.println("Warning: Occupancy count already 0, cannot decrement further during deletion.");
        }
    }

    // 4. Commit the record deletion (if count wasn't updated, or if count update failed)
    // If writeOccupancyCount succeeded, it already committed.
    if (!count_updated) {
       if (!EEPROM.commit()) {
          Serial.println("Error: EEPROM commit failed during deletion.");
          return false; // Commit failed
       }
    }

    Serial.print("iButton deleted from slot ");
    Serial.print(slot_to_delete);
    Serial.print(" (Address: ");
    Serial.print(getRecordAddress(slot_to_delete));
    Serial.println(")");
    return true; // Deletion successful (even if count update had issues)
}


void printIButtonID(const byte* id) {
  for (int i = 0; i < IBUTTON_ID_LEN; i++) {
    if (id[i] < 16) Serial.print("0");  // Add leading zero for values < 0x10
    Serial.print(id[i], HEX);
    if (i < IBUTTON_ID_LEN - 1) Serial.print(" ");  // Add space between bytes
  }
  // Serial.println(); // Remove newline here if called inline elsewhere
}


void printAllRegisteredIButtons() {
  Serial.println("\n--- Registered iButtons in EEPROM ---");
  if (max_managed_ibuttons <= 0) {
    Serial.println("iButton manager not initialized.");
    return;
  }
  IButtonRecord record;
  bool any_registered = false;
  for (int i = 0; i < max_managed_ibuttons; ++i) {
    EEPROM.get(getRecordAddress(i), record);  // Read from calculated address
    if (record.is_valid) {
      any_registered = true;
      Serial.printf("Slot %d (Addr %d): Valid=YES, AssocID=%u, iButtonID=",
                    i, getRecordAddress(i), record.associated_id);
      printIButtonID(record.ibutton_id);  // Re-use the print ID function
      Serial.println();                   // Add newline after printing the ID
    }
    // Optional: Print empty slots for more detail
    // else {
    //   Serial.printf("Slot %d (Addr %d): Valid=NO\n", i, getRecordAddress(i));
    // }
  }
  if (!any_registered) {
    Serial.println("No iButtons are currently registered.");
  }
  Serial.println("-------------------------------------");
}


uint32_t readOccupancyCount() {
    uint32_t count = 0;
    EEPROM.get(EEPROM_OCCUPANCY_COUNT_ADDR, count);
    // Basic validation: EEPROM returns FFFFFFFF if never written or corrupted in a specific way.
    // While technically a valid uint32_t, it's unlikely as an occupancy count.
    if (count == UINT32_MAX) {
        Serial.println("Warning: Read occupancy count looks invalid (0xFFFFFFFF), returning 0.");
        return 0;
    }
    return count;
}


bool writeOccupancyCount(uint32_t count) {
    EEPROM.put(EEPROM_OCCUPANCY_COUNT_ADDR, count);
    if (!EEPROM.commit()) {
        Serial.println("Error: EEPROM commit failed while writing occupancy count.");
        return false;
    }
    Serial.printf("Occupancy count updated to: %u\n", count);
    return true;
}
