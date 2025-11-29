#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <SPI.h>
#include <SD.h>
#include "time.h"

// LCD I2C 16x2
LiquidCrystal_I2C lcd(0x27, 16, 2);

// RTC
RTC_DS3231 rtc;
bool rtcOk = true;

// SD card
File myFile;
const int SD_CS = 5;
bool sdOK = false;

// WiFi & Firebase
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define API_KEY "AIzaSyDnf-WMvqFhibhCYCIAqFRFtb_a5LqxLBg"
#define DATABASE_URL "https://fir-demo-esp32-ec28b-default-rtdb.firebaseio.com/"
#define USER_EMAIL ""
#define USER_PASSWORD ""

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Biến thời gian ghi Firebase & SD
unsigned long lastSend = 0;

// Biến cảm biến mô phỏng
float temp1 = 0, sal1 = 0; // Sensor1 - cửa sông
float temp2 = 0, sal2 = 0; // Sensor2 - ao nuôi

// Task handle
TaskHandle_t rtcDisplayTaskHandle;

// Ghi SD
void WriteFile(const char * path, const char * message) {
  if (!sdOK) return;
  myFile = SD.open(path, FILE_APPEND);
  if (myFile) {
    myFile.println(message);
    myFile.close();
    Serial.println("Đã ghi vào SD: " + String(message));
  } else {
    Serial.println("Không ghi được vào SD.");
  }
}

// Task hiển thị thời gian RTC lên LCD mỗi giây
void displayTimeTask(void *parameter) {
  while (true) {
    DateTime now = rtcOk ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);

    char timeStr[17];
    sprintf(timeStr, "Time: %02d:%02d:%02d", now.hour(), now.minute(), now.second());

    char dateStr[17];
    sprintf(dateStr, "Date: %02d/%02d/%04d", now.day(), now.month(), now.year());

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(timeStr);
    lcd.setCursor(0, 1);
    lcd.print(dateStr);

    vTaskDelay(1000 / portTICK_PERIOD_MS); // delay 1 giây
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Khoi dong...");

  // RTC
  if (!rtc.begin()) {
    rtcOk = false;
  } else if (rtc.lostPower()) {
    rtc.adjust(DateTime(2025, 7, 12, 9, 26, 30));
  }

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long wifiTimeout = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiTimeout < 10000) {
    delay(500);
    Serial.print(".");
  }
  bool wifiOK = (WiFi.status() == WL_CONNECTED);
  Serial.println(wifiOK ? "\nWiFi da ket noi" : "\nKhong ket noi WiFi");

  // NTP
  configTime(7 * 3600, 0, "pool.ntp.org");

  // Firebase
  if (wifiOK) {
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    Firebase.begin(&config, &auth);
    Firebase.reconnectNetwork(true);
  }

  // SD Card
  if (SD.begin(SD_CS)) {
    sdOK = true;
    WriteFile("/data.txt", "RTC_Time, Sal1(ppt), Temp1(C), Sal2(ppt), Temp2(C)");
    Serial.println("SD card da san sang.");
  } else {
    Serial.println("Khong the khoi tao the nho SD.");
  }

  // Tạo task hiển thị thời gian
  xTaskCreatePinnedToCore(
    displayTimeTask,       // function
    "RTC Display Task",    // tên task
    2048,                  // stack size
    NULL,                  // param
    1,                     // priority
    &rtcDisplayTaskHandle, // handle
    1                      // core 1
  );

  Serial.println("Setup hoan tat.");
}

void loop() {
  if (millis() - lastSend >= 60000 || lastSend == 0) {
    lastSend = millis();

    // Dữ liệu mô phỏng sensor1 (cửa sông)
    temp1 = random(250, 350) / 10.0;
    sal1 = random(150, 350) / 10.0;

    // Dữ liệu mô phỏng sensor2 (ao nuôi)
    temp2 = random(250, 350) / 10.0;
    sal2 = random(150, 350) / 10.0;

    // Thời gian RTC để ghi SD
    DateTime nowRTC = rtcOk ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
    char rtcFormatted[25];
    sprintf(rtcFormatted, "%04d-%02d-%02d %02d:%02d:%02d",
            nowRTC.year(), nowRTC.month(), nowRTC.day(),
            nowRTC.hour(), nowRTC.minute(), nowRTC.second());

    // Thời gian epoch để gửi Firebase
    time_t nowEpoch;
    time(&nowEpoch);
    String timestampStr = String(nowEpoch);

    if (WiFi.status() == WL_CONNECTED && Firebase.ready()) {
      // Sensor 1 - cửa sông
      String path1 = "/He_thong_canh_bao/sensors/sensor1/history/" + timestampStr;
      Firebase.setFloat(fbdo, path1 + "/temperature", temp1);
      Firebase.setFloat(fbdo, path1 + "/salinity", sal1);
      Firebase.setString(fbdo, path1 + "/formatted_time", rtcFormatted);

      // Sensor 2 - ao nuôi
      String path2 = "/He_thong_canh_bao/sensors/sensor2/history/" + timestampStr;
      Firebase.setFloat(fbdo, path2 + "/temperature", temp2);
      Firebase.setFloat(fbdo, path2 + "/salinity", sal2);
      Firebase.setString(fbdo, path2 + "/formatted_time", rtcFormatted);

      Serial.println("Đã gửi dữ liệu sensor1 & sensor2 lên Firebase.");
    } else {
      Serial.println("Không gửi được Firebase (mất WiFi).");
    }

    // Ghi dữ liệu vào SD
    String line = String(rtcFormatted) + ", " +
                  String(sal1, 2) + ", " + String(temp1, 1) + ", " +
                  String(sal2, 2) + ", " + String(temp2, 1);
    WriteFile("/data.txt", line.c_str());
  }

  delay(10); // giữ loop nhẹ
}
