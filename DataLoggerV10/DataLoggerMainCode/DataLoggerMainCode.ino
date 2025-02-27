#define BLYNK_TEMPLATE_ID "TMPL3BfrlNlee"
#define BLYNK_TEMPLATE_NAME "Data Logger Project"
#define BLYNK_AUTH_TOKEN "HycRF3AL_2WJchL5CD5PwkhQ00BwCX7o"

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <SD.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <BlynkSimpleEsp32.h>
#include <TinyGPS++.h>

// Pin Definitions
#define DHTPIN1 4
#define DHTPIN2 15
#define SPEED_SENSOR_PIN 2
#define SD_CS 5
#define COLLISION_SENSOR_PIN 12
#define TILT_SENSOR_PIN 14
#define DHTTYPE DHT11

// Constants for Speed Calculation
#define PULSES_PER_REVOLUTION 20
#define WHEEL_DIAMETER_CM 3
#define WHEEL_DIAMETER_METERS (WHEEL_DIAMETER_CM / 100.0)
#define WHEEL_CIRCUMFERENCE_METERS (PI * WHEEL_DIAMETER_METERS)

// DHT Sensors
DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);

// SD Card
const int chipSelect = SD_CS;

// Wi-Fi and Google Sheets URL
const char* ssid = "Virus";
const char* password = "Devendra@9625";
const char* googleSheetsURL = "https://script.google.com/macros/s/AKfycbzfTydkFkVWMBPuL8ZNhb26Vm1FM4L7lRzxSnfQI1ZfJEZOXjWqqULbY2f_x03RgoMfAw/exec";

// Blynk
BlynkTimer timer;

// I2C LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Tilt Sensor
bool accidentDetected = false;
bool switchStatePrevious = HIGH;

// Timing control
unsigned long previousMillis = 0;
const long interval = 5000;

// Speed Sensor
volatile int pulseCount = 0;
unsigned long lastPulseTime = 0;
float speedRPM = 0.0;
float speedKMH = 0.0;

// Collision sensor state
int collisionState = HIGH;

// GPS and A9G Module
TinyGPSPlus gps;
bool gpsAvailable = false;
float latitude = 0.0;
float longitude = 0.0;

// Function prototypes
void sendSMS(String message);
void IRAM_ATTR pulseCounter();

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  delay(2000);
  Serial2.println("AT+CMGF=1");
  delay(1000);

  lcd.begin();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Data Logger");
  lcd.setCursor(0, 1);
  lcd.print("Project");
  delay(3000);
  lcd.clear();

  dht1.begin();
  dht2.begin();

  if (!SD.begin(chipSelect)) {
    Serial.println("SD card initialization failed!");
    lcd.setCursor(0, 0);
    lcd.print("SD Init Failed!");
    return;
  }
  Serial.println("SD card initialized.");
  lcd.setCursor(0, 0);
  lcd.print("SD Initialized");
  delay(2000);
  lcd.clear();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi.");
  lcd.setCursor(0, 0);
  lcd.print("Wi-Fi Connected");
  delay(2000);
  lcd.clear();

  pinMode(COLLISION_SENSOR_PIN, INPUT);
  pinMode(TILT_SENSOR_PIN, INPUT);

  pinMode(SPEED_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SPEED_SENSOR_PIN), pulseCounter, FALLING);

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);
}

void loop() {
  unsigned long currentMillis = millis();

  while (Serial2.available()) {
    if (gps.encode(Serial2.read())) {
      if (gps.location.isValid()) {
        latitude = gps.location.lat();
        longitude = gps.location.lng();
        gpsAvailable = true;
      } else {
        gpsAvailable = false;
      }
    }
  }

  float ambientTemperature = dht1.readTemperature();
  float motorTemperature = dht2.readTemperature();

  if (isnan(ambientTemperature) || isnan(motorTemperature)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  collisionState = digitalRead(COLLISION_SENSOR_PIN);
  int tiltState = digitalRead(TILT_SENSOR_PIN);

  if (tiltState == LOW && switchStatePrevious == HIGH) {
    accidentDetected = true;
    String messageToSend = "Accident Detected!\n";
    if (gpsAvailable) {
      messageToSend += "Lat: " + String(latitude, 6) + ", Lon: " + String(longitude, 6);
    } else {
      messageToSend += "Location not available";
    }
    sendSMS(messageToSend);
  }

  if (collisionState == LOW) {
    String collisionMessage = "Collision Detected!\n";
    if (gpsAvailable) {
      collisionMessage += "Lat: " + String(latitude, 6) + ", Lon: " + String(longitude, 6);
    } else {
      collisionMessage += "Location not available";
    }
    sendSMS(collisionMessage);
  }

  switchStatePrevious = tiltState;

  if (currentMillis - previousMillis >= interval) {
    noInterrupts();
    int pulses = pulseCount;
    pulseCount = 0;
    interrupts();

    speedRPM = (pulses / (float)PULSES_PER_REVOLUTION) * 60.0;
    speedKMH = (speedRPM * WHEEL_CIRCUMFERENCE_METERS * 60) / 1000;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Bat Temp: " + String(ambientTemperature) + "C");
    delay(2000);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Accident: " + String(accidentDetected ? "Yes" : "No"));
    lcd.setCursor(0, 1);
    lcd.print("Speed: " + String(speedKMH) + " km/h");
    delay(2000);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Motor: " + String(motorTemperature) + "C");
    delay(2000);

    logData(ambientTemperature, motorTemperature, accidentDetected, collisionState, speedKMH);

    Blynk.virtualWrite(V0, ambientTemperature);
    Blynk.virtualWrite(V1, motorTemperature);
    Blynk.virtualWrite(V2, speedKMH);

    // Reset accidentDetected after displaying
    accidentDetected = false;

    previousMillis = currentMillis;
  }

  Blynk.run();
}

void logData(float ambientTemp, float motorTemp, bool accident, int collision, float speed) {
  File dataFile = SD.open("datalog.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.print("Bat Temp: ");
    dataFile.print(ambientTemp);
    dataFile.print(", Motor Temp: ");
    dataFile.print(motorTemp);
    dataFile.print(", Accident: ");
    dataFile.print(accident);
    dataFile.print(", Collision: ");
    dataFile.print(collision);
    dataFile.print(", Speed: ");
    dataFile.print(speed);
    dataFile.println(" km/h");
    dataFile.close();
  } else {
    Serial.println("Error opening datalog.txt");
  }

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String urlWithParams = googleSheetsURL;
    urlWithParams += "?temp=" + String(ambientTemp) + "&motorTemp=" + String(motorTemp);
    urlWithParams += "&accident=" + String(accident) + "&collision=" + String(collision);
    urlWithParams += "&speed=" + String(speed);
    
    http.begin(urlWithParams);
    int httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println(httpResponseCode);
      Serial.println(response);
    } else {
      Serial.print("Error on HTTP request: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("Error in Wi-Fi connection");
  }
}

void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

void sendSMS(String message) {
  Serial2.println("AT+CMGS=\"+919604263692\"");
  delay(1000);
  Serial2.println(message);
  delay(100);
  Serial2.write(26);
  delay(1000);
}
