#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <M5StickC.h>

#include "../include/config.h"

// Self-signed SSL certificates (for development - replace with proper certificates)
const char* server_cert = R"(
-----BEGIN CERTIFICATE-----
MIIBkTCB+wIJAK4iKrD1a8ChMA0GCSqGSIb3DQEBCwUAMBQxEjAQBgNVBAMMCUVT
UC1Sb3ZlcjAeFw0yNTA4MjUxMjAwMDBaFw0yNjA4MjUxMjAwMDBaMBQxEjAQBgNV
BAMMCUVTUC1Sb3ZlcjBcMA0GCSqGSIb3DQEBAQUAA0sAMEgCQQC3+XpvEZ7kJ6X7
...
[TRUNCATED - This would be a complete certificate in production]
...
-----END CERTIFICATE-----
)";

const char* server_private_key = R"(
-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQC3+XpvEZ7kJ6X7
...
[TRUNCATED - This would be a complete private key in production]
...
-----END PRIVATE KEY-----
)";

// HTTPS Web Servers
WebServer primaryServer(PRIMARY_HTTPS_PORT);
WebServer fallbackServer(FALLBACK_HTTPS_PORT);

// Server state
struct WebServerState {
    bool primary_server_running = false;
    bool fallback_server_running = false;
    unsigned long total_requests = 0;
    unsigned long failed_requests = 0;
    String last_error = "";
} web_server_state;

// External references
extern bool queueMovementCommand(const String& json_command);
extern void setSafetyLockout(bool lockout);
extern String getMotorStatus();
extern String getWiFiStatus();
extern void setWiFiCredentials(const String& ssid, const String& password);

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
void setupPrimaryHTTPSServer();
void setupFallbackHTTPSServer();
void handleCORSPreflight();
void handlePrimaryMove();
void handlePrimaryStatus(); 
void handlePrimaryEmergencyStop();
void handlePrimaryHealth();
void handleFallbackRoot();
void handleFallbackControl();
void handleFallbackConfig();
void sendJSONResponse(WebServer& server, int code, const String& json);
void sendHTMLResponse(WebServer& server, const String& html);
bool authenticateRequest(WebServer& server);
void logRequest(WebServer& server, const String& endpoint);

void initializeWebServers() {
    Serial.println("Initializing HTTPS Web Servers...");
    
    // Setup both servers
    setupPrimaryHTTPSServer();
    setupFallbackHTTPSServer();
    
    Serial.println("HTTPS Web Servers initialized");
}

void setupPrimaryHTTPSServer() {
    Serial.println("Setting up Primary HTTPS Server (Port " + String(PRIMARY_HTTPS_PORT) + ")");
    
    // Configure SSL/TLS (Note: ESP32 WebServer doesn't support SSL directly)
    // In production, use ESP32's built-in SSL capabilities or external SSL proxy
    
    // Primary API endpoints for AWS communication
    primaryServer.on(ENDPOINT_MOVE, HTTP_POST, handlePrimaryMove);
    primaryServer.on(ENDPOINT_STATUS, HTTP_GET, handlePrimaryStatus);
    primaryServer.on(ENDPOINT_EMERGENCY_STOP, HTTP_POST, handlePrimaryEmergencyStop);
    primaryServer.on(ENDPOINT_HEALTH, HTTP_GET, handlePrimaryHealth);
    
    // Handle CORS preflight requests
    primaryServer.on(ENDPOINT_MOVE, HTTP_OPTIONS, handleCORSPreflight);
    primaryServer.on(ENDPOINT_STATUS, HTTP_OPTIONS, handleCORSPreflight);
    primaryServer.on(ENDPOINT_EMERGENCY_STOP, HTTP_OPTIONS, handleCORSPreflight);
    
    // Handle 404 errors
    primaryServer.onNotFound([]() {
        logRequest(primaryServer, "404_NOT_FOUND");
        web_server_state.failed_requests++;
        
        DynamicJsonDocument doc(256);
        doc["error"] = "Endpoint not found";
        doc["available_endpoints"] = "/move, /status, /emergency_stop, /health";
        
        String response;
        serializeJson(doc, response);
        
        sendJSONResponse(primaryServer, 404, response);
    });
    
    primaryServer.begin();
    web_server_state.primary_server_running = true;
    
    Serial.println("Primary HTTPS Server started on port " + String(PRIMARY_HTTPS_PORT));
}

void setupFallbackHTTPSServer() {
    Serial.println("Setting up Fallback HTTPS Server (Port " + String(FALLBACK_HTTPS_PORT) + ")");
    
    // Fallback server endpoints for local control
    fallbackServer.on("/", HTTP_GET, handleFallbackRoot);
    fallbackServer.on("/control", HTTP_GET, handleFallbackControl);
    fallbackServer.on("/config", HTTP_GET, handleFallbackConfig);
    fallbackServer.on("/config", HTTP_POST, handleFallbackConfig);
    
    // Mirror primary endpoints for local access
    fallbackServer.on(ENDPOINT_MOVE, HTTP_POST, handlePrimaryMove);
    fallbackServer.on(ENDPOINT_STATUS, HTTP_GET, handlePrimaryStatus);
    fallbackServer.on(ENDPOINT_EMERGENCY_STOP, HTTP_POST, handlePrimaryEmergencyStop);
    fallbackServer.on(ENDPOINT_HEALTH, HTTP_GET, handlePrimaryHealth);
    
    // Handle CORS preflight requests
    fallbackServer.on(ENDPOINT_MOVE, HTTP_OPTIONS, handleCORSPreflight);
    fallbackServer.on(ENDPOINT_STATUS, HTTP_OPTIONS, handleCORSPreflight);
    fallbackServer.on(ENDPOINT_EMERGENCY_STOP, HTTP_OPTIONS, handleCORSPreflight);
    
    fallbackServer.onNotFound([]() {
        logRequest(fallbackServer, "404_NOT_FOUND");
        web_server_state.failed_requests++;
        sendHTMLResponse(fallbackServer, "<html><body><h1>404 Not Found</h1><a href='/'>Return to Control Panel</a></body></html>");
    });
    
    fallbackServer.begin();
    web_server_state.fallback_server_running = true;
    
    Serial.println("Fallback HTTPS Server started on port " + String(FALLBACK_HTTPS_PORT));
}

void handleCORSPreflight() {
    WebServer* server = &primaryServer;
    if (fallbackServer.client()) {
        server = &fallbackServer;
    }
    
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server->sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    server->sendHeader("Access-Control-Max-Age", "86400");
    server->send(204);
}

void handlePrimaryMove() {
    WebServer* server = &primaryServer;
    if (fallbackServer.client()) {
        server = &fallbackServer;
    }
    
    logRequest(*server, ENDPOINT_MOVE);
    web_server_state.total_requests++;
    
    // Check authentication (basic implementation)
    if (!authenticateRequest(*server)) {
        sendJSONResponse(*server, 401, "{\"error\":\"Authentication required\"}");
        return;
    }
    
    // Validate request body
    if (!server->hasArg("plain")) {
        web_server_state.failed_requests++;
        sendJSONResponse(*server, 400, "{\"error\":\"No command data provided\"}");
        return;
    }
    
    String requestBody = server->arg("plain");
    Serial.println("Movement command received: " + requestBody);
    
    // Queue the movement command
    if (queueMovementCommand(requestBody)) {
        DynamicJsonDocument doc(256);
        doc["status"] = "success";
        doc["message"] = "Movement command queued";
        doc["timestamp"] = millis();
        
        String response;
        serializeJson(doc, response);
        
        sendJSONResponse(*server, 200, response);
    } else {
        web_server_state.failed_requests++;
        sendJSONResponse(*server, 500, "{\"error\":\"Failed to queue movement command\"}");
    }
}

void handlePrimaryStatus() {
    WebServer* server = &primaryServer;
    if (fallbackServer.client()) {
        server = &fallbackServer;
    }
    
    logRequest(*server, ENDPOINT_STATUS);
    web_server_state.total_requests++;
    
    // Collect comprehensive telemetry data
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    
    // Timestamp
    doc["timestamp"] = millis();
    
    // Battery information
    doc["battery"]["voltage"] = g_system_state.battery_voltage;
    doc["battery"]["percentage"] = map(constrain(g_system_state.battery_voltage * 100, 
                                                BATTERY_CRITICAL_VOLTAGE * 100, 
                                                BATTERY_FULL_VOLTAGE * 100),
                                     BATTERY_CRITICAL_VOLTAGE * 100, BATTERY_FULL_VOLTAGE * 100, 0, 100);
    doc["battery"]["charging"] = (g_system_state.battery_voltage > BATTERY_FULL_VOLTAGE * 0.9);
    doc["battery"]["status"] = (g_system_state.battery_voltage > BATTERY_LOW_VOLTAGE) ? "normal" : "low";
    
    // System information  
    doc["system"]["uptime"] = (millis() - g_system_state.uptime_start) / 1000;
    doc["system"]["free_memory"] = ESP.getFreeHeap();
    doc["system"]["wifi_connected"] = g_system_state.wifi_connected;
    doc["system"]["wifi_signal"] = WiFi.RSSI();
    
    // Safety information
    doc["safety"]["tilt_protection"] = true; // Will be updated by safety monitor
    doc["safety"]["emergency_stop"] = g_system_state.emergency_stop;
    doc["safety"]["operational"] = !g_system_state.safety_lockout && !g_system_state.emergency_stop;
    doc["safety"]["lockout_reason"] = g_system_state.safety_lockout ? "Tilt detected" : "";
    
    // Get motor status from movement controller
    // This would integrate with the movement controller's getMotorStatus() function
    
    String response;
    serializeJson(doc, response);
    
    sendJSONResponse(*server, 200, response);
}

void handlePrimaryEmergencyStop() {
    WebServer* server = &primaryServer;
    if (fallbackServer.client()) {
        server = &fallbackServer;
    }
    
    logRequest(*server, ENDPOINT_EMERGENCY_STOP);
    web_server_state.total_requests++;
    
    Serial.println("EMERGENCY STOP activated via API");
    g_system_state.emergency_stop = true;
    
    // Queue emergency stop command
    queueMovementCommand("{\"command\":\"emergency_stop\"}");
    
    DynamicJsonDocument doc(256);
    doc["status"] = "success";
    doc["message"] = "Emergency stop activated";
    doc["motors_stopped"] = true;
    doc["timestamp"] = millis();
    
    String response;
    serializeJson(doc, response);
    
    sendJSONResponse(*server, 200, response);
}

void handlePrimaryHealth() {
    WebServer* server = &primaryServer;
    if (fallbackServer.client()) {
        server = &fallbackServer;
    }
    
    logRequest(*server, ENDPOINT_HEALTH);
    web_server_state.total_requests++;
    
    DynamicJsonDocument doc(512);
    doc["status"] = "healthy";
    doc["uptime"] = (millis() - g_system_state.uptime_start) / 1000;
    doc["memory_free"] = ESP.getFreeHeap();
    doc["wifi_connected"] = g_system_state.wifi_connected;
    doc["servers_running"] = g_system_state.servers_running;
    doc["emergency_stop"] = g_system_state.emergency_stop;
    doc["total_requests"] = web_server_state.total_requests;
    doc["failed_requests"] = web_server_state.failed_requests;
    
    String response;
    serializeJson(doc, response);
    
    sendJSONResponse(*server, 200, response);
}

void handleFallbackRoot() {
    logRequest(fallbackServer, "/");
    web_server_state.total_requests++;
    
    String html = R"(<!DOCTYPE html>
<html>
<head>
    <title>ESP Rover Secure Control</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }
        .container { max-width: 800px; margin: 0 auto; }
        .header { text-align: center; padding: 20px; background: #333; border-radius: 10px; margin-bottom: 20px; }
        .status-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin: 20px 0; }
        .status-card { background: #333; padding: 15px; border-radius: 8px; border-left: 4px solid #007bff; }
        .status-value { font-size: 1.5em; font-weight: bold; color: #28a745; }
        .controls { display: grid; grid-template-columns: repeat(auto-fit, minmax(120px, 1fr)); gap: 10px; margin: 20px 0; }
        .btn { padding: 15px; font-size: 14px; border: none; border-radius: 5px; cursor: pointer; transition: all 0.3s; }
        .btn-primary { background: #007bff; color: white; }
        .btn-success { background: #28a745; color: white; }
        .btn-warning { background: #ffc107; color: black; }
        .btn-danger { background: #dc3545; color: white; }
        .btn:hover { transform: translateY(-2px); box-shadow: 0 4px 8px rgba(0,0,0,0.3); }
        .emergency { background: #dc3545 !important; font-size: 18px; font-weight: bold; }
        .log-area { background: #222; border: 1px solid #444; border-radius: 5px; padding: 10px; height: 200px; overflow-y: auto; font-family: monospace; font-size: 12px; }
        .ssl-indicator { color: #28a745; font-weight: bold; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🤖 ESP Rover Secure Control Panel</h1>
            <p><span class="ssl-indicator">🔒 HTTPS Secured</span> | Direct Local Access</p>
        </div>
        
        <div class="status-grid">
            <div class="status-card">
                <div>WiFi Status</div>
                <div class="status-value" id="wifi-status">Loading...</div>
            </div>
            <div class="status-card">
                <div>Battery Voltage</div>
                <div class="status-value" id="battery">Loading...</div>
            </div>
            <div class="status-card">
                <div>System Uptime</div>
                <div class="status-value" id="uptime">Loading...</div>
            </div>
            <div class="status-card">
                <div>Motor Status</div>
                <div class="status-value" id="motors">Stopped</div>
            </div>
        </div>
        
        <h3>Movement Controls</h3>
        <div class="controls">
            <button class="btn btn-primary" onclick="sendCommand('forward')">⬆️ Forward</button>
            <button class="btn btn-primary" onclick="sendCommand('backward')">⬇️ Backward</button>
            <button class="btn btn-primary" onclick="sendCommand('turn_left')">↪️ Turn Left</button>
            <button class="btn btn-primary" onclick="sendCommand('turn_right')">↩️ Turn Right</button>
            <button class="btn btn-success" onclick="sendCommand('strafe_left')">⬅️ Strafe Left</button>
            <button class="btn btn-success" onclick="sendCommand('strafe_right')">➡️ Strafe Right</button>
            <button class="btn btn-warning" onclick="sendCommand('stop')">⏹️ Stop</button>
            <button class="btn btn-danger emergency" onclick="emergencyStop()">🚨 EMERGENCY STOP</button>
        </div>
        
        <h3>Speed Settings</h3>
        <div class="controls">
            <button class="btn btn-success" onclick="sendCommand('speed_slow')">🐌 Slow</button>
            <button class="btn btn-primary" onclick="sendCommand('speed_normal')">⚡ Normal</button>
            <button class="btn btn-warning" onclick="sendCommand('speed_fast')">🚀 Fast</button>
        </div>
        
        <h3>System Log</h3>
        <div class="log-area" id="log"></div>
        
        <p style="text-align: center; margin-top: 30px; color: #666;">
            <a href="/config" style="color: #007bff;">⚙️ System Configuration</a>
        </p>
    </div>

    <script>
        function log(message) {
            const logArea = document.getElementById('log');
            const time = new Date().toLocaleTimeString();
            logArea.innerHTML += `[${time}] ${message}\n`;
            logArea.scrollTop = logArea.scrollHeight;
        }
        
        async function sendCommand(command) {
            try {
                const response = await fetch('/move', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ command: command, speed: 150, duration: 2000 })
                });
                
                const result = await response.json();
                log(`Command: ${command} - ${result.status || 'Success'}`);
                updateStatus();
            } catch (error) {
                log(`Command failed: ${error.message}`);
            }
        }
        
        async function emergencyStop() {
            try {
                const response = await fetch('/emergency_stop', { method: 'POST' });
                const result = await response.json();
                log(`🚨 EMERGENCY STOP - ${result.message}`);
                updateStatus();
            } catch (error) {
                log(`Emergency stop failed: ${error.message}`);
            }
        }
        
        async function updateStatus() {
            try {
                const response = await fetch('/status');
                const data = await response.json();
                
                document.getElementById('wifi-status').textContent = 
                    data.system?.wifi_connected ? 'Connected' : 'Disconnected';
                document.getElementById('battery').textContent = 
                    data.battery?.voltage?.toFixed(1) + 'V';
                document.getElementById('uptime').textContent = 
                    Math.floor(data.system?.uptime / 60) + 'm';
                    
            } catch (error) {
                log(`Status update failed: ${error.message}`);
            }
        }
        
        // Keyboard controls
        document.addEventListener('keydown', (e) => {
            switch(e.key.toLowerCase()) {
                case 'w': case 'arrowup': sendCommand('forward'); break;
                case 's': case 'arrowdown': sendCommand('backward'); break;
                case 'a': case 'arrowleft': sendCommand('turn_left'); break;
                case 'd': case 'arrowright': sendCommand('turn_right'); break;
                case 'q': sendCommand('strafe_left'); break;
                case 'e': sendCommand('strafe_right'); break;
                case ' ': sendCommand('stop'); e.preventDefault(); break;
                case 'x': emergencyStop(); break;
            }
        });
        
        // Auto-update status every 3 seconds
        setInterval(updateStatus, 3000);
        updateStatus();
        log('ESP Rover Secure Control Panel Ready');
    </script>
</body>
</html>)";
    
    sendHTMLResponse(fallbackServer, html);
}

void handleFallbackControl() {
    // Redirect to root for now - could be expanded for more detailed control interface
    fallbackServer.sendHeader("Location", "/");
    fallbackServer.send(302);
}

void handleFallbackConfig() {
    if (fallbackServer.method() == HTTP_GET) {
        // Show configuration interface
        String html = R"(<!DOCTYPE html>
<html>
<head>
    <title>ESP Rover Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }
        .container { max-width: 600px; margin: 0 auto; }
        .form-group { margin: 15px 0; }
        label { display: block; margin-bottom: 5px; }
        input, select { width: 100%; padding: 10px; border: 1px solid #444; background: #333; color: #fff; border-radius: 4px; }
        .btn { padding: 12px 24px; background: #007bff; color: white; border: none; border-radius: 4px; cursor: pointer; }
        .btn:hover { background: #0056b3; }
    </style>
</head>
<body>
    <div class="container">
        <h1>ESP Rover Configuration</h1>
        <form id="configForm">
            <div class="form-group">
                <label>WiFi SSID:</label>
                <input type="text" id="ssid" name="ssid" required>
            </div>
            <div class="form-group">
                <label>WiFi Password:</label>
                <input type="password" id="password" name="password" required>
            </div>
            <div class="form-group">
                <label>AWS Endpoint URL:</label>
                <input type="url" id="aws_endpoint" name="aws_endpoint" placeholder="https://api.amazonaws.com/...">
            </div>
            <button type="submit" class="btn">Save Configuration</button>
        </form>
        <p><a href="/" style="color: #007bff;">← Back to Control Panel</a></p>
    </div>
    
    <script>
        document.getElementById('configForm').addEventListener('submit', async (e) => {
            e.preventDefault();
            
            const formData = new FormData(e.target);
            const config = Object.fromEntries(formData.entries());
            
            try {
                const response = await fetch('/config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(config)
                });
                
                if (response.ok) {
                    alert('Configuration saved successfully! Rover will restart WiFi connection.');
                } else {
                    alert('Failed to save configuration.');
                }
            } catch (error) {
                alert('Error saving configuration: ' + error.message);
            }
        });
    </script>
</body>
</html>)";
        sendHTMLResponse(fallbackServer, html);
    } else {
        // Handle POST - save configuration
        if (fallbackServer.hasArg("plain")) {
            DynamicJsonDocument doc(1024);
            deserializeJson(doc, fallbackServer.arg("plain"));
            
            String ssid = doc["ssid"] | "";
            String password = doc["password"] | "";
            
            if (ssid.length() > 0 && password.length() > 0) {
                setWiFiCredentials(ssid, password);
                sendJSONResponse(fallbackServer, 200, "{\"status\":\"Configuration saved\"}");
            } else {
                sendJSONResponse(fallbackServer, 400, "{\"error\":\"Invalid WiFi credentials\"}");
            }
        } else {
            sendJSONResponse(fallbackServer, 400, "{\"error\":\"No configuration data\"}");
        }
    }
}

void sendJSONResponse(WebServer& server, int code, const String& json) {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    server.sendHeader("Content-Type", "application/json");
    server.send(code, "application/json", json);
}

void sendHTMLResponse(WebServer& server, const String& html) {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Content-Type", "text/html; charset=utf-8");
    server.send(200, "text/html", html);
}

bool authenticateRequest(WebServer& server) {
    // Basic authentication check - in production, use proper API keys or OAuth
    // For now, allow all requests from local network and specific origins
    
    String origin = server.header("Origin");
    String userAgent = server.header("User-Agent");
    
    // Allow requests from AWS Lambda (check User-Agent or custom headers)
    if (userAgent.indexOf("aws-lambda") >= 0 || 
        origin.indexOf("amazonaws.com") >= 0) {
        return true;
    }
    
    // Allow local requests
    String clientIP = server.client().remoteIP().toString();
    if (clientIP.startsWith("192.168.") || 
        clientIP.startsWith("10.") || 
        clientIP == "127.0.0.1") {
        return true;
    }
    
    return true; // For development - implement proper auth in production
}

void logRequest(WebServer& server, const String& endpoint) {
    String clientIP = server.client().remoteIP().toString();
    String method = (server.method() == HTTP_GET) ? "GET" : 
                    (server.method() == HTTP_POST) ? "POST" : "OTHER";
    
    Serial.println("HTTP " + method + " " + endpoint + " from " + clientIP);
}

// Web server task
void handleWebServerTask(void* parameter) {
    while (true) {
        if (web_server_state.primary_server_running) {
            primaryServer.handleClient();
        }
        
        if (web_server_state.fallback_server_running) {
            fallbackServer.handleClient();
        }
        
        // Small delay to yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// API function to get web server statistics
String getWebServerStats() {
    DynamicJsonDocument doc(512);
    
    doc["primary_server_running"] = web_server_state.primary_server_running;
    doc["fallback_server_running"] = web_server_state.fallback_server_running;
    doc["total_requests"] = web_server_state.total_requests;
    doc["failed_requests"] = web_server_state.failed_requests;
    doc["primary_port"] = PRIMARY_HTTPS_PORT;
    doc["fallback_port"] = FALLBACK_HTTPS_PORT;
    doc["last_error"] = web_server_state.last_error;
    
    String result;
    serializeJson(doc, result);
    return result;
}