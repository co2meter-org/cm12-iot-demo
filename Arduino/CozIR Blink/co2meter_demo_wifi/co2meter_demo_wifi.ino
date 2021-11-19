#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>
#include "ESPAsyncWebServer.h"
#include "adp5350.h"

#include <Adafruit_BME280.h>

// Wifi credentials
char WIFI_SSID[]="Your WiFi SSID Here";
char WIFI_PASSWORD[]="You WiFi Password Here";

// Definitions for BME280
#define BME_SCL 4
#define BME_SDA 19
TwoWire I2CBME = TwoWire(0);
Adafruit_BME280 bme;

// Creating WebServer with Port 80 (HTTP Unsecure)
AsyncWebServer server(80);
int status = WL_IDLE_STATUS;

// Defining the ADP5350 PMIC
ADP5350 adp;

// Defining the READY Pin input from the Blink and its interrupt routine
struct READY_PIN {
  const uint8_t PIN;
  bool pressed;
};

READY_PIN ready_pin = { 16, false };

void IRAM_ATTR isr() {
  ready_pin.pressed = true;
}

// Get CO2 from Blink
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

void setup() {
// put your setup code here, to run once:
  // Using WiFi so we need LDO 3 for extra current consumption
  adp.enableLDO(3, 1);
  // Lowing the CPU to 80MHz helps conserve battery consumption
  setCpuFrequencyMhz(80);

  // Weird bug in Arduino Library that we need to disconnect before we can connect
  WiFi.disconnect(true);
  Serial.begin(115200);
  // initialise AWS connection
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to Wifi network: ");
    Serial.println(WIFI_SSID);
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

  pinMode(18, OUTPUT);
  digitalWrite(18, LOW);
  delay(10);
  pinMode(ready_pin.PIN, INPUT);
  attachInterrupt(ready_pin.PIN, isr, RISING);

  adp.setCharger(1);
  adp.enableFuelGauge(1);
  adp.enableLDO(2, 0);
  
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

  server.on("/values/co2", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(18, HIGH);
    adp.enableLDO(2,0);
    int co2 = getco2();
    adp.enableLDO(2,0);
    char payload[512];
    time_t now = time(nullptr);
    sprintf(payload, "{\"co2\":\" %d \", \"timestamp\":\" %d \" }",co2,now);
    Serial.println(payload);
    request->send(200, "text/plain", payload);
    digitalWrite(18, LOW);
  });

  server.on("/values/temperature", HTTP_GET   , [](AsyncWebServerRequest *request){
    digitalWrite(18, HIGH);
    float temperature = bme.readTemperature();
    char payload[512];
    time_t now = time(nullptr);
    sprintf(payload, "{\"temperature\":\" %f \", \"timestamp\":\" %d \" }",temperature,now);
    Serial.println(payload);
    request->send(200, "text/plain", payload);
    digitalWrite(18, LOW);
  });
  
   server.on("/values/humidity", HTTP_GET, [](AsyncWebServerRequest *request){
    digitalWrite(18, HIGH);
    float humidity = bme.readHumidity();
    char payload[512];
    time_t now = time(nullptr);
    sprintf(payload, "{\"humidity\":\" %f \", \"timestamp\":\" %d \" }",humidity,now);
    Serial.println(payload);
    request->send(200, "text/plain", payload);
    digitalWrite(18, LOW);
  });
  
  server.on("/values/pressure", HTTP_GET, [](AsyncWebServerRequest *request){
    digitalWrite(18, HIGH);
    float pressure = bme.readPressure();
    char payload[512];
    time_t now = time(nullptr);
    sprintf(payload, "{\"pressure\":\" %f \", \"timestamp\":\" %d \" }",pressure,now);
    Serial.println(payload);
    request->send(200, "text/plain", payload);
    digitalWrite(18, LOW);
  });

  server.on("/values/battery", HTTP_GET, [](AsyncWebServerRequest *request){
    digitalWrite(18, HIGH);
    uint16_t battery = adp.batteryVoltage();
    char payload[512];
    time_t now = time(nullptr);
    sprintf(payload, "{\"battery\":\" %d \", \"timestamp\":\" %d \" }",battery,now);
    Serial.println(payload);
    request->send(200, "text/plain", payload);
    digitalWrite(18, LOW);
  });
  
  server.on("/values/all", HTTP_GET, [](AsyncWebServerRequest *request){
    digitalWrite(18, HIGH);
    int co2 = getco2();
    float temperature = bme.readTemperature();
    float humidity = bme.readHumidity();
    float pressure = bme.readPressure();
    uint16_t battery = adp.batteryVoltage();
    char payload[512];
    time_t now = time(nullptr);
    sprintf(payload, "{\"co2\":\" %d \", \"temperature\":\" %f \", \"humidity\":\" %f \", \"pressure\":\" %f \", \"timestamp\":\" %d \", \"battery\":\" %d \"  }",co2,temperature,humidity,pressure,now,battery);
    Serial.println(payload);
    request->send(200, "text/plain", payload);
    digitalWrite(18, LOW);
  });

  
  server.on("/values/calibrate", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(18, HIGH);
    const char * calibrationPoint = request->getParam("cal")->value().c_str();
    adp.enableLDO(2,1);
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
    Serial2.print('Z',1);
    delay(100);
    if (Serial2.available() > 0) {
      byte co2[Serial2.available()];
      Serial2.readBytes(co2, Serial2.available());
    }
    delay(100);
    char * cal_command = (char *)malloc(16);
    sprintf(cal_command, "X %s\r\n", calibrationPoint);
    Serial.print(cal_command);
    Serial2.print(cal_command);
    delay(100);
    if (Serial2.available() > 0) {
      byte cal_ret[Serial2.available()];
      Serial2.readBytes(cal_ret, Serial2.available());
    }
    adp.enableLDO(2,0);
    digitalWrite(18, LOW);
  });

  server.on("/values/zero", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(18, HIGH);
    adp.enableLDO(2,1);
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
    Serial2.print('Z',1);
    delay(100);
    if (Serial2.available() > 0) {
      byte co2[Serial2.available()];
      Serial2.readBytes(co2, Serial2.available());
    }
    delay(100);
    Serial2.print("U\r\n");
    delay(100);
    if (Serial2.available() > 0) {
      byte cal_ret[Serial2.available()];
      Serial2.readBytes(cal_ret, Serial2.available());
    }
    adp.enableLDO(2,0);
    digitalWrite(18, LOW);
  });

  server.on("/values/ambient", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(18, HIGH);
    adp.enableLDO(2,1);
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
    Serial2.print('Z',1);
    delay(100);
    if (Serial2.available() > 0) {
      byte co2[Serial2.available()];
      Serial2.readBytes(co2, Serial2.available());
    }
    delay(100);
    Serial2.print("G\r\n");
    delay(100);
    if (Serial2.available() > 0) {
      byte cal_ret[Serial2.available()];
      Serial2.readBytes(cal_ret, Serial2.available());
    }
    adp.enableLDO(2,0);
    digitalWrite(18, LOW);
  });
  
  server.begin();
  MDNS.addService("co2sensor", "_tcp", 80);
}

void loop() {
  // put your main code here, to run repeatedly:

}
