#include <WiFi.h>
#include <HTTPClient.h>
#include <LiquidCrystal_I2C.h>

// =========================
// CONFIGURATION
// =========================
const char* ssid = "surya";
const char* password = "00000000";

String writeAPIKey = " J2M22MLCQ43RTUIL";
String readAPIKey  = "XYJ2ZGNISSF6CVRF";
String channelID   = "3126135";

const int soilPin = 34;
const int relayPin = 26;

// LCD setup
LiquidCrystal_I2C lcd(0x27, 16, 2);

// =========================
// FUNCTION TO GET AI PREDICTION
// =========================
int getAIPrediction() {
  if (WiFi.status() != WL_CONNECTED) return 0;

  HTTPClient http;
  String url = "https://api.thingspeak.com/channels/" + channelID + "/fields/5/last.txt?api_key=" + readAPIKey;

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    int aiDecision = payload.toInt();
    Serial.print("AI Prediction: ");
    Serial.println(aiDecision);
    http.end();
    return aiDecision;
  } else {
    Serial.println("Failed to fetch AI prediction");
    http.end();
    return 0;
  }
}

// =========================
// FUNCTION TO SEND DATA TO THINGSPEAK
// =========================
void sendToThingSpeak(float soilPercent, float temperature, float humidity) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = "http://api.thingspeak.com/update?api_key=J2M22MLCQ43RTUIL" + writeAPIKey +
               "&field1=" + String(soilPercent) +
               "&field2=" + String(temperature) +
               "&field3=" + String(humidity);

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    Serial.println("Data sent to ThingSpeak ✅");
  } else {
    Serial.println("Failed to send data ❌");
  }
  http.end();
}

// =========================
// SETUP
// =========================
void setup() {
  Serial.begin(115200);
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH); // pump OFF

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  lcd.clear();
  lcd.print("WiFi connected");
  delay(1000);
}

// =========================
// MAIN LOOP
// =========================
void loop() {
  // --- 1. Read soil sensor ---
  int rawValue = analogRead(soilPin);
  float soilPercent = map(rawValue, 4095, 0, 0, 100); // calibrate for your sensor
  Serial.print("Soil Moisture: ");
  Serial.print(soilPercent);
  Serial.println("%");

  // --- 2. Dummy weather data (can link to API if needed) ---
  float temperature = 28.5;  // replace with actual data if using API
  float humidity = 65.0;

  // --- 3. Send to ThingSpeak ---
  sendToThingSpeak(soilPercent, temperature, humidity);

  // --- 4. Get AI prediction ---
  int aiDecision = getAIPrediction();

  bool shouldWater = false;
  if (aiDecision == 1) {
    Serial.println("AI: Suggests watering");
    shouldWater = true;
  } else if (aiDecision == 0) {
    Serial.println("AI: Suggests skip watering");
    shouldWater = false;
  } else {
    Serial.println("AI: No valid data");
  }

  // --- 5. Control relay ---
  if (shouldWater) {
    digitalWrite(relayPin, LOW); // pump ON
    lcd.clear();
    lcd.print("Watering...💧");
    Serial.println("Pump: ON");
  } else {
    digitalWrite(relayPin, HIGH); // pump OFF
    lcd.clear();
    lcd.print("No watering 💧");
    Serial.println("Pump: OFF");
  }

  // --- 6. Wait before next update ---
  delay(20000);  // 20 seconds (ThingSpeak limit)
}

