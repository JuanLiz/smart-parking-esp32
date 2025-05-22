# Smart Parking System with ESP32 and iButton

An ESP32-based smart parking system using DS1990A iButtons for authentication, MQTT for mobile app communication (enabling Two-Factor Authentication and remote management), and local EEPROM storage for parking occupancy and iButton registry.

The mobile app repository can be found [here](https://github.com/JuanLiz/smart-parking-mqtt).

## Features

* **iButton Authentication:** Utilizes DS1990A iButtons for secure access control.
* **Local Data Persistence:** Registered iButtons and current parking occupancy are stored in the ESP32's EEPROM, ensuring data survives reboots.
* **Servo-Controlled Barrier:** Manages a physical parking barrier using a servomotor.
* **User Feedback:** Provides real-time information and alerts to users via a 16x2 I2C LCD display and an auditory buzzer.
* **MQTT Communication:** Integrates with a mobile application (React Native) over MQTT for:
  * Remote iButton registration (pairing).
  * Two-Factor Authentication (2FA) for entry, requiring mobile app confirmation.
  * Remote iButton deletion.
* **Occupancy Control:** Tracks the number of available parking spaces, displaying "Parking Full" and denying entry when capacity is reached.
* **Status Publishing:** Regularly publishes system status (e.g., online, occupancy) to an MQTT topic.

## Hardware Required

* ESP32 Development Board
* DS1990A iButton(s)
* 1-Wire iButton Reader (a simple probe connected to a GPIO pin with a pull-up resistor)
* Servomotor (e.g., SG90 or similar)
* 16x2 I2C LCD Display
* Active or Passive Buzzer
* Appropriate resistors (4.7kΩ pull-up for the 1-Wire data line)
* Breadboard and connecting wires

## Software & Libraries

* Arduino IDE or PlatformIO IDE (recommended for ESP32 development)
* **ESP32 Core Libraries:**
  * `WiFi.h`: For WiFi connectivity.
  * `EEPROM.h`: For persistent storage.
  * `Servo.h`: For controlling the servo motor (ESP32-specific version if applicable).
* **External Libraries:**
  * `OneWire.h`: For 1-Wire communication with iButtons.
  * `LiquidCrystal_I2C.h`: For I2C communication with the LCD.
  * `PubSubClient.h`: For MQTT client functionality.

## Setup & Installation

1. **Clone the Repository:**

    ```bash
    git clone https://github.com/JuanLiz/smpart-parking-esp32.git
    cd smart-parking-esp32
    ```

2. **Library Installation:**
   * Open the project in Arduino IDE or PlatformIO.
   * Install the libraries listed above using the Arduino Library Manager

3. **Hardware Connections:**
   * Connect the iButton reader's data line to the GPIO pin defined in `ibutton_manager.h` or your main sketch (ensure a pull-up resistor is used).
   * Connect the servo motor signal pin to the GPIO pin defined for it.
   * Connect the I2C LCD (SDA, SCL) to the ESP32's default I2C pins or as configured.
   * Connect the buzzer to a digital GPIO pin.
   * Power the components appropriately.

4. **Configuration:**
   * Modify `smart-parking-esp32.ino` (or a dedicated configuration file if you create one) to set your:
      * WiFi network credentials (`WIFI_SSID` and `WIFI_PASSWORD`).
      * MQTT broker details (server address, port, base topic prefix).
      * Pin definitions for peripherals if different from defaults.

5. **Upload Firmware:**
   * Select the correct ESP32 board in your IDE.
   * Compile and upload the sketch to your ESP32.

## Functionality Overview

The system initializes by connecting to WiFi and the MQTT broker. It then monitors the iButton reader.

* **Entry Attempt:**
    1. User presents a registered iButton.
    2. System checks if parking is full. If so, denies entry.
    3. If space is available and 2FA is enabled, an MQTT message is sent to the companion app.
    4. User confirms/denies entry via the app.
    5. If approved (or if 2FA is not required/disabled for that user), the servo opens the barrier, occupancy is updated, and feedback is provided.
* **Exit Attempt:**
    1. User presents a registered iButton (that was previously marked as 'inside').
    2. The servo opens the barrier, occupancy is updated, and feedback is provided.
* **Remote Management (via MQTT from Mobile App):**
  * **Pairing:** App initiates pairing mode; user presents new iButton to the reader; ESP32 registers it.
  * **Deletion:** App initiates delete mode; user presents iButton to be deleted; ESP32 removes it from EEPROM.
* **Status Updates:** The ESP32 periodically publishes its online status and current parking occupancy to MQTT topics.
* **User Feedback:** The LCD displays messages like "Access Granted," "Access Denied," "Parking Full," "Present iButton," and current occupancy. The buzzer provides auditory cues for success, failure, and alerts.

## Detailed Project Log

For a comprehensive understanding of the project's architecture, development phases, component choices, and detailed flowcharts, please refer to the [Project Logbook](./logbook.md) or [Project Logbook (in Spanish)](./logbook-es.md).
