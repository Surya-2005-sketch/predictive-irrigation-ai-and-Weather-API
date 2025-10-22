/**************************************************************
* ESP32 Smart Predictive Irrigation System
* Features: WeatherAPI + Soil Moisture + Relay Pump + LCD + Blynk IoT App + ThingSpeak
* Author: Surya’s Project (2025)
**************************************************************/

#define BLYNK_TEMPLATE_ID "TMPL3Ctd1vG_x"
#define BLYNK_TEMPLATE_NAME "P Irrigation"
#define BLYNK_AUTH_TOKEN "m4WZ8Y46-7wzMtltyoSKCe33rf6bmmRZ"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>

// ===== User WiFi + WeatherAPI =====
const char* SSID = "surya";
const char* PASSWORD = "00000000";
const char* OWM_API_KEY = "6a8529d7bcf04eb2930114356251110";
const char* LATITUDE  = "18.115066";
const char* LONGITUDE = "83.393841";

// ===== ThingSpeak Setup =====
const char* THINGSPEAK_WRITE_API = "J2M22MLCQ43RTUIL"; // <- replace if different

// ===== Pin Configuration =====
#define SOIL_PIN 34
#define RELAY_PIN 26
#define SWITCH_PIN 25
const bool RELAY_ACTIVE_HIGH = true;

// ===== Soil Sensor Calibration =====
const int SOIL_WET_ADC = 1200;
const int SOIL_DRY_ADC = 3500;

// ===== Behaviour Constants =====
const int MOISTURE_TARGET = 55;
const int MOISTURE_HYSTERESIS = 3;
const float RAIN_PROB_THRESHOLD = 50.0;
const int WATERING_MAX_SECONDS = 120;
const int MIN_INTERVAL_BETWEEN_WATERINGS = 6 * 60 * 60; // 6 hrs
const float SECONDS_PER_PERCENT_DEFICIT = 2.0;
const unsigned long WEATHER_REFRESH_INTERVAL = 30 * 60UL * 1000UL; // 30 min

// ===== LCD Setup =====
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===== Global Variables =====
WiFiClient client;
unsigned long lastWeatherFetch = 0;
float nextRainProbability = 0.0;
float currentTemperature = 0.0;
String weatherCondition = "Unknown";
unsigned long lastWateringTime = 0;

// ===== Blynk Virtual Pins =====
// V1 = Soil Moisture (%)
// V2 = Temperature (°C)
// V3 = Rain Probability (%)
// V4 = Weather Condition
// V5 = Manual Pump Control Button

BLYNK_WRITE(V5) {
  int value = param.asInt();  // 1 = ON, 0 = OFF
  if (value == 1) {
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? HIGH : LOW);
    Serial.println("Blynk: Pump ON");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Blynk: Pump ON ");
  } else {
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    Serial.println("Blynk: Pump OFF");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Blynk: Pump OFF");
  }
}

// ===== Function Declarations =====
void fetchWeatherForecast();
void parseWeatherAPIPayload(const String& payload);
int readSoilPercent();
void pumpOn();
void pumpOff();
void uploadToThingSpeak(int soilPercent, float temperature, float rainProb, int pumpState);

// ===== Functions =====
void pumpOn() {
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? HIGH : LOW);
  Serial.println("Pump ON");
}

void pumpOff() {
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
  Serial.println("Pump OFF");
}

int readSoilPercent() {
  int adcValue = analogRead(SOIL_PIN);
  adcValue = constrain(adcValue, SOIL_WET_ADC, SOIL_DRY_ADC);
  int percent = map(adcValue, SOIL_DRY_ADC, SOIL_WET_ADC, 0, 100);
  return constrain(percent, 0, 100);
}

void fetchWeatherForecast() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Reconnecting WiFi...");
    WiFi.begin(SSID, PASSWORD);
    delay(1000);
    if (WiFi.status() != WL_CONNECTED) return;
  }

  String url = "http://api.weatherapi.com/v1/current.json?key=" + String(OWM_API_KEY) +
               "&q=" + String(LATITUDE) + "," + String(LONGITUDE);

  Serial.println("Fetching weather...");
  HTTPClient http;
  if (http.begin(client, url)) {
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      parseWeatherAPIPayload(payload);
    } else {
      Serial.printf("Weather API error: %d\n", httpCode);
      lcd.setCursor(0, 1);
      lcd.print("Weather fetch err");
    }
    http.end();
  } else {
    Serial.println("HTTP begin failed");
    lcd.setCursor(0, 1);
    lcd.print("HTTP begin fail");
  }
}

void parseWeatherAPIPayload(const String& payload) {
  StaticJsonDocument<10000> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.println("JSON parse error");
    currentTemperature = 0.0;
    nextRainProbability = 0.0;
    weatherCondition = "Unknown";
    lcd.setCursor(0, 1);
    lcd.print("JSON parse err ");
    return;
  }

  currentTemperature = doc["current"]["temp_c"] | 0.0f;
  weatherCondition = doc["current"]["condition"]["text"].as<String>();
  float precip_mm = doc["current"]["precip_mm"] | 0.0;
  nextRainProbability = (precip_mm > 0.5) ? 70.0 : 10.0;

  Serial.printf("Temp: %.1f°C, Condition: %s, Rain%%: %.1f\n",
                currentTemperature, weatherCondition.c_str(), nextRainProbability);
}

// ===== ThingSpeak Upload =====
void uploadToThingSpeak(int soilPercent, float temperature, float rainProb, int pumpState) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://api.thingspeak.com/update?api_key=" + String(THINGSPEAK_WRITE_API);
    url += "&field1=" + String(soilPercent);
    url += "&field2=" + String(temperature);
    url += "&field3=" + String(rainProb);
    url += "&field4=" + String(pumpState);
    // prefer begin with client
    if (http.begin(client, url)) {
      int httpCode = http.GET();
      if (httpCode > 0) Serial.println("✅ Data sent to ThingSpeak!");
      else Serial.println("❌ Failed to send data.");
      http.end();
    } else {
      Serial.println("❌ HTTP begin failed (ThingSpeak).");
    }
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  analogReadResolution(12);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Irrigation Init");

  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  lcd.setCursor(0, 1);
  lcd.print("WiFi Connected ");
  delay(1000);
  lcd.clear();

  Blynk.begin(BLYNK_AUTH_TOKEN, SSID, PASSWORD);
  lcd.setCursor(0, 0);
  lcd.print("System Ready");
  delay(1500);
}

// ===== Loop =====
void loop() {
  Blynk.run();

  bool manualSwitch = digitalRead(SWITCH_PIN) == LOW;
  if (manualSwitch) {
    Serial.println("Manual switch ON → Pump forced ON");
    pumpOn();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Manual Mode");
    lcd.setCursor(0, 1);
    lcd.print("Pump ON");
    delay(500);
    return;
  }

  // Weather fetch every 30 min
  if (millis() - lastWeatherFetch > WEATHER_REFRESH_INTERVAL || lastWeatherFetch == 0) {
    fetchWeatherForecast();
    lastWeatherFetch = millis();
  }

  int soilPercent = readSoilPercent();

  // Display info
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.printf("Soil:%d%% T:%.1fC", soilPercent, currentTemperature);
  lcd.setCursor(0, 1);
  String condShort = weatherCondition.length() > 10 ? weatherCondition.substring(0, 10) : weatherCondition;
  lcd.print(condShort + " ");
  lcd.printf("Rain:%.0f%%", nextRainProbability);

  Serial.printf("Soil:%d%%, Temp:%.1fC, Rain:%.1f%%, Cond:%s\n",
                soilPercent, currentTemperature, nextRainProbability, weatherCondition.c_str());

  bool shouldWater = false;

  if (soilPercent + MOISTURE_HYSTERESIS < MOISTURE_TARGET) {
    if (nextRainProbability >= RAIN_PROB_THRESHOLD) {
      Serial.println("Rain likely - skipping watering");
      lcd.setCursor(0, 1);
      lcd.print("Skipping water ");
    } else {
      unsigned long sinceLast = (millis() / 1000UL) - lastWateringTime;
      if (lastWateringTime != 0 && sinceLast < MIN_INTERVAL_BETWEEN_WATERINGS) {
        Serial.printf("Watered %lu s ago - wait\n", sinceLast);
        lcd.setCursor(0, 1);
        lcd.printf("Wait %lu min", (MIN_INTERVAL_BETWEEN_WATERINGS - sinceLast) / 60);
      } else shouldWater = true;
    }
  } else {
    Serial.println("Soil moisture OK - no watering");
    lcd.setCursor(0, 1);
    lcd.print("Soil OK        ");
  }

  if (shouldWater) {
    int deficit = MOISTURE_TARGET - soilPercent;
    if (deficit < 1) deficit = 1;
    int wateringTime = round(deficit * SECONDS_PER_PERCENT_DEFICIT);
    wateringTime = wateringTime > WATERING_MAX_SECONDS ? WATERING_MAX_SECONDS : wateringTime;

    Serial.printf("Watering for %d seconds\n", wateringTime);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Watering...");
    lcd.setCursor(0, 1);
    lcd.printf("Pump ON %ds", wateringTime);

    pumpOn();
    delay(wateringTime * 1000UL);
    pumpOff();

    lastWateringTime = millis() / 1000UL;
    Serial.println("Watering complete");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Watering done");
    lcd.setCursor(0, 1);
    lcd.print("Pump OFF      ");
    delay(2000);
  }

  // Send data to Blynk
  Blynk.virtualWrite(V1, soilPercent);
  Blynk.virtualWrite(V2, currentTemperature);
  Blynk.virtualWrite(V3, nextRainProbability);
  Blynk.virtualWrite(V4, weatherCondition);

  // Upload to ThingSpeak every 5 min
  static unsigned long lastUpload = 0;
  if (millis() - lastUpload > 300000) { // 5 minutes
    int pumpState = (digitalRead(RELAY_PIN) == (RELAY_ACTIVE_HIGH ? HIGH : LOW)) ? 1 : 0;
    uploadToThingSpeak(soilPercent, currentTemperature, nextRainProbability, pumpState);
    lastUpload = millis();
  }

  delay(60000);  // 1 min main loop
}




