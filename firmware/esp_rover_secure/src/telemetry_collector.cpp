#include <M5StickC.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../include/config.h"

// Telemetry data structures
struct BatteryTelemetry {
    float voltage = 0.0;
    int percentage = 0;
    bool charging = false;
    String status = "unknown";
    float current = 0.0;
    float temperature = 0.0;
};

struct IMUTelemetry {
    struct {
        float x = 0.0;
        float y = 0.0; 
        float z = 0.0;
    } acceleration;
    
    struct {
        float x = 0.0;
        float y = 0.0;
        float z = 0.0;
    } gyroscope;
    
    struct {
        float roll = 0.0;
        float pitch = 0.0;
        float yaw = 0.0;
    } orientation;
    
    float max_tilt = 0.0;
};

struct SystemTelemetry {
    unsigned long uptime = 0;
    int wifi_signal = 0;
    size_t free_memory = 0;
    size_t total_memory = 0;
    bool wifi_connected = false;
    float cpu_temperature = 0.0;
    String last_error = "";
};

struct NetworkTelemetry {
    String ip_address = "";
    String mac_address = "";
    String ssid = "";
    int rssi = 0;
    unsigned long bytes_sent = 0;
    unsigned long bytes_received = 0;
    unsigned long connection_uptime = 0;
};

// Telemetry collector state
struct TelemetryState {
    BatteryTelemetry battery;
    IMUTelemetry imu;
    SystemTelemetry system;
    NetworkTelemetry network;
    
    unsigned long last_collection_time = 0;
    unsigned long collection_count = 0;
    bool aws_submission_enabled = false;
    String aws_endpoint = "";
    
    // Telemetry history for trending
    float battery_history[10] = {0};
    float tilt_history[10] = {0};
    int history_index = 0;
} telemetry_state;

// External references
extern String getMotorStatus();
extern String getSafetyStatus();
extern String getWiFiStatus();
extern String getWebServerStats();

extern struct SystemState {
    bool initialized;
    bool wifi_connected;
    bool servers_running;
    bool safety_lockout;
    unsigned long uptime_start;
    String last_error;
    float battery_voltage;
    bool emergency_stop;
} g_system_state;

// Function declarations
void collectBatteryTelemetry();
void collectIMUTelemetry();
void collectSystemTelemetry();
void collectNetworkTelemetry();
void updateTelemetryHistory();
void saveTelemetryToFile();
bool submitTelemetryToAWS();
String formatTelemetryJSON();

void initializeTelemetryCollector() {
    Serial.println("Initializing Telemetry Collector...");
    
    // Initialize telemetry state
    telemetry_state.last_collection_time = millis();
    telemetry_state.collection_count = 0;
    
    // Load AWS configuration if available
    if (SPIFFS.exists("/aws_config.json")) {
        File file = SPIFFS.open("/aws_config.json", "r");
        if (file) {
            DynamicJsonDocument doc(512);
            deserializeJson(doc, file);
            telemetry_state.aws_endpoint = doc["endpoint"] | "";
            telemetry_state.aws_submission_enabled = !telemetry_state.aws_endpoint.isEmpty();
            file.close();
        }
    }
    
    Serial.println("Telemetry Collector initialized");
    if (telemetry_state.aws_submission_enabled) {
        Serial.println("AWS telemetry submission enabled: " + telemetry_state.aws_endpoint);
    }
}

void collectBatteryTelemetry() {
    // Get battery voltage from AXP192 power management IC
    telemetry_state.battery.voltage = M5.Axp.GetBatVoltage();
    
    // Calculate percentage (approximate based on lithium ion discharge curve)
    float voltage = telemetry_state.battery.voltage;
    if (voltage > BATTERY_FULL_VOLTAGE) {
        telemetry_state.battery.percentage = 100;
    } else if (voltage < BATTERY_CRITICAL_VOLTAGE) {
        telemetry_state.battery.percentage = 0;
    } else {
        // Linear interpolation between critical and full voltage
        telemetry_state.battery.percentage = (int)((voltage - BATTERY_CRITICAL_VOLTAGE) / 
            (BATTERY_FULL_VOLTAGE - BATTERY_CRITICAL_VOLTAGE) * 100);
    }
    
    // Determine charging status (check if USB power is present)
    telemetry_state.battery.charging = M5.Axp.GetVBusVoltage() > 4.0;
    
    // Get additional battery metrics if available
    telemetry_state.battery.current = M5.Axp.GetBatCurrent();
    telemetry_state.battery.temperature = M5.Axp.GetTempInAXP192();
    
    // Determine status
    if (voltage < BATTERY_CRITICAL_VOLTAGE) {
        telemetry_state.battery.status = "critical";
    } else if (voltage < BATTERY_LOW_VOLTAGE) {
        telemetry_state.battery.status = "low";
    } else if (telemetry_state.battery.charging) {
        telemetry_state.battery.status = "charging";
    } else {
        telemetry_state.battery.status = "normal";
    }
}

void collectIMUTelemetry() {
    // Get accelerometer data
    M5.IMU.getAccelData(&telemetry_state.imu.acceleration.x, 
                        &telemetry_state.imu.acceleration.y, 
                        &telemetry_state.imu.acceleration.z);
    
    // Get gyroscope data
    M5.IMU.getGyroData(&telemetry_state.imu.gyroscope.x,
                       &telemetry_state.imu.gyroscope.y,
                       &telemetry_state.imu.gyroscope.z);
    
    // Calculate orientation (roll, pitch, yaw)
    float accelX = telemetry_state.imu.acceleration.x;
    float accelY = telemetry_state.imu.acceleration.y;
    float accelZ = telemetry_state.imu.acceleration.z;
    
    telemetry_state.imu.orientation.roll = atan2(accelY, accelZ) * 180.0 / PI;
    telemetry_state.imu.orientation.pitch = atan2(-accelX, sqrt(accelY * accelY + accelZ * accelZ)) * 180.0 / PI;
    
    // For yaw, we would need magnetometer or integration - using static value for now
    // In production, this would integrate gyroscope Z-axis over time
    static float yaw_accumulator = 0.0;
    yaw_accumulator += telemetry_state.imu.gyroscope.z * (TELEMETRY_INTERVAL_MS / 1000.0);
    telemetry_state.imu.orientation.yaw = yaw_accumulator;
    
    // Keep yaw in reasonable range
    if (telemetry_state.imu.orientation.yaw > 180.0) telemetry_state.imu.orientation.yaw -= 360.0;
    if (telemetry_state.imu.orientation.yaw < -180.0) telemetry_state.imu.orientation.yaw += 360.0;
    
    // Calculate maximum tilt angle
    float current_tilt = max(abs(telemetry_state.imu.orientation.roll), 
                           abs(telemetry_state.imu.orientation.pitch));
    if (current_tilt > telemetry_state.imu.max_tilt) {
        telemetry_state.imu.max_tilt = current_tilt;
    }
}

void collectSystemTelemetry() {
    // System uptime
    telemetry_state.system.uptime = (millis() - g_system_state.uptime_start) / 1000;
    
    // Memory information
    telemetry_state.system.free_memory = ESP.getFreeHeap();
    telemetry_state.system.total_memory = ESP.getHeapSize();
    
    // WiFi information
    telemetry_state.system.wifi_connected = (WiFi.status() == WL_CONNECTED);
    if (telemetry_state.system.wifi_connected) {
        telemetry_state.system.wifi_signal = WiFi.RSSI();
    } else {
        telemetry_state.system.wifi_signal = 0;
    }
    
    // CPU temperature (if available)
    telemetry_state.system.cpu_temperature = temperatureRead();
    
    // Last error from global state
    telemetry_state.system.last_error = g_system_state.last_error;
}

void collectNetworkTelemetry() {
    if (WiFi.status() == WL_CONNECTED) {
        telemetry_state.network.ip_address = WiFi.localIP().toString();
        telemetry_state.network.ssid = WiFi.SSID();
        telemetry_state.network.rssi = WiFi.RSSI();
        
        // Network uptime (time since WiFi connected)
        static unsigned long wifi_connect_time = 0;
        static bool was_connected = false;
        
        if (!was_connected) {
            wifi_connect_time = millis();
            was_connected = true;
        }
        
        telemetry_state.network.connection_uptime = (millis() - wifi_connect_time) / 1000;
        
    } else {
        telemetry_state.network.connection_uptime = 0;
    }
    
    // MAC address
    telemetry_state.network.mac_address = WiFi.macAddress();
    
    // Network byte counters would need to be implemented at the network layer
    // For now, using placeholder values
}

void updateTelemetryHistory() {
    // Update circular buffers for trending data
    telemetry_state.battery_history[telemetry_state.history_index] = telemetry_state.battery.voltage;
    telemetry_state.tilt_history[telemetry_state.history_index] = 
        max(abs(telemetry_state.imu.orientation.roll), abs(telemetry_state.imu.orientation.pitch));
    
    telemetry_state.history_index = (telemetry_state.history_index + 1) % 10;
}

String formatTelemetryJSON() {
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    
    // Add timestamp
    doc["timestamp"] = millis();
    doc["collection_count"] = telemetry_state.collection_count;
    
    // Battery telemetry
    JsonObject battery = doc.createNestedObject("battery");
    battery["voltage"] = telemetry_state.battery.voltage;
    battery["percentage"] = telemetry_state.battery.percentage;
    battery["charging"] = telemetry_state.battery.charging;
    battery["status"] = telemetry_state.battery.status;
    battery["current"] = telemetry_state.battery.current;
    battery["temperature"] = telemetry_state.battery.temperature;
    
    // IMU telemetry
    JsonObject imu = doc.createNestedObject("imu");
    JsonObject acceleration = imu.createNestedObject("acceleration");
    acceleration["x"] = telemetry_state.imu.acceleration.x;
    acceleration["y"] = telemetry_state.imu.acceleration.y;
    acceleration["z"] = telemetry_state.imu.acceleration.z;
    
    JsonObject gyroscope = imu.createNestedObject("gyroscope");
    gyroscope["x"] = telemetry_state.imu.gyroscope.x;
    gyroscope["y"] = telemetry_state.imu.gyroscope.y;
    gyroscope["z"] = telemetry_state.imu.gyroscope.z;
    
    JsonObject orientation = imu.createNestedObject("orientation");
    orientation["roll"] = telemetry_state.imu.orientation.roll;
    orientation["pitch"] = telemetry_state.imu.orientation.pitch;
    orientation["yaw"] = telemetry_state.imu.orientation.yaw;
    
    imu["max_tilt_detected"] = telemetry_state.imu.max_tilt;
    
    // System telemetry
    JsonObject system = doc.createNestedObject("system");
    system["uptime"] = telemetry_state.system.uptime;
    system["wifi_signal"] = telemetry_state.system.wifi_signal;
    system["free_memory"] = telemetry_state.system.free_memory;
    system["memory_usage_percent"] = ((float)(telemetry_state.system.total_memory - telemetry_state.system.free_memory) / telemetry_state.system.total_memory) * 100;
    system["wifi_connected"] = telemetry_state.system.wifi_connected;
    system["cpu_temperature"] = telemetry_state.system.cpu_temperature;
    system["last_error"] = telemetry_state.system.last_error;
    
    // Network telemetry
    JsonObject network = doc.createNestedObject("network");
    network["ip_address"] = telemetry_state.network.ip_address;
    network["mac_address"] = telemetry_state.network.mac_address;
    network["ssid"] = telemetry_state.network.ssid;
    network["rssi"] = telemetry_state.network.rssi;
    network["connection_uptime"] = telemetry_state.network.connection_uptime;
    
    // Add component status from other modules
    // This would integrate with other modules' status functions
    
    String result;
    serializeJson(doc, result);
    return result;
}

void saveTelemetryToFile() {
    // Save recent telemetry data to file for persistence
    File file = SPIFFS.open("/telemetry_latest.json", "w");
    if (file) {
        String telemetryJSON = formatTelemetryJSON();
        file.print(telemetryJSON);
        file.close();
    }
}

bool submitTelemetryToAWS() {
    if (!telemetry_state.aws_submission_enabled || telemetry_state.aws_endpoint.isEmpty()) {
        return false;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Cannot submit telemetry: WiFi not connected");
        return false;
    }
    
    // Create telemetry submission payload
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    doc["rover_id"] = WiFi.macAddress();
    doc["telemetry"] = serialized(formatTelemetryJSON());
    doc["submission_time"] = millis();
    
    String payload;
    serializeJson(doc, payload);
    
    // Submit via HTTPS POST (implementation would use WiFiClientSecure)
    // This is a placeholder for AWS submission logic
    Serial.println("Submitting telemetry to AWS: " + telemetry_state.aws_endpoint);
    Serial.println("Payload size: " + String(payload.length()) + " bytes");
    
    // In production, implement actual HTTPS POST to AWS API Gateway/Lambda
    return true;
}

// API functions
String getCurrentTelemetry() {
    return formatTelemetryJSON();
}

void setAWSEndpoint(const String& endpoint) {
    telemetry_state.aws_endpoint = endpoint;
    telemetry_state.aws_submission_enabled = !endpoint.isEmpty();
    
    // Save to configuration file
    File file = SPIFFS.open("/aws_config.json", "w");
    if (file) {
        DynamicJsonDocument doc(256);
        doc["endpoint"] = endpoint;
        serializeJson(doc, file);
        file.close();
    }
    
    Serial.println("AWS endpoint configured: " + endpoint);
}

String getTelemetryStats() {
    DynamicJsonDocument doc(512);
    
    doc["collection_count"] = telemetry_state.collection_count;
    doc["last_collection_time"] = telemetry_state.last_collection_time;
    doc["aws_submission_enabled"] = telemetry_state.aws_submission_enabled;
    doc["aws_endpoint"] = telemetry_state.aws_endpoint;
    doc["collection_interval_ms"] = TELEMETRY_INTERVAL_MS;
    
    // Battery trend (average of last 10 readings)
    float battery_average = 0.0;
    for (int i = 0; i < 10; i++) {
        battery_average += telemetry_state.battery_history[i];
    }
    battery_average /= 10.0;
    doc["battery_trend_average"] = battery_average;
    
    // Tilt trend (max of last 10 readings)
    float max_recent_tilt = 0.0;
    for (int i = 0; i < 10; i++) {
        if (telemetry_state.tilt_history[i] > max_recent_tilt) {
            max_recent_tilt = telemetry_state.tilt_history[i];
        }
    }
    doc["max_recent_tilt"] = max_recent_tilt;
    
    String result;
    serializeJson(doc, result);
    return result;
}

// Telemetry collection task
void handleTelemetryTask(void* parameter) {
    while (true) {
        unsigned long current_time = millis();
        
        // Collect all telemetry data
        collectBatteryTelemetry();
        collectIMUTelemetry();
        collectSystemTelemetry();
        collectNetworkTelemetry();
        
        // Update trending data
        updateTelemetryHistory();
        
        // Update collection statistics
        telemetry_state.last_collection_time = current_time;
        telemetry_state.collection_count++;
        
        // Save telemetry to file
        saveTelemetryToFile();
        
        // Submit to AWS if enabled
        if (telemetry_state.aws_submission_enabled) {
            submitTelemetryToAWS();
        }
        
        // Update global system state with key metrics
        g_system_state.battery_voltage = telemetry_state.battery.voltage;
        g_system_state.wifi_connected = telemetry_state.system.wifi_connected;
        
        // Task delay
        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_INTERVAL_MS));
    }
}