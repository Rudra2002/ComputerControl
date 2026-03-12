#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// WiFi credentials
const char *ssid = "Home";
const char *password = "Rudra@200210201710";

// Login credentials
const char *validUsername = "Rudra";
const char *validPassword = "Rudra@200210201710";

// Pin definitions
const int pc_led_pin = D1;
const int pc_power_pin = D2;
const int pc_restart_pin = D3;
const int led_pin = LED_BUILTIN;

// Variables
int pc_status = 0;
unsigned long lastStatusCheck = 0;

AsyncWebServer server(80);

// Helper: check session cookie
bool isAuthenticated(AsyncWebServerRequest *request) {
  if (request->hasHeader("Cookie")) {
    String cookie = request->getHeader("Cookie")->value();
    if (cookie.indexOf("ESPSESSION=valid") >= 0) {
      return true;
    }
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  
  // Pin setup
  pinMode(pc_led_pin, INPUT);
  pinMode(pc_power_pin, OUTPUT);
  pinMode(pc_restart_pin, OUTPUT);
  pinMode(led_pin, OUTPUT);
  
  // Initial LED blink
  for(int i = 0; i < 4; i++) {
    digitalWrite(led_pin, HIGH);
    delay(500);
    digitalWrite(led_pin, LOW);
    delay(500);
  }
   
  // Initialize LittleFS
  if (!LittleFS.begin()) {
    Serial.println("An error occurred while mounting LittleFS");
    return;
  }
  Serial.println("LittleFS mounted successfully");
  
  // WiFi setup
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    digitalWrite(led_pin, !digitalRead(led_pin));
  }
  
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());
  digitalWrite(led_pin, LOW);
  
  // Serve static files
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.serveStatic("/style.css", LittleFS, "/style.css");
  server.serveStatic("/script.js", LittleFS, "/script.js");
  server.serveStatic("/dashboard.css", LittleFS, "/dashboard.css");
  server.serveStatic("/dashboard.js", LittleFS, "/dashboard.js");
  
  // Protected dashboard route
  server.on("/dashboard.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (isAuthenticated(request)) {
      request->send(LittleFS, "/dashboard.html", "text/html", false, [](const String& var) {
        if (var == "IP_ADDRESS") {
          return WiFi.localIP().toString();
        }
        return String();
      });
    } else {
      request->redirect("/");
    }
  });
  
  // Login handler
  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("username", true) && request->hasParam("password", true)) {
      String username = request->getParam("username", true)->value();
      String password = request->getParam("password", true)->value();
      if (username == validUsername && password == validPassword) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Login successful");
        // Set session cookie (valid for 5 minutes)
        response->addHeader("Set-Cookie", "ESPSESSION=valid; Max-Age=300");
        request->send(response);
      } else {
        request->send(401, "text/plain", "Invalid credentials");
      }
    } else {
      request->send(400, "text/plain", "Missing parameters");
    }
  });
  
  // Dashboard redirect
  server.on("/dashboard", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (isAuthenticated(request)) {
      request->redirect("/dashboard.html");
    } else {
      request->redirect("/");
    }
  });
  
  // Ping endpoint for session checking
  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (isAuthenticated(request)) {
      request->send(200, "text/plain", "OK");
    } else {
      request->send(401, "text/plain", "Session expired");
    }
  });
  
  // Logout
  server.on("/logout", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
    response->addHeader("Set-Cookie", "ESPSESSION=deleted; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
    response->addHeader("Location", "/");
    request->send(response);
  });
  
  // PC Status API
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) {
      request->send(401, "text/plain", "Unauthorized");
      return;
    }
    
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument doc(200);
    doc["status"] = pc_status;
    doc["uptime"] = millis() / 1000;
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    
    serializeJson(doc, *response);
    request->send(response);
  });
  
  // Power button API
  server.on("/power", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) {
      request->send(401, "text/plain", "Unauthorized");
      return;
    }
    
    pc_power_button();
    
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument doc(100);
    doc["success"] = true;
    doc["message"] = "Power button pressed";
    
    serializeJson(doc, *response);
    request->send(response);
  });
  
  // Restart button API
  server.on("/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) {
      request->send(401, "text/plain", "Unauthorized");
      return;
    }
    
    pc_restart_button();
    
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument doc(100);
    doc["success"] = true;
    doc["message"] = "Restart button pressed";
    
    serializeJson(doc, *response);
    request->send(response);
  });
  
  // Handle 404
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });
  
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  // Check PC status every 100ms
  if (millis() - lastStatusCheck > 100) {
    check_pc_status();
    lastStatusCheck = millis();
  }
}

int reading() {
  return digitalRead(pc_led_pin);
}

void check_pc_status() {
  static unsigned long statusTimer = 0;
  static int lastReading = 0;
  
  int currentReading = reading();
  
  if (currentReading == 1) {
    pc_status = 1;
    statusTimer = millis();
  } else {
    if (millis() - statusTimer >= 5000) { // 5 second timeout
      pc_status = 0;
    }
  }
}

void pc_power_button() {
  digitalWrite(pc_power_pin, HIGH);
  digitalWrite(led_pin, HIGH);
  delay(2000);
  digitalWrite(pc_power_pin, LOW);
  digitalWrite(led_pin, LOW);
}

void pc_restart_button() {
  digitalWrite(pc_restart_pin, HIGH);
  digitalWrite(led_pin, HIGH);
  delay(2000);
  digitalWrite(pc_restart_pin, LOW);
  digitalWrite(led_pin, LOW);
}
