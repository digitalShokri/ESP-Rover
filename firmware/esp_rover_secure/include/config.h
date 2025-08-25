#ifndef CONFIG_H
#define CONFIG_H

// Hardware Configuration
#define ROVER_I2C_ADDRESS 0x38
#define SDA_PIN 0
#define SCL_PIN 26
#define I2C_FREQUENCY 100000UL

// Motor register addresses
#define MOTOR1_REG 0x00  // Front Left
#define MOTOR2_REG 0x01  // Front Right  
#define MOTOR3_REG 0x02  // Back Left
#define MOTOR4_REG 0x03  // Back Right

// Network Configuration
#define PRIMARY_HTTPS_PORT 443
#define FALLBACK_HTTPS_PORT 8443
#define WIFI_CONNECTION_TIMEOUT 10000
#define WIFI_RECONNECT_INTERVAL 30000

// Safety Configuration
#define TILT_THRESHOLD_DEGREES 80.0
#define SAFETY_CHECK_INTERVAL_MS 50
#define MOTOR_TIMEOUT_MS 2000
#define EMERGENCY_STOP_DURATION_MS 5000

// Performance Configuration  
#define TELEMETRY_INTERVAL_MS 3000
#define DISPLAY_UPDATE_INTERVAL_MS 1000
#define STATUS_UPDATE_INTERVAL_MS 500
#define WATCHDOG_TIMEOUT_MS 10000

// Movement Configuration
#define SPEED_SLOW_PWM 100
#define SPEED_NORMAL_PWM 150  
#define SPEED_FAST_PWM 200
#define MAX_SPEED_PWM 255

// Battery Configuration
#define BATTERY_LOW_VOLTAGE 3.3
#define BATTERY_CRITICAL_VOLTAGE 3.0
#define BATTERY_FULL_VOLTAGE 4.2

// SSL/TLS Configuration
#define SSL_HANDSHAKE_TIMEOUT_MS 10000
#define MAX_CERT_SIZE 2048
#define MAX_KEY_SIZE 2048

// Memory Management
#define JSON_BUFFER_SIZE 2048
#define WEB_RESPONSE_BUFFER_SIZE 4096
#define MAX_HEAP_USAGE_PERCENT 80

// Multi-threading Configuration  
#define WEB_SERVER_TASK_STACK_SIZE 8192
#define TELEMETRY_TASK_STACK_SIZE 4096
#define SAFETY_TASK_STACK_SIZE 2048
#define MOTOR_TASK_STACK_SIZE 4096

// Task Priorities (higher number = higher priority)
#define SAFETY_TASK_PRIORITY 3
#define MOTOR_TASK_PRIORITY 2
#define TELEMETRY_TASK_PRIORITY 2  
#define WEB_SERVER_TASK_PRIORITY 1

// API Endpoints
#define ENDPOINT_MOVE "/move"
#define ENDPOINT_STATUS "/status" 
#define ENDPOINT_EMERGENCY_STOP "/emergency_stop"
#define ENDPOINT_HEALTH "/health"
#define ENDPOINT_CONFIG "/config"

// Logging Configuration
#define MAX_LOG_ENTRIES 100
#define LOG_ENTRY_MAX_LENGTH 256

// Default AP Configuration
#define DEFAULT_AP_PASSWORD "rover123"
#define AP_NAME_PREFIX "ESP-Rover-"

// System Limits
#define MAX_COMMAND_QUEUE_SIZE 10
#define MAX_TELEMETRY_HISTORY 50
#define MAX_ERROR_HISTORY 20

#endif // CONFIG_H