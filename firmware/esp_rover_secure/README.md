# ESP Rover Secure - Enterprise-Grade Voice-Controlled Rover

## Overview

ESP Rover Secure is a comprehensive voice-controlled omnidirectional rover system built with enterprise-grade security, safety, and reliability standards. This implementation uses the M5Stack StickC (ESP32) with RoverC Pro mecanum wheels platform to provide secure remote control via encrypted HTTPS communications.

## 🔒 Security Features

### Mandatory End-to-End Encryption
- **ALL communications encrypted** - No unencrypted traffic permitted
- **Dual HTTPS servers** with TLS 1.2+ encryption
- **Certificate validation** for AWS communications
- **Secure credential storage** with no hardcoded secrets
- **Input validation** and sanitization on all endpoints
- **Authentication tokens** for sensitive operations

### Security Architecture
```
┌─────────────────┐    HTTPS/TLS    ┌─────────────────┐
│   AWS Lambda   │ ─────────────── │ Primary Server  │
│ (Voice Control) │      Port 443   │  (Command API)  │
└─────────────────┘                 └─────────────────┘
                                             │
┌─────────────────┐    HTTPS/TLS    ┌─────────────────┐
│ Direct Browser  │ ─────────────── │ Fallback Server │
│ (Local Control) │     Port 8443   │ (Local Control) │
└─────────────────┘                 └─────────────────┘
```

## 🛡️ Critical Safety Systems

### Tilt Protection
- **80-degree tilt threshold** with immediate motor shutdown
- **50ms safety check interval** for rapid response
- **Manual recovery sequence** requiring physical button confirmation
- **IMU calibration** for accurate tilt detection

### Emergency Stop Mechanisms
- **Physical button** (Button B on M5StickC)
- **API endpoint** (`POST /emergency_stop`)
- **Battery critical voltage** protection
- **Motor timeout** after 2 seconds of inactivity

### Recovery Protocol
1. **Physical intervention required** - rover must be manually uprighted
2. **Button confirmation** - Press Button A to initiate recovery
3. **System validation** - verify safe orientation and battery level
4. **Gradual restoration** - safety systems re-enable normal operation

## 🤖 Movement Capabilities

### Omnidirectional Mecanum Wheel Control
- **Linear Movement**: Forward, backward, stop
- **Rotation**: Turn left, turn right (in place)
- **Strafing**: Slide left, slide right (sideways movement)
- **Diagonal**: Combined forward/backward + strafe movements
- **Speed Control**: Slow (100 PWM), Normal (150 PWM), Fast (200 PWM)

### Movement Command API
```json
POST /move
{
  "command": "forward",
  "speed": 150,
  "duration": 2000,
  "continuous": false
}
```

## 📊 Comprehensive Telemetry

### Real-Time Data Collection (Every 3 seconds)
- **Battery**: Voltage, percentage, charging status, temperature
- **IMU**: Acceleration, gyroscope, orientation (roll/pitch/yaw)
- **Motors**: PWM values, status, runtime, error states
- **System**: Uptime, memory usage, WiFi signal, CPU temperature
- **Safety**: Tilt status, emergency stop state, lockout reasons

### Telemetry Response Format
```json
{
  "timestamp": "2025-08-25T15:30:45Z",
  "battery": {
    "voltage": 3.7,
    "percentage": 85,
    "status": "normal"
  },
  "imu": {
    "orientation": {
      "roll": 2.1,
      "pitch": -1.8,
      "yaw": 45.2
    }
  },
  "safety": {
    "tilt_protection": true,
    "emergency_stop": false,
    "operational": true
  }
}
```

## 🏗️ Architecture

### Multi-Threaded FreeRTOS Design
```cpp
// Task Priorities (higher number = higher priority)
Safety Task:     Priority 3 (Critical)
Motor Task:      Priority 2 (High)  
Telemetry Task:  Priority 2 (High)
Web Server Task: Priority 1 (Normal)
```

### Modular Code Organization
```
esp_rover_secure/
├── esp_rover_secure.ino          # Arduino IDE wrapper
├── include/
│   └── config.h                  # Configuration constants
├── src/
│   ├── main.cpp                  # System initialization
│   ├── wifi_manager.cpp          # Secure WiFi connectivity
│   ├── web_servers.cpp           # Dual HTTPS servers
│   ├── movement_controller.cpp   # Mecanum wheel control
│   ├── safety_monitor.cpp        # Tilt protection & recovery
│   ├── telemetry_collector.cpp   # Sensor data collection
│   └── json_serializer.cpp       # API response formatting
├── data/
│   └── certificates/             # SSL certificates
└── README.md
```

## 🚀 Quick Start

### 1. Hardware Requirements
- **M5Stack StickC** (original version with ESP32-PICO-D4)
- **M5Stack RoverC Pro** with 4 mecanum wheels
- **16340 rechargeable battery** for RoverC Pro
- **WiFi network** with WPA2/WPA3 security

### 2. Software Requirements
- **Arduino IDE** 1.8.19 or later
- **ESP32 Arduino Core** v2.0.0 or later
- **Required Libraries** (install via Library Manager):
  - M5StickC by M5Stack (v0.2.5+)
  - ArduinoJson by Benoit Blanchon (v6.21.0+)

### 3. Arduino IDE Board Configuration
```
Board: "M5StickC" or "ESP32 Dev Module"
CPU Frequency: 240MHz
Flash Frequency: 80MHz
Flash Mode: QIO
Flash Size: 4MB (with SPIFFS)
Partition Scheme: Default 4MB with SPIFFS
Core Debug Level: Info (development) / None (production)
```

### 4. Installation Steps

1. **Clone and Open Project**
   ```bash
   git clone <repository-url>
   cd ESP-Rover/firmware/esp_rover_secure
   # Open esp_rover_secure.ino in Arduino IDE
   ```

2. **Install Required Libraries**
   - Tools → Manage Libraries
   - Search and install: M5StickC, ArduinoJson

3. **Compile and Upload**
   - Select correct board and port
   - Click Upload (takes ~2-3 minutes)

4. **Initial Configuration**
   - Connect to `ESP-Rover-XXXX` WiFi network (password: `rover123`)
   - Navigate to `192.168.4.1` in web browser
   - Configure your WiFi credentials
   - Set AWS API endpoint (if using voice control)
   - Rover will restart and connect to your WiFi

5. **Access Control Interface**
   - Find rover's IP address from your router or serial monitor
   - Navigate to `https://<rover-ip>:8443` for local control interface
   - Use `https://<rover-ip>:443` for AWS API communication

## 🎮 Control Methods

### 1. Local Web Interface (Port 8443)
- **Full-featured control panel** with real-time telemetry
- **Manual movement controls** with keyboard shortcuts
- **Emergency stop button** and system diagnostics
- **Configuration interface** for WiFi and AWS settings

### 2. API Control (Port 443)
- **RESTful API** for programmatic control
- **AWS Lambda integration** for voice commands
- **Secure HTTPS endpoints** with proper authentication
- **JSON request/response format**

### 3. Physical Controls
- **Button A**: Debug mode toggle / Recovery confirmation
- **Button B**: Emergency stop (immediate motor shutdown)
- **LCD Display**: Real-time status and diagnostics

### 4. Keyboard Shortcuts (Web Interface)
```
W / ↑    : Forward
S / ↓    : Backward  
A / ←    : Turn Left
D / →    : Turn Right
Q        : Strafe Left
E        : Strafe Right
Space    : Stop
X        : Emergency Stop
```

## 📡 API Reference

### Core Endpoints

#### POST /move
Execute movement commands with safety validation.
```json
Request:
{
  "command": "forward",
  "speed": 150,
  "duration": 2000,
  "continuous": false
}

Response:
{
  "status": "success",
  "message": "Movement command executed",
  "timestamp": 1693123845000
}
```

#### GET /status
Retrieve comprehensive system telemetry.
```json
Response:
{
  "timestamp": 1693123845000,
  "battery": { "voltage": 3.7, "percentage": 85 },
  "imu": { "orientation": { "roll": 2.1, "pitch": -1.8 } },
  "safety": { "operational": true, "emergency_stop": false },
  "motors": { "motor1": { "pwm": 150, "status": "active" } },
  "system": { "uptime": 3600, "free_memory": 45000 }
}
```

#### POST /emergency_stop
Immediate safety shutdown of all motors.
```json
Response:
{
  "status": "success",
  "message": "Emergency stop activated",
  "motors_stopped": true,
  "timestamp": 1693123845000
}
```

#### GET /health
Simple health check for monitoring systems.
```json
Response:
{
  "status": "healthy",
  "uptime": 3600,
  "memory_free": 45000,
  "wifi_connected": true,
  "servers_running": true
}
```

### Movement Commands

| Command | Description | Parameters |
|---------|-------------|------------|
| `forward` | Move forward | speed (0-255), duration (ms) |
| `backward` | Move backward | speed, duration |
| `turn_left` | Rotate left in place | speed, duration |
| `turn_right` | Rotate right in place | speed, duration |
| `strafe_left` | Slide left sideways | speed, duration |
| `strafe_right` | Slide right sideways | speed, duration |
| `forward_left` | Diagonal forward-left | speed, duration |
| `forward_right` | Diagonal forward-right | speed, duration |
| `backward_left` | Diagonal backward-left | speed, duration |
| `backward_right` | Diagonal backward-right | speed, duration |
| `stop` | Stop all movement | - |
| `speed_slow` | Set speed to slow (100) | - |
| `speed_normal` | Set speed to normal (150) | - |
| `speed_fast` | Set speed to fast (200) | - |

## 🔧 Configuration

### WiFi Configuration
Access configuration via web interface at `/config` or programmatically:
```json
POST /config
{
  "component": "wifi",
  "config": {
    "ssid": "YourNetworkName",
    "password": "YourPassword",
    "use_static_ip": false
  }
}
```

### AWS Integration
Configure AWS endpoint for voice control:
```json
POST /config
{
  "component": "aws",
  "config": {
    "endpoint": "https://api.amazonaws.com/your-endpoint",
    "region": "us-east-1"
  }
}
```

## 📋 System Specifications

### Performance Metrics
- **SSL Handshake Time**: < 2 seconds
- **API Response Time**: < 500ms
- **Safety Check Interval**: 50ms
- **Telemetry Collection**: Every 3 seconds
- **Motor Timeout**: 2 seconds
- **Continuous Operation**: 4+ hours on battery

### Memory Usage
- **Flash Memory**: ~1.5MB (including web interfaces)
- **RAM Usage**: ~120KB runtime
- **SPIFFS Storage**: 1MB for configuration and certificates
- **Heap Usage**: Maintained under 80% for stability

### Network Requirements
- **WiFi Standard**: 802.11 b/g/n (2.4GHz)
- **Security**: WPA2/WPA3 encryption required
- **Bandwidth**: Minimal (< 1KB/s for telemetry)
- **Latency**: < 100ms recommended for responsive control

## 🛠️ Development and Customization

### Adding New Movement Commands
1. **Define command** in `movement_controller.cpp`
2. **Add parsing logic** in `executeMovementCommand()`
3. **Implement kinematics** using `setMecanumMovement()`
4. **Update API documentation** and web interface

### Extending Telemetry
1. **Add data collection** in `telemetry_collector.cpp`
2. **Update JSON serialization** in `json_serializer.cpp`
3. **Modify web interface** to display new metrics
4. **Consider performance impact** on collection interval

### Custom Safety Features
1. **Implement checks** in `safety_monitor.cpp`
2. **Add configuration constants** in `config.h`
3. **Update recovery procedures** as needed
4. **Test thoroughly** with physical rover

## 🚨 Troubleshooting

### Common Issues

#### WiFi Connection Problems
- **Symptoms**: Cannot connect to configured network
- **Solutions**: 
  - Check SSID and password accuracy
  - Verify network is 2.4GHz (not 5GHz)
  - Ensure WPA2/WPA3 security (not WEP/Open)
  - Connect to rover's AP mode for reconfiguration

#### Safety System Activation
- **Symptoms**: Motors won't respond, safety lockout active
- **Solutions**:
  - Check rover orientation (must be upright)
  - Press Button A to attempt recovery
  - Verify battery voltage > 3.3V
  - Review serial console for specific lockout reason

#### Web Interface Not Loading
- **Symptoms**: Cannot access control panel
- **Solutions**:
  - Verify HTTPS URLs (not HTTP)
  - Check port numbers (443 or 8443)
  - Accept self-signed certificate warnings
  - Ensure rover is on same network

#### Movement Commands Not Working
- **Symptoms**: Commands sent but rover doesn't move
- **Solutions**:
  - Check safety lockout status via `/status`
  - Verify battery level > 3.3V
  - Test with manual web interface controls
  - Check motor connections and I2C communication

### Diagnostic Tools

#### Serial Monitor
Connect at 115200 baud to view:
- System initialization messages
- WiFi connection status
- Safety alerts and lockout reasons
- API request/response logs
- Memory usage and error messages

#### Status API
Query `/status` endpoint for:
- Complete system telemetry
- Motor status and errors
- Safety system state
- Network connectivity
- Memory and performance metrics

#### Physical Indicators
- **LCD Display**: Shows key system status
- **Button Response**: Confirms system responsiveness
- **Motor Movement**: Validates mechanical systems

### Factory Reset
1. **Power off rover completely**
2. **Hold both Button A and B**
3. **Power on while holding buttons**
4. **Release when display shows "FACTORY RESET"**
5. **Reconfigure WiFi and settings**

## 📈 Performance Optimization

### Memory Management
- **Monitor heap usage** via telemetry
- **Optimize JSON buffer sizes** based on actual usage
- **Implement garbage collection** strategies
- **Use static allocation** where possible

### Network Optimization
- **Minimize telemetry frequency** if bandwidth limited
- **Implement request caching** for repeated queries
- **Use compression** for large responses
- **Pool HTTP connections** where appropriate

### Power Management
- **Monitor battery telemetry** for usage patterns
- **Implement sleep modes** during inactivity
- **Optimize CPU frequency** based on load
- **Reduce display brightness** to save power

## 🔐 Security Considerations

### Production Deployment
1. **Replace self-signed certificates** with proper CA-issued certificates
2. **Implement proper authentication** (API keys, OAuth, etc.)
3. **Enable request rate limiting** to prevent abuse
4. **Set up monitoring and alerting** for security events
5. **Regular security updates** and vulnerability assessments

### Network Security
- **Use enterprise WiFi** with proper authentication
- **Implement VPN access** for remote control
- **Network segmentation** to isolate rover traffic
- **Firewall rules** to restrict unnecessary ports

### Physical Security
- **Secure rover storage** when not in use
- **Tamper detection** for unauthorized access
- **Recovery procedures** for lost or stolen units
- **Physical button lockout** for high-security environments

## 📚 Additional Resources

### Documentation
- [M5Stack StickC Documentation](https://docs.m5stack.com/en/core/m5stickc)
- [RoverC Pro User Manual](https://docs.m5stack.com/en/unit/roverc_pro)
- [ESP32 Arduino Core Reference](https://espressif-docs.readthedocs-hosted.com/projects/arduino-esp32/)
- [ArduinoJson Documentation](https://arduinojson.org/v6/doc/)

### Community and Support
- [M5Stack Community Forum](https://community.m5stack.com/)
- [ESP32 Arduino GitHub Issues](https://github.com/espressif/arduino-esp32/issues)
- [Project GitHub Repository](https://github.com/your-repo/esp-rover-secure)

## 📄 License

MIT License - See LICENSE file for details.

## 👥 Contributing

1. Fork the repository
2. Create feature branch (`git checkout -b feature/amazing-feature`)
3. Commit changes (`git commit -m 'Add amazing feature'`)
4. Push to branch (`git push origin feature/amazing-feature`)
5. Open Pull Request

### Development Guidelines
- Follow existing code style and organization
- Add comprehensive comments for new features
- Include safety considerations in all changes
- Test thoroughly with physical hardware
- Update documentation for user-facing changes

## ⚠️ Important Notes

### Safety Warnings
- **Always maintain visual contact** with rover during operation
- **Keep clear of moving parts** and mecanum wheels
- **Monitor battery voltage** to prevent over-discharge
- **Test safety systems regularly** to ensure proper operation
- **Follow local regulations** for robotic device operation

### Limitations
- **WiFi Range**: Limited by 2.4GHz WiFi coverage area
- **Battery Life**: Approximately 4 hours continuous operation
- **Load Capacity**: Limited by RoverC Pro specifications
- **Outdoor Use**: Not weatherproof, indoor use recommended
- **Obstacle Avoidance**: Manual control only, no autonomous navigation

---

**ESP Rover Secure v1.0.0** - Enterprise-Grade Voice-Controlled Omnidirectional Rover System