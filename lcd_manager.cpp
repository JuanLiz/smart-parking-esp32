#include "lcd_manager.h"

// --- Objeto LCD (privado a este módulo) ---
// El constructor toma (dirección_i2c, columnas, filas)
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);
bool lcd_initialized = false;

// Variables para lcdPrintTemporary
String prev_line1 = "";
String prev_line2 = "";
bool temporary_message_active = false;
unsigned long temporary_message_end_time = 0;

// Para saber qué restaurar si no se especifica
extern uint32_t current_occupancy; // Asume que current_occupancy es global en tu .ino
extern uint32_t TOTAL_PARKING_SPACES; // Asume que TOTAL_PARKING_SPACES es global en tu .ino


// --- Implementación de Funciones ---

bool setupLCDManager(int sda_pin, int scl_pin) {
  // Wire.begin() es llamado automáticamente por la biblioteca LiquidCrystal_I2C
  // si no se ha llamado antes. Si necesitas pines específicos, llámalo aquí:
  // Wire.begin(sda_pin, scl_pin); // Para ESP32, los pines por defecto suelen ser 21 (SDA) y 22 (SCL)

  // Necesitas verificar si la LCD está conectada.
  // La biblioteca LiquidCrystal_I2C no tiene un método directo como "isConnected".
  // Una forma es intentar inicializar y ver si falla, o usar un I2C scanner.
  // Por ahora, intentaremos inicializar.
  // Algunas versiones de la biblioteca llaman a Wire.begin internamente.
  // Para ser explícitos, especialmente si cambias pines:
  if (sda_pin != -1 && scl_pin != -1) { // -1 podría ser un indicador para no llamar a Wire.begin aquí
      Wire.begin(sda_pin, scl_pin);
  }


  // Escaneo I2C para verificar la dirección (opcional pero bueno para debug)
  bool device_found = false;
  Serial.println("Scanning I2C bus for LCD...");
  byte error, address;
  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address,HEX);
      Serial.println(" !");
      if (address == LCD_ADDRESS) {
          device_found = true;
      }
    } else if (error == 4) {
      // Serial.print("Unknown error at address 0x");
      // if (address<16) Serial.print("0");
      // Serial.println(address,HEX);
    }
  }
  if (!device_found) {
      Serial.printf("LCD not found at address 0x%02X. Please check wiring and LCD_ADDRESS.\n", LCD_ADDRESS);
      lcd_initialized = false;
      return false;
  }
  Serial.printf("LCD expected at 0x%02X was found.\n", LCD_ADDRESS);


  // Inicializar la LCD
  lcd.init(); // Inicializa la LCD (algunas bibliotecas usan lcd.begin())
  lcd.backlight(); // Encender la luz de fondo
  lcdClear();
  lcdPrint("LCD Initialized", "Smart Parking");
  delay(1000); // Mostrar mensaje de inicialización
  lcd_initialized = true;
  return true;
}

void lcdPrint(const String& line1, const String& line2, bool clear_display) {
  if (!lcd_initialized) return;
  if (temporary_message_active && millis() < temporary_message_end_time) return; // No sobreescribir mensaje temporal

  if (clear_display) {
    lcd.clear();
  }
  lcd.setCursor(0, 0);
  lcd.print(line1.substring(0, LCD_COLS)); // Truncar si es más largo
  if (line2 != "") {
    lcd.setCursor(0, 1);
    lcd.print(line2.substring(0, LCD_COLS)); // Truncar si es más largo
  }
  prev_line1 = line1; // Guardar para restauración
  prev_line2 = line2;
  temporary_message_active = false; // Cualquier print normal cancela el temporal
}

void lcdPrintAt(uint8_t col, uint8_t row, const String& message) {
  if (!lcd_initialized) return;
  if (temporary_message_active && millis() < temporary_message_end_time) return;

  if (row < LCD_ROWS && col < LCD_COLS) {
    lcd.setCursor(col, row);
    lcd.print(message.substring(0, LCD_COLS - col)); // Truncar si excede
  }
  // No actualizamos prev_line1/2 aquí porque es una escritura parcial
  temporary_message_active = false;
}

void lcdClear() {
  if (!lcd_initialized) return;
  lcd.clear();
  prev_line1 = "";
  prev_line2 = "";
  temporary_message_active = false;
}

void lcdBacklightOn() {
  if (!lcd_initialized) return;
  lcd.backlight();
}

void lcdBacklightOff() {
  if (!lcd_initialized) return;
  lcd.noBacklight();
}

void lcdDisplayWelcome() {
  if (!lcd_initialized) return;
  lcdPrint(" Smart Parking ", "  Bienvenido!  ");
}

void lcdDisplayOccupancy(uint32_t current_occupied, uint32_t total_spaces) {
  if (!lcd_initialized) return;
  if (temporary_message_active && millis() < temporary_message_end_time) return; // No sobreescribir mensaje temporal

  String line1 = "Ocupacion:";
  String line2 = String(current_occupied) + "/" + String(total_spaces) + " Libres:" + String(total_spaces - current_occupied);

  // Centrar línea 1 si es corta
  int padding1 = (LCD_COLS - line1.length()) / 2;
  String padded_line1 = "";
  for(int i=0; i<padding1; ++i) padded_line1 += " ";
  padded_line1 += line1;

  // Ajustar línea 2 para que quepa
  if (line2.length() > LCD_COLS) {
      line2 = String(current_occupied) + "/" + String(total_spaces) + " L:" + String(total_spaces - current_occupied);
      if (line2.length() > LCD_COLS) { // Aun así es larga
          line2 = String(current_occupied) + "/" + String(total_spaces); // La más básica
      }
  }
  int padding2 = (LCD_COLS - line2.length()) / 2;
  String padded_line2 = "";
  for(int i=0; i<padding2; ++i) padded_line2 += " ";
  padded_line2 += line2;


  lcdPrint(padded_line1, padded_line2, true); // Limpiar y mostrar
}


void lcdPrintTemporary(const String& temp_line1, const String& temp_line2, unsigned long duration_ms,
                       const String& restore_line1_custom, const String& restore_line2_custom) {
    if (!lcd_initialized) return;

    // Guardar el estado actual si no hay ya un mensaje temporal activo
    if (!temporary_message_active) {
        // prev_line1 y prev_line2 ya deberían tener el mensaje actual visible
        // si se usó lcdPrint(). Si se usó lcdPrintAt() o lcdClear(), pueden estar vacíos.
        // Para ser más robustos, podríamos leer de la LCD si la librería lo permitiera,
        // pero la mayoría no lo hace. Asumimos que prev_line1/2 son representativos o
        // el usuario quiere restaurar ocupación.
    }

    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(temp_line1.substring(0, LCD_COLS));
    if (temp_line2 != "") {
        lcd.setCursor(0,1);
        lcd.print(temp_line2.substring(0, LCD_COLS));
    }

    temporary_message_active = true;
    temporary_message_end_time = millis() + duration_ms;

    // Guardar qué restaurar
    // Si no se proveen líneas de restauración custom, usaremos las previas.
    // Si las previas están vacías (ej. después de un clear), por defecto mostramos ocupación.
    // Esta parte de la restauración se manejará en un loopLCDManager()
}

// NUEVO: Función para manejar el loop de la LCD (restaurar mensajes temporales)
void loopLCDManager(uint32_t current_occupied_val, uint32_t total_spaces_val) {
    if (!lcd_initialized) return;

    if (temporary_message_active && millis() >= temporary_message_end_time) {
        temporary_message_active = false;
        // Ahora usa los parámetros pasados a esta función
        lcdDisplayOccupancy(current_occupied_val, total_spaces_val);
    }
}

