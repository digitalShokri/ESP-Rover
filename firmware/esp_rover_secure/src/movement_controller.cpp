#include <Wire.h>
#include <M5StickC.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "../include/config.h"

// Movement command structure
struct MovementCommand {
    String command;           // "forward", "backward", "strafe_left", etc.
    int speed;               // PWM value 0-255
    int duration;            // milliseconds (0 for continuous)
    bool continuous;         // true for continuous movement
    unsigned long timestamp; // when command was issued
};

// Motor status structure  
struct MotorStatus {
    int pwm_value;          // Current PWM setting (0-255)
    String status;          // "active", "stopped", "error"
    unsigned long runtime;  // Total operation time in ms
    bool error_state;       // Motor error flag
};

// Movement controller state
struct MovementState {
    bool motors_active = false;
    bool safety_lockout = false;
    unsigned long last_command_time = 0;
    MovementCommand current_command;
    MotorStatus motor_status[4]; // 4 motors: FL, FR, BL, BR
    int current_speed_setting = SPEED_NORMAL_PWM;
    bool emergency_stop_active = false;
} movement_state;

// Command queue for thread-safe operation
QueueHandle_t commandQueue = NULL;
#define COMMAND_QUEUE_SIZE 10

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
void initializeI2CMotorController();
void setMotorSpeed(uint8_t motor_index, int8_t speed);
void stopAllMotors();
void executeMovementCommand(const MovementCommand& command);
void setMecanumMovement(float x, float y, float rotation, int speed);
bool parseMovementCommand(const String& json_command, MovementCommand& cmd);
void updateMotorStatus();
void handleMotorTimeout();

void initializeMovementController() {
    Serial.println("Initializing Movement Controller...");
    
    // Initialize I2C for motor communication
    initializeI2CMotorController();
    
    // Create command queue for thread-safe command processing
    commandQueue = xQueueCreate(COMMAND_QUEUE_SIZE, sizeof(MovementCommand));
    if (commandQueue == NULL) {
        Serial.println("Failed to create movement command queue");
        return;
    }
    
    // Initialize motor status
    for (int i = 0; i < 4; i++) {
        movement_state.motor_status[i].pwm_value = 0;
        movement_state.motor_status[i].status = "stopped";
        movement_state.motor_status[i].runtime = 0;
        movement_state.motor_status[i].error_state = false;
    }
    
    // Ensure all motors are stopped initially
    stopAllMotors();
    
    Serial.println("Movement Controller initialized");
}

void initializeI2CMotorController() {
    Serial.println("Initializing I2C motor controller...");
    
    // Initialize I2C with specified pins and frequency
    Wire.begin(SDA_PIN, SCL_PIN, I2C_FREQUENCY);
    delay(100);
    
    // Test I2C communication with RoverC
    Wire.beginTransmission(ROVER_I2C_ADDRESS);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
        Serial.println("I2C motor controller communication established");
    } else {
        Serial.println("I2C motor controller communication failed, error: " + String(error));
        g_system_state.last_error = "I2C motor fail";
    }
    
    // Stop all motors to ensure safe state
    stopAllMotors();
}

void setMotorSpeed(uint8_t motor_index, int8_t speed) {
    if (motor_index < 1 || motor_index > 4) {
        Serial.println("Invalid motor index: " + String(motor_index));
        return;
    }
    
    // Check for safety lockout
    if (movement_state.safety_lockout || g_system_state.safety_lockout || g_system_state.emergency_stop) {
        speed = 0; // Force stop if safety lockout is active
    }
    
    // Clamp speed to valid range
    speed = constrain(speed, -MAX_SPEED_PWM, MAX_SPEED_PWM);
    
    // Calculate register address
    uint8_t motor_reg = MOTOR1_REG + (motor_index - 1);
    
    // Send I2C command to motor controller
    Wire.beginTransmission(ROVER_I2C_ADDRESS);
    Wire.write(motor_reg);
    Wire.write((uint8_t)speed);
    uint8_t result = Wire.endTransmission();
    
    if (result == 0) {
        // Update motor status
        movement_state.motor_status[motor_index - 1].pwm_value = speed;
        movement_state.motor_status[motor_index - 1].status = (speed == 0) ? "stopped" : "active";
        movement_state.motor_status[motor_index - 1].error_state = false;
        
        Serial.println("Motor " + String(motor_index) + " speed set to: " + String(speed));
    } else {
        Serial.println("Failed to set motor " + String(motor_index) + " speed, I2C error: " + String(result));
        movement_state.motor_status[motor_index - 1].error_state = true;
        movement_state.motor_status[motor_index - 1].status = "error";
        g_system_state.last_error = "Motor I2C error";
    }
}

void stopAllMotors() {
    Serial.println("Stopping all motors");
    
    for (int i = 1; i <= 4; i++) {
        setMotorSpeed(i, 0);
    }
    
    movement_state.motors_active = false;
    movement_state.emergency_stop_active = false;
    
    // Update all motor statuses
    for (int i = 0; i < 4; i++) {
        movement_state.motor_status[i].pwm_value = 0;
        movement_state.motor_status[i].status = "stopped";
    }
}

void setMecanumMovement(float x, float y, float rotation, int speed) {
    // Mecanum wheel kinematics
    // x: forward/backward (-1.0 to 1.0)
    // y: strafe left/right (-1.0 to 1.0)  
    // rotation: rotate left/right (-1.0 to 1.0)
    
    // Calculate individual wheel speeds
    float front_left = x + y + rotation;
    float front_right = x - y - rotation;
    float back_left = x - y + rotation;
    float back_right = x + y - rotation;
    
    // Find the maximum absolute value to normalize
    float max_speed = max(max(abs(front_left), abs(front_right)), 
                         max(abs(back_left), abs(back_right)));
    
    // Normalize to prevent exceeding maximum speed
    if (max_speed > 1.0) {
        front_left /= max_speed;
        front_right /= max_speed;
        back_left /= max_speed;
        back_right /= max_speed;
    }
    
    // Apply speed scaling and send to motors
    setMotorSpeed(1, (int8_t)(front_left * speed));   // Front Left
    setMotorSpeed(2, (int8_t)(front_right * speed));  // Front Right
    setMotorSpeed(3, (int8_t)(back_left * speed));    // Back Left
    setMotorSpeed(4, (int8_t)(back_right * speed));   // Back Right
    
    Serial.println("Mecanum movement - FL:" + String(front_left * speed) + 
                   " FR:" + String(front_right * speed) +
                   " BL:" + String(back_left * speed) + 
                   " BR:" + String(back_right * speed));
}

bool parseMovementCommand(const String& json_command, MovementCommand& cmd) {
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json_command);
    
    if (error) {
        Serial.println("Failed to parse movement command JSON: " + String(error.c_str()));
        return false;
    }
    
    cmd.command = doc["command"] | "";
    cmd.speed = doc["speed"] | movement_state.current_speed_setting;
    cmd.duration = doc["duration"] | 0;
    cmd.continuous = doc["continuous"] | false;
    cmd.timestamp = millis();
    
    // Validate command
    if (cmd.command.length() == 0) {
        Serial.println("Empty movement command");
        return false;
    }
    
    // Clamp speed to valid range
    cmd.speed = constrain(cmd.speed, 0, MAX_SPEED_PWM);
    
    return true;
}

void executeMovementCommand(const MovementCommand& command) {
    Serial.println("Executing movement command: " + command.command + 
                   " speed:" + String(command.speed) + 
                   " duration:" + String(command.duration));
    
    // Check for safety lockout
    if (movement_state.safety_lockout || g_system_state.safety_lockout) {
        Serial.println("Movement blocked by safety lockout");
        return;
    }
    
    // Handle emergency stop
    if (g_system_state.emergency_stop || command.command == "emergency_stop") {
        stopAllMotors();
        movement_state.emergency_stop_active = true;
        return;
    }
    
    // Update state
    movement_state.current_command = command;
    movement_state.last_command_time = millis();
    movement_state.motors_active = true;
    
    // Execute based on command type
    if (command.command == "stop") {
        stopAllMotors();
    }
    else if (command.command == "forward") {
        setMecanumMovement(1.0, 0.0, 0.0, command.speed);
    }
    else if (command.command == "backward") {
        setMecanumMovement(-1.0, 0.0, 0.0, command.speed);
    }
    else if (command.command == "strafe_left") {
        setMecanumMovement(0.0, -1.0, 0.0, command.speed);
    }
    else if (command.command == "strafe_right") {
        setMecanumMovement(0.0, 1.0, 0.0, command.speed);
    }
    else if (command.command == "turn_left") {
        setMecanumMovement(0.0, 0.0, -1.0, command.speed);
    }
    else if (command.command == "turn_right") {
        setMecanumMovement(0.0, 0.0, 1.0, command.speed);
    }
    else if (command.command == "forward_left") {
        setMecanumMovement(0.7, -0.7, 0.0, command.speed);
    }
    else if (command.command == "forward_right") {
        setMecanumMovement(0.7, 0.7, 0.0, command.speed);
    }
    else if (command.command == "backward_left") {
        setMecanumMovement(-0.7, -0.7, 0.0, command.speed);
    }
    else if (command.command == "backward_right") {
        setMecanumMovement(-0.7, 0.7, 0.0, command.speed);
    }
    else if (command.command == "speed_slow") {
        movement_state.current_speed_setting = SPEED_SLOW_PWM;
    }
    else if (command.command == "speed_normal") {
        movement_state.current_speed_setting = SPEED_NORMAL_PWM;
    }
    else if (command.command == "speed_fast") {
        movement_state.current_speed_setting = SPEED_FAST_PWM;
    }
    else {
        Serial.println("Unknown movement command: " + command.command);
        return;
    }
}

void handleMotorTimeout() {
    // Auto-stop motors after timeout if not continuous movement
    if (movement_state.motors_active && !movement_state.current_command.continuous) {
        unsigned long elapsed = millis() - movement_state.last_command_time;
        
        unsigned long timeout = MOTOR_TIMEOUT_MS;
        if (movement_state.current_command.duration > 0) {
            timeout = movement_state.current_command.duration;
        }
        
        if (elapsed > timeout) {
            Serial.println("Motor timeout - stopping motors");
            stopAllMotors();
        }
    }
}

void updateMotorStatus() {
    // Update runtime for active motors
    static unsigned long last_update = 0;
    unsigned long now = millis();
    unsigned long delta = now - last_update;
    last_update = now;
    
    for (int i = 0; i < 4; i++) {
        if (movement_state.motor_status[i].pwm_value != 0) {
            movement_state.motor_status[i].runtime += delta;
        }
    }
}

// API functions for external access
bool queueMovementCommand(const String& json_command) {
    MovementCommand cmd;
    if (!parseMovementCommand(json_command, cmd)) {
        return false;
    }
    
    BaseType_t result = xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(100));
    return (result == pdPASS);
}

void setSafetyLockout(bool lockout) {
    movement_state.safety_lockout = lockout;
    if (lockout) {
        Serial.println("Movement safety lockout activated");
        stopAllMotors();
    } else {
        Serial.println("Movement safety lockout cleared");
    }
}

String getMotorStatus() {
    DynamicJsonDocument doc(1024);
    
    for (int i = 0; i < 4; i++) {
        String motor_key = "motor" + String(i + 1);
        doc[motor_key]["pwm"] = movement_state.motor_status[i].pwm_value;
        doc[motor_key]["status"] = movement_state.motor_status[i].status;
        doc[motor_key]["runtime"] = movement_state.motor_status[i].runtime;
        doc[motor_key]["error"] = movement_state.motor_status[i].error_state;
    }
    
    doc["motors_active"] = movement_state.motors_active;
    doc["safety_lockout"] = movement_state.safety_lockout;
    doc["emergency_stop"] = movement_state.emergency_stop_active;
    doc["last_command"] = movement_state.current_command.command;
    doc["current_speed_setting"] = movement_state.current_speed_setting;
    
    String result;
    serializeJson(doc, result);
    return result;
}

// Motor control task
void handleMotorTask(void* parameter) {
    MovementCommand command;
    
    while (true) {
        // Check for queued commands
        if (xQueueReceive(commandQueue, &command, pdMS_TO_TICKS(100)) == pdPASS) {
            executeMovementCommand(command);
        }
        
        // Handle motor timeout and status updates
        handleMotorTimeout();
        updateMotorStatus();
        
        // Small delay
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}