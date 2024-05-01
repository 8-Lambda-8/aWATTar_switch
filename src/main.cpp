#include <Arduino.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

WiFiManager wm;
WiFiManagerParameter custom_field;
WiFiClientSecure client;

const char TypeSelect_str[] PROGMEM =
    "<label for='%s'>Type</label><select id='%s'><option "
    "value='c'>Cheap</option><option value='e'>Expensive</option></select>";

class TypeSelect : public WiFiManagerParameter {
 public:
  TypeSelect(const char *id) : WiFiManagerParameter("") {
    const char *buff = (const char *)malloc(sizeof(TypeSelect_str) + sizeof(id) * 2 + 1);
    sprintf((char *)buff, TypeSelect_str, id, id);
    init(NULL, NULL, NULL, 1, buff, WFM_LABEL_BEFORE);
  }

  char getValue() { return WiFiManagerParameter::getValue()[0]; }
};

const char numberInput[] PROGMEM = " type=\"number\" min=\"1\" max=\"23\" ";

#define NAME "aWATTar_SWITCH"
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
  Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n", time.tm_year + 1900, time.tm_mon + 1,
                time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
}

void SwitchRelay(uint8_t x, boolean b) { digitalWrite(RelayPins[x], b); }

time_t hours[24];
float prices[24];

time_t hours_[24];
float prices_[24];

void setup() {
  Serial.begin(115200);

  // init Diplay
  Wire.begin(5, 4);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
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

  for (uint8_t i = 0; i < sizeof(RelayPins); i++) {
    pinMode(RelayPins[i], OUTPUT);
    SwitchRelay(i, false);
  }

  // Connect to the WiFI
  WiFi.mode(WIFI_STA);
  wm.setCustomHeadElement(NAME);
  wm.setConfigPortalBlocking(false);

  WiFiManagerParameter R1_head("<h2>Relais 1:</h2>");
  TypeSelect R1_type("R1_type");
  WiFiManagerParameter R1_hours("R1_hours", "Hours", "5", 2, (const char *)FPSTR(numberInput));

  WiFiManagerParameter R2_head("<h2>Relais 2:</h2>");
  TypeSelect R2_type("R2_type");
  WiFiManagerParameter R2_hours("R2_hours", "Hours", "5", 2, (const char *)FPSTR(numberInput));

  wm.addParameter(&R1_head);
  wm.addParameter(&R1_type);
  wm.addParameter(&R1_hours);
  wm.addParameter(&R2_head);
  wm.addParameter(&R2_type);
  wm.addParameter(&R2_hours);
  wm.setSaveParamsCallback(saveParamCallback);

  std::vector<const char *> menu = {"wifi", "info", "param", "sep", "restart", "exit"};
  wm.setMenu(menu);

  // set dark theme
  wm.setClass("invert");

  wm.setConfigPortalTimeout(30);
  wm.setAPClientCheck(true);
  wm.setHostname(NAME);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Connect ToWeb Portalfor WiFi  Config");
  display.display();

  while (!wm.autoConnect(NAME)) {
    // Connection Failed
    wm.process();
  }
  wm.startWebPortal();

  // connected
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 8);
  display.print("Connected To:\n\"");
  display.setTextSize(2);
  display.print(WiFi.SSID());
  display.setTextSize(1);
  display.print("\"\n\n\nIP: \n");
  display.setTextSize(2);
  display.print(WiFi.localIP());
  display.display();

  delay(3000);

  configTime(MY_TZ, MY_NTP_SERVER);
  client.setInsecure();

  display.clearDisplay();
  display.setCursor(0, 8);
  display.print("get Time\n");
  display.display();
  display.setTextSize(1);

  uint64_t timer = 0;
  while (now < 1000) {
    wm.process();
    if (millis() - timer < 1000) continue;
    timer = millis();

    time(&now);
    display.print(".");
    display.display();
  }

  makeHTTPRequest();
}

void drawDiagram() {
  float range = prices[23] - prices[0];
  uint8_t zeroLine = 30 * (prices[23] / range);
  if (prices[0] > 0) {
    range = prices[23];
    zeroLine = 30;
  }
  zeroLine++;

  Serial.printf("pmin %5.2f\n", prices[0]);
  Serial.printf("pmax %5.2f\n", prices[23]);

  Serial.printf("range %5.2f\n", range);
  Serial.printf("zero %03d\n", zeroLine);

  float scale = 30.0 / range;
  Serial.printf("Scale %5.2f\n", scale);

  display.clearDisplay();
  display.drawLine(0, 32, 128, 32, 1);
  display.drawLine(0, 44, 128, 44, 1);
  for (size_t i = 0; i < 128; i += 2) display.drawPixel(i, zeroLine, 1);

  display.setTextSize(1);
  for (size_t i = 0; i < 24; i++) {
    display.drawLine(4 + i * 5, zeroLine - prices_[i] * scale, 8 + i * 5,
                     zeroLine - prices_[i] * scale, 1);

    if (now > hours_[i] && now < (hours_[i] + 3600)) {  // current Hour
      for (size_t j = 4; j < 9; j++)
        display.drawLine(j + i * 5, zeroLine - prices_[i] * scale, j + i * 5, zeroLine, 1);

      display.drawLine(6 + i * 5, 33, 6 + i * 5, 43, 1);

      display.setTextSize(2);
      display.setCursor(20, 50);
      display.printf("%05.2f", prices[i]);
      display.setTextSize(1);
      display.print("ct/kWh");
    } else
      display.drawLine(6 + i * 5, zeroLine - prices_[i] * scale, 6 + i * 5, zeroLine, 1);

    for (size_t j = 0; j < 5; j++)  // cheapest hours
      if (prices_[i] == prices[j]) {
        display.drawLine(4 + i * 5, 43, 8 + i * 5, 43, 1);
        display.drawLine(4 + i * 5, 42, 8 + i * 5, 42, 1);
      }

    display.drawPixel(6 + i * 5, 33, 1);

    if (i % 3 == 0) {
      display.setCursor(1 + i * 5, 35);
      tm tmx;
      localtime_r(&hours_[i], &tmx);
      display.printf("%02d", tmx.tm_hour);
    }
  }
  display.display();
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
  client.print("/v1/marketdata?start=");
  client.print(now - (now % (24 * 60 * 60)) - 7200);
  client.print("000");
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
      prices[i] = (float)doc["data"][i]["marketprice"] / 10 * 1.03 + 1.5;
      hours[i] = (time_t)doc["data"][i]["start_timestamp"] / 1000;
    }

    memcpy(prices_, prices, sizeof(float) * 24);
    memcpy(hours_, hours, sizeof(time_t) * 24);

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
      Serial.printf("%02d: %05.2f  ", i, prices[i]);
      printTime(tmx);
    }
    Serial.println();

  } else {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }
}

uint64_t timer = 30000;
void loop() {
  wm.process();

  // update Time
  time(&now);
  localtime_r(&now, &tim);

  if (millis() - timer < 30000) return;
  timer = millis();

  // update JSON
  if (tim.tm_min == 0) {
    makeHTTPRequest();
    Serial.println("cheapest 5:");
    for (size_t i = 0; i < 5; i++) {
      tm tmx;
      localtime_r(&hours[i], &tmx);

      Serial.printf("%05.2f  ", prices[i]);
      printTime(tmx);
    }
  }

  drawDiagram();

  // Switch
  bool on = false;
  for (size_t i = 0; i < 5; i++) {
    if (now > hours[i] && now < (hours[i] + 3600)) {
      on = true;
    }
  }
  SwitchRelay(0, on);
  SwitchRelay(1, on);
}
