#ifndef IBUTTON_MANAGER_H
#define IBUTTON_MANAGER_H

#include <Arduino.h>
#include <OneWire.h>
#include <EEPROM.h>

// --- Constants ---
#define IBUTTON_ID_LEN 8       // Length of the iButton ID in bytes
#define INVALID_ASSOCIATED_ID 0 // Value indicating an invalid or not-found associated ID
const int EEPROM_CONFIG_OFFSET = 16; // Bytes reserved at the beginning of EEPROM for configuration
const uint32_t EEPROM_INIT_SIGNATURE = 0xCAFEFE0D; // 
const int EEPROM_SIGNATURE_ADDR = 0;             // Store signature at address 0
const int EEPROM_OCCUPANCY_COUNT_ADDR = 4;  // Use next 4 bytes after signature for the counter


// --- Data Structure ---
// Structure to store one record in EEPROM
struct IButtonRecord {
  bool is_valid;
  uint32_t associated_id;
  byte ibutton_id[IBUTTON_ID_LEN]; // The physical ID of the iButton
  bool is_inside; // Flag to track if the iButton holder is inside
};


// --- Public Function Declarations ---

/**
 * @brief Initializes the iButton manager.
 * Configures OneWire, initializes EEPROM with offset, and stores parameters.
 * Must be called in the main setup().
 * @param pin The GPIO pin connected to the OneWire data line.
 * @param max_records The maximum number of iButton records to manage.
 */
void setupIButtonManager(uint8_t pin, int max_records);

/**
 * @brief Reads an iButton present on the reader.
 * @param id_buffer Buffer where the read ID will be stored (must be IBUTTON_ID_LEN bytes).
 * @return true if a valid DS1990A iButton was read successfully, false otherwise.
 */
bool readIButton(byte* id_buffer);

/**
 * @brief Gets the full record for a given iButton ID.
 * Searches EEPROM after the offset.
 * @param ibutton_id The physical ID of the iButton to search for (IBUTTON_ID_LEN bytes).
 * @param[out] record_out Reference to an IButtonRecord struct where the found record will be copied.
 * @param[out] record_index Optional pointer to store the index (slot) where the record was found. Can be nullptr.
 * @return true if the iButton is registered and found, false otherwise.
 */
bool getIButtonRecord(const byte* ibutton_id, IButtonRecord &record_out, int* record_index = nullptr);

/**
 * @brief Registers a new iButton in EEPROM after the offset.
 * Searches for a free slot, automatically generates the next available associated ID,
 * and stores the information. Prevents duplicate registrations.
 * @param ibutton_id The physical ID of the iButton to register (IBUTTON_ID_LEN bytes).
 * @return true if registration was successful, false if no space is available, the iButton already exists, or an ID could not be generated.
 */
bool registerIButton(const byte* ibutton_id);

/**
 * @brief Updates an existing iButton record in EEPROM.
 * Used internally to update the is_inside flag.
 * @param index The index (slot) of the record to update.
 * @param record The IButtonRecord data to write.
 * @return true if the update and commit were successful, false otherwise.
 */
bool updateIButtonRecord(int index, const IButtonRecord& record);

/**
 * @brief Deletes the registration of an iButton from EEPROM.
 * Searches for the iButton by its physical ID (after the offset) and marks its slot as invalid.
 * @param ibutton_id The physical ID of the iButton to delete (IBUTTON_ID_LEN bytes).
 * @return true if deletion was successful, false if the iButton was not found.
 */
bool deleteIButton(const byte* ibutton_id);

/**
 * @brief Prints an iButton ID in hexadecimal format to the Serial monitor.
 * @param id The buffer containing the iButton ID (IBUTTON_ID_LEN bytes).
 */
void printIButtonID(const byte* id);

/**
 * @brief Prints all currently registered iButtons stored in EEPROM (after the offset) to the Serial monitor.
 * Useful for debugging.
 */
void printAllRegisteredIButtons();

/**
 * @brief Reads the current occupancy count from EEPROM.
 * @return The stored occupancy count, or 0 if read fails or looks invalid (e.g., negative interpreted value).
 */
uint32_t readOccupancyCount();

/**
 * @brief Writes the current occupancy count to EEPROM.
 * @param count The occupancy count to write.
 * @return true if write and commit were successful, false otherwise.
 */
bool writeOccupancyCount(uint32_t count);


#endif // IBUTTON_MANAGER_H