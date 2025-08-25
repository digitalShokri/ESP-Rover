#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <M5StickC.h>

#include "../include/config.h"

// WiFi configuration structure
struct WiFiConfig {
    String ssid = "";
    String password = "";
    bool use_static_ip = false;
    IPAddress static_ip;
    IPAddress gateway;
    IPAddress subnet;
    IPAddress dns1;
    IPAddress dns2;
} wifi_config;

// WiFi state
struct WiFiState {
    bool connected = false;
    bool ap_mode = false;
    String ip_address = "";
    int signal_strength = 0;
    unsigned long last_reconnect_attempt = 0;
    String ap_name = "";
    int connection_failures = 0;
} wifi_state;

// External system state reference
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
bool loadWiFiConfig();
void saveWiFiConfig();
void startAccessPoint();
void connectToWiFi();
void handleWiFiReconnection();
void updateWiFiStatus();

void initializeWiFiManager() {
    Serial.println("Initializing WiFi Manager...");
    
    // Load saved WiFi configuration
    if (!loadWiFiConfig()) {
        Serial.println("No WiFi config found, will start in AP mode");
    }
    
    // Set WiFi mode and attempt connection
    if (wifi_config.ssid.length() > 0) {
        connectToWiFi();
    } else {
        startAccessPoint();
    }
    
    Serial.println("WiFi Manager initialized");
}

bool loadWiFiConfig() {
    if (!SPIFFS.exists("/wifi_config.json")) {
        return false;
    }
    
    File file = SPIFFS.open("/wifi_config.json", "r");
    if (!file) {
        Serial.println("Failed to open WiFi config file");
        return false;
    }
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.println("Failed to parse WiFi config: " + String(error.c_str()));
        return false;
    }
    
    wifi_config.ssid = doc["ssid"] | "";
    wifi_config.password = doc["password"] | "";
    wifi_config.use_static_ip = doc["use_static_ip"] | false;
    
    if (wifi_config.use_static_ip) {
        wifi_config.static_ip.fromString(doc["static_ip"] | "");
        wifi_config.gateway.fromString(doc["gateway"] | "");
        wifi_config.subnet.fromString(doc["subnet"] | "255.255.255.0");
        wifi_config.dns1.fromString(doc["dns1"] | "8.8.8.8");
        wifi_config.dns2.fromString(doc["dns2"] | "8.8.4.4");
    }
    
    Serial.println("WiFi configuration loaded for SSID: " + wifi_config.ssid);
    return true;
}

void saveWiFiConfig() {
    File file = SPIFFS.open("/wifi_config.json", "w");
    if (!file) {
        Serial.println("Failed to save WiFi config file");
        return;
    }
    
    DynamicJsonDocument doc(1024);
    doc["ssid"] = wifi_config.ssid;
    doc["password"] = wifi_config.password;
    doc["use_static_ip"] = wifi_config.use_static_ip;
    
    if (wifi_config.use_static_ip) {
        doc["static_ip"] = wifi_config.static_ip.toString();
        doc["gateway"] = wifi_config.gateway.toString();
        doc["subnet"] = wifi_config.subnet.toString();
        doc["dns1"] = wifi_config.dns1.toString();
        doc["dns2"] = wifi_config.dns2.toString();
    }
    
    serializeJson(doc, file);
    file.close();
    
    Serial.println("WiFi configuration saved");
}

void startAccessPoint() {
    // Generate unique AP name using MAC address
    wifi_state.ap_name = String(AP_NAME_PREFIX) + WiFi.macAddress().substring(12);
    wifi_state.ap_name.replace(":", "");
    
    WiFi.mode(WIFI_AP);
    bool ap_started = WiFi.softAP(wifi_state.ap_name.c_str(), DEFAULT_AP_PASSWORD);
    
    if (ap_started) {
        wifi_state.ap_mode = true;
        wifi_state.ip_address = WiFi.softAPIP().toString();
        Serial.println("Access Point started: " + wifi_state.ap_name);
        Serial.println("AP IP Address: " + wifi_state.ip_address);
        Serial.println("AP Password: " + String(DEFAULT_AP_PASSWORD));
    } else {
        Serial.println("Failed to start Access Point");
        g_system_state.last_error = "AP start failed";
    }
}

void connectToWiFi() {
    Serial.println("Attempting to connect to WiFi: " + wifi_config.ssid);
    
    WiFi.mode(WIFI_STA);
    
    // Configure static IP if enabled
    if (wifi_config.use_static_ip) {
        if (!WiFi.config(wifi_config.static_ip, wifi_config.gateway, 
                         wifi_config.subnet, wifi_config.dns1, wifi_config.dns2)) {
            Serial.println("Failed to configure static IP");
        } else {
            Serial.println("Static IP configured: " + wifi_config.static_ip.toString());
        }
    }
    
    WiFi.begin(wifi_config.ssid.c_str(), wifi_config.password.c_str());
    
    // Wait for connection with timeout
    unsigned long start_time = millis();
    while (WiFi.status() != WL_CONNECTED && 
           (millis() - start_time) < WIFI_CONNECTION_TIMEOUT) {
        delay(500);
        Serial.print(".");
        
        // Update display during connection
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(2, 2);
        M5.Lcd.println("Connecting WiFi...");
        M5.Lcd.println(wifi_config.ssid);
        
        unsigned long elapsed = millis() - start_time;
        int progress = (elapsed * 100) / WIFI_CONNECTION_TIMEOUT;
        M5.Lcd.println("Progress: " + String(progress) + "%");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifi_state.connected = true;
        wifi_state.ap_mode = false;
        wifi_state.ip_address = WiFi.localIP().toString();
        wifi_state.signal_strength = WiFi.RSSI();
        wifi_state.connection_failures = 0;
        
        g_system_state.wifi_connected = true;
        
        Serial.println("");
        Serial.println("WiFi connected successfully!");
        Serial.println("IP Address: " + wifi_state.ip_address);
        Serial.println("Signal Strength: " + String(wifi_state.signal_strength) + " dBm");
        
        // Configure NTP for time synchronization (needed for SSL certificates)
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        
    } else {
        Serial.println("");
        Serial.println("WiFi connection failed");
        wifi_state.connection_failures++;
        g_system_state.last_error = "WiFi connect fail";
        
        // Fall back to AP mode after too many failures
        if (wifi_state.connection_failures >= 3) {
            Serial.println("Too many connection failures, starting AP mode");
            startAccessPoint();
        }
    }
}

void handleWiFiReconnection() {
    // Only attempt reconnection if we should be in station mode and we're not connected
    if (wifi_config.ssid.length() > 0 && !wifi_state.connected && !wifi_state.ap_mode) {
        unsigned long now = millis();
        
        if (now - wifi_state.last_reconnect_attempt > WIFI_RECONNECT_INTERVAL) {
            Serial.println("Attempting WiFi reconnection...");
            wifi_state.last_reconnect_attempt = now;
            connectToWiFi();
        }
    }
}

void updateWiFiStatus() {
    if (wifi_state.connected) {
        // Check if we're still connected
        if (WiFi.status() != WL_CONNECTED) {
            wifi_state.connected = false;
            g_system_state.wifi_connected = false;
            Serial.println("WiFi connection lost");
            g_system_state.last_error = "WiFi disconnected";
        } else {
            // Update signal strength
            wifi_state.signal_strength = WiFi.RSSI();
        }
    }
}

// WiFi Manager API functions for web interface
void setWiFiCredentials(const String& ssid, const String& password) {
    wifi_config.ssid = ssid;
    wifi_config.password = password;
    saveWiFiConfig();
    
    Serial.println("WiFi credentials updated for SSID: " + ssid);
    
    // Attempt to connect with new credentials
    connectToWiFi();
}

void setStaticIPConfig(bool use_static, const String& ip, const String& gateway, 
                      const String& subnet, const String& dns1, const String& dns2) {
    wifi_config.use_static_ip = use_static;
    
    if (use_static) {
        wifi_config.static_ip.fromString(ip);
        wifi_config.gateway.fromString(gateway);
        wifi_config.subnet.fromString(subnet);
        wifi_config.dns1.fromString(dns1);
        wifi_config.dns2.fromString(dns2);
    }
    
    saveWiFiConfig();
    Serial.println("Static IP configuration updated");
}

String getWiFiStatus() {
    DynamicJsonDocument doc(1024);
    
    doc["connected"] = wifi_state.connected;
    doc["ap_mode"] = wifi_state.ap_mode;
    doc["ip_address"] = wifi_state.ip_address;
    doc["signal_strength"] = wifi_state.signal_strength;
    doc["ssid"] = wifi_state.connected ? WiFi.SSID() : wifi_config.ssid;
    doc["ap_name"] = wifi_state.ap_name;
    doc["connection_failures"] = wifi_state.connection_failures;
    doc["use_static_ip"] = wifi_config.use_static_ip;
    
    if (wifi_config.use_static_ip) {
        doc["static_ip"] = wifi_config.static_ip.toString();
        doc["gateway"] = wifi_config.gateway.toString();
        doc["subnet"] = wifi_config.subnet.toString();
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

// Task function for WiFi management
void handleWiFiTask(void* parameter) {
    while (true) {
        updateWiFiStatus();
        handleWiFiReconnection();
        
        // Task delay
        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
    }
}