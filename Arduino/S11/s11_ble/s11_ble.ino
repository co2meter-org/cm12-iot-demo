/*
    Video: https://www.youtube.com/watch?v=oCMOYS71NIU
    Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
    Ported to Arduino ESP32 by Evandro Copercini

   Create a BLE server that, once we receive a connection, will send periodic notifications.
   The service advertises itself as: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
   Has a characteristic of: 6E400002-B5A3-F393-E0A9-E50E24DCCA9E - used for receiving data with "WRITE" 
   Has a characteristic of: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E - used to send data with  "NOTIFY"

   The design of creating the BLE server is:
   1. Create a BLE Server
   2. Create a BLE Service
   3. Create a BLE Characteristic on the Service
   4. Create a BLE Descriptor on the characteristic
   5. Start the service.
   6. Start advertising.

   In this example rxValue is the data received (only accessible inside that function).
   And txValue is the data to be sent, in this example just a byte incremented every second. 
*/
#include <EEPROM.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Adafruit_BME280.h>

BLEServer *pServer = NULL;
BLECharacteristic * pTxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;
uint8_t txValue = 0;

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"


enum {
  FIRST_START,
  HR5_HIGH,
  HR5_LOW,
  HR6_HIGH,
  HR6_LOW,
  HR7_HIGH,
  HR7_LOW,
  HR8_HIGH,
  HR8_LOW,
  HR9_HIGH,
  HR9_LOW
} REGS;

void save_hr_to_eeprom();
void get_hr_from_eeprom();

#define BME_SCL 4
#define BME_SDA 19
TwoWire I2CBME = TwoWire(0);
Adafruit_BME280 bme;

struct READY_PIN {
  const uint8_t PIN;
  bool pressed;
};

int getco2() {
  digitalWrite(15, HIGH);
  delay(35);
  while (digitalRead(16) == 1) {}
  
  byte startarray[] = { 0x68, 0x10, 0x00, 0x09, 0x00, 0x01, 0x02, 0x00, 0x01, 0xa5, 0x5b };
  Serial2.write(startarray, 11);
  delay(50);
  Serial2.flush();
  byte bytearray[] = { 0xfe, 0x04, 0x00, 0x03, 0x00, 0x01, 0xd5, 0xc5 };
  Serial2.write(bytearray, 8);
  delay(10);
  int timeout = 0;
  while (Serial2.available() <= 0) {
    delay(1);
    timeout++;
    if (timeout > 1000) {
      Serial.println("timed out");
      return 0;
    }
  }
  byte co2array[Serial2.available()];
  int i = 0;
  while (Serial2.peek() != -1) {
    co2array[i] = Serial2.read();
    i++;
  }
  if (sizeof(co2array) < 5) {
    Serial.println("response is incomplete");
    return 0;
  }
  int co2 = (co2array[3] << 8) | co2array[4];

  pinMode(15, OUTPUT);
  digitalWrite(15, LOW);
  
  return co2;
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string rxValue = pCharacteristic->getValue();

      if (rxValue.length() > 0) {
        char payload[512];
        if (rxValue == "co2") {
          int co2 = getco2();
          sprintf(payload, "{\"co2\":\" %d \" }",co2);
          Serial.println(payload);
        } else if (rxValue == "temperature") {
          float temperature = bme.readTemperature();
          sprintf(payload, "{\"temperature\":\" %f \" }",temperature);
          Serial.println(payload);
        } else if (rxValue == "pressure") {
          float pressure = bme.readPressure();
          sprintf(payload, "{\"pressure\":\" %f \" }",pressure);
          Serial.println(payload);
        } else if (rxValue == "humidity") {
          float humidity = bme.readHumidity();
          sprintf(payload, "{\"humidity\":\" %f \" }",humidity);
          Serial.println(payload);
        } else if (rxValue == "all") {
          int co2 = getco2();
          float temperature = bme.readTemperature();
          float humidity = bme.readHumidity();
          float pressure = bme.readPressure();
          sprintf(payload, "{\"co2\":\" %d \", \"temperature\":\" %f \", \"humidity\":\" %f \", \"pressure\":\" %f \" }",co2,temperature,humidity,pressure);
          Serial.println(payload);
        } else {
          Serial.println("not a valid option");
        }
        if (deviceConnected) {
          
          int payload_size = sizeof(payload) / sizeof(char);
          int num_payloads = ceil(payload_size / 20);
          for (int i = 0; i < num_payloads; i++) {
            std::string spayload = "";
            for (int j = 0; j < 20; j++) {
              if ((i * 20 + j) >= payload_size) {
                spayload[(i * 20) + j] = '\0';
                break;
              }
              spayload = spayload + payload[(i * 20) + j];
            }
            Serial.println(spayload.c_str());
            pTxCharacteristic->setValue(spayload);
            pTxCharacteristic->notify();
            delay(100); // bluetooth stack will go into congestion, if too many packets are sent
          }
        }
      }
    }
};


void setup() {
  Serial.begin(115200);

  // Create the BLE Device
  BLEDevice::init("co2sensor");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pTxCharacteristic = pService->createCharacteristic(
                    CHARACTERISTIC_UUID_TX,
                    BLECharacteristic::PROPERTY_NOTIFY
                  );
                      
  pTxCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic * pRxCharacteristic = pService->createCharacteristic(
                       CHARACTERISTIC_UUID_RX,
                      BLECharacteristic::PROPERTY_WRITE
                    );

  pRxCharacteristic->setCallbacks(new MyCallbacks());

  // Start the service
  pService->start();

  // Start advertising
  pServer->getAdvertising()->start();
  Serial.println("Waiting a client connection to notify...");
  
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);
  delay(35);
  Serial2.begin(9600, SERIAL_8N1, 14, 13);
  
  EEPROM.begin(12);
  char first_start = EEPROM.read(0);
  //first_start = 'N';
  if (first_start != 'S') {
    Serial.print("First time running..");
    //Read Sensor State Data
    save_hr_to_eeprom();
    //Set the sensor to single measurement mode
    byte sr_mode[] = { 0x68, 0x10, 0x00, 0x0A, 0x00, 0x01, 0x02, 0x00, 0x01, 0xA5, 0x68 };
    Serial2.write(sr_mode, 11);
    delay(10);
    Serial2.flush();
    EEPROM.write(0,(byte)'S');
    EEPROM.commit();
  } else {
    Serial.println("Set the HR Registers");
    get_hr_from_eeprom();
    //Tell sensor to start measurement
    delay(10);
    Serial2.flush();
    byte start_msmt[] = { 0x68, 0x10, 0x00, 0x09, 0x00, 0x01, 0x02, 0x00, 0x01, 0xA5, 0x5B };
    Serial2.write(start_msmt, 11);
    //Delay 50ms to give the sensor to set nRDY high
    delay(50);
    Serial2.flush();
  }
  
  delay(120);
  I2CBME.begin(BME_SDA,BME_SCL,100000);
  unsigned status;
  status = bme.begin(0x76,&I2CBME);
  if (!status) {
    Serial.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
    Serial.print("SensorID was: 0x"); Serial.println(bme.sensorID(),16);
    Serial.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
    Serial.print("   ID of 0x56-0x58 represents a BMP 280,\n");
    Serial.print("        ID of 0x60 represents a BME 280.\n");
    Serial.print("        ID of 0x61 represents a BME 680.\n");
    
    Serial.flush();
  }
  pinMode(16,INPUT);
  while (digitalRead(16) == 1) {}
}

void loop() {

    if (deviceConnected) {
      /*int co2 = getco2();
        pTxCharacteristic->setValue(&co2);
        pTxCharacteristic->notify();
        txValue++;
        delay(10); // bluetooth stack will go into congestion, if too many packets are sent
       */
    }

    // disconnecting
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // give the bluetooth stack the chance to get things ready
        pServer->startAdvertising(); // restart advertising
        Serial.println("start advertising");
        oldDeviceConnected = deviceConnected;
    }
    // connecting
    if (deviceConnected && !oldDeviceConnected) {
    // do stuff here on connecting
        oldDeviceConnected = deviceConnected;
    }
}


void get_hr_from_eeprom() {
  byte hr_state[19] = {0};
  hr_state[0] = 0xFE;
  hr_state[1] = 0x10;
  hr_state[2] = 0x00;
  hr_state[3] = 0x04;
  hr_state[4] = 0x00;
  hr_state[5] = 0x05;
  hr_state[6] = 0x0A;
  for (int i = 1; i < 12; i++) {
    hr_state[i+4] = EEPROM.read(i);
  }
  uint16_t crc = 0xFFFF;
  for (int pos = 0; pos < 18; pos++) {
    crc ^= (uint16_t)hr_state[pos];

    for (int j = 8; j !=0; j--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  hr_state[17] = (byte)(((crc & 0xFF00) >> 8) & 0xFF);
  hr_state[18] = (byte)(crc & 0xFF);
  Serial2.write(hr_state, 19);
  delay(10);
  Serial2.flush();
}

void save_hr_to_eeprom() {
  byte hr_read[] = { 0x68, 0x03, 0x00, 0x04, 0x00, 0x05, 0xCD, 0x31 };
  Serial2.write(hr_read, 8);
  delay(10);
  int millisec = 0;
  while (Serial2.available() <= 0) {
    delay(1);
    millisec++;
    if (millisec > 1500) break;
  }

  if (Serial2.available() <= 0) {
    Serial.println("no data available");
    return;
  }

  int responseSize = Serial2.available();
  byte response[responseSize];
  int count = 0;
  while (count < responseSize) {
    response[count] = Serial2.read();
    count++;
  }
  if (responseSize >= 15) {
    for (int i = 3; i < 13; i++) {
      EEPROM.write((i-2), response[i]);
    }
    EEPROM.commit();
  }
}
