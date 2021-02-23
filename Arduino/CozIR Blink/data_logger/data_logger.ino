#include <EEPROM.h>
#include <time.h>
#include "adp5350.h"
#include "ESPAsyncWebServer.h"
#include <ESPmDNS.h>
#include "SPIFFS.h"

#include <Adafruit_BME280.h>

// This include is for the AWS IOT library that we installed
// This include is for Wifi functionality
#include <WiFi.h>

#define WIFI_MODE 0
#define UART_MODE 1
#define COMMAND_MODE WIFI_MODE

// Wifi credentials
char WIFI_SSID[]="Your WiFi SSID Here";
char WIFI_PASSWORD[]="Your WiFi Password Here";

// Connection status
int status = WL_IDLE_STATUS;

// Payload array to store thing shadow JSON document
char payload[512];

// Counter for iteration
int counter = 0;

// Definitions for BME280
#define BME_SCL 4
#define BME_SDA 19
TwoWire I2CBME = TwoWire(0);
Adafruit_BME280 bme;

// Definition for ADP5350 PMIC
ADP5350 adp;

// Definitions for how long the process will sleep
#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  1 * 60        /* Time ESP32 will go to sleep (in seconds) */

#define WAKEUP_PIN 35

// Definition for the ready pin and its interrupt routine
struct READY_PIN {
  const uint8_t PIN;
  bool pressed;
};

READY_PIN ready_pin = { 16, false };

void IRAM_ATTR isr() {
  ready_pin.pressed = true;
}

void IRAM_ATTR wakeup_isr() {
  Serial.println("wakeup pin pressed");
}

int startTimeMicros;
int endTimeMicros;


hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
AsyncWebServer server(80);
bool sleepFlag = false;
bool commandMode = false;

void IRAM_ATTR timerExpired() {
  //portENTER_CRITICAL_ISR(&timerMux);
  Serial.println("Timer Expired");
  detachInterrupt(digitalPinToInterrupt(35));
  sleepFlag = true;
  //portEXIT_CRITICAL_ISR(&timerMux);
}

// Getting CO2 from Blink
int getco2() {
  ready_pin.pressed = false;
  adp.enableLDO(2, 1);
  int timeout = 0;
  while (ready_pin.pressed == false) {
    delay(1);
    timeout++;
    if (timeout > 3000) {
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


// Go to sleep routine
void goToSleep() {
  //Deep Sleep
  WiFi.disconnect(true);
  
  Serial.println("Going to sleep now");
  Serial.flush(); 

  //Get the processing time and subtract it from the sleep time so we can try and data log at more exact intervals
  endTimeMicros = micros();
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR - (endTimeMicros - startTimeMicros));
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);
  esp_deep_sleep_start();
}


void enableSleepTimer() {
  Serial.println("Enabling Timer");
  timer = timerBegin(0,80,true);
  timerAttachInterrupt(timer, &timerExpired, true);

  timerAlarmWrite(timer, 30000000, true);
  timerAlarmEnable(timer);
}

void disableSleepTimer() {
  Serial.println("Disabling Timer");
  timerAlarmDisable(timer);
  timerDetachInterrupt(timer);
  timerEnd(timer);
}

void dataDump() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error occurred while mounting SPIFFS");
    //goToSleep();
  }
  
  File readlogger = SPIFFS.open("/datalog.csv", "r");
  if (!readlogger) {
    Serial.println("Failed to open the data log");
    //goToSleep();
  }
  Serial.println("seconds,co2");
  while (readlogger.available()) {
    Serial.write(readlogger.read());
  }
  commandMode = true;
}


void captureDataLog() {
  digitalWrite(18, HIGH);
  if (!SPIFFS.begin(true)) {
    Serial.println("An error occurred while mounting SPIFFS");
    goToSleep();
  }

  File logger = SPIFFS.open("/datalog.csv", "a");
  if (!logger) {
    Serial.println("Failed to open the data log");
    goToSleep();
  }
  Serial.print("Total Space:   ");
  Serial.print(SPIFFS.totalBytes());
  Serial.println(" bytes");
  Serial.print("Total used Space:     ");
  Serial.print(SPIFFS.usedBytes());
  Serial.println(" bytes");

  

  time_t now = time(nullptr);
  int co2 = getco2();

  char log_line[128];
  sprintf(log_line, "%d,%d\r\n", now, co2);
  int bytesWritten = logger.print(log_line);
  if (bytesWritten == 0)
    Serial.println("Failed to write to file");
  logger.close();
//  Serial.print("Log line:   ");
//  Serial.print(log_line);

  digitalWrite(18, LOW);
  goToSleep();
}


void calibrate() {
  Serial.println("Ambient Calibrating");
    disableSleepTimer();
    enableSleepTimer();
    digitalWrite(18, HIGH);
    adp.enableLDO(2,1);
    int timeout = 0;
    while (ready_pin.pressed == false) {
      delay(1);
      timeout++;
      if (timeout > 5000) {
        Serial.println("nready timed out at 5 sec");
        adp.enableLDO(2, 0);
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
}

void zero() {
  Serial.println("Zero Calibrating");
    disableSleepTimer();
    enableSleepTimer();
    digitalWrite(18, HIGH);
    adp.enableLDO(2,1);
    int timeout = 0;
    while (ready_pin.pressed == false) {
      delay(1);
      timeout++;
      if (timeout > 5000) {
        Serial.println("nready timed out at 5 sec");
        adp.enableLDO(2, 0);
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
}


void printBatteryLevel() {
  Serial.print("Current Battery Voltage: ");
  Serial.print(adp.batteryVoltage());
  Serial.println("");
}


void deleteDataLog() {
  Serial.println("Are you sure you want to delete the data log? (y for yes, any other key for no)");

  while (Serial.available() <= 0);

  char response = Serial.read();
  if (response == 'y') {
    Serial.println("Removing Data Log from Memory...");
    if (!SPIFFS.begin(true)) {
      Serial.println("An error occurred while mounting SPIFFS");
      //goToSleep();
    }
    
    SPIFFS.remove("/datalog.csv");
  } else {
    Serial.println("Ok, doing nothing..");
  }
}

void enterServerMode() {
  Serial.println("Entering Server Mode for Measurements and Calibration");

  // Weird bug with Arduino Library, need to disconnect from WiFi before attempting to connect
  WiFi.disconnect(true);
  // initialise WiFi connection
  int attempts = 0;
  while (status != WL_CONNECTED) {
    // After 5 failed attempts of trying to connect to the WiFI just go to sleep to conserve battery
    if (attempts > 5) {
      goToSleep();
    }
    Serial.print("Attempting to connect to Wifi network: ");
    status = WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    delay(5000);
  }
  Serial.println("Connected to Wifi!");

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Getting the current time in GMT
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  while (!time(nullptr)) {
    delay(1000);
  }
  
  if (!MDNS.begin("esp32")) {
    Serial.println("Error setting up MDNS responder!");
    goToSleep();
  }

  enableSleepTimer();
  disableCore0WDT();
  disableCore1WDT();
  disableLoopWDT();

  server.on("/values/co2", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Getting CO2");
    disableSleepTimer();
    enableSleepTimer();
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
    Serial.println("Getting Temperature");
    disableSleepTimer();
    enableSleepTimer();
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
    Serial.println("Getting Humidity");
    disableSleepTimer();
    enableSleepTimer();
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
    Serial.println("Getting Pressure");
    disableSleepTimer();
    enableSleepTimer();
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
    Serial.println("Getting Battery");
    disableSleepTimer();
    enableSleepTimer();
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
    Serial.println("Getting All");
    disableSleepTimer();
    enableSleepTimer();
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
    Serial.println("Calibrating to Target");
    disableSleepTimer();
    enableSleepTimer();
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
    Serial.println("Zero Calibrating");
    disableSleepTimer();
    enableSleepTimer();
    digitalWrite(18, HIGH);
    zero();
    digitalWrite(18, LOW);
  });

  server.on("/values/ambient", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Ambient Calibrating");
    disableSleepTimer();
    enableSleepTimer();
    digitalWrite(18, HIGH);
    calibrate();
    digitalWrite(18, LOW);
  });

  server.on("/values/log", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Sending Data log file");
    if (!SPIFFS.begin(true)) {
      Serial.println("An error occurred while mounting SPIFFS");
      //goToSleep();
    }
    disableSleepTimer();
    enableSleepTimer();
    digitalWrite(18, HIGH);
    request->send(SPIFFS, "/datalog.csv");
    digitalWrite(18, LOW);
  });
  
  server.begin();
  MDNS.addService("co2sensor", "_tcp", 80);
}


void enterCommandMode() {
  commandMode = true;
  Serial.println("Command Options:\r\n  c -> ambient calibrate\r\n  z -> zero calibrate\r\n  d -> data dump\r\n  s -> sleep\r\n  b -> check battery level\r\n  r -> delete data\r\n");
  Serial.println("Ready for Command: ");
}

void enterWakeupMode() {
  if (COMMAND_MODE == WIFI_MODE) {
    enterServerMode();
  } else {
    enterCommandMode();
  }
}


void setup() {
  // put your setup code here, to run once:
  // Since we're using WiFi, we need to turn on the second LDO for extra current
  adp.enableLDO(3, 1);
  adp.setCharger(1);
  adp.enableFuelGauge(1);
  startTimeMicros = micros();
  delay(1000);
  // Reducing the CPU Frequency is better for power consumption, helping to extend battery life
  setCpuFrequencyMhz(80);

  // Serial for debugging
  Serial.begin(115200);
  Serial.println("starting datalog application");

  // Setting up the LED
  pinMode(18, OUTPUT);
  digitalWrite(18, LOW);

  // Turn off the LDO to the sensor as a precautionary measure to make sure we are able to read
  adp.enableLDO(2, 0);

  // Setting up the READY pin
  delay(10);
  pinMode(ready_pin.PIN, INPUT);
  attachInterrupt(ready_pin.PIN, isr, RISING);
  pinMode(WAKEUP_PIN, INPUT);
  attachInterrupt(WAKEUP_PIN, wakeup_isr, FALLING);

  // Setting up the UART for the Sensor
  Serial2.begin(38400, SERIAL_8N1, 14, 13);
  delay(120);

  // Setting up the BME280
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
    
    goToSleep();
  }

  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : enterWakeupMode(); break;
    case ESP_SLEEP_WAKEUP_EXT1 : enterWakeupMode(); break;
    case ESP_SLEEP_WAKEUP_TIMER : captureDataLog(); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : captureDataLog(); break;
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  if (sleepFlag) {
    sleepFlag = false;
    goToSleep();
  }

  if (commandMode == true) {
    
    while (Serial.available() <= 0);
    byte command = Serial.read();
    switch (command) {
      case 'c':
        calibrate();
        break;
      case 'z':
        zero();
        break;
      case 'd':
        dataDump();
        break;
      case 's':
        goToSleep();
        break;
      case 'b':
        printBatteryLevel();
        break;
      case 'r':
        deleteDataLog();
        break;
      default:
        commandMode = false;
        goToSleep();
        break;
    }
  }
  
}
