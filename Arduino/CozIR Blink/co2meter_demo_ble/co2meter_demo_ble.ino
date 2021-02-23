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
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Adafruit_BME280.h>
#include "adp5350.h"

ADP5350 adp;
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


#define BME_SCL 4
#define BME_SDA 19
TwoWire I2CBME = TwoWire(0);
Adafruit_BME280 bme;

struct READY_PIN {
  const uint8_t PIN;
  bool pressed;
};

READY_PIN ready_pin = { 16, false };

void IRAM_ATTR isr() {
  ready_pin.pressed = true;
}

int getco2() {
  ready_pin.pressed = false;
  adp.enableLDO(2, 1);
  int timeout = 0;
  while (ready_pin.pressed == false) {
    delay(1);
    timeout++;
    if (timeout > 5000) {
      Serial.println("nready timed out at 5 sec");
      adp.enableLDO(2, 0);
      return 0;
    }
  }

  Serial2.flush();
  Serial2.print('Z', 1);
  timeout = 0;
  while (Serial2.available() <= 0) {
    delay(1);
    timeout++;
    if (timeout > 3000) {
      Serial.println("timed out");
      adp.enableLDO(2, 0);
      return 0;
    }
  }
  byte co2array[Serial2.available()];
  int i = 0;
  while (Serial2.peek() != -1) {
    co2array[i] = Serial2.read();
    i++;
  }
  if (sizeof(co2array) < 3) {
    Serial.println("response is incomplete");
    adp.enableLDO(2, 0);
    return 0;
  }
  if (co2array[2] != 0x55) {
    Serial.println("response is not reliable");
    adp.enableLDO(2, 0);
    return 0;
  }
  int co2 = (co2array[0] << 8) | co2array[1];
  
  digitalWrite(15, HIGH);
  ready_pin.pressed = false;
  adp.enableLDO(2, 0);
  
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
      digitalWrite(18, HIGH);
      if (rxValue.length() > 0) {
        char payload[512];
        if (rxValue == "co2") {
          adp.enableLDO(2,1);
          int co2 = getco2();
          adp.enableLDO(2,0);
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
          adp.enableLDO(2,1);
          int co2 = getco2();
          adp.enableLDO(2,0);
          float temperature = bme.readTemperature();
          float humidity = bme.readHumidity();
          float pressure = bme.readPressure();
          sprintf(payload, "{\"co2\":\" %d \", \"temperature\":\" %f \", \"humidity\":\" %f \", \"pressure\":\" %f \" }",co2,temperature,humidity,pressure);
          Serial.println(payload);
        } else {
          Serial.println("not a valid option");
        }
        if (deviceConnected) {
          
          int payload_size = strlen(payload);
          int num_payloads = ceil((float)payload_size / 20.0);
          Serial.print("payload size: "); Serial.println(payload_size);
          Serial.print("num_payloads: "); Serial.println(num_payloads);
          for (int i = 0; i < num_payloads; i++) {
            std::string spayload = "";
            for (int j = 0; j < 20; j++) {
              if ((i * 20 + j) > payload_size) {
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
        digitalWrite(18, LOW);
      }
    }
    
};


void setup() {

  adp.enableLDO(3, 1);
  setCpuFrequencyMhz(80);
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
  
  gpio_hold_dis(GPIO_NUM_15);
  pinMode(18, OUTPUT);
  digitalWrite(18, LOW);
  delay(10);
  pinMode(ready_pin.PIN, INPUT);
  attachInterrupt(ready_pin.PIN, isr, RISING);
  
  Serial2.begin(38400, SERIAL_8N1, 14, 13);
  delay(120);
  I2CBME.begin(BME_SDA,BME_SCL,100000);
  unsigned status;
  status = bme.begin(0x77,&I2CBME);
  if (!status) {
    Serial.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
    Serial.print("SensorID was: 0x"); Serial.println(bme.sensorID(),16);
    Serial.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
    Serial.print("   ID of 0x56-0x58 represents a BMP 280,\n");
    Serial.print("        ID of 0x60 represents a BME 280.\n");
    Serial.print("        ID of 0x61 represents a BME 680.\n");
    
    Serial.flush();
  }

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
