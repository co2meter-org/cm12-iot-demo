#include <EEPROM.h>

#include <Adafruit_BME280.h>

// This include is for the AWS IOT library that we installed
#include <AWS_IOT.h>
// This include is for Wifi functionality
#include <WiFi.h>

// Declare an instance of the AWS IOT library
AWS_IOT hornbill;

// Wifi credentials
char WIFI_SSID[]="Your WiFi SSID Here";
char WIFI_PASSWORD[]="Your WiFi Password Here";

// Thing details
char HOST_ADDRESS[]="your-custom-endpoint.amazonaws.com";
char CLIENT_ID[]= "co2-sensor";
char TOPIC_NAME[]= "$aws/things/co2-sensor/shadow/update";
char RULE_TOPIC_NAME[] = "iot/topic";

// Connection status
int status = WL_IDLE_STATUS;
// Payload array to store thing shadow JSON document
char payload[512];
// Counter for iteration
int counter = 0;

#define BME_SCL 4
#define BME_SDA 19
TwoWire I2CBME = TwoWire(0);
Adafruit_BME280 bme;

#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  3600        /* Time ESP32 will go to sleep (in seconds) */

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
  if(hornbill.connect(HOST_ADDRESS,CLIENT_ID)== 0) {
    Serial.println("Connected to AWS, bru");
    delay(1000);
  }
  else {
    Serial.println("AWS connection failed, Check the HOST Address");
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    Serial.println("Going to sleep now");
    Serial.flush(); 
    esp_deep_sleep_start();
  }
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
  
  I2CBME.begin(BME_SDA,BME_SCL,100000);
  unsigned status;
  status = bme.begin(0x76,&I2CBME);
  if (!status) {
    /*Serial.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
    Serial.print("SensorID was: 0x"); Serial.println(bme.sensorID(),16);
    Serial.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
    Serial.print("   ID of 0x56-0x58 represents a BMP 280,\n");
    Serial.print("        ID of 0x60 represents a BME 280.\n");
    Serial.print("        ID of 0x61 represents a BME 680.\n");
    */
    //Deep Sleep
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    //Serial.println("Going to sleep now");
    //Serial.flush(); 
    esp_deep_sleep_start();
  }
  pinMode(16,INPUT);
  while (digitalRead(16) == 1) {}
}

void loop() {
  // put your main code here, to run repeatedly:
  Serial.println("Sending data...");

  // Uploads new telemetry to ThingsBoard using MQTT. 
  // See https://thingsboard.io/docs/reference/mqtt-api/#telemetry-upload-api 
  // for more details

  //TempAndHumidity lastValues = dht.getTempAndHumidity();
  float temperature = bme.readTemperature();
  float humidity = bme.readHumidity();
  float pressure = bme.readPressure();
  int co2 = getco2();
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT sensor!");
  } else {
    Serial.print("temperature: ");
    Serial.print(temperature);
    Serial.print("\n");
    Serial.print("humidity: ");
    Serial.print(humidity);
    Serial.print("\n");
    Serial.print("pressure: ");
    Serial.print(pressure);
    Serial.print("\n");
    Serial.print("co2: ");
    Serial.print(co2);
    Serial.print("\n");
    
    sprintf(payload,"{\"state\":{\"reported\":{\"co2\":\" %d \", \"temperature\":\" %f \", \"humidity\":\" %f \", \"pressure\":\" %f \" }}}",co2,temperature,humidity,pressure);
    Serial.println(payload);
    if(hornbill.publish(TOPIC_NAME,payload) == 0) {
      Serial.println("Message published successfully");
    }
    else {
      Serial.println("Message was not published");
      counter++;
      if (counter < 10)
        return;
      counter = 0;
    }
  }


  pinMode(15, OUTPUT);
  digitalWrite(15, LOW);
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.println("Going to sleep now");
  Serial.flush(); 
  esp_deep_sleep_start();
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
