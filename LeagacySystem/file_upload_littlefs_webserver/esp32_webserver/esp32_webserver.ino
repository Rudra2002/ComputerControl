#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

// Replace with your network credentials
const char *ssid = "Rudra_2.4G";
const char *password = "Rudra@2002";

// Demo credentials (change for deployment)
const char *validUsername = "admin";
const char *validPassword = "password123";

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

  if (!LittleFS.begin()) {
    Serial.println("An error occurred while mounting LittleFS");
    return;
  }

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println(WiFi.localIP());

  // Serve static files
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/style.css", "text/css");
  });
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/script.js", "application/javascript");
  });
  server.on("/dashboard.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (isAuthenticated(request)) {
      request->send(LittleFS, "/dashboard.html", "text/html");
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

  // Dashboard route (redirect to /dashboard.html)
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

  server.begin();
}

void loop() {
  // Nothing to do here
}
