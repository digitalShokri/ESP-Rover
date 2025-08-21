# ESP Rover Voice-Controlled Robot

A voice-controlled omnidirectional rover using M5StickC, RoverC Pro, and AWS services for speech-to-text processing.

## Architecture

```
Voice Input → Web Interface → AWS Lambda → Transcribe → Command Parser → Rover Movement
```

### Components
- **Hardware**: M5StickC + RoverC Pro with mecanum wheels
- **Voice Processing**: AWS Transcribe via API Gateway + Lambda
- **Control Interface**: Web-based UI with voice recording
- **Communication**: WiFi (HTTP REST API)

## Quick Start

### 1. Deploy AWS Infrastructure
```bash
cd terraform
chmod +x deploy.sh
./deploy.sh
```
Note the API Gateway URL from the output.

### 2. Flash Rover Firmware
1. Install Arduino IDE and required libraries (see `firmware/libraries.txt`)
2. Open `firmware/esp_rover/esp_rover.ino`
3. Flash to M5StickC (original)
4. Connect to ESP-Rover-XXXX WiFi network (password: rover123)

### 3. Configure Rover
1. Navigate to rover IP address in browser
2. Configure your WiFi credentials
3. Set AWS API endpoint from step 1
4. Rover will reconnect to your WiFi

### 4. Control Options
- **Web Interface**: Navigate to rover's IP address
- **Voice Commands**: Click microphone button, speak command (3s max)
- **Manual Controls**: Use on-screen buttons or keyboard
- **Physical Controls**: Button A (debug toggle), Button B (emergency stop)

## Voice Commands

### Movement Commands
- **Basic**: "forward", "backward", "stop"
- **Turning**: "turn left", "turn right", "left", "right"
- **Strafing**: "strafe left", "strafe right", "slide left", "slide right"
- **Diagonal**: "forward left", "forward right", "backward left", "backward right"

### Speed Modifiers
- **Slow**: "slowly", "slow"
- **Fast**: "quickly", "fast"
- **Examples**: "move forward slowly", "turn right fast"

### Emergency Commands
- **Stop**: "stop", "halt", "freeze"

## Features

### Rover Capabilities
- **Omnidirectional Movement**: Mecanum wheels for any direction
- **Voice Control**: 3-second voice commands with AWS Transcribe
- **Manual Control**: Web interface with touch/keyboard controls
- **Status Display**: Real-time IP, battery, WiFi, command status
- **Debug System**: Configurable logging levels and serial output
- **Auto-Safety**: Motors auto-stop after 2 seconds inactivity
- **Configuration**: Persistent WiFi and AWS settings

### AWS Services
- **API Gateway**: RESTful endpoint for voice processing
- **Lambda**: Voice-to-command parsing with fuzzy matching
- **S3**: Temporary audio file storage (auto-cleanup)
- **Transcribe**: Speech-to-text conversion
- **CloudWatch**: Comprehensive logging and monitoring

## Configuration Files

### Command Mapping (`shared/command_config.json`)
Maps voice commands to motor movements with speed multipliers and durations.

### AWS Infrastructure (`terraform/`)
Complete Terraform setup for serverless voice processing pipeline.

## Development

### Project Structure
```
ESP-Rover/
├── terraform/              # AWS infrastructure
│   ├── main.tf             # Terraform configuration
│   ├── lambda/             # Lambda function code
│   └── deploy.sh           # Deployment script
├── firmware/               # Arduino code
│   └── esp_rover/          # Main rover firmware
├── shared/                 # Configuration files
│   └── command_config.json # Command mappings
└── README.md              # This file
```

### Debugging
- **Serial Monitor**: 115200 baud for debug output
- **Web Interface**: Real-time status and command logs
- **AWS CloudWatch**: Lambda execution and API Gateway logs
- **Debug Toggle**: Button A on rover enables/disables debug mode

### Motor Control Matrix
Mecanum wheel movement patterns:
```
Action         FL  FR  RL  RR
Forward        +   +   +   +
Backward       -   -   -   -  
Turn Left      -   +   -   +
Turn Right     +   -   +   -
Strafe Left    -   +   +   -
Strafe Right   +   -   -   +
```

## Troubleshooting

### WiFi Issues
1. Check rover display for connection status
2. Connect to ESP-Rover-XXXX AP for configuration
3. Verify SSID/password in configuration

### Voice Commands Not Working
1. Verify AWS endpoint configuration
2. Check AWS Lambda logs in CloudWatch
3. Ensure microphone permissions in browser
4. Test with manual commands first

### Motor Problems
1. Check battery voltage on display
2. Verify I2C connections (SDA=0, SCL=26)
3. Use manual controls to test individual movements
4. Check debug logs for motor commands

### Common Issues
- **Low Battery**: Affects motor performance and WiFi stability
- **AWS Timeout**: 30-second Lambda timeout for transcription
- **Browser Compatibility**: Chrome/Safari recommended for voice recording
- **Network Latency**: Voice processing requires stable internet connection

## Hardware Specifications

### M5StickC
- **MCU**: ESP32-PICO-D4
- **Display**: 80x160 TFT LCD
- **Battery**: 95mAh rechargeable
- **WiFi**: 802.11 b/g/n
- **I2C**: 0-SDA, 26-SCL

### RoverC Pro
- **Motors**: 4x N20 worm gear motors
- **Wheels**: 4x mecanum wheels (omnidirectional)
- **Control**: STM32F030C6T6 via I2C (0x38)
- **Power**: 16340 battery (700mAh)
- **Expansion**: 2x servo channels, 2x Grove I2C

## Cost Breakdown
- M5StickC: ~$15
- RoverC Pro: ~$45
- AWS Usage: <$5/month (typical use)
- **Total**: ~$65 for complete system

## Future Enhancements
- Camera integration for visual feedback
- Obstacle detection and avoidance
- Multiple rover coordination
- Custom wake word detection
- Mobile app development
- Advanced path planning algorithms