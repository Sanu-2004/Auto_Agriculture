#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "DHT.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <time.h>

// WiFi
const char *ssid = "SensorAP";
const char *password = "";

// Pin config
const int relay1Pin = 12;
const int soilSensorPin = A0;
const int DHTPIN = 27;
const int DHTTYPE = DHT11;

// Log file
const char *logFileName = "/sensor_log.json";
const int logMaxDays = 7;

DHT dht(DHTPIN, DHTTYPE);
AsyncWebServer server(80);
DNSServer dnsServer;

// Sensor variables
int soilValue = 0;
float temperature = 0;
float humidity = 0;
int moisture = 0;

// Soil sensor Threshold
const int SoilThreshold = 50;
bool relayOverride = false;

// JSON Document
JsonDocument jsonDoc;

void setup() {
  Serial.begin(115200);

  pinMode(relay1Pin, OUTPUT);
  digitalWrite(relay1Pin, HIGH); // Initialize relay to OFF
  // delay(2000);

  dht.begin();

  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed");
    if (LittleFS.format()) {
      Serial.println("LittleFS formatted, trying to mount again");
      if (LittleFS.begin()) {
        Serial.println("LittleFS mounted after format");
      } else {
        Serial.println("LittleFS mount failed after format");
      }
    } else {
      Serial.println("LittleFS format failed");
    }
    return;
  } else {
    Serial.println("LittleFS Mounted Successfully");
  }

  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  dnsServer.start(53, "*", WiFi.softAPIP());

  if (MDNS.begin("esp32")) {
    Serial.println("MDNS responder started");
  }

  configTime(0, 0, "pool.ntp.org"); // NTP time

  server.on("/chart.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/chart.js")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/chart.js", "application/javascript");
      request->send(response);
    } else {
      request->send(404, "text/plain", "File not found");
    }
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/index.html")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html");
      request->send(response);
    } else {
      request->send(404, "text/plain", "File not found");
    }
  });

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    soilValue = analogRead(soilSensorPin);
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    moisture = (soilValue / 4095.00) * 100;

    jsonDoc["soil"] = moisture;
    jsonDoc["temperature"] = temperature;
    jsonDoc["humidity"] = humidity;

    String jsonString;
    serializeJson(jsonDoc, jsonString);
    request->send(200, "application/json", jsonString);
  });

  server.on("/history", HTTP_GET, [](AsyncWebServerRequest *request) {
    File file = LittleFS.open(logFileName, FILE_READ);
    if (!file) {
      request->send(404, "text/plain", "Log file not found");
      return;
    }
    AsyncWebServerResponse *response = request->beginResponse(file, String(""), String("application/json"));
    response->addHeader("Content-Encoding", "plain");
    request->send(response);
    file.close();
  });

  server.on("/viewlog", HTTP_GET, [](AsyncWebServerRequest *request) {
    File file = LittleFS.open(logFileName, FILE_READ);
    if (!file) {
      request->send(404, "text/plain", "Log file not found");
      return;
    }
    String fileContent = file.readString();
    file.close();
    request->send(200, "text/plain", fileContent);
  });

  server.on("/relay1/on", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(relay1Pin, LOW);
    relayOverride = true;
    request->send(200, "text/plain", "Relay 1 ON");
  });

  server.on("/relay1/off", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(relay1Pin, HIGH);
    relayOverride = false;
    request->send(200, "text/plain", "Relay 1 OFF");
  });

  server.begin();
  WiFi.softAPConfig(IP, IP, IPAddress(255, 255, 255, 0));

  Serial.print("LittleFS total bytes: ");
  Serial.println(LittleFS.totalBytes());
  Serial.print("LittleFS used bytes: ");
  Serial.println(LittleFS.usedBytes());

}

void loop() {
  dnsServer.processNextRequest();
  soilValue = analogRead(soilSensorPin);
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  moisture = (soilValue / 4095.00) * 100;


  logSensorData(moisture, temperature, humidity);

  if (relayOverride) {
    digitalWrite(relay1Pin, LOW);
  } else {
    if (moisture > SoilThreshold) {
      digitalWrite(relay1Pin, LOW);
    } else {
      digitalWrite(relay1Pin, HIGH);
    }
  }

  delay(2000);
}

void logSensorData(int soil, float temp, float hum) {
  File file = LittleFS.open(logFileName, FILE_APPEND);
  if (!file) return;

  time_t now = time(nullptr);
  if (now == 0) return;

  char timestamp[20];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

  DynamicJsonDocument logEntry(128);
  logEntry["timestamp"] = timestamp;
  logEntry["soil"] = soil;
  logEntry["temperature"] = temp;
  logEntry["humidity"] = hum;

  serializeJson(logEntry, file);
  file.println();
  file.close();

  cleanLogFile();
}

void cleanLogFile() {
  File file = LittleFS.open(logFileName, FILE_READ);
  if (!file) return;

  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) return;

  JsonArray entries = doc.as<JsonArray>();
  if (entries.size() > 0) {
    time_t cutoffTime = time(nullptr) - (logMaxDays * 86400);

    JsonArray filteredEntries;
    for (JsonObject entry : entries) {
      struct tm tm;
      strptime(entry["timestamp"], "%Y-%m-%d %H:%M:%S", &tm);
      time_t entryTime = mktime(&tm);
      if (entryTime >= cutoffTime) {
        filteredEntries.add(entry);
      }
    }

    File newFile = LittleFS.open(logFileName, FILE_WRITE);
    if (newFile) {
      serializeJson(filteredEntries, newFile);
      newFile.close();
    }
  }
}