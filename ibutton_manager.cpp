#include "ibutton_manager.h"

// Objeto OneWire (privado a este módulo)
OneWire ds(IBUTTON_PIN);

// --- Implementación de Funciones ---

void setupIButtonManager() {
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("Error al inicializar EEPROM!");
    // Aquí podrías decidir detener la ejecución o manejar el error
    delay(1000);
    ESP.restart();
  }
  Serial.printf("EEPROM inicializada. Tamaño: %d bytes. Capacidad: %d registros.\n", EEPROM_SIZE, MAX_IBUTTONS);
  // Opcional: Puedes añadir aquí una verificación inicial de la EEPROM si es necesario
}

bool readIButton(byte* id_buffer) {
  if (!ds.search(id_buffer)) {
    ds.reset_search();
    delay(50); // Pequeña pausa para evitar lecturas fantasma muy rápidas
    return false;
  }

  // Verificar CRC
  if (OneWire::crc8(id_buffer, 7) != id_buffer[7]) {
    Serial.println("Error CRC leyendo iButton.");
    return false;
  }

  // Verificar si es un DS1990A (Family Code 0x01)
  if (id_buffer[0] != 0x01) {
    Serial.print("Dispositivo OneWire no es DS1990A. Código Familia: 0x");
    Serial.println(id_buffer[0], HEX);
    return false;
  }

  // Si todo está OK
  return true;
}

bool registerIButton(uint32_t associated_id, const byte* ibutton_id) {
  int first_free_slot = -1;
  IButtonRecord record;

  // 1. Verificar si ya existe y buscar slot libre
  for (int i = 0; i < MAX_IBUTTONS; ++i) {
    EEPROM.get(i * sizeof(IButtonRecord), record);

    if (record.is_valid) {
      // Comprobar si el ID ya está registrado
      if (memcmp(record.ibutton_id, ibutton_id, IBUTTON_ID_LEN) == 0) {
        Serial.println("Error: iButton ya está registrado.");
        return false; // Ya existe
      }
    } else if (first_free_slot == -1) {
      // Encontrar el primer slot libre
      first_free_slot = i;
    }
  }

  // 2. Si no hay slots libres
  if (first_free_slot == -1) {
    Serial.println("Error: No hay espacio libre en EEPROM para registrar.");
    return false;
  }

  // 3. Crear el nuevo registro y guardarlo
  record.is_valid = true;
  record.associated_id = associated_id;
  memcpy(record.ibutton_id, ibutton_id, IBUTTON_ID_LEN);

  EEPROM.put(first_free_slot * sizeof(IButtonRecord), record);

  // 4. Confirmar escritura en EEPROM
  if (EEPROM.commit()) {
    Serial.print("iButton registrado en slot ");
    Serial.print(first_free_slot);
    Serial.print(" con ID asociado ");
    Serial.println(associated_id);
    return true;
  } else {
    Serial.println("Error: Falló el commit a EEPROM al registrar.");
    return false;
  }
}

bool isIButtonRegistered(const byte* ibutton_id, uint32_t &associated_id_out) {
  IButtonRecord record;
  for (int i = 0; i < MAX_IBUTTONS; ++i) {
    EEPROM.get(i * sizeof(IButtonRecord), record);
    // Verificar si el slot es válido y si los IDs coinciden
    if (record.is_valid && memcmp(record.ibutton_id, ibutton_id, IBUTTON_ID_LEN) == 0) {
      associated_id_out = record.associated_id; // Devolver el ID asociado
      return true; // Encontrado y válido
    }
  }
  associated_id_out = INVALID_ASSOCIATED_ID; // No encontrado, devolver ID inválido
  return false; // No encontrado
}

bool deleteIButton(const byte* ibutton_id) {
  IButtonRecord record;
  bool found = false;
  int slot_to_delete = -1;

  // 1. Buscar el iButton
  for (int i = 0; i < MAX_IBUTTONS; ++i) {
    EEPROM.get(i * sizeof(IButtonRecord), record);
    if (record.is_valid && memcmp(record.ibutton_id, ibutton_id, IBUTTON_ID_LEN) == 0) {
      found = true;
      slot_to_delete = i;
      break; // Encontrado, salir del bucle
    }
  }

  // 2. Si no se encontró
  if (!found) {
    Serial.println("Error: iButton a eliminar no encontrado.");
    return false;
  }

  // 3. Marcar como inválido y guardar
  record.is_valid = false; // Marcar como libre/inválido
  // Opcional: podrías limpiar los otros campos (associated_id, ibutton_id) a 0
  // record.associated_id = 0;
  // memset(record.ibutton_id, 0, IBUTTON_ID_LEN);

  EEPROM.put(slot_to_delete * sizeof(IButtonRecord), record);

  // 4. Confirmar escritura
  if (EEPROM.commit()) {
    Serial.print("iButton eliminado del slot ");
    Serial.println(slot_to_delete);
    return true;
  } else {
    Serial.println("Error: Falló el commit a EEPROM al eliminar.");
    return false;
  }
}

void printIButtonID(const byte* id) {
  for (int i = 0; i < IBUTTON_ID_LEN; i++) {
    if (id[i] < 16) Serial.print("0"); // Añadir cero inicial para valores < 0x10
    Serial.print(id[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

void printAllRegisteredIButtons() {
  Serial.println("--- iButtons Registrados en EEPROM ---");
  IButtonRecord record;
  bool any_registered = false;
  for (int i = 0; i < MAX_IBUTTONS; ++i) {
    EEPROM.get(i * sizeof(IButtonRecord), record);
    if (record.is_valid) {
      any_registered = true;
      Serial.printf("Slot %d: Valido=SÍ, ID_Asociado=%u, ID_iButton=", i, record.associated_id);
      printIButtonID(record.ibutton_id); // Reutilizamos la función de impresión
    }
    // Opcional: Imprimir slots vacíos
    // else {
    //   Serial.printf("Slot %d: Valido=NO\n", i);
    // }
  }
   if (!any_registered) {
     Serial.println("No hay iButtons registrados.");
   }
  Serial.println("--------------------------------------");
}