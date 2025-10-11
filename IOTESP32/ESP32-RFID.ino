#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "time.h"

// I2C LCD 設定
#define I2C_SDA 32
#define I2C_SCL 33
LiquidCrystal_I2C lcd(0x3F, 16, 2);  // 若無法顯示可改為 0x27

// RC522 感應器
#define SS_PIN_1   21  // 進門
#define RST_PIN_1  22
#define SS_PIN_2   26  // 出門
#define RST_PIN_2  25
MFRC522 mfrc522_1(SS_PIN_1, RST_PIN_1);
MFRC522 mfrc522_2(SS_PIN_2, RST_PIN_2);

// WiFi
const char* ssid     = "A";
const char* password = "ieug4362";

// Webhooks
const char* mysqlUrl   = "http://192.168.12.19:3000/upload";
const char* sheetUrl   = "";

// LINE BOT 設定
const char* lineBotToken = "";
const char* lineUserId   = "";

// NTP 時間
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 8 * 3600;
const int   daylightOffset_sec = 0;

unsigned long lastUpdate = 0;

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi connected.");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init(); lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("RFID Init...");

  SPI.begin();
  mfrc522_1.PCD_Init();
  mfrc522_2.PCD_Init();

  delay(1000);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Scan your card");
  lcd.setCursor(0, 1); lcd.print("Time: " + getTime());
}

void loop() {
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    lcd.setCursor(0, 1);
    lcd.print("Time: " + getTime() + " ");
  }

  if (mfrc522_1.PICC_IsNewCardPresent() && mfrc522_1.PICC_ReadCardSerial()) {
    handleCard(mfrc522_1, "IN");
    mfrc522_1.PICC_HaltA();
  }

  if (mfrc522_2.PICC_IsNewCardPresent() && mfrc522_2.PICC_ReadCardSerial()) {
    handleCard(mfrc522_2, "OUT");
    mfrc522_2.PICC_HaltA();
  }

  delay(100);
}

void handleCard(MFRC522 &reader, String direction) {
  String uid = "";
  for (byte i = 0; i < reader.uid.size; i++) {
    uid += (reader.uid.uidByte[i] < 0x10 ? "0" : "");
    uid += String(reader.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();

  String timeStr = getTime();

  Serial.println("[" + direction + "] UID: " + uid);
  Serial.println("Time: " + timeStr);

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(direction + ": " + uid.substring(0, 8));
  lcd.setCursor(0, 1); lcd.print("Time: " + timeStr);

  uploadData(uid, direction, timeStr);

  delay(3000);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Scan your card");
}

String getTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "No Time";
  char buffer[20];
  strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
  return String(buffer);
}

void uploadData(String uid, String direction, String timeStr) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected!");
    return;
  }

  String authorizedFlag = "0";  // 預設為未授權

  // ✅ 先上傳到 Google Sheets
  {
    HTTPClient sheetHttp;
    String fullSheetUrl = String(sheetUrl) +
                          "?value1=" + uid +
                          "&value2=" + direction +
                          "&value3=ESP32" +
                          "&value4=" + timeStr +
                          "&value5=" + authorizedFlag;  // 預設未授權

    sheetHttp.begin(fullSheetUrl);
    int sheetResponse = sheetHttp.GET();
    if (sheetResponse > 0) {
      Serial.println("✅ Google Sheets OK");
    } else {
      Serial.println("❌ Sheets Failed: " + String(sheetResponse));
    }
    sheetHttp.end();
  }

  // ✅ 再傳給 MySQL
  {
    WiFiClient client;
    HTTPClient mysqlHttp;
    mysqlHttp.begin(client, mysqlUrl);
    mysqlHttp.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String postData = "value1=" + uid +
                      "&value2=" + direction +
                      "&value3=ESP32" +
                      "&value4=" + timeStr;

    int mysqlResponse = mysqlHttp.POST(postData);
    String payload = "";

    if (mysqlResponse > 0) {
      payload = mysqlHttp.getString();
      Serial.println("MySQL Response: " + payload);

      if (payload.indexOf("\"authorized\":false") != -1) {
        authorizedFlag = "0";
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("Unauthorized");
        delay(3000);
        sendLineBotWarning(uid, timeStr);  // 警告
      } else {
        authorizedFlag = "1";
        Serial.println("✅ MySQL OK");
      }

    } else {
      Serial.println("❌ MySQL Failed: " + String(mysqlResponse));
    }

    mysqlHttp.end();
  }
}



void sendLineBotWarning(String uid, String timeStr) {
  WiFiClientSecure secureClient;
  secureClient.setInsecure();  // 忽略 SSL 憑證驗證

  HTTPClient http;
  http.begin(secureClient, "");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(lineBotToken));

  String jsonPayload = "{"
    "\"to\": \"" + String(lineUserId) + "\","
    "\"messages\":["
      "{"
        "\"type\":\"text\","
        "\"text\":\"🚨 警告：未授權卡片刷卡\\nUID: " + uid + "\\n時間: " + timeStr + "\""
      "}"
    "]"
  "}";

  int httpCode = http.POST(jsonPayload);
  Serial.println("[LINE] HTTP Code: " + String(httpCode));

  if (httpCode > 0) {
    Serial.println("[LINE] Response: " + http.getString());
  } else {
    Serial.println("[LINE] Failed: " + http.errorToString(httpCode));
  }

  http.end();
}

