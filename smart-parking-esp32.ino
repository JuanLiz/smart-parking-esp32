#include <OneWire.h>
#include <EEPROM.h>

OneWire ds(33);

byte ibutton[8];

bool existe = true;
bool flag;

char* keyStatus; 

// Get button status
void getKeyCode() {
  byte present = 0;
  byte data[12];
  keyStatus = "";

  if (!ds.search(ibutton)) {
    ds.reset_search();
    return;
  }

  // Read first 
  if (OneWire::crc8(ibutton, 7) != ibutton[7]) {
    keyStatus = "CRC Invalid";
    return;
  }

  if (ibutton[0] != 0x01) {
    keyStatus = "No DS1990A";
    return;
  }
  keyStatus = "ok";
  // Reading from LSB to MSB (Reversed)
  for (int i = 7; i >= 0; i--) {
      Serial.print(ibutton[i], HEX);
      Serial.print(" ");
    }
    Serial.println(" ");
  ds.reset();
}

void setup() {
  Serial.begin(115200);
}

void loop() {
  getKeyCode();

}
