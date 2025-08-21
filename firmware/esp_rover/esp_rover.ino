#include <M5StickC.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <Wire.h>

// RoverC I2C Configuration
#define ROVER_ADDRESS 0x38
#define SDA_PIN 0
#define SCL_PIN 26

// Motor register addresses
#define MOTOR1_REG 0x00
#define MOTOR2_REG 0x01
#define MOTOR3_REG 0x02
#define MOTOR4_REG 0x03

// Configuration
struct Config {
  String wifi_ssid = "";
  String wifi_password = "";
  String aws_endpoint = "";
  bool debug_enabled = true;
  String log_level = "DEBUG";
  bool status_led_enabled = true;
} config;

// System state
struct SystemState {
  bool wifi_connected = false;
  bool aws_configured = false;
  String ip_address = "";
  String last_error = "";
  unsigned long last_command_time = 0;
  bool motors_active = false;
  String last_command = "";
  float battery_voltage = 0.0;
  int wifi_strength = 0;
} state;

// Web server
WebServer server(80);

// Logging system
enum LogLevel { DEBUG_LEVEL, INFO_LEVEL, WARN_LEVEL, ERROR_LEVEL };
LogLevel currentLogLevel = DEBUG_LEVEL;

// Function declarations
void setupWiFi();
void setupWebServer();
void setupRoverC();
void handleWebRoot();
void handleCommand();
void handleStatus();
void handleConfig();
void executeCommand(JsonObject command);
void moveRover(String action, String direction, String speed);
void setMotorSpeed(uint8_t motor, int8_t speed);
void stopAllMotors();
void updateDisplay();
void updateStatus();
void logMessage(LogLevel level, String message);
void setStatusLED(bool state);
bool loadConfig();
void saveConfig();

void setup() {
  // Initialize M5StickC
  M5.begin();
  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(1);
  
  // Initialize Serial for debugging
  Serial.begin(115200);
  logMessage(INFO_LEVEL, "ESP Rover starting...");
  
  // Initialize SPIFFS for config storage
  if (!SPIFFS.begin(true)) {
    logMessage(ERROR_LEVEL, "SPIFFS initialization failed");
    state.last_error = "SPIFFS failed";
  }
  
  // Load configuration
  loadConfig();
  
  // Initialize RoverC
  setupRoverC();
  
  // Initialize WiFi
  setupWiFi();
  
  // Setup web server
  setupWebServer();
  
  // Setup mDNS
  if (MDNS.begin("esp-rover")) {
    logMessage(INFO_LEVEL, "mDNS responder started: esp-rover.local");
  }
  
  logMessage(INFO_LEVEL, "ESP Rover initialized successfully");
  updateDisplay();
}

void loop() {
  M5.update();
  server.handleClient();
  
  // Update system status
  updateStatus();
  
  // Update display every second
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 1000) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  // Handle button presses
  if (M5.BtnA.wasPressed()) {
    // Toggle debug mode
    config.debug_enabled = !config.debug_enabled;
    logMessage(INFO_LEVEL, String("Debug mode: ") + (config.debug_enabled ? "ON" : "OFF"));
  }
  
  if (M5.BtnB.wasPressed()) {
    // Emergency stop
    stopAllMotors();
    logMessage(WARN_LEVEL, "Emergency stop activated");
    state.last_command = "EMERGENCY_STOP";
  }
  
  // Auto-stop motors after 2 seconds of inactivity
  if (state.motors_active && (millis() - state.last_command_time > 2000)) {
    stopAllMotors();
    state.motors_active = false;
    logMessage(DEBUG_LEVEL, "Auto-stop: Motors deactivated");
  }
  
  delay(10);
}

void setupRoverC() {
  logMessage(INFO_LEVEL, "Initializing RoverC I2C communication...");
  
  Wire.begin(SDA_PIN, SCL_PIN, 100000UL);
  delay(100);
  
  // Stop all motors initially
  stopAllMotors();
  
  logMessage(INFO_LEVEL, "RoverC initialized");
}

void setupWiFi() {
  logMessage(INFO_LEVEL, "Starting WiFi setup...");
  
  // Start as AP for initial configuration if no credentials
  if (config.wifi_ssid.length() == 0) {
    String ap_name = "ESP-Rover-" + String(WiFi.macAddress()).substring(12);
    ap_name.replace(":", "");
    
    WiFi.softAP(ap_name.c_str(), "rover123");
    logMessage(INFO_LEVEL, "Started AP: " + ap_name);
    state.ip_address = WiFi.softAPIP().toString();
    return;
  }
  
  // Connect to existing WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifi_ssid.c_str(), config.wifi_password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    logMessage(DEBUG_LEVEL, "Connecting to WiFi...");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    state.wifi_connected = true;
    state.ip_address = WiFi.localIP().toString();
    state.wifi_strength = WiFi.RSSI();
    logMessage(INFO_LEVEL, "WiFi connected: " + state.ip_address);
  } else {
    logMessage(ERROR_LEVEL, "WiFi connection failed");
    state.last_error = "WiFi connection failed";
    // Fallback to AP mode
    String ap_name = "ESP-Rover-" + String(WiFi.macAddress()).substring(12);
    WiFi.softAP(ap_name.c_str(), "rover123");
    state.ip_address = WiFi.softAPIP().toString();
    logMessage(INFO_LEVEL, "Fallback AP started: " + ap_name);
  }
}

void setupWebServer() {
  // Serve static files
  server.on("/", handleWebRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/command", HTTP_POST, handleCommand);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/config", HTTP_POST, handleConfig);
  
  // CORS headers
  server.onNotFound([]() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(404, "text/plain", "Not Found");
  });
  
  server.begin();
  logMessage(INFO_LEVEL, "Web server started on port 80");
}

void handleWebRoot() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>ESP Rover Control</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }
        .container { max-width: 600px; margin: 0 auto; }
        .status { background: #333; padding: 15px; border-radius: 5px; margin: 10px 0; }
        .controls { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin: 20px 0; }
        button { padding: 15px; font-size: 16px; border: none; border-radius: 5px; cursor: pointer; }
        .move-btn { background: #007bff; color: white; }
        .stop-btn { background: #dc3545; color: white; }
        .voice-btn { background: #28a745; color: white; padding: 20px; margin: 20px 0; }
        .recording { background: #ff6b6b !important; animation: pulse 1s infinite; }
        @keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.5; } 100% { opacity: 1; } }
        .log { background: #222; padding: 10px; border-radius: 5px; font-family: monospace; font-size: 12px; max-height: 200px; overflow-y: auto; }
    </style>
</head>
<body>
    <div class="container">
        <h1>ESP Rover Control</h1>
        
        <div class="status">
            <h3>Status</h3>
            <p>IP: <span id="ip">Loading...</span></p>
            <p>WiFi: <span id="wifi">Loading...</span></p>
            <p>Battery: <span id="battery">Loading...</span></p>
            <p>Last Command: <span id="lastCmd">None</span></p>
        </div>
        
        <button id="voiceBtn" class="voice-btn" onclick="toggleVoiceRecording()">
            🎤 Hold to Record Voice Command
        </button>
        
        <div class="controls">
            <button class="move-btn" onclick="sendCommand('move', 'forward')">Forward</button>
            <button class="move-btn" onclick="sendCommand('move', 'backward')">Backward</button>
            <button class="move-btn" onclick="sendCommand('turn', 'left')">Turn Left</button>
            <button class="move-btn" onclick="sendCommand('turn', 'right')">Turn Right</button>
            <button class="move-btn" onclick="sendCommand('strafe', 'left')">Strafe Left</button>
            <button class="move-btn" onclick="sendCommand('strafe', 'right')">Strafe Right</button>
            <button class="stop-btn" onclick="sendCommand('stop', '')">STOP</button>
            <button class="stop-btn" onclick="sendCommand('stop', '')">EMERGENCY</button>
        </div>
        
        <div class="log" id="logOutput"></div>
    </div>

    <script>
        let mediaRecorder;
        let audioChunks = [];
        let isRecording = false;
        let recordingTimeout;

        async function updateStatus() {
            try {
                const response = await fetch('/status');
                const data = await response.json();
                document.getElementById('ip').textContent = data.ip_address;
                document.getElementById('wifi').textContent = data.wifi_connected ? 'Connected' : 'Disconnected';
                document.getElementById('battery').textContent = data.battery_voltage.toFixed(1) + 'V';
                document.getElementById('lastCmd').textContent = data.last_command || 'None';
            } catch (e) {
                console.error('Status update failed:', e);
            }
        }

        async function sendCommand(action, direction, speed = 'normal') {
            const command = { action, direction, speed };
            try {
                const response = await fetch('/command', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(command)
                });
                const result = await response.json();
                addLog(`Command sent: ${action} ${direction} - ${result.status}`);
            } catch (e) {
                addLog(`Command failed: ${e.message}`);
            }
        }

        async function toggleVoiceRecording() {
            if (!isRecording) {
                startRecording();
            } else {
                stopRecording();
            }
        }

        async function startRecording() {
            try {
                const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
                mediaRecorder = new MediaRecorder(stream);
                audioChunks = [];
                
                mediaRecorder.ondataavailable = (event) => {
                    audioChunks.push(event.data);
                };
                
                mediaRecorder.onstop = () => {
                    const audioBlob = new Blob(audioChunks, { type: 'audio/wav' });
                    processVoiceCommand(audioBlob);
                };
                
                mediaRecorder.start();
                isRecording = true;
                document.getElementById('voiceBtn').classList.add('recording');
                document.getElementById('voiceBtn').textContent = '🔴 Recording... (3s max)';
                
                // Auto-stop after 3 seconds
                recordingTimeout = setTimeout(stopRecording, 3000);
                
                addLog('Voice recording started');
            } catch (e) {
                addLog(`Recording failed: ${e.message}`);
            }
        }

        function stopRecording() {
            if (mediaRecorder && isRecording) {
                mediaRecorder.stop();
                isRecording = false;
                document.getElementById('voiceBtn').classList.remove('recording');
                document.getElementById('voiceBtn').textContent = '🎤 Hold to Record Voice Command';
                
                if (recordingTimeout) {
                    clearTimeout(recordingTimeout);
                }
                
                // Stop all tracks
                mediaRecorder.stream.getTracks().forEach(track => track.stop());
                addLog('Voice recording stopped');
            }
        }

        async function processVoiceCommand(audioBlob) {
            try {
                addLog('Processing voice command...');
                
                // Convert audio to base64
                const arrayBuffer = await audioBlob.arrayBuffer();
                const base64Audio = btoa(String.fromCharCode(...new Uint8Array(arrayBuffer)));
                
                // Send to AWS Lambda (if configured)
                // For now, just simulate processing
                addLog('Voice command processed (AWS integration needed)');
                
            } catch (e) {
                addLog(`Voice processing failed: ${e.message}`);
            }
        }

        function addLog(message) {
            const logOutput = document.getElementById('logOutput');
            const time = new Date().toLocaleTimeString();
            logOutput.innerHTML += `[${time}] ${message}\n`;
            logOutput.scrollTop = logOutput.scrollHeight;
        }

        // Update status every 2 seconds
        setInterval(updateStatus, 2000);
        updateStatus();

        // Keyboard controls
        document.addEventListener('keydown', (e) => {
            switch(e.key) {
                case 'w': case 'ArrowUp': sendCommand('move', 'forward'); break;
                case 's': case 'ArrowDown': sendCommand('move', 'backward'); break;
                case 'a': case 'ArrowLeft': sendCommand('turn', 'left'); break;
                case 'd': case 'ArrowRight': sendCommand('turn', 'right'); break;
                case 'q': sendCommand('strafe', 'left'); break;
                case 'e': sendCommand('strafe', 'right'); break;
                case ' ': sendCommand('stop', ''); e.preventDefault(); break;
            }
        });
        
        addLog('ESP Rover Control Interface Ready');
    </script>
</body>
</html>
  )";
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", html);
}

void handleStatus() {
  DynamicJsonDocument doc(1024);
  
  doc["wifi_connected"] = state.wifi_connected;
  doc["ip_address"] = state.ip_address;
  doc["last_error"] = state.last_error;
  doc["last_command"] = state.last_command;
  doc["last_command_time"] = state.last_command_time;
  doc["motors_active"] = state.motors_active;
  doc["battery_voltage"] = state.battery_voltage;
  doc["wifi_strength"] = state.wifi_strength;
  doc["uptime"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["debug_enabled"] = config.debug_enabled;
  
  String response;
  serializeJson(doc, response);
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", response);
}

void handleCommand() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No command data\"}");
    return;
  }
  
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  
  if (error) {
    logMessage(ERROR_LEVEL, "JSON parsing failed: " + String(error.c_str()));
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  
  executeCommand(doc.as<JsonObject>());
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"status\":\"Command executed\"}");
}

void handleConfig() {
  if (server.method() == HTTP_GET) {
    DynamicJsonDocument doc(1024);
    doc["wifi_ssid"] = config.wifi_ssid;
    doc["aws_endpoint"] = config.aws_endpoint;
    doc["debug_enabled"] = config.debug_enabled;
    doc["log_level"] = config.log_level;
    
    String response;
    serializeJson(doc, response);
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", response);
  } else {
    // Handle POST - update configuration
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, server.arg("plain"));
    
    if (doc.containsKey("wifi_ssid")) config.wifi_ssid = doc["wifi_ssid"].as<String>();
    if (doc.containsKey("wifi_password")) config.wifi_password = doc["wifi_password"].as<String>();
    if (doc.containsKey("aws_endpoint")) config.aws_endpoint = doc["aws_endpoint"].as<String>();
    if (doc.containsKey("debug_enabled")) config.debug_enabled = doc["debug_enabled"];
    if (doc.containsKey("log_level")) config.log_level = doc["log_level"].as<String>();
    
    saveConfig();
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"status\":\"Configuration updated\"}");
  }
}

void executeCommand(JsonObject command) {
  String action = command["action"] | "";
  String direction = command["direction"] | "";
  String speed = command["speed"] | "normal";
  
  logMessage(DEBUG_LEVEL, "Executing command: " + action + " " + direction + " " + speed);
  
  state.last_command = action + "_" + direction;
  state.last_command_time = millis();
  
  moveRover(action, direction, speed);
  setStatusLED(true);
}

void moveRover(String action, String direction, String speed) {
  // Speed multiplier
  int baseSpeed = 100;
  if (speed == "slow") baseSpeed = 50;
  else if (speed == "fast") baseSpeed = 127;
  
  logMessage(DEBUG_LEVEL, "moveRover: " + action + " " + direction + " speed=" + String(baseSpeed));
  
  if (action == "stop") {
    stopAllMotors();
    return;
  }
  
  state.motors_active = true;
  
  // Motor control based on command configuration
  if (action == "move") {
    if (direction == "forward") {
      setMotorSpeed(1, baseSpeed);   // Front left
      setMotorSpeed(2, baseSpeed);   // Front right  
      setMotorSpeed(3, baseSpeed);   // Rear left
      setMotorSpeed(4, baseSpeed);   // Rear right
    } else if (direction == "backward") {
      setMotorSpeed(1, -baseSpeed);
      setMotorSpeed(2, -baseSpeed);
      setMotorSpeed(3, -baseSpeed);
      setMotorSpeed(4, -baseSpeed);
    }
  }
  else if (action == "turn") {
    if (direction == "left") {
      setMotorSpeed(1, -baseSpeed);  // Front left backward
      setMotorSpeed(2, baseSpeed);   // Front right forward
      setMotorSpeed(3, -baseSpeed);  // Rear left backward  
      setMotorSpeed(4, baseSpeed);   // Rear right forward
    } else if (direction == "right") {
      setMotorSpeed(1, baseSpeed);   // Front left forward
      setMotorSpeed(2, -baseSpeed);  // Front right backward
      setMotorSpeed(3, baseSpeed);   // Rear left forward
      setMotorSpeed(4, -baseSpeed);  // Rear right backward
    }
  }
  else if (action == "strafe") {
    if (direction == "left") {
      setMotorSpeed(1, -baseSpeed);  // Front left
      setMotorSpeed(2, baseSpeed);   // Front right
      setMotorSpeed(3, baseSpeed);   // Rear left
      setMotorSpeed(4, -baseSpeed);  // Rear right
    } else if (direction == "right") {
      setMotorSpeed(1, baseSpeed);   // Front left
      setMotorSpeed(2, -baseSpeed);  // Front right
      setMotorSpeed(3, -baseSpeed);  // Rear left
      setMotorSpeed(4, baseSpeed);   // Rear right
    }
  }
  else if (action == "diagonal") {
    if (direction == "forward_left") {
      setMotorSpeed(1, 0);           // Front left
      setMotorSpeed(2, baseSpeed);   // Front right
      setMotorSpeed(3, baseSpeed);   // Rear left
      setMotorSpeed(4, 0);           // Rear right
    } else if (direction == "forward_right") {
      setMotorSpeed(1, baseSpeed);   // Front left
      setMotorSpeed(2, 0);           // Front right
      setMotorSpeed(3, 0);           // Rear left
      setMotorSpeed(4, baseSpeed);   // Rear right
    }
    // Add other diagonal directions as needed
  }
}

void setMotorSpeed(uint8_t motor, int8_t speed) {
  uint8_t reg = MOTOR1_REG + (motor - 1);
  
  Wire.beginTransmission(ROVER_ADDRESS);
  Wire.write(reg);
  Wire.write((uint8_t)speed);
  Wire.endTransmission();
  
  if (config.debug_enabled) {
    logMessage(DEBUG_LEVEL, "Motor " + String(motor) + " speed: " + String(speed));
  }
}

void stopAllMotors() {
  for (int i = 1; i <= 4; i++) {
    setMotorSpeed(i, 0);
  }
  state.motors_active = false;
  setStatusLED(false);
  logMessage(DEBUG_LEVEL, "All motors stopped");
}

void updateStatus() {
  // Update battery voltage
  state.battery_voltage = M5.Axp.GetBatVoltage();
  
  // Update WiFi strength if connected
  if (state.wifi_connected) {
    state.wifi_strength = WiFi.RSSI();
  }
}

void updateDisplay() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(2, 2);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(1);
  
  // Title (smaller for M5StickC 80x160 screen)
  M5.Lcd.println("ESP ROVER");
  M5.Lcd.println("=========");
  
  // WiFi Status
  M5.Lcd.setTextColor(state.wifi_connected ? GREEN : RED);
  M5.Lcd.println("WiFi:" + String(state.wifi_connected ? "OK" : "FAIL"));
  
  // IP Address (truncated for small screen)
  M5.Lcd.setTextColor(WHITE);
  String shortIP = state.ip_address;
  if (shortIP.length() > 12) {
    shortIP = shortIP.substring(shortIP.lastIndexOf('.') - 3);
  }
  M5.Lcd.println("IP:" + shortIP);
  
  // Battery
  M5.Lcd.setTextColor(state.battery_voltage > 3.3 ? GREEN : RED);
  M5.Lcd.println("Bat:" + String(state.battery_voltage, 1) + "V");
  
  // Last Command (shortened)
  M5.Lcd.setTextColor(WHITE);
  String shortCmd = state.last_command;
  if (shortCmd.length() > 10) {
    shortCmd = shortCmd.substring(0, 10);
  }
  M5.Lcd.println("Cmd:" + shortCmd);
  
  // Motor Status
  M5.Lcd.setTextColor(state.motors_active ? GREEN : WHITE);
  M5.Lcd.println("Mot:" + String(state.motors_active ? "ON" : "OFF"));
  
  // Error status (shortened)
  if (state.last_error.length() > 0) {
    M5.Lcd.setTextColor(RED);
    String shortError = state.last_error;
    if (shortError.length() > 12) {
      shortError = shortError.substring(0, 12);
    }
    M5.Lcd.println("Err:" + shortError);
  }
  
  // Debug info (compact)
  if (config.debug_enabled) {
    M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.println("DBG:ON");
  }
  
  // Button labels (bottom of 160px screen)
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(2, 140);
  M5.Lcd.println("A:Dbg B:Stop");
}

void logMessage(LogLevel level, String message) {
  if (level < currentLogLevel) return;
  
  String levelStr = "";
  switch (level) {
    case DEBUG_LEVEL: levelStr = "DEBUG"; break;
    case INFO_LEVEL: levelStr = "INFO"; break;
    case WARN_LEVEL: levelStr = "WARN"; break;
    case ERROR_LEVEL: levelStr = "ERROR"; break;
  }
  
  String logEntry = "[" + String(millis()) + "] " + levelStr + ": " + message;
  
  if (config.debug_enabled) {
    Serial.println(logEntry);
  }
}

void setStatusLED(bool state) {
  if (config.status_led_enabled) {
    // M5StickC Plus doesn't have a controllable LED, use display indication
    // This could be extended to control external LEDs if needed
  }
}

bool loadConfig() {
  if (!SPIFFS.exists("/config.json")) {
    logMessage(INFO_LEVEL, "No config file found, using defaults");
    return false;
  }
  
  File file = SPIFFS.open("/config.json", "r");
  if (!file) {
    logMessage(ERROR_LEVEL, "Failed to open config file");
    return false;
  }
  
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    logMessage(ERROR_LEVEL, "Failed to parse config file");
    return false;
  }
  
  config.wifi_ssid = doc["wifi_ssid"] | "";
  config.wifi_password = doc["wifi_password"] | "";
  config.aws_endpoint = doc["aws_endpoint"] | "";
  config.debug_enabled = doc["debug_enabled"] | true;
  config.log_level = doc["log_level"] | "DEBUG";
  config.status_led_enabled = doc["status_led_enabled"] | true;
  
  logMessage(INFO_LEVEL, "Configuration loaded");
  return true;
}

void saveConfig() {
  File file = SPIFFS.open("/config.json", "w");
  if (!file) {
    logMessage(ERROR_LEVEL, "Failed to save config file");
    return;
  }
  
  DynamicJsonDocument doc(1024);
  doc["wifi_ssid"] = config.wifi_ssid;
  doc["wifi_password"] = config.wifi_password;
  doc["aws_endpoint"] = config.aws_endpoint;
  doc["debug_enabled"] = config.debug_enabled;
  doc["log_level"] = config.log_level;
  doc["status_led_enabled"] = config.status_led_enabled;
  
  serializeJson(doc, file);
  file.close();
  
  logMessage(INFO_LEVEL, "Configuration saved");
}