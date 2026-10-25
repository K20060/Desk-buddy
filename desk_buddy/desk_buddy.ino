#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <RTClib.h>

#define DHT_PIN 4
#define TOUCH_PIN 27
#define BUZZER_PIN 26
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define DHT_TYPE DHT11

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHT dht(DHT_PIN, DHT_TYPE);
RTC_DS3231 rtc;

bool rtcReady = false;
bool timerRunning = false;
uint32_t timerRemainingSeconds = 300;
uint32_t timerLastTickMs = 0;
bool buttonWasDown = false;
bool touchArmed = false;
uint32_t touchReleasedSinceMs = 0;
uint32_t lastTouchAcceptedMs = 0;
uint32_t lastPageChangeMs = 0;
uint32_t lastDhtReadMs = 0;
uint32_t lastWeatherFetchMs = 0;
uint32_t lastWifiRetryMs = 0;
uint32_t lastClockDrawnSecond = UINT32_MAX;
uint32_t lastTimerDrawnSecond = UINT32_MAX;
uint8_t page = 0;
float indoorTemperature = NAN;
float indoorHumidity = NAN;
String weatherTemperature = "--";
String weatherDescription = "Waiting for WiFi";
bool weatherAvailable = false;
bool timerAlarmActive = false;
uint8_t timerAlarmBeepsRemaining = 0;
uint32_t timerAlarmNextMs = 0;
bool buzzerActive = false;
uint32_t buzzerStopMs = 0;

const char *WIFI_SSID = "Vijaya lakshmi";
const char *WIFI_PASSWORD = "Vijaya!1985";

const uint32_t PAGE_INTERVAL_MS = 4000;
const uint32_t DHT_INTERVAL_MS = 2500;
const uint32_t WEATHER_INTERVAL_MS = 15UL * 60UL * 1000UL;

void drawHeader(const char *title) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(title);
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);
}

void drawWifiStatus() {
  display.drawFastHLine(0, 54, SCREEN_WIDTH, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 56);
  if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi: connected");
  } else if (WiFi.status() == WL_IDLE_STATUS || WiFi.status() == WL_DISCONNECTED) {
    display.print("WiFi: reconnecting");
  } else {
    display.print("WiFi: connecting");
  }
}

void drawTimePage() {
  drawHeader("DESK BUDDY TIME (IST)");
  if (!rtcReady) {
    display.setCursor(0, 22);
    display.println("RTC not found");
  } else {
    DateTime now = rtc.now();
    char clockText[12];
    char dateText[18];
    snprintf(clockText, sizeof(clockText), "%02d:%02d", now.hour(), now.minute());
    snprintf(dateText, sizeof(dateText), "%02d-%02d-%04d", now.day(), now.month(), now.year());
    display.setTextSize(3);
    display.setCursor(0, 18);
    display.print(clockText);
    display.setTextSize(1);
    display.setCursor(25, 44);
    display.print(dateText);

    // Small day/night symbol in the free area at the right of the clock.
    // Sunrise is 06:00 and sunset is 18:00 local Indian time.
    if (now.hour() >= 6 && now.hour() < 18) {
      display.drawCircle(106, 30, 8, SSD1306_WHITE);
      display.drawFastHLine(94, 30, 5, SSD1306_WHITE);
      display.drawFastHLine(113, 30, 5, SSD1306_WHITE);
      display.drawFastVLine(106, 18, 5, SSD1306_WHITE);
      display.drawFastVLine(106, 37, 5, SSD1306_WHITE);
      display.drawLine(98, 22, 101, 25, SSD1306_WHITE);
      display.drawLine(111, 35, 114, 38, SSD1306_WHITE);
      display.drawLine(111, 25, 114, 22, SSD1306_WHITE);
      display.drawLine(98, 38, 101, 35, SSD1306_WHITE);
    } else {
      display.fillCircle(106, 30, 8, SSD1306_WHITE);
      display.fillCircle(110, 26, 8, SSD1306_BLACK);
      display.drawPixel(118, 20, SSD1306_WHITE);
      display.drawPixel(121, 35, SSD1306_WHITE);
      display.drawPixel(96, 19, SSD1306_WHITE);
    }
  }
  drawWifiStatus();
  display.display();
}

void drawThermometerIcon() {
  // Compact outline thermometer for the open right-hand area of the OLED.
  display.drawRoundRect(101, 17, 8, 21, 4, SSD1306_WHITE);
  display.fillCircle(105, 40, 5, SSD1306_WHITE);
  display.fillRect(103, 27, 4, 13, SSD1306_WHITE);
  display.drawFastHLine(112, 21, 5, SSD1306_WHITE);
  display.drawFastHLine(112, 27, 3, SSD1306_WHITE);
  display.drawFastHLine(112, 33, 5, SSD1306_WHITE);
}

void drawIndoorPage() {
  drawHeader("INDOOR CONDITIONS");
  display.setCursor(0, 18);
  if (isnan(indoorTemperature) || isnan(indoorHumidity)) {
    display.println("DHT11 error");
    display.setTextSize(1);
    display.setCursor(0, 48);
    display.println("Check sensor wires");
  } else {
    display.setTextSize(2);
    display.print(indoorTemperature, 0);
    display.println(" C");
    display.setTextSize(1);
    display.setCursor(0, 42);
    display.print(indoorHumidity, 0);
    display.println(" % RH");
    drawThermometerIcon();
  }
  drawWifiStatus();
  display.display();
}

void drawWeatherPage() {
  drawHeader("LOCAL WEATHER");
  display.setTextSize(2);
  display.setCursor(0, 18);
  display.print(weatherTemperature);
  display.println(" C");
  display.setTextSize(1);
  display.setCursor(0, 43);
  String label = weatherDescription;
  // Leave the right-hand area clear for the thermometer symbol.
  if (label.length() > 16) label = label.substring(0, 16);
  display.println(label);
  drawThermometerIcon();
  drawWifiStatus();
  display.display();
}

void drawTimerPage() {
  drawHeader("5 MINUTE TIMER");
  uint32_t minutes = timerRemainingSeconds / 60;
  uint32_t seconds = timerRemainingSeconds % 60;
  char timerText[8];
  snprintf(timerText, sizeof(timerText), "%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
  display.setTextSize(3);
  display.setCursor(17, 19);
  display.println(timerText);
  display.setTextSize(1);
  display.setCursor(5, 44);
  if (timerRunning) display.print("Running: touch to pause");
  else if (timerRemainingSeconds == 0) display.print("Finished: touch restart");
  else display.print("Paused: touch to start");
  drawWifiStatus();
  display.display();
}

void drawCurrentPage() {
  if (page == 0) drawTimePage();
  else if (page == 1) drawIndoorPage();
  else if (page == 2) drawWeatherPage();
  else drawTimerPage();
}

bool syncRtcFromNetwork() {
  // India Standard Time is UTC+5:30; the POSIX sign is intentionally reversed.
  configTzTime("IST-5:30", "pool.ntp.org", "time.nist.gov");
  struct tm timeInfo;
  for (uint8_t attempt = 0; attempt < 20; ++attempt) {
    if (getLocalTime(&timeInfo, 250)) {
      if (rtcReady) {
        rtc.adjust(DateTime(timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
                            timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec));
      }
      return true;
    }
    delay(250);
  }
  return false;
}

void fetchWeather() {
  lastWeatherFetchMs = millis();
  if (WiFi.status() != WL_CONNECTED) {
    weatherAvailable = false;
    weatherDescription = "Home WiFi not connected";
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  http.setUserAgent("DeskBuddyESP32/1.0");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  // This compact response avoids the large JSON document that can exceed ESP32 memory.
  if (!http.begin(client, "https://wttr.in/?format=%25t+%25C")) {
    weatherAvailable = false;
    weatherDescription = "Weather link failed";
    return;
  }

  int status = http.GET();
  if (status == HTTP_CODE_OK) {
    String response = http.getString();
    response.trim();
    int separator = response.indexOf(' ');
    if (separator > 0 && response.length() > separator + 1) {
      weatherTemperature = response.substring(0, separator);
      weatherTemperature.replace("+", "");
      weatherTemperature.replace("°C", "");
      weatherDescription = response.substring(separator + 1);
      weatherAvailable = true;
    } else {
      weatherAvailable = false;
      weatherDescription = "Weather data error";
    }
  } else if (status == 429) {
    weatherAvailable = false;
    weatherDescription = "Weather busy; retrying";
  } else {
    weatherAvailable = false;
    weatherDescription = "Weather error " + String(status);
  }
  http.end();
}

void maintainWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastWifiRetryMs < 30000) return;
  lastWifiRetryMs = millis();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  weatherDescription = "Reconnecting WiFi";
}

void startBuzzerTone(uint16_t frequency, uint16_t durationMs) {
  // Drive the passive piezo directly with ESP32 LEDC PWM on GPIO26.
  ledcWriteTone(BUZZER_PIN, frequency);
  buzzerActive = true;
  buzzerStopMs = millis() + durationMs;
}

void serviceBuzzer() {
  if (buzzerActive && (int32_t)(millis() - buzzerStopMs) >= 0) {
    ledcWriteTone(BUZZER_PIN, 0);
    buzzerActive = false;
  }
}

void handleTimerTouch() {
  // Do not accept a touch until the pad has first been released after power-up.
  // This prevents the TTP223's brief boot signal from starting the timer.
  bool isTouched = digitalRead(TOUCH_PIN) == HIGH;
  uint32_t now = millis();
  if (!touchArmed) {
    if (!isTouched) {
      if (touchReleasedSinceMs == 0) touchReleasedSinceMs = now;
      if (now - touchReleasedSinceMs >= 150) touchArmed = true;
    } else {
      touchReleasedSinceMs = 0;
    }
    return;
  }

  if (isTouched && !buttonWasDown && now - lastTouchAcceptedMs >= 250) {
    buttonWasDown = true;
    lastTouchAcceptedMs = now;
    if (timerRemainingSeconds == 0) timerRemainingSeconds = 300;
    timerRunning = !timerRunning;
    timerLastTickMs = now;
    startBuzzerTone(3200, 120);
    page = 3;
    lastPageChangeMs = now;
    lastTimerDrawnSecond = UINT32_MAX;
    drawTimerPage();
  }
  if (!isTouched) buttonWasDown = false;
}

void startTimerAlarm() {
  // Three short beeps make the finished timer easy to notice.
  timerAlarmActive = true;
  timerAlarmBeepsRemaining = 3;
  timerAlarmNextMs = millis();
}

void serviceTimerAlarm() {
  if (!timerAlarmActive || millis() < timerAlarmNextMs) return;
  if (timerAlarmBeepsRemaining == 0) {
    timerAlarmActive = false;
    return;
  }
  startBuzzerTone(4000, 300);
  --timerAlarmBeepsRemaining;
  timerAlarmNextMs = millis() + 450;
}

void updateTimer() {
  if (!timerRunning) return;
  uint32_t now = millis();
  uint32_t elapsedSeconds = (now - timerLastTickMs) / 1000;
  if (elapsedSeconds == 0) return;
  timerLastTickMs += elapsedSeconds * 1000;
  if (elapsedSeconds >= timerRemainingSeconds) {
    timerRemainingSeconds = 0;
    timerRunning = false;
    startTimerAlarm();
  } else {
    timerRemainingSeconds -= elapsedSeconds;
  }
}

void setup() {
  // 80 MHz and Wi-Fi modem sleep reduce USB power use while keeping the display live.
  setCpuFrequencyMhz(80);
  pinMode(TOUCH_PIN, INPUT);
  ledcAttach(BUZZER_PIN, 4000, 8);
  ledcWriteTone(BUZZER_PIN, 0);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  dht.begin();
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 18);
  display.println("Desk Buddy starting...");
  display.setCursor(0, 32);
  display.println("Joining home WiFi...");
  display.display();

  rtcReady = rtc.begin();
  if (rtcReady && rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t wifiStartMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartMs < 20000) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    weatherDescription = "Getting local weather";
    syncRtcFromNetwork();
  } else {
    lastWifiRetryMs = millis();
    weatherDescription = "Home WiFi not connected";
  }

  lastPageChangeMs = millis();
  fetchWeather();
  drawCurrentPage();
  // A one-time chirp confirms the piezo and its GPIO26 wire after every restart.
  startBuzzerTone(3000, 700);
}

void loop() {
  uint32_t now = millis();
  handleTimerTouch();
  updateTimer();
  serviceTimerAlarm();
  serviceBuzzer();

  if (now - lastDhtReadMs >= DHT_INTERVAL_MS) {
    lastDhtReadMs = now;
    float newTemperature = dht.readTemperature();
    float newHumidity = dht.readHumidity();
    if (!isnan(newTemperature) && !isnan(newHumidity)) {
      indoorTemperature = newTemperature;
      indoorHumidity = newHumidity;
    }
    if (page == 1) drawIndoorPage();
  }

  maintainWifi();
  if (WiFi.status() == WL_CONNECTED && now - lastWeatherFetchMs >= WEATHER_INTERVAL_MS) {
    fetchWeather();
    if (page == 2) drawWeatherPage();
  }

  if (now - lastPageChangeMs >= PAGE_INTERVAL_MS) {
    page = (page + 1) % 4;
    lastPageChangeMs = now;
    drawCurrentPage();
  }

  if (page == 0 && rtcReady && now - lastClockDrawnSecond >= 1000) {
    lastClockDrawnSecond = now;
    drawTimePage();
  }
  if (page == 3 && timerRemainingSeconds != lastTimerDrawnSecond) {
    lastTimerDrawnSecond = timerRemainingSeconds;
    drawTimerPage();
  }
  // No busy loop: leave time for the ESP32 to idle between updates.
  delay(50);
}