#ifndef LCD_MANAGER_H
#define LCD_MANAGER_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h> // Biblioteca para LCD I2C

// --- Constantes (ajusta según tu LCD) ---
#define LCD_ADDRESS 0x27 // Dirección I2C común para LCDs 16x2 (puede ser 0x3F u otra, ¡verifica!)
#define LCD_COLS 16      // Columnas de tu LCD
#define LCD_ROWS 2       // Filas de tu LCD

// --- Declaraciones de Funciones Públicas ---

/**
 * @brief Inicializa la pantalla LCD I2C.
 * Debe llamarse en el setup() principal.
 * @param sda_pin El pin GPIO para SDA (por defecto Wire usa los pines estándar del ESP32).
 * @param scl_pin El pin GPIO para SCL (por defecto Wire usa los pines estándar del ESP32).
 * @return true si la LCD fue detectada e inicializada, false en caso contrario.
 */
bool setupLCDManager(int sda_pin = 21, int scl_pin = 22); // Valores por defecto

/**
 * @brief Muestra un mensaje en la LCD. Limpia la pantalla antes.
 * @param line1 Mensaje para la primera línea (máx LCD_COLS caracteres).
 * @param line2 Mensaje para la segunda línea (máx LCD_COLS caracteres, opcional).
 * @param clear_display Si es true (por defecto), limpia la pantalla antes de escribir.
 */
void lcdPrint(const String& line1, const String& line2 = "", bool clear_display = true);

/**
 * @brief Muestra un mensaje en una posición específica sin limpiar la pantalla.
 * @param col Columna donde empezar a escribir (0-indexed).
 * @param row Fila donde empezar a escribir (0-indexed).
 * @param message Mensaje a mostrar.
 */
void lcdPrintAt(uint8_t col, uint8_t row, const String& message);

/**
 * @brief Limpia la pantalla LCD.
 */
void lcdClear();

/**
 * @brief Enciende la luz de fondo de la LCD.
 */
void lcdBacklightOn();

/**
 * @brief Apaga la luz de fondo de la LCD.
 */
void lcdBacklightOff();

/**
 * @brief Muestra un mensaje de bienvenida o estado inicial.
 */
void lcdDisplayWelcome();

/**
 * @brief Muestra el estado actual de ocupación.
 * @param current_occupied Número de espacios ocupados.
 * @param total_spaces Número total de espacios.
 */
void lcdDisplayOccupancy(uint32_t current_occupied, uint32_t total_spaces);

/**
 * @brief Muestra un mensaje temporalmente y luego restaura el mensaje anterior (o uno por defecto).
 * @param temp_line1 Mensaje temporal para la línea 1.
 * @param temp_line2 Mensaje temporal para la línea 2 (opcional).
 * @param duration_ms Duración en milisegundos para mostrar el mensaje temporal.
 * @param restore_line1 Mensaje a restaurar en línea 1 después del temporal (opcional, si no se provee, muestra ocupación).
 * @param restore_line2 Mensaje a restaurar en línea 2 después del temporal (opcional).
 */
void lcdPrintTemporary(const String& temp_line1, const String& temp_line2, unsigned long duration_ms,
                       const String& restore_line1 = "", const String& restore_line2 = "");

void loopLCDManager(uint32_t current_occupied_val, uint32_t total_spaces_val);

#endif // LCD_MANAGER_H