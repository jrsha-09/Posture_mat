#include <HX711_ADC.h>
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "time.h"

// ----------------- WiFi Config -----------------
const char* WIFI_SSID="adcd";//replace your wifi
const char* WIFI_PASSWORD="12345678";//replace your wifi passwords

// ----------------- Firebase Config ---------------
const char* FIREBASE_PROJECT_ID = "qwerty";
const char* FIREBASE_COLLECTION = "sensor_data";
const char* FIREBASE_API_KEY = "AIzaSyCalqmmAax1vUa-ZqEUXmt_GIO21gMtA";//create your firebase api

// ----------------- NTP Config -----------------
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 0;

// ----------------- Pin Config (SAFE) -----------------
const int dout1 = 13, sck1 = 14;
const int dout2 = 27, sck2 = 26;
const int dout3 = 25, sck3 = 33;
const int dout4 = 15, sck4 = 2;
const int rbutton = 4;
float sum;

const float THRESHOLD = 4.0;//adjust threshold according to requirement

HX711_ADC LoadCell1(dout1, sck1);
HX711_ADC LoadCell2(dout2, sck2);
HX711_ADC LoadCell3(dout3, sck3);
HX711_ADC LoadCell4(dout4, sck4);

LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClientSecure client;

String firestoreURL = "https://firestore.googleapis.com/v1/projects/" + 
                      String(FIREBASE_PROJECT_ID) + 
                      "/databases/(default)/documents/" + 
                      String(FIREBASE_COLLECTION) + 
                      "?key=" + String(FIREBASE_API_KEY);

static float val1 = 0, val2 = 0, val3 = 0, val4 = 0;
unsigned long lastFirebaseUpdate = 0;
const long firebaseInterval = 120000;
bool aboveThreshold = false;
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

void setup() {
  Serial.begin(115200);
  EEPROM.begin(64);
  pinMode(rbutton, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");

  // ✅ FIRST connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 400) {
    delay(500);
    Serial.print(".");
    lcd.setCursor(tries % 16, 1);
    lcd.print(".");
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Connected!");
    lcd.clear();
    lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
  } else {
    Serial.println("\n❌ WiFi FAILED!");
    lcd.clear();
    lcd.print("WiFi Fail!");
    while (true); // Stop here if no WiFi
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  client.setInsecure();

  delay(1000);
  lcd.clear();
  lcd.print("Init Load Cells");

  LoadCell1.begin(); LoadCell2.begin(); LoadCell3.begin(); LoadCell4.begin();
  LoadCell1.start(2000, true);
  LoadCell2.start(2000, true);
  LoadCell3.start(2000, true);
  LoadCell4.start(2000, true);

  if (LoadCell1.getTareTimeoutFlag() || LoadCell2.getTareTimeoutFlag() ||
      LoadCell3.getTareTimeoutFlag() || LoadCell4.getTareTimeoutFlag()) {
    lcd.clear(); lcd.print("Tare Timeout!");
    Serial.println("Tare timeout! Check wiring.");
    while (1);
  }

  LoadCell1.setCalFactor(-45.30464);//callibrate value might different for your load cell
  LoadCell2.setCalFactor(-55.33996);//callibrate value might different for your load cell
  LoadCell3.setCalFactor(55.40839);//callibrate value might different for your load cell
  LoadCell4.setCalFactor(-50.8697);//callibrate value might different for your load cell

  lcd.clear();
  lcd.print("Setup Complete");
  delay(1000);
}

bool checkThreshold(float v1, float v2, float v3, float v4) {
  return (abs(v1) > THRESHOLD || abs(v2) > THRESHOLD || abs(v3) > THRESHOLD || abs(v4) > THRESHOLD);
}

String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "1970-01-01T00:00:00Z";
  }
  char timeString[25];
  strftime(timeString, sizeof(timeString), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(timeString);
}

void sendToFirebase(float v1, float v2, float v3, float v4) {

  String timestamp = getFormattedTime();
  DynamicJsonDocument doc(1024);
  doc["fields"]["Right up"]["doubleValue"] = v1;
  doc["fields"]["Left up"]["doubleValue"] = v2;
  doc["fields"]["Right down"]["doubleValue"] = v3;
  doc["fields"]["Left down"]["doubleValue"] = v4;
  doc["fields"]["Sum"]["doubleValue"] = sum;
  doc["fields"]["timestamp"]["timestampValue"] = timestamp;

  String jsonStr;
  serializeJson(doc, jsonStr);

  if (client.connect("firestore.googleapis.com", 443)) {
    client.println("POST " + firestoreURL + " HTTP/1.1");
    client.println("Host: firestore.googleapis.com");
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(jsonStr.length());
    client.println(); client.println(jsonStr);

    while (client.connected()) {
      if (client.readStringUntil('\n') == "\r") break;
    }

    String response = client.readString();
    Serial.println("Firebase response: " + response.substring(0, 100));
    delay(1000);
  } else {
    Serial.println("Firebase send failed.");
    delay(1000);
  }
  client.stop();
}

void handleTareButton() {
  int reading = digitalRead(rbutton);
  if (reading != lastButtonState) lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > debounceDelay && reading == LOW) {
    LoadCell1.tareNoDelay();
    LoadCell2.tareNoDelay();
    LoadCell3.tareNoDelay();
    LoadCell4.tareNoDelay();
    lcd.clear(); lcd.print("Tare Done");
    delay(500);
    aboveThreshold = false;
  }
  lastButtonState = reading;
}

void displayReadings(float lc1, float lc2, float lc3, float lc4) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(lc1);
  lcd.setCursor(7, 0); lcd.print(lc2);
  lcd.setCursor(0, 1);  lcd.print(lc3);
  lcd.setCursor(7, 1);  lcd.print(lc4);
  lcd.setCursor(12,1); lcd.print(sum);
}

void loop() {
  static bool newDataReady = false;

  if (LoadCell1.update() && LoadCell2.update() && LoadCell3.update() && LoadCell4.update()) {
    newDataReady = true;
  }

  handleTareButton();

  if (newDataReady) {
    val1 = LoadCell1.getData()/1000;
    val2 = LoadCell2.getData()/1000;
    val3 = LoadCell3.getData()/1000;
    val4 = LoadCell4.getData()/1000;
    sum=val1+val2+val3+val4;
    Serial.printf("LC1: %.2f | LC2: %.2f | LC3: %.2f | LC4: %.2f |sum:%.2f\n", val1, val2, val3, val4,sum);

  
    displayReadings(val1, val2, val3, val4);
    
    bool currentAboveThreshold = checkThreshold(val1, val2, val3, val4);

    if (currentAboveThreshold) {
      if (!aboveThreshold || (millis() - lastFirebaseUpdate >= firebaseInterval)) {
        sendToFirebase(val1, val2, val3, val4);
        lastFirebaseUpdate = millis();
      }
      aboveThreshold = true;
    } else {
      aboveThreshold = false;
    }

    newDataReady = false;
  }

  delay(100);
} 