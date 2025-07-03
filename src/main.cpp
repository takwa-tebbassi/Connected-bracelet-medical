
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "MAX30105.h"
#include "heartRate.h"  
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <Firebase.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); 

MAX30105 particleSensor;

//DS18B20 sensor
#define ONE_WIRE_BUS 15
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

float temperature = 0.0;
const byte RATE_SIZE = 4; // Averaging window for BPM
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute;
int beatAvg;
float spO2 = 0.0;
const long MIN_IR_VALUE = 90000;
float tempC = 0.0;

unsigned long lastPushTime = 0;
const long pushInterval = 60000; // every 60 seconds

#define API_KEY "your api key here"
#define DATABASE_URL "your firebase database url"

#define USER_EMAIL "user email here" //a user who's already in the database
#define USER_PASSWORD "user Password here" //your user password

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// WiFi credentials
const char* ssid = "";  // Replace with your WiFi SSID
const char* password = ""; // Replace with your WiFi password

float calculateSpO2Simple(long redValue, long irValue);
void sendVitals(float spo2, int beatAvg, float tempC);


void setup() {
  Serial.begin(115200);
  delay(1000);

  // Connect to Wi-Fi
Serial.println("Connecting to WiFi...");
WiFi.begin(ssid, password);
unsigned long startAttemptTime = millis();

while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
  delay(500);
  Serial.print(".");
}
if (WiFi.status() != WL_CONNECTED) {
  Serial.println("WiFi failed!");
}


config.api_key = API_KEY;
auth.user.email = USER_EMAIL;
auth.user.password = USER_PASSWORD;
config.database_url = DATABASE_URL;

Firebase.begin(&config, &auth);
Firebase.reconnectWiFi(true);

Serial.println("Connected to WiFi!");
Serial.print("IP address: ");
Serial.println(WiFi.localIP());  // Print the ESP32 IP address


  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED failed"));
    while (1);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Initializing...");
  display.display();

  // Sensor init
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30102 not found");
    display.setCursor(0, 10);
    display.print("Sensor error");
    display.display();
    while (1);
  }

  // Sensor configuration

  sensors.begin();

  //particleSensor.setup(0x1F, 8, 3, 100, 411, 4096);
  // LED brightness, sample average, LED mode, sample rate, pulse width, ADC range

  particleSensor.setup();                      
  particleSensor.setPulseAmplitudeRed(0x1F);   
  particleSensor.setPulseAmplitudeGreen(0); 
  // particleSensor.enableDIETEMPRDY(); 

  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Place your finger...");
  display.display();
}

void loop() {
  long irValue = particleSensor.getIR();
  long redValue = particleSensor.getRed();

  if (irValue < MIN_IR_VALUE) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Please place finger");
    display.display();
    return;
  }

  if (checkForBeat(irValue)) {  
    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute  = 60 / (delta / 1000.0);
    if (beatsPerMinute > 20 && beatsPerMinute < 255) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;

      beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++)
        beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }

    //sensor ds18b20
    sensors.requestTemperatures();
    float measuredTemp = sensors.getTempCByIndex(0);


    temperature = particleSensor.readTemperature(); // max30102 temperature
    spO2 = calculateSpO2Simple(redValue, irValue);
  }


  // Serial Monitor Output
  Serial.print("IR: ");  Serial.print(irValue);
  Serial.print(" | Red: ");  Serial.print(redValue);
  Serial.print(" | BPM: ");  Serial.print(beatAvg);
  Serial.print(" | Temp: ");  Serial.print(temperature, 1);
  Serial.print(" C | SpO2: ");  Serial.print(spO2, 1);  Serial.println(" %");
  Serial.print(" | Body Temp: "); Serial.println(tempC);

  // OLED Display
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("BPM: ");
  display.print(beatAvg);

  display.setCursor(0, 16);
  display.print("Temp: ");
  display.print(temperature, 1);
  display.print(" C");

  display.setCursor(0, 32);
  display.print("SpO2: ");
  display.print(spO2, 1);
  display.print(" %");

  display.setCursor(0, 48);
  display.print("urTemp: ");
  display.print(tempC, 1);
  display.print(" C");

  display.display();

  //send to firebase db
  //sendVitals(spO2, beatAvg, tempC);

  if (millis() - lastPushTime >= pushInterval) {
  lastPushTime = millis();
  sendVitals(spO2, beatAvg, tempC);
  }
}

float calculateSpO2Simple(long redValue, long irValue) {
  float ratio = (float)redValue / irValue;
  float spO2 = 110.0 - 25.0 * ratio;
  return constrain(spO2, 0.0, 100.0);}

  // sending to firebase realtime
void sendVitals(float spo2, int beatAvg, float tempC) {
String path = "/your path here"; // This is the path to the table where you can view the vital data in the real-time database (Firebase)
FirebaseJson json;
json.set("heart_rate", beatAvg
json.set("spo2", spo2);
json.set("temperature", tempC);
//Firebase.RTDB.pushJSON(&fbdo, path, &json)

if (Firebase.RTDB.setJSON(&fbdo, path, &json)) {
Serial.println("Vitals sent!");
} else {
Serial.println("Firebase push failed:");
Serial.println(fbdo.errorReason());
}

};