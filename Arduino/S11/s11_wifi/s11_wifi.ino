#include <EEPROM.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>
#include "ESPAsyncWebServer.h"

#include <Adafruit_BME280.h>

// Wifi credentials
char WIFI_SSID[]="Your WiFi SSID Here";
char WIFI_PASSWORD[]="Your WiFi Password Here";

#define BME_SCL 4
#define BME_SDA 19
TwoWire I2CBME = TwoWire(0);
Adafruit_BME280 bme;
AsyncWebServer server(80);
int status = WL_IDLE_STATUS;


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

void setup() {
// put your setup code here, to run once:
  WiFi.disconnect(true);
  Serial.begin(115200);
  // initialise AWS connection
  while (status != WL_CONNECTED) {
    //Serial.print("Attempting to connect to Wifi network: ");
    //Serial.println(WIFI_SSID);
    status = WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    delay(5000);
  }
  Serial.println("Connected to Wifi!");

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (!MDNS.begin("esp32")) {
        Serial.println("Error setting up MDNS responder!");
        while(1) {
            delay(1000);
        }
    }
    Serial.println("mDNS responder started");
  
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  while (!time(nullptr)) {
    delay(1000);
  }

  gpio_hold_dis(GPIO_NUM_15);
  pinMode(15, OUTPUT);
  digitalWrite(15, LOW);
  delay(10);
  pinMode(ready_pin.PIN, INPUT);
  attachInterrupt(ready_pin.PIN, isr, RISING);
  
  Serial2.begin(38400, SERIAL_8N1, 14, 13);
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

  server.on("/values/co2", HTTP_GET, [](AsyncWebServerRequest *request){
    int co2 = getco2();
    char payload[512];
    time_t now = time(nullptr);
    sprintf(payload, "{\"co2\":\" %d \", \"timestamp\":\" %d \" }",co2,now);
    Serial.println(payload);
    request->send(200, "text/plain", payload);
  });

  server.on("/values/temperature", HTTP_GET   , [](AsyncWebServerRequest *request){
    float temperature = bme.readTemperature();
    char payload[512];
    time_t now = time(nullptr);
    sprintf(payload, "{\"temperature\":\" %f \", \"timestamp\":\" %d \" }",temperature,now);
    Serial.println(payload);
    request->send(200, "text/plain", payload);
  });
   server.on("/values/humidity", HTTP_GET, [](AsyncWebServerRequest *request){
    float humidity = bme.readHumidity();
    char payload[512];
    time_t now = time(nullptr);
    sprintf(payload, "{\"humidity\":\" %f \", \"timestamp\":\" %d \" }",humidity,now);
    Serial.println(payload);
    request->send(200, "text/plain", payload);
  });
  
  server.on("/values/pressure", HTTP_GET, [](AsyncWebServerRequest *request){
    float pressure = bme.readPressure();
    char payload[512];
    time_t now = time(nullptr);
    sprintf(payload, "{\"pressure\":\" %f \", \"timestamp\":\" %d \" }",pressure,now);
    Serial.println(payload);
    request->send(200, "text/plain", payload);
  });
  
  server.on("/values/all", HTTP_GET, [](AsyncWebServerRequest *request){
    int co2 = getco2();
    float temperature = bme.readTemperature();
    float humidity = bme.readHumidity();
    float pressure = bme.readPressure();
    char payload[512];
    time_t now = time(nullptr);
    sprintf(payload, "{\"co2\":\" %d \", \"temperature\":\" %f \", \"humidity\":\" %f \", \"pressure\":\" %f \", \"timestamp\":\" %d \" }",co2,temperature,humidity,pressure,now);
    Serial.println(payload);
    request->send(200, "text/plain", payload);
  });
  
  server.begin();
  MDNS.addService("co2sensor", "_tcp", 80);
}

void loop() {
  // put your main code here, to run repeatedly:

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
