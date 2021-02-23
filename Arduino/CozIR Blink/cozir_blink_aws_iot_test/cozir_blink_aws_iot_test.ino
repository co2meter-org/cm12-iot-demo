#include <EEPROM.h>
#include <time.h>
#include "adp5350.h"
#include "certs.h"
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include "ESPAsyncWebServer.h"
#include <ESPmDNS.h>

#include <Adafruit_BME280.h>

// This include is for the AWS IOT library that we installed
// This include is for Wifi functionality
#include <WiFi.h>

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
#define TIME_TO_SLEEP  30 * 60        /* Time ESP32 will go to sleep (in seconds) */

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

// The name of the device. This MUST match up with the name defined in the AWS console
#define DEVICE_NAME "co2-sensor"

// The MQTTT endpoint for the device (unique for each AWS account but shared amongst devices within the account)
#define AWS_IOT_ENDPOINT "a1qphq4dovfqf6-ats.iot.us-east-2.amazonaws.com"

// The MQTT topic that this device should publish to
#define AWS_IOT_TOPIC "$aws/things/co2-sensor/shadow/update"
//#define AWS_IOT_TOPIC "my/topic"

// How many times we should attempt to connect to AWS
#define AWS_MAX_RECONNECT_TRIES 50

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(512);

hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
AsyncWebServer server(80);
bool sleepFlag = false;

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

void connectToAWS()
{
  // Configure WiFiClientSecure to use the AWS certificates we generated
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  // Connect to the MQTT broker on the AWS endpoint we defined earlier
  client.begin(AWS_IOT_ENDPOINT, 8883, net);

  // Try to connect to AWS and count how many times we retried.
  int retries = 0;
  Serial.print("Connecting to AWS IOT");

  while (!client.connect(DEVICE_NAME) && retries < AWS_MAX_RECONNECT_TRIES) {
    Serial.print(".");
    delay(100);
    retries++;
  }

  // Make sure that we did indeed successfully connect to the MQTT broker
  // If not we just end the function and wait for the next loop.
  if(!client.connected()){
    Serial.println(" Timeout!");
    return;
  }

  // If we land here, we have successfully connected to AWS!
  // And we can subscribe to topics and send messages.
  Serial.println("Connected!");
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


void updateShadow() {
  // Connecting to AWS
  connectToAWS();
  
  Serial.println("Sending data...");
  // Turning on the LED
  digitalWrite(18, HIGH);

  // Getting values
  float temperature = bme.readTemperature();
  float humidity = bme.readHumidity();
  float pressure = bme.readPressure();
  int co2 = getco2();
  uint16_t battery = adp.batteryVoltage();

  // We want to make sure we're getting a CO2 Value
  if (co2 == 0) {
    adp.enableLDO(2,0);
    delay(3000);
    
    return;
  }
  
  char rule_payload[512];
  time_t now = time(nullptr);
  time_t delete_time = now + (5 * 24 * 60 * 60);
  sprintf(rule_payload, "{\"state\":{\"reported\":{\"co2\":\" %d \", \"temperature\":\" %f \", \"humidity\":\" %f \", \"pressure\":\" %f \", \"battery\":\" %d \", \"name\":\" co2-sensor \", \"timestamp\":\" %d \", \"ttl\": \" %d \" }}}",co2,temperature,humidity,pressure,battery,now,delete_time);
  Serial.println(rule_payload);
  int err = client.publish(AWS_IOT_TOPIC, rule_payload);
  if (err == 0) {
    Serial.println("publish success");
  } else {
    Serial.println("publish fail");
    Serial.print(err);
    Serial.println("");
  }
  

  digitalWrite(18, LOW);
  goToSleep();
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

void enterServerMode() {
  Serial.println("Entering Server Mode for Measurements and Calibration");
  
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
  Serial.println("starting aws iot application");

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

  // Setting up the LED
  pinMode(18, OUTPUT);
  digitalWrite(18, LOW);

  // Enable Charging and Fuel Gauge so we can see how much battery is left
  adp.setCharger(1);
  adp.enableFuelGauge(1);

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
    case ESP_SLEEP_WAKEUP_EXT0 : enterServerMode(); break;
    case ESP_SLEEP_WAKEUP_EXT1 : enterServerMode(); break;
    case ESP_SLEEP_WAKEUP_TIMER : updateShadow(); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : updateShadow(); break;
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  if (sleepFlag) {
    sleepFlag = false;
    goToSleep();
  }
}
