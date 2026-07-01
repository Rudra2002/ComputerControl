#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
//  Hardware Pin Assignments (ESP8266 - Wemos D1 Mini)
// ============================================================
// D# mappings: D0=16, D1=5, D2=4, D3=0, D4=2 (LED), D5=14, D6=12, D7=13, D8=15
// Choose pins that are safe for input/output and don't affect boot mode.
#define PIN_PC_STATUS_LED   D1    // D5 — Input: reads PC power LED
#define PIN_POWER_RELAY     D2     // D1 — Output: triggers PC power button
#define PIN_RESTART_RELAY   D7     // D2 — Output: triggers PC restart button
#define PIN_BUILTIN_LED     2     // D4 — Output: onboard LED (active LOW)

// ============================================================
//  WiFiManager Configuration
// ============================================================
#define AP_NAME             "ComputerControl-Setup"
#define AP_PASSWORD         "setup1234"      // Minimum 8 chars for WPA2
#define CONFIG_PORTAL_TIMEOUT  180           // seconds before portal auto-closes
#define CONFIG_RESET_PIN    0               // D7 and flash to reset config (otherwise 0)
// ============================================================
//  MQTT Defaults (overridden by WiFiManager portal)
// ============================================================
#define DEFAULT_MQTT_HOST   "broker.emqx.io"
#define DEFAULT_MQTT_PORT   "1883"
#define DEFAULT_DEVICE_ID   "esp8266-pc-ctrl"
#define DEFAULT_MQTT_USER   ""
#define DEFAULT_MQTT_PASS   ""

// ============================================================
//  MQTT Topics (built from device ID at runtime)
// ============================================================
#define TOPIC_PREFIX        "computercontrol/"
#define TOPIC_STATUS        "/status"
#define TOPIC_COMMAND       "/command"
#define TOPIC_RESPONSE      "/response"
#define TOPIC_LWT           "/lwt"

// ============================================================
//  Timing
// ============================================================
#define STATUS_PUBLISH_INTERVAL_MS  2000     // Publish status every 2 seconds
#define PC_STATUS_CHECK_INTERVAL_MS 100      // Check PC LED every 100ms
#define PC_STATUS_TIMEOUT_MS        5000     // Consider PC off after 5s of no LED signal
#define BUTTON_PRESS_DURATION_MS    500      // How long to hold power/restart relay
#define MQTT_RECONNECT_INTERVAL_MS  5000     // Retry MQTT connection every 5s

// ============================================================
//  Security
// ============================================================
#define MQTT_KEEPALIVE_SEC  15               // Faster disconnect detection
#define MAX_COMMAND_RATE_MS 3000             // Rate-limit commands (min interval between commands)

#endif // CONFIG_H
