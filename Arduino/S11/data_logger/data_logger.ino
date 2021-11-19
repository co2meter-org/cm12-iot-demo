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
#define COMMAND_MODE UART_MODE

// Wifi credentials
char WIFI_SSID[]="Your WiFi SSID Here";
char WIFI_PASSWORD[]="Your WiFi Password Here";

// Connection status
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

void printHex(uint8_t num) {
  char hexCar[2];

  sprintf(hexCar, "%02X ", num);
  Serial.print(hexCar);
}


int getco2() {
  digitalWrite(15, HIGH);
  delay(35);
  int timeout = 0;
  while (digitalRead(16) == 1) {
    delay(1);
    timeout++;
    if (timeout > 5000) {
      Serial.println("timed out");
      return 0;
    }
  }

  Serial1.println("starting measurement");
  //byte startarray[] = { 0x68, 0x10, 0x00, 0x09, 0x00, 0x01, 0x02, 0x00, 0x01, 0xa5, 0x5b };
  //Serial2.write(startarray, 11);
  delay(50);
  Serial2.flush();
  Serial1.println("getting the co2");
  byte bytearray[] = { 0xfe, 0x04, 0x00, 0x03, 0x00, 0x01, 0xd5, 0xc5 };
  Serial2.write(bytearray, 8);
  delay(10);
  timeout = 0;
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
  for(i=0; i<sizeof(co2array); i++){
    printHex(co2array[i]);
  }
  Serial.println("");
  Serial.println("got co2:");
  Serial.print(co2);
  Serial.println("");

  pinMode(15, OUTPUT);
  digitalWrite(15, LOW);
  
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

void calibrate(int target) {
  int timeout = 0;
  byte tar_lo = (target & 0xff);
  byte tar_hi = ((target >> 8) & 0xff);
  Serial.println("Hi byte:");
  Serial.print(tar_hi);
  Serial.println("");
  Serial.println("Lo byte:");
  Serial.print(tar_lo);
  Serial.println("");
  byte cal_target[] = { 0x68, 0x10, 0x00, 0x02, 0x00, 0x01, 0x02, tar_hi, tar_lo, 0x00, 0x00 };
  uint16_t crc = 0xffff;
  for (int pos = 0; pos < 9; pos++) {
    crc ^= (uint16_t)cal_target[pos];          // XOR byte into least sig. byte of crc
  
    for (int i = 8; i != 0; i--) {    // Loop over each bit
      if ((crc & 0x0001) != 0) {      // If the LSB is set
        crc >>= 1;                    // Shift right and XOR 0xA001
        crc ^= 0xA001;
      }
      else                            // Else LSB is not set
        crc >>= 1;                    // Just shift right
    }
  }
  cal_target[10] = ((crc >> 8) & 0xFF);
  cal_target[9] = (crc & 0xFF);

  for(int i=0; i<sizeof(cal_target); i++){
    printHex(cal_target[i]);
  }
  
  Serial2.write(cal_target, 11);
  delay(10);
  timeout = 0;
  while (Serial2.available() <= 0) {
    delay(1);
    timeout++;
    if (timeout > 1000) {
      Serial.println("timed out");
      return;
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
    return;
  }

  byte cal_span[] = { 0x68, 0x10, 0x00, 0x01, 0x00, 0x01, 0x02, 0x7C, 0x05, 0x85, 0x10 };
  Serial2.write(cal_span, 11);
  delay(10);
  timeout = 0;
  while (Serial2.available() <= 0) {
    delay(1);
    timeout++;
    if (timeout > 1000) {
      Serial.println("timed out");
      return;
    }
  }
  byte rarray[Serial2.available()];
  i = 0;
  while (Serial2.peek() != -1) {
    rarray[i] = Serial2.read();
    i++;
  }
  for(int i=0; i<sizeof(rarray); i++){
    printHex(rarray[i]);
  }
  if (sizeof(rarray) < 5) {
    Serial.println("response is incomplete");
    return;
  }
}

void ambient_calibrate() {
  Serial.println("Ambient Calibrating");
  //disableSleepTimer();
  //enableSleepTimer();
  digitalWrite(18, HIGH);
  byte cal_ambient[11] = { 0x68, 0x10, 0x00, 0x01, 0x00, 0x01, 0x02, 0x7C, 0x06, 0xC5, 0x11 };
  /*uint16_t crc = 0xffff;
  for (int pos = 0; pos < 9; pos++) {
    crc ^= (UInt16)cal_ambient[pos];          // XOR byte into least sig. byte of crc
  
    for (int i = 8; i != 0; i--) {    // Loop over each bit
      if ((crc & 0x0001) != 0) {      // If the LSB is set
        crc >>= 1;                    // Shift right and XOR 0xA001
        crc ^= 0xA001;
      }
      else                            // Else LSB is not set
        crc >>= 1;                    // Just shift right
    }
  }
  cal_ambient[9] = ((crc >> 8) & 0xFF);
  cal_ambient[10] = (crc & 0xFF);
  */
  Serial2.write(cal_ambient, 11);
  delay(10);
  int timeout = 0;
  while (Serial2.available() <= 0) {
    delay(1);
    timeout++;
    if (timeout > 1000) {
      Serial.println("timed out");
      return;
    }
  }
  byte co2array[Serial2.available()];
  int i = 0;
  while (Serial2.peek() != -1) {
    co2array[i] = Serial2.read();
    i++;
  }
  for(i=0; i<sizeof(co2array); i++){
    printHex(co2array[i]);
  }
  if (sizeof(co2array) < 5) {
    Serial.println("response is incomplete");
    return;
  }
  digitalWrite(18, LOW);
}

void zero() {
  Serial.println("Zero Calibrating");
  //disableSleepTimer();
  //enableSleepTimer();
  digitalWrite(18, HIGH);
  byte cal_zero[] = { 0x68, 0x10, 0x00, 0x01, 0x00, 0x01, 0x02, 0x7C, 0x07, 0x04, 0xE2 };
  Serial2.write(cal_zero, 11);
  delay(10);
  int timeout = 0;
  while (Serial2.available() <= 0) {
    delay(1);
    timeout++;
    if (timeout > 1000) {
      Serial.println("timed out");
      return;
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
    return;
  }
  digitalWrite(18, LOW);
}

void span_calibrate() {
  Serial.println("Enter your target calibration: ");
  while (Serial.available() <= 0);
  delay(100);
  long span_target = Serial.parseInt();
  Serial.println("span target: ");
  Serial.print(span_target);
  Serial.println("");
  Serial.println("Calibrating to target...");
  digitalWrite(18, HIGH);
  int timeout = 0;
  byte tar_lo = (span_target & 0xff);
  byte tar_hi = ((span_target >> 8) & 0xff);
  Serial.println("Hi byte:");
  Serial.print(tar_hi);
  Serial.println("");
  Serial.println("Lo byte:");
  Serial.print(tar_lo);
  Serial.println("");
  byte cal_target[] = { 0x68, 0x10, 0x00, 0x02, 0x00, 0x01, 0x02, tar_hi, tar_lo, 0x00, 0x00 };
  uint16_t crc = 0xffff;
  for (int pos = 0; pos < 9; pos++) {
    crc ^= (uint16_t)cal_target[pos];          // XOR byte into least sig. byte of crc
  
    for (int i = 8; i != 0; i--) {    // Loop over each bit
      if ((crc & 0x0001) != 0) {      // If the LSB is set
        crc >>= 1;                    // Shift right and XOR 0xA001
        crc ^= 0xA001;
      }
      else                            // Else LSB is not set
        crc >>= 1;                    // Just shift right
    }
  }
  cal_target[10] = ((crc >> 8) & 0xFF);
  cal_target[9] = (crc & 0xFF);

  for(int i=0; i<sizeof(cal_target); i++){
    printHex(cal_target[i]);
  }
  
  Serial2.write(cal_target, 11);
  delay(10);
  timeout = 0;
  while (Serial2.available() <= 0) {
    delay(1);
    timeout++;
    if (timeout > 1000) {
      Serial.println("timed out");
      return;
    }
  }
  byte co2array[Serial2.available()];
  int i = 0;
  while (Serial2.peek() != -1) {
    co2array[i] = Serial2.read();
    i++;
  }
  for(int i=0; i<sizeof(co2array); i++){
    printHex(co2array[i]);
  }
  if (sizeof(co2array) < 5) {
    Serial.println("response is incomplete");
    return;
  }

  byte cal_span[] = { 0x68, 0x10, 0x00, 0x01, 0x00, 0x01, 0x02, 0x7C, 0x05, 0x85, 0x10 };
  Serial2.write(cal_span, 11);
  delay(10);
  timeout = 0;
  while (Serial2.available() <= 0) {
    delay(1);
    timeout++;
    if (timeout > 1000) {
      Serial.println("timed out");
      return;
    }
  }
  byte rarray[Serial2.available()];
  i = 0;
  while (Serial2.peek() != -1) {
    rarray[i] = Serial2.read();
    i++;
  }
  for(int i=0; i<sizeof(rarray); i++){
    printHex(rarray[i]);
  }
  if (sizeof(rarray) < 5) {
    Serial.println("response is incomplete");
    return;
  }
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
    //adp.enableLDO(2,0);
    int co2 = getco2();
    //adp.enableLDO(2,0);
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
    byte start_msmt[] = { 0x68, 0x10, 0x00, 0x09, 0x00, 0x01, 0x02, 0x00, 0x01, 0xA5, 0x5B };
    Serial2.write(start_msmt, 11);
    //Delay 50ms to give the sensor to set nRDY high
    delay(50);
    Serial2.flush();
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
    //adp.enableLDO(2,1);
    int timeout = 0;
    while (ready_pin.pressed == false) {
      delay(1);
      timeout++;
      if (timeout > 5000) {
        Serial.println("nready timed out at 5 sec");
        //adp.enableLDO(2, 0);
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
    //adp.enableLDO(2,0);
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
    ambient_calibrate();
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
  Serial.println("Command Options:\r\n  c -> ambient calibrate\r\n  z -> zero calibrate\r\n  x -> span calibration\r\n  d -> data dump\r\n  s -> sleep\r\n  b -> check battery level\r\n  r -> delete data\r\n");
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
  // With S11 we leave the power always on
  adp.enableLDO(2, 1);
  adp.setCharger(1);
  adp.enableFuelGauge(1);
  startTimeMicros = micros();
  delay(1000);
  // Reducing the CPU Frequency is better for power consumption, helping to extend battery life
  setCpuFrequencyMhz(80);

  // Serial for debugging
  Serial.begin(115200);
  Serial.println("starting datalog application");

  delay(35);
  // Setting up the LED
  pinMode(18, OUTPUT);
  digitalWrite(18, HIGH);
  delay(10);
  // Setting up the Wakeup Pin
  pinMode(WAKEUP_PIN, INPUT);
  attachInterrupt(WAKEUP_PIN, wakeup_isr, FALLING);

  //Setup and Set EN
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);
  delay(35);
  Serial2.begin(9600, SERIAL_8N1, 14, 13);
  
  EEPROM.begin(12);
  char first_start = EEPROM.read(0);
  byte rd_sr_mode[] = { 0x68, 0x03, 0x00, 0x0A, 0x00, 0x01, 0xDC, 0xF3 };
  Serial2.write(rd_sr_mode, 8);
  delay(10);
  int timeout = 0;
  while (Serial2.available() <= 0) {
    delay(1);
    timeout++;
    if (timeout > 1000) {
      Serial.println("timed out");
      break;
    }
  }
  byte co2array[Serial2.available()];
  int i = 0;
  while (Serial2.peek() != -1) {
    co2array[i] = Serial2.read();
    i++;
  }
  for(i=0; i<sizeof(co2array); i++){
    printHex(co2array[i]);
  }
  
  byte sr_mode[] = { 0x68, 0x10, 0x00, 0x0A, 0x00, 0x01, 0x02, 0x00, 0x01, 0xA5, 0x68 };
  Serial2.write(sr_mode, 11);
  Serial2.flush();
  delay(10);

  byte rst_mode[] = { 0x68, 0x10, 0x00, 0x01, 0x00, 0x01, 0x02, 0x00, 0xFF, 0x25, 0x93 };
  Serial2.write(sr_mode, 11);
  Serial2.flush();
  delay(150);
  //first_start = 'N';
  if (first_start != 'S') {
    Serial.print("First time running..");
    //Read Sensor State Data
    save_hr_to_eeprom();
    
    EEPROM.write(0,(byte)'S');
    EEPROM.commit();
  }
  
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
        ambient_calibrate();
        break;
      case 'z':
        zero();
        break;
      case 'x':
        span_calibrate();
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
