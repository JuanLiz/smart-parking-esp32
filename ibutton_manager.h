#ifndef IBUTTON_MANAGER_H
#define IBUTTON_MANAGER_H

#include <Arduino.h>
#include <OneWire.h>
#include <EEPROM.h>

// --- Constantes ---
#define MAX_IBUTTONS 20        // Máximo número de iButtons a almacenar
#define IBUTTON_ID_LEN 8       // Longitud del ID del iButton (bytes)
#define IBUTTON_PIN 33         // Pin donde está conectado el lector OneWire
#define INVALID_ASSOCIATED_ID 0 // Un valor para indicar ID asociado no encontrado o inválido

// --- Estructura de Datos ---
struct IButtonRecord {
  bool is_valid;
  uint32_t associated_id;
  byte ibutton_id[IBUTTON_ID_LEN];
};

// Tamaño total necesario en EEPROM
const int EEPROM_SIZE = sizeof(IButtonRecord) * MAX_IBUTTONS;

// --- Declaraciones de Funciones Públicas ---

/**
 * @brief Inicializa el gestor de iButtons (EEPROM y OneWire).
 * Debe llamarse en el setup() principal.
 */
void setupIButtonManager();

/**
 * @brief Lee un iButton presente en el lector.
 * @param id_buffer Buffer donde se almacenará el ID leído (debe tener tamaño IBUTTON_ID_LEN).
 * @return true si se leyó un iButton DS1990A válido, false en caso contrario.
 */
bool readIButton(byte* id_buffer);

/**
 * @brief Registra un nuevo iButton en la EEPROM.
 * Busca un slot libre y guarda la información. No permite registrar duplicados.
 * @param associated_id El ID personalizado a asociar con este iButton.
 * @param ibutton_id El ID físico del iButton a registrar (8 bytes).
 * @return true si el registro fue exitoso, false si no hay espacio o el iButton ya existe.
 */
bool registerIButton(uint32_t associated_id, const byte* ibutton_id);

/**
 * @brief Verifica si un iButton está registrado y recupera su ID asociado.
 * @param ibutton_id El ID físico del iButton a verificar (8 bytes).
 * @param[out] associated_id_out Referencia donde se guardará el ID asociado si se encuentra.
 * @return true si el iButton está registrado, false en caso contrario.
 */
bool isIButtonRegistered(const byte* ibutton_id, uint32_t &associated_id_out);

/**
 * @brief Elimina el registro de un iButton de la EEPROM.
 * Busca el iButton por su ID físico y marca su slot como inválido.
 * @param ibutton_id El ID físico del iButton a eliminar (8 bytes).
 * @return true si la eliminación fue exitosa, false si el iButton no se encontró.
 */
bool deleteIButton(const byte* ibutton_id);

/**
 * @brief Imprime un ID de iButton en formato hexadecimal al Serial.
 * @param id El buffer con el ID del iButton (8 bytes).
 */
void printIButtonID(const byte* id);

/**
 * @brief Imprime todos los iButtons registrados actualmente en la EEPROM al Serial.
 * Útil para depuración.
 */
void printAllRegisteredIButtons();

#endif // IBUTTON_MANAGER_H