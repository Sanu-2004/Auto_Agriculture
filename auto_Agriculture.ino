#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "DHT.h"
#include <ArduinoJson.h>  // For JSON data
#include <LittleFS.h>     // Include LittleFS
#include <ESPmDNS.h>
#include <DNSServer.h>

// WiFi
const char *ssid = "SensorAP";
const char *password = "";

// Relay Pinsupload
const int relay1Pin = 12;  // Soil moisture relay
const int relay2Pin = 13;  // Temp/humidity relay
const int soilSensorPin = A0;
const int DHTPIN = 27;
const int DHTTYPE = DHT11;

DHT dht(DHTPIN, DHTTYPE);
AsyncWebServer server(80);
DNSServer dnsServer;
int soilValue = 0;
float temperature = 0;
float humidity = 0;
int moisture = 0;

// soil sensor Threshold
const int SoilThreshold = 50;

// Thresholds for t/h sensor
const float highTempThreshold = 30.0;
const float lowHumidityThreshold = 40.0;

// JSON Document
JsonDocument jsonDoc;


void setup() {
  Serial.begin(115200);

  pinMode(relay1Pin, OUTPUT);
  pinMode(relay2Pin, OUTPUT);
  digitalWrite(relay1Pin, HIGH);  // Initialize relays to OFF
  digitalWrite(relay2Pin, HIGH);

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

  dnsServer.start(53, "*", IP);
  if (MDNS.begin("esp32")) {
    Serial.println("MDNS responder started");
  }

  server.on("/chart.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (LittleFS.exists("/chart.js")) {
            AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/chart.js", "application/javascript");
            request->send(response);
        } else {
            request->send(404, "text/plain", "File not found");
        }
    });

  // Web server routes
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
    moisture = (soilValue/4095.00) * 100;

    jsonDoc["soil"] = moisture;
    jsonDoc["temperature"] = temperature;
    jsonDoc["humidity"] = humidity;

    String jsonString;
    serializeJson(jsonDoc, jsonString);
    request->send(200, "application/json", jsonString);
  });

  server.on("/relay1/on", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(relay1Pin, LOW);
    request->send(200, "text/plain", "Relay 1 ON");
  });

  server.on("/relay1/off", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(relay1Pin, HIGH);
    request->send(200, "text/plain", "Relay 1 OFF");
  });

  server.on("/relay2/on", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(relay2Pin, LOW);
    request->send(200, "text/plain", "Relay 2 ON");
  });

  server.on("/relay2/off", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(relay2Pin, HIGH);
    request->send(200, "text/plain", "Relay 2 OFF");
  });

  server.begin();
  WiFi.softAPConfig(IP, IP, IPAddress(255, 255, 255, 0));
}

void loop() {
  dnsServer.processNextRequest();
  soilValue = analogRead(soilSensorPin);
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  moisture = (soilValue/4095.00) * 100;
  // Serial.println(moisture);

  // // Soil Moisture Control
  if (moisture > SoilThreshold) {
    digitalWrite(relay1Pin, LOW);  // Turn on relay if soil is dry
  } else {
    digitalWrite(relay1Pin, HIGH);  // Turn off relay if soil is wet
  }

  // // Temperature/Humidity Control
  // if (temperature > highTempThreshold || humidity < lowHumidityThreshold) {
  //   digitalWrite(relay2Pin, LOW);  // Turn on relay if temp high or humidity low
  // } else {
  //   digitalWrite(relay2Pin, HIGH);  // Turn off relay if temp/humidity normal
  // }

  delay(2000);
}
