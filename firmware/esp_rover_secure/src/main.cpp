#include <M5StickC.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../include/config.h"

// Forward declarations
extern void initializeWiFiManager();
extern void initializeWebServers(); 
extern void initializeMovementController();
extern void initializeSafetyMonitor();
extern void initializeTelemetryCollector();
extern void initializeJSONSerializer();

extern void handleWebServerTask(void* parameter);
extern void handleTelemetryTask(void* parameter);
extern void handleSafetyTask(void* parameter);
extern void handleMotorTask(void* parameter);

// Global system state
struct SystemState {
    bool initialized = false;
    bool wifi_connected = false;
    bool servers_running = false;
    bool safety_lockout = false;
    unsigned long uptime_start = 0;
    String last_error = "";
    float battery_voltage = 0.0;
    bool emergency_stop = false;
} g_system_state;

// Task handles
TaskHandle_t webServerTaskHandle = NULL;
TaskHandle_t telemetryTaskHandle = NULL; 
TaskHandle_t safetyTaskHandle = NULL;
TaskHandle_t motorTaskHandle = NULL;

void setup() {
    // Initialize M5StickC hardware
    M5.begin();
    M5.Lcd.setRotation(1);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(1);
    
    // Display startup message
    M5.Lcd.setCursor(2, 2);
    M5.Lcd.println("ESP Rover Secure");
    M5.Lcd.println("Initializing...");
    
    // Initialize Serial for debugging
    Serial.begin(115200);
    Serial.println("ESP Rover Secure starting...");
    
    // Initialize watchdog timer
    esp_task_wdt_init(WATCHDOG_TIMEOUT_MS / 1000, true);
    esp_task_wdt_add(NULL);
    
    // Initialize SPIFFS for configuration storage
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS initialization failed");
        g_system_state.last_error = "SPIFFS failed";
        return;
    }
    
    g_system_state.uptime_start = millis();
    
    // Initialize core components
    initializeWiFiManager();
    initializeMovementController();
    initializeSafetyMonitor();  
    initializeTelemetryCollector();
    initializeJSONSerializer();
    initializeWebServers();
    
    // Create FreeRTOS tasks for multi-threading
    xTaskCreate(
        handleWebServerTask,
        "WebServers",
        WEB_SERVER_TASK_STACK_SIZE,
        NULL,
        WEB_SERVER_TASK_PRIORITY,
        &webServerTaskHandle
    );
    
    xTaskCreate(
        handleTelemetryTask,
        "Telemetry", 
        TELEMETRY_TASK_STACK_SIZE,
        NULL,
        TELEMETRY_TASK_PRIORITY,
        &telemetryTaskHandle
    );
    
    xTaskCreate(
        handleSafetyTask,
        "Safety",
        SAFETY_TASK_STACK_SIZE, 
        NULL,
        SAFETY_TASK_PRIORITY,
        &safetyTaskHandle
    );
    
    xTaskCreate(
        handleMotorTask,
        "Motors",
        MOTOR_TASK_STACK_SIZE,
        NULL, 
        MOTOR_TASK_PRIORITY,
        &motorTaskHandle
    );
    
    // Setup mDNS
    if (MDNS.begin("esp-rover-secure")) {
        Serial.println("mDNS responder started: esp-rover-secure.local");
    }
    
    g_system_state.initialized = true;
    g_system_state.servers_running = true;
    
    Serial.println("ESP Rover Secure initialized successfully");
    
    // Update display
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(2, 2);
    M5.Lcd.println("ESP Rover Secure");
    M5.Lcd.println("System Ready");
}

void loop() {
    // Reset watchdog
    esp_task_wdt_reset();
    
    // Update M5StickC inputs
    M5.update();
    
    // Handle physical button presses
    if (M5.BtnA.wasPressed()) {
        // Toggle debug mode (implementation in respective modules)
        Serial.println("Debug mode toggle requested");
    }
    
    if (M5.BtnB.wasPressed()) {
        // Emergency stop
        g_system_state.emergency_stop = true;
        Serial.println("EMERGENCY STOP - Button B pressed");
    }
    
    // Update display periodically
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL_MS) {
        updateMainDisplay();
        lastDisplayUpdate = millis();
    }
    
    // Check system health
    checkSystemHealth();
    
    // Small delay to prevent tight loop
    delay(10);
}

void updateMainDisplay() {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(2, 2);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(1);
    
    // Title
    M5.Lcd.println("ESP ROVER SECURE");
    M5.Lcd.println("===============");
    
    // WiFi Status
    M5.Lcd.setTextColor(g_system_state.wifi_connected ? GREEN : RED);
    M5.Lcd.println("WiFi:" + String(g_system_state.wifi_connected ? "OK" : "FAIL"));
    
    // HTTPS Status
    M5.Lcd.setTextColor(g_system_state.servers_running ? GREEN : RED);
    M5.Lcd.println("HTTPS:" + String(g_system_state.servers_running ? "OK" : "FAIL"));
    
    // Safety Status
    M5.Lcd.setTextColor(g_system_state.safety_lockout ? RED : GREEN);
    M5.Lcd.println("Safety:" + String(g_system_state.safety_lockout ? "LOCK" : "OK"));
    
    // Battery
    M5.Lcd.setTextColor(g_system_state.battery_voltage > BATTERY_LOW_VOLTAGE ? GREEN : RED);
    M5.Lcd.println("Bat:" + String(g_system_state.battery_voltage, 1) + "V");
    
    // Uptime
    M5.Lcd.setTextColor(WHITE);
    unsigned long uptime = (millis() - g_system_state.uptime_start) / 1000;
    M5.Lcd.println("Up:" + String(uptime) + "s");
    
    // Emergency stop indicator
    if (g_system_state.emergency_stop) {
        M5.Lcd.setTextColor(RED);
        M5.Lcd.println("** EMERGENCY **");
    }
    
    // Error status
    if (g_system_state.last_error.length() > 0) {
        M5.Lcd.setTextColor(RED);
        String shortError = g_system_state.last_error;
        if (shortError.length() > 12) {
            shortError = shortError.substring(0, 12);
        }
        M5.Lcd.println("Err:" + shortError);
    }
    
    // Button labels
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(2, 140);
    M5.Lcd.println("A:Dbg B:STOP");
}

void checkSystemHealth() {
    // Update battery voltage
    g_system_state.battery_voltage = M5.Axp.GetBatVoltage();
    
    // Check for low battery
    if (g_system_state.battery_voltage < BATTERY_CRITICAL_VOLTAGE) {
        g_system_state.emergency_stop = true;
        Serial.println("CRITICAL: Battery voltage too low - Emergency stop");
    }
    
    // Check free heap memory
    size_t freeHeap = ESP.getFreeHeap();
    size_t totalHeap = ESP.getHeapSize();
    float heapUsagePercent = ((float)(totalHeap - freeHeap) / totalHeap) * 100;
    
    if (heapUsagePercent > MAX_HEAP_USAGE_PERCENT) {
        Serial.println("WARNING: High memory usage: " + String(heapUsagePercent) + "%");
        g_system_state.last_error = "High memory usage";
    }
    
    // Check task health (basic check if tasks are still running)
    if (webServerTaskHandle && eTaskGetState(webServerTaskHandle) == eDeleted) {
        Serial.println("ERROR: Web server task stopped");
        g_system_state.last_error = "Web server failed";
    }
    
    if (safetyTaskHandle && eTaskGetState(safetyTaskHandle) == eDeleted) {
        Serial.println("CRITICAL: Safety task stopped");
        g_system_state.emergency_stop = true;
        g_system_state.last_error = "Safety task failed";
    }
}