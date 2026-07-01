/*
 * ESP32 Computer Control — Main Firmware
 * 
 * Controls a PC via GPIO (power, restart) and monitors status via LED pin.
 * Communicates with a remote web dashboard through MQTT.
 * WiFi + MQTT broker settings are configured via WiFiManager captive portal.
 * 
 * Security features:
 *   - No hardcoded credentials (WiFiManager portal)
 *   - MQTT authentication (username/password)
 *   - Command rate limiting (prevents rapid-fire relay triggers)
 *   - Topic namespacing with unique device ID
 *   - LWT (Last Will & Testament) for offline detection
 *   - Config reset via physical button (GPIO 0)
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include <EEPROM.h>

// ============================================================
//  Globals
// ============================================================
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// MQTT settings (loaded from flash / WiFiManager)
char mqtt_host[64]   = DEFAULT_MQTT_HOST;
char mqtt_port[6]    = DEFAULT_MQTT_PORT;
char device_id[32]   = DEFAULT_DEVICE_ID;
char mqtt_user[32]   = DEFAULT_MQTT_USER;
char mqtt_pass[64]   = DEFAULT_MQTT_PASS;

// Runtime topic strings (built in setup)
String topicStatus;
String topicCommand;
String topicResponse;
String topicLWT;

// State
int  pcStatus              = 0;
unsigned long lastStatusPublish  = 0;
unsigned long lastStatusCheck    = 0;
unsigned long lastCommandTime    = 0;
unsigned long lastMqttReconnect  = 0;
unsigned long pcLedOnTimestamp   = 0;

// WiFiManager save flag
bool shouldSaveConfig = false;

// ============================================================
//  Forward Declarations
// ============================================================
void setupPins();
void setupWiFiManager();
void loadConfig();
void saveConfig();
void buildTopics();
void connectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void publishStatus();
void checkPCStatus();
void pcPowerButton();
void pcRestartButton();
void handleCommand(const char* payload, unsigned int length);
void blinkLED(int times, int delayMs);

// ============================================================
//  WiFiManager save callback
// ============================================================
void saveConfigCallback() {
    shouldSaveConfig = true;
}

// ============================================================
//  Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n=============================");
    Serial.println(" ESP8266 Computer Control v2.0");
    Serial.println("=============================\n");

    setupPins();
    blinkLED(3, 200);  // Visual boot indicator

    // Check if config reset is requested (hold GPIO 0 / BOOT button)
    pinMode(CONFIG_RESET_PIN, INPUT_PULLUP);
    delay(100);
    if (digitalRead(CONFIG_RESET_PIN) == LOW) {
        Serial.println("[CONFIG] Reset button held — clearing saved config...");
        blinkLED(10, 100);  // Rapid blink to confirm reset
        // Clear EEPROM-stored config
        EEPROM.begin(512);
        for (int i = 0; i < 512; i++) EEPROM.write(i, 0);
        EEPROM.commit();
        EEPROM.end();
        // WiFiManager will also reset
        WiFiManager wm;
        wm.resetSettings();
        Serial.println("[CONFIG] Config cleared. Entering setup portal...");
    }

    loadConfig();
    setupWiFiManager();
    buildTopics();

    // Configure MQTT
    mqttClient.setServer(mqtt_host, atoi(mqtt_port));
    mqttClient.setCallback(mqttCallback);
    mqttClient.setKeepAlive(MQTT_KEEPALIVE_SEC);

    connectMQTT();

    Serial.println("[BOOT] Setup complete. Entering main loop.");
}

// ============================================================
//  Loop
// ============================================================
void loop() {
    // Maintain MQTT connection
    if (!mqttClient.connected()) {
        unsigned long now = millis();
        if (now - lastMqttReconnect >= MQTT_RECONNECT_INTERVAL_MS) {
            lastMqttReconnect = now;
            connectMQTT();
        }
    }
    mqttClient.loop();

    // Check PC status (fast polling)
    if (millis() - lastStatusCheck >= PC_STATUS_CHECK_INTERVAL_MS) {
        checkPCStatus();
        lastStatusCheck = millis();
    }

    // Publish status periodically
    if (millis() - lastStatusPublish >= STATUS_PUBLISH_INTERVAL_MS) {
        publishStatus();
        lastStatusPublish = millis();
    }
}

// ============================================================
//  Pin Configuration
// ============================================================
void setupPins() {
    pinMode(PIN_PC_STATUS_LED, INPUT);
    pinMode(PIN_POWER_RELAY, OUTPUT);
    pinMode(PIN_RESTART_RELAY, OUTPUT);
    pinMode(PIN_BUILTIN_LED, OUTPUT);

    // Ensure relays are OFF on boot
    digitalWrite(PIN_POWER_RELAY, LOW);
    digitalWrite(PIN_RESTART_RELAY, LOW);
    digitalWrite(PIN_BUILTIN_LED, LOW);
}

// ============================================================
//  Config: Load from Preferences (flash)
// ============================================================
void loadConfig() {
    // Load config from EEPROM (JSON blob)
    EEPROM.begin(512);
    String stored = "";
    for (int i = 0; i < 512; i++) {
        char c = EEPROM.read(i);
        if (c == 0) break;
        stored += c;
    }
    if (stored.length() > 0) {
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, stored);
        if (!err) {
            String h = doc["host"] | DEFAULT_MQTT_HOST;
            String p = doc["port"] | DEFAULT_MQTT_PORT;
            String d = doc["devid"] | DEFAULT_DEVICE_ID;
            String u = doc["user"] | DEFAULT_MQTT_USER;
            String pw = doc["pass"] | DEFAULT_MQTT_PASS;

            h.toCharArray(mqtt_host, sizeof(mqtt_host));
            p.toCharArray(mqtt_port, sizeof(mqtt_port));
            d.toCharArray(device_id, sizeof(device_id));
            u.toCharArray(mqtt_user, sizeof(mqtt_user));
            pw.toCharArray(mqtt_pass, sizeof(mqtt_pass));
        }
    }
    EEPROM.end();

    Serial.printf("[CONFIG] Loaded — Host: %s  Port: %s  Device: %s  User: %s\n",
                  mqtt_host, mqtt_port, device_id,
                  strlen(mqtt_user) > 0 ? mqtt_user : "(none)");
}

// ============================================================
//  Config: Save to Preferences (flash)
// ============================================================
void saveConfig() {
    // Save config to EEPROM as JSON
    DynamicJsonDocument doc(512);
    doc["host"] = String(mqtt_host);
    doc["port"] = String(mqtt_port);
    doc["devid"] = String(device_id);
    doc["user"] = String(mqtt_user);
    doc["pass"] = String(mqtt_pass);
    String out;
    serializeJson(doc, out);
    if (out.length() >= 512) {
        Serial.println("[CONFIG] Error: config too large to save.");
        return;
    }
    EEPROM.begin(512);
    for (size_t i = 0; i < out.length(); i++) EEPROM.write(i, out[i]);
    EEPROM.write(out.length(), 0); // terminator
    EEPROM.commit();
    EEPROM.end();
    Serial.println("[CONFIG] Saved to EEPROM.");
}

// ============================================================
//  WiFiManager: Setup with custom MQTT fields
// ============================================================
void setupWiFiManager() {
    WiFiManager wm;

    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);
    wm.setConnectTimeout(20);

    // Custom parameters for MQTT configuration
    WiFiManagerParameter param_mqtt_host("mqtt_host", "MQTT Broker Host", mqtt_host, sizeof(mqtt_host));
    WiFiManagerParameter param_mqtt_port("mqtt_port", "MQTT Broker Port", mqtt_port, sizeof(mqtt_port));
    WiFiManagerParameter param_device_id("device_id", "Device ID", device_id, sizeof(device_id));
    WiFiManagerParameter param_mqtt_user("mqtt_user", "MQTT Username (optional)", mqtt_user, sizeof(mqtt_user));
    WiFiManagerParameter param_mqtt_pass("mqtt_pass", "MQTT Password (optional)", mqtt_pass, sizeof(mqtt_pass));

    // Add a separator heading
    WiFiManagerParameter param_heading("<br><h3>MQTT Configuration</h3>");
    wm.addParameter(&param_heading);
    wm.addParameter(&param_mqtt_host);
    wm.addParameter(&param_mqtt_port);
    wm.addParameter(&param_device_id);
    wm.addParameter(&param_mqtt_user);
    wm.addParameter(&param_mqtt_pass);

    // WPA2-protected AP
    bool connected = wm.autoConnect(AP_NAME, AP_PASSWORD);

    if (!connected) {
        Serial.println("[WIFI] Failed to connect. Restarting...");
        delay(3000);
        ESP.restart();
    }

    Serial.printf("[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());

    // Read custom parameters after portal saves
        if (shouldSaveConfig) {
        strncpy(mqtt_host, param_mqtt_host.getValue(), sizeof(mqtt_host) - 1);
        strncpy(mqtt_port, param_mqtt_port.getValue(), sizeof(mqtt_port) - 1);
        strncpy(device_id, param_device_id.getValue(), sizeof(device_id) - 1);
        strncpy(mqtt_user, param_mqtt_user.getValue(), sizeof(mqtt_user) - 1);
        strncpy(mqtt_pass, param_mqtt_pass.getValue(), sizeof(mqtt_pass) - 1);
            saveConfig();
    }
}

// ============================================================
//  Build MQTT topic strings from device ID
// ============================================================
void buildTopics() {
    String prefix = String(TOPIC_PREFIX) + String(device_id);
    topicStatus   = prefix + TOPIC_STATUS;
    topicCommand  = prefix + TOPIC_COMMAND;
    topicResponse = prefix + TOPIC_RESPONSE;
    topicLWT      = prefix + TOPIC_LWT;

    Serial.printf("[MQTT] Topics — Status: %s | Cmd: %s | Resp: %s | LWT: %s\n",
                  topicStatus.c_str(), topicCommand.c_str(),
                  topicResponse.c_str(), topicLWT.c_str());
}

// ============================================================
//  MQTT: Connect with LWT
// ============================================================
void connectMQTT() {
    if (mqttClient.connected()) return;

    Serial.printf("[MQTT] Connecting to %s:%s...\n", mqtt_host, mqtt_port);

    // LWT payload — sent by broker if ESP32 disconnects unexpectedly
    String lwtPayload = "{\"online\":false}";

    String clientId = "esp8266-" + String(device_id) + "-" + String(random(0xFFFF), HEX);

    bool connected;
    if (strlen(mqtt_user) > 0) {
        connected = mqttClient.connect(
            clientId.c_str(),
            mqtt_user,
            mqtt_pass,
            topicLWT.c_str(),
            1,      // QoS 1 for LWT
            true,   // retain LWT
            lwtPayload.c_str()
        );
    } else {
        connected = mqttClient.connect(
            clientId.c_str(),
            NULL,
            NULL,
            topicLWT.c_str(),
            1,
            true,
            lwtPayload.c_str()
        );
    }

    if (connected) {
        Serial.println("[MQTT] Connected!");
        digitalWrite(PIN_BUILTIN_LED, HIGH);

        // Publish online status (clear LWT retain)
        mqttClient.publish(topicLWT.c_str(), "{\"online\":true}", true);

        // Subscribe to command topic
        mqttClient.subscribe(topicCommand.c_str(), 1);  // QoS 1
        Serial.printf("[MQTT] Subscribed to: %s\n", topicCommand.c_str());
    } else {
        Serial.printf("[MQTT] Connection failed, rc=%d. Retry in %ds.\n",
                       mqttClient.state(), MQTT_RECONNECT_INTERVAL_MS / 1000);
        digitalWrite(PIN_BUILTIN_LED, LOW);
    }
}

// ============================================================
//  MQTT: Message callback
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // Validate topic
    if (String(topic) != topicCommand) {
        Serial.printf("[MQTT] Ignoring unknown topic: %s\n", topic);
        return;
    }

    // Safety: limit payload size
    if (length > 256) {
        Serial.println("[MQTT] Payload too large, ignoring.");
        return;
    }

    handleCommand((const char*)payload, length);
}

// ============================================================
//  Command Handler (with rate limiting)
// ============================================================
void handleCommand(const char* payload, unsigned int length) {
    // Rate limiting — prevent rapid-fire relay triggers
    unsigned long now = millis();
    if (now - lastCommandTime < MAX_COMMAND_RATE_MS) {
        Serial.println("[CMD] Rate limited — ignoring command.");
        // Publish rate-limit response
        JsonDocument doc;
        doc["success"] = false;
        doc["message"] = "Rate limited. Wait before sending another command.";
        char buffer[128];
        serializeJson(doc, buffer);
        mqttClient.publish(topicResponse.c_str(), buffer);
        return;
    }

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error) {
        Serial.printf("[CMD] JSON parse error: %s\n", error.c_str());
        return;
    }

    const char* action = doc["action"];
    if (!action) {
        Serial.println("[CMD] Missing 'action' field.");
        return;
    }

    Serial.printf("[CMD] Received action: %s\n", action);

    // Execute command
    JsonDocument resp;
    resp["success"] = true;

    if (strcmp(action, "power") == 0) {
        pcPowerButton();
        resp["message"] = "Power button pressed";
        lastCommandTime = now;
    } else if (strcmp(action, "restart") == 0) {
        pcRestartButton();
        resp["message"] = "Restart button pressed";
        lastCommandTime = now;
    } else if (strcmp(action, "status") == 0) {
        // On-demand status request
        publishStatus();
        resp["message"] = "Status published";
    } else {
        resp["success"] = false;
        resp["message"] = "Unknown action";
        Serial.printf("[CMD] Unknown action: %s\n", action);
    }

    // Publish response
    char buffer[128];
    serializeJson(resp, buffer);
    mqttClient.publish(topicResponse.c_str(), buffer);
}

// ============================================================
//  Publish PC Status
// ============================================================
void publishStatus() {
    if (!mqttClient.connected()) return;

    JsonDocument doc;
    doc["pc"]     = pcStatus;
    doc["uptime"] = millis() / 1000;
    doc["rssi"]   = WiFi.RSSI();
    doc["ip"]     = WiFi.localIP().toString();

    char buffer[192];
    serializeJson(doc, buffer);
    mqttClient.publish(topicStatus.c_str(), buffer);
}

// ============================================================
//  Check PC Status via LED pin
// ============================================================
void checkPCStatus() {
    int currentReading = digitalRead(PIN_PC_STATUS_LED);

    if (currentReading == HIGH) {
        pcStatus = 1;
        pcLedOnTimestamp = millis();
    } else {
        if (millis() - pcLedOnTimestamp >= PC_STATUS_TIMEOUT_MS) {
            pcStatus = 0;
        }
    }
}

// ============================================================
//  PC Power Button — momentary relay press
// ============================================================
void pcPowerButton() {
    Serial.println("[GPIO] Power button pressed.");
    digitalWrite(PIN_POWER_RELAY, HIGH);
    digitalWrite(PIN_BUILTIN_LED, HIGH);
    delay(BUTTON_PRESS_DURATION_MS);
    digitalWrite(PIN_POWER_RELAY, LOW);
    digitalWrite(PIN_BUILTIN_LED, LOW);
}

// ============================================================
//  PC Restart Button — momentary relay press
// ============================================================
void pcRestartButton() {
    Serial.println("[GPIO] Restart button pressed.");
    digitalWrite(PIN_RESTART_RELAY, HIGH);
    digitalWrite(PIN_BUILTIN_LED, HIGH);
    delay(BUTTON_PRESS_DURATION_MS);
    digitalWrite(PIN_RESTART_RELAY, LOW);
    digitalWrite(PIN_BUILTIN_LED, LOW);
}

// ============================================================
//  Utility: Blink LED
// ============================================================
void blinkLED(int times, int delayMs) {
    for (int i = 0; i < times; i++) {
        digitalWrite(PIN_BUILTIN_LED, HIGH);
        delay(delayMs);
        digitalWrite(PIN_BUILTIN_LED, LOW);
        delay(delayMs);
    }
}
