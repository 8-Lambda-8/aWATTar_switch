#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "wifiPasswd.h"

WiFiClientSecure client;

#define HOST "api.awattar.at"

#define MY_NTP_SERVER "at.pool.ntp.org"
#define MY_TZ "CET-1CEST,M3.5.0/02,M10.5.0/03"

time_t now;
tm tim;

// DISPLAY
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3c

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const uint8_t RelayPins[] = {14, 12};

void makeHTTPRequest();

void printTime(tm time) {
  Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n", time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
                time.tm_hour, time.tm_min, time.tm_sec);
}

void SwitchRelay(uint8_t x, boolean b) { digitalWrite(RelayPins[x], b); }

time_t hours[24];
float prices[24];

void setup() {
  Serial.begin(115200);

  //init Diplay
  Wire.begin(5,4);
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
  }
  
  display.clearDisplay();
  display.display();
  delay(20);

  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setCursor(0, 8);
  display.print("Startup...");
  display.display();

  delay(20);
  display.clearDisplay();
  display.setCursor(0, 8);
  display.print("connect\nWiFi...\n");
  display.print(ssid);
  display.display();

  for (uint8_t i = 0; i < sizeof(RelayPins); i++) {
    pinMode(RelayPins[i], OUTPUT);
    SwitchRelay(i, false);
  }

  // Connect to the WiFI
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  delay(20);
  display.clearDisplay();
  display.setCursor(0, 8);
  display.print("connected\nIP:\n");
  display.print(WiFi.localIP());
  display.display();

  configTime(MY_TZ, MY_NTP_SERVER);
  client.setInsecure();

  makeHTTPRequest();
}

void makeHTTPRequest() {
  // Opening connection to server (Use 80 as port if HTTP)
  if (!client.connect(HOST, 443)) {
    Serial.println(F("Connection failed"));
    return;
  }

  // give the esp a breather
  yield();

  // Send HTTP request
  client.print(F("GET "));
  client.print("/v1/marketdata");
  client.println(F(" HTTP/1.1"));

  // Headers
  client.print(F("Host: "));
  client.println(HOST);

  client.println(F("Cache-Control: no-cache"));

  if (client.println() == 0) {
    Serial.println(F("Failed to send request"));
    return;
  }

  //  Check HTTP status
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
    Serial.print(F("Unexpected response: "));
    Serial.println(status);
    return;
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    Serial.println(F("Invalid response"));
    return;
  }

  // peek() will look at the character, but not take it off the queue
  while (client.available() && client.peek() != '{') {
    char c = 0;
    client.readBytes(&c, 1);
    Serial.print(c);
    Serial.println("BAD");
  }
  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, client);

  if (!error) {
    for (size_t i = 0; i < 24; i++) {
      prices[i] = doc["data"][i]["marketprice"];
      hours[i] = (time_t)doc["data"][i]["start_timestamp"] / 1000;
    }

    float swapPrices;
    uint64_t swapHours;
    for (size_t i = 0; i < 24; i++) {
      for (size_t j = i + 1; j < 24; j++) {
        if (prices[i] > prices[j]) {
          swapPrices = prices[i];
          prices[i] = prices[j];
          prices[j] = swapPrices;

          swapHours = hours[i];
          hours[i] = hours[j];
          hours[j] = swapHours;
        }
      }
    }

    for (size_t i = 0; i < 24; i++) {
      tm tmx;
      localtime_r(&hours[i], &tmx);
      Serial.printf("%02d: %06.2f  ", i, prices[i]);
      printTime(tmx);
    }
    Serial.println();

  } else {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }
}

void loop() {
  // update Time
  time(&now);
  localtime_r(&now, &tim);

  printTime(tim);

  // update JSON
  if (tim.tm_min == 0) {
    makeHTTPRequest();
    Serial.println("cheapest 5:");
    for (size_t i = 0; i < 5; i++) {
      tm tmx;
      localtime_r(&hours[i], &tmx);

      Serial.printf("%06.2f  ", prices[i]);
      printTime(tmx);
    }
  }

  // Switch
  bool on = false;
  for (size_t i = 0; i < 5; i++) {
    if (now > hours[i] && now < (hours[i] + 3600)) {
      on = true;
    }
  }
  SwitchRelay(0, on);
  SwitchRelay(1, on);

  for (size_t i = 0; i < 24; i++) {
    if (now > hours[i] && now < (hours[i] + 3600)) {
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 8);
      display.printf("%02d-%02d %02d:", tim.tm_mon + 1, tim.tm_mday, tim.tm_hour);
      display.setCursor(0, 30);
      display.printf("%05.2f",prices[i]);
      display.setTextSize(1);
      display.print("ct/kWh");
      display.display();
    }
  }

  // wait 1 min
  delay(60 * 1000);
}
