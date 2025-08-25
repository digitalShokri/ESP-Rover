#include <ArduinoJson.h>
#include <M5StickC.h>
#include <WiFi.h>

#include "../include/config.h"

// JSON serializer utility functions
void initializeJSONSerializer() {
    Serial.println("Initializing JSON Serializer...");
    // No specific initialization needed for ArduinoJson
    Serial.println("JSON Serializer initialized");
}

String createSuccessResponse(const String& message, const JsonObject& data = JsonObject()) {
    DynamicJsonDocument doc(512);
    
    doc["status"] = "success";
    doc["message"] = message;
    doc["timestamp"] = millis();
    
    if (!data.isNull()) {
        doc["data"] = data;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

String createErrorResponse(const String& error, int error_code = 400) {
    DynamicJsonDocument doc(256);
    
    doc["status"] = "error";
    doc["error"] = error;
    doc["error_code"] = error_code;
    doc["timestamp"] = millis();
    
    String result;
    serializeJson(doc, result);
    return result;
}

String createMovementResponse(const String& command, const String& status) {
    DynamicJsonDocument doc(256);
    
    doc["status"] = status;
    doc["command"] = command;
    doc["timestamp"] = millis();
    
    if (status == "success") {
        doc["message"] = "Movement command executed successfully";
    } else {
        doc["message"] = "Movement command failed";
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

String createHealthCheckResponse() {
    DynamicJsonDocument doc(512);
    
    // External references
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
    
    doc["status"] = "healthy";
    doc["timestamp"] = millis();
    doc["uptime_seconds"] = (millis() - g_system_state.uptime_start) / 1000;
    doc["memory_free"] = ESP.getFreeHeap();
    doc["memory_total"] = ESP.getHeapSize();
    doc["wifi_connected"] = g_system_state.wifi_connected;
    doc["servers_running"] = g_system_state.servers_running;
    doc["safety_lockout"] = g_system_state.safety_lockout;
    doc["emergency_stop"] = g_system_state.emergency_stop;
    
    if (g_system_state.wifi_connected) {
        doc["wifi_ssid"] = WiFi.SSID();
        doc["wifi_rssi"] = WiFi.RSSI();
        doc["ip_address"] = WiFi.localIP().toString();
    }
    
    if (!g_system_state.last_error.isEmpty()) {
        doc["last_error"] = g_system_state.last_error;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

String createEmergencyStopResponse() {
    DynamicJsonDocument doc(256);
    
    doc["status"] = "success";
    doc["message"] = "Emergency stop activated";
    doc["timestamp"] = millis();
    doc["motors_stopped"] = true;
    doc["action_taken"] = "All motor commands halted immediately";
    
    String result;
    serializeJson(doc, result);
    return result;
}

// Create comprehensive status response with all system telemetry
String createStatusResponse() {
    // This function integrates data from all system components
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    
    extern struct SystemState g_system_state;
    extern String getCurrentTelemetry();
    extern String getMotorStatus(); 
    extern String getSafetyStatus();
    extern String getWiFiStatus();
    extern String getWebServerStats();
    
    // Main timestamp and system info
    doc["timestamp"] = millis();
    doc["rover_id"] = WiFi.macAddress();
    doc["firmware_version"] = "1.0.0-secure";
    
    // Parse and merge telemetry data
    DynamicJsonDocument telemetryDoc(JSON_BUFFER_SIZE);
    deserializeJson(telemetryDoc, getCurrentTelemetry());
    
    // Copy key telemetry sections
    if (telemetryDoc.containsKey("battery")) {
        doc["battery"] = telemetryDoc["battery"];
    }
    
    if (telemetryDoc.containsKey("imu")) {
        doc["imu"] = telemetryDoc["imu"];
    }
    
    if (telemetryDoc.containsKey("system")) {
        doc["system"] = telemetryDoc["system"];
    }
    
    if (telemetryDoc.containsKey("network")) {
        doc["network"] = telemetryDoc["network"];
    }
    
    // Parse and add motor status
    DynamicJsonDocument motorDoc(512);
    deserializeJson(motorDoc, getMotorStatus());
    if (motorDoc.containsKey("motor1")) {
        doc["motors"] = motorDoc;
    }
    
    // Parse and add safety status
    DynamicJsonDocument safetyDoc(512);
    deserializeJson(safetyDoc, getSafetyStatus());
    if (safetyDoc.containsKey("tilt_protection")) {
        doc["safety"] = safetyDoc;
    }
    
    // Parse and add WiFi status details
    DynamicJsonDocument wifiDoc(512);
    deserializeJson(wifiDoc, getWiFiStatus());
    if (wifiDoc.containsKey("connected")) {
        JsonObject wifi = doc.createNestedObject("wifi_details");
        wifi["connected"] = wifiDoc["connected"];
        wifi["ap_mode"] = wifiDoc["ap_mode"];
        wifi["signal_strength"] = wifiDoc["signal_strength"];
        wifi["connection_failures"] = wifiDoc["connection_failures"];
    }
    
    // Parse and add web server statistics
    DynamicJsonDocument serverDoc(512);
    deserializeJson(serverDoc, getWebServerStats());
    if (serverDoc.containsKey("total_requests")) {
        doc["server_stats"] = serverDoc;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

// Configuration response helpers
String createConfigResponse(const JsonObject& config) {
    DynamicJsonDocument doc(512);
    
    doc["status"] = "success";
    doc["message"] = "Configuration retrieved";
    doc["timestamp"] = millis();
    doc["config"] = config;
    
    String result;
    serializeJson(doc, result);
    return result;
}

String createConfigUpdateResponse(const String& component) {
    DynamicJsonDocument doc(256);
    
    doc["status"] = "success";
    doc["message"] = component + " configuration updated successfully";
    doc["timestamp"] = millis();
    doc["restart_required"] = (component == "wifi" || component == "network");
    
    String result;
    serializeJson(doc, result);
    return result;
}

// Utility functions for parsing incoming JSON
bool parseMovementCommand(const String& json, String& command, int& speed, int& duration, bool& continuous) {
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        Serial.println("JSON parsing error: " + String(error.c_str()));
        return false;
    }
    
    command = doc["command"] | "";
    speed = doc["speed"] | SPEED_NORMAL_PWM;
    duration = doc["duration"] | MOTOR_TIMEOUT_MS;
    continuous = doc["continuous"] | false;
    
    // Validate command
    if (command.isEmpty()) {
        Serial.println("Empty command in JSON");
        return false;
    }
    
    // Clamp values to safe ranges
    speed = constrain(speed, 0, MAX_SPEED_PWM);
    duration = constrain(duration, 0, 30000); // Max 30 seconds
    
    return true;
}

bool parseConfigurationUpdate(const String& json, String& component, JsonObject& config) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        Serial.println("Configuration JSON parsing error: " + String(error.c_str()));
        return false;
    }
    
    component = doc["component"] | "";
    if (component.isEmpty()) {
        Serial.println("No component specified in configuration update");
        return false;
    }
    
    if (!doc.containsKey("config")) {
        Serial.println("No config data in configuration update");
        return false;
    }
    
    config = doc["config"];
    return true;
}

// Response formatting for different content types
String formatJSONResponse(const String& json) {
    // Validate and format JSON response
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        // Return error response if JSON is invalid
        return createErrorResponse("Invalid JSON format", 500);
    }
    
    // Re-serialize with proper formatting
    String result;
    serializeJson(doc, result);
    return result;
}

String formatHTMLResponse(const String& title, const String& content) {
    String html = "<!DOCTYPE html><html><head>";
    html += "<title>" + title + "</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }";
    html += ".container { max-width: 800px; margin: 0 auto; }";
    html += ".header { text-align: center; padding: 20px; background: #333; border-radius: 10px; margin-bottom: 20px; }";
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<div class='header'><h1>" + title + "</h1></div>";
    html += content;
    html += "</div></body></html>";
    
    return html;
}

// Specialized response builders for different endpoints
String createTelemetryResponse(const String& format = "json") {
    extern String getCurrentTelemetry();
    
    String telemetry = getCurrentTelemetry();
    
    if (format == "html") {
        // Create HTML formatted telemetry display
        DynamicJsonDocument doc(JSON_BUFFER_SIZE);
        deserializeJson(doc, telemetry);
        
        String content = "<h2>Real-time Telemetry</h2>";
        content += "<div style='display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px;'>";
        
        if (doc.containsKey("battery")) {
            content += "<div style='background: #333; padding: 15px; border-radius: 5px;'>";
            content += "<h3>Battery</h3>";
            content += "<p>Voltage: " + String(doc["battery"]["voltage"].as<float>(), 1) + "V</p>";
            content += "<p>Percentage: " + String(doc["battery"]["percentage"].as<int>()) + "%</p>";
            content += "<p>Status: " + doc["battery"]["status"].as<String>() + "</p>";
            content += "</div>";
        }
        
        if (doc.containsKey("imu")) {
            content += "<div style='background: #333; padding: 15px; border-radius: 5px;'>";
            content += "<h3>Orientation</h3>";
            content += "<p>Roll: " + String(doc["imu"]["orientation"]["roll"].as<float>(), 1) + "°</p>";
            content += "<p>Pitch: " + String(doc["imu"]["orientation"]["pitch"].as<float>(), 1) + "°</p>";
            content += "<p>Max Tilt: " + String(doc["imu"]["max_tilt_detected"].as<float>(), 1) + "°</p>";
            content += "</div>";
        }
        
        content += "</div>";
        content += "<p><a href='/'>← Back to Control Panel</a></p>";
        
        return formatHTMLResponse("ESP Rover Telemetry", content);
    }
    
    return formatJSONResponse(telemetry);
}

// Error response builders
String createValidationErrorResponse(const String& field, const String& issue) {
    DynamicJsonDocument doc(256);
    
    doc["status"] = "error";
    doc["error_type"] = "validation_error";
    doc["field"] = field;
    doc["issue"] = issue;
    doc["timestamp"] = millis();
    
    String result;
    serializeJson(doc, result);
    return result;
}

String createAuthenticationErrorResponse() {
    DynamicJsonDocument doc(256);
    
    doc["status"] = "error";
    doc["error_type"] = "authentication_error";
    doc["message"] = "Authentication required";
    doc["timestamp"] = millis();
    
    String result;
    serializeJson(doc, result);
    return result;
}

String createRateLimitErrorResponse(int retry_after_seconds) {
    DynamicJsonDocument doc(256);
    
    doc["status"] = "error";
    doc["error_type"] = "rate_limit_exceeded";
    doc["message"] = "Too many requests";
    doc["retry_after"] = retry_after_seconds;
    doc["timestamp"] = millis();
    
    String result;
    serializeJson(doc, result);
    return result;
}