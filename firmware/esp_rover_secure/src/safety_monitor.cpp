#include <M5StickC.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../include/config.h"

// Safety monitor state
struct SafetyState {
    bool tilt_protection_active = true;
    bool emergency_stop_active = false;
    bool safety_lockout = false;
    float current_roll = 0.0;
    float current_pitch = 0.0;
    float current_yaw = 0.0;
    float max_tilt_detected = 0.0;
    String lockout_reason = "";
    unsigned long last_safe_time = 0;
    unsigned long emergency_start_time = 0;
    bool recovery_button_pressed = false;
    int consecutive_safe_readings = 0;
} safety_state;

// IMU calibration data
struct IMUCalibration {
    float accel_offset_x = 0.0;
    float accel_offset_y = 0.0;
    float accel_offset_z = 0.0;
    float gyro_offset_x = 0.0;
    float gyro_offset_y = 0.0;
    float gyro_offset_z = 0.0;
    bool calibrated = false;
} imu_calibration;

// External references
extern void setSafetyLockout(bool lockout);
extern bool queueMovementCommand(const String& json_command);

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
void calibrateIMU();
void updateIMUData();
void checkTiltSafety();
void checkBatterySafety();
void handleEmergencyStop();
void handleRecoverySequence();
void displaySafetyAlert();
bool isRoverUpright();

void initializeSafetyMonitor() {
    Serial.println("Initializing Safety Monitor...");
    
    // Initialize IMU
    M5.IMU.Init();
    delay(100);
    
    // Perform IMU calibration
    calibrateIMU();
    
    // Initialize safety state
    safety_state.tilt_protection_active = true;
    safety_state.last_safe_time = millis();
    
    Serial.println("Safety Monitor initialized");
    Serial.println("Tilt threshold: " + String(TILT_THRESHOLD_DEGREES) + " degrees");
}

void calibrateIMU() {
    Serial.println("Calibrating IMU... Keep rover level and stationary for 5 seconds");
    
    // Display calibration message
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(2, 2);
    M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.println("IMU Calibration");
    M5.Lcd.println("Keep level &");
    M5.Lcd.println("stationary");
    M5.Lcd.println("for 5 seconds");
    
    float accel_sum_x = 0, accel_sum_y = 0, accel_sum_z = 0;
    float gyro_sum_x = 0, gyro_sum_y = 0, gyro_sum_z = 0;
    const int samples = 100;
    
    for (int i = 0; i < samples; i++) {
        float accelX, accelY, accelZ;
        float gyroX, gyroY, gyroZ;
        
        M5.IMU.getAccelData(&accelX, &accelY, &accelZ);
        M5.IMU.getGyroData(&gyroX, &gyroY, &gyroZ);
        
        accel_sum_x += accelX;
        accel_sum_y += accelY;
        accel_sum_z += accelZ;
        
        gyro_sum_x += gyroX;
        gyro_sum_y += gyroY;
        gyro_sum_z += gyroZ;
        
        delay(50);
        
        // Update progress on display
        if (i % 20 == 0) {
            M5.Lcd.setCursor(2, 80);
            M5.Lcd.setTextColor(GREEN);
            M5.Lcd.println("Progress: " + String(i * 100 / samples) + "%");
        }
    }
    
    // Calculate offsets
    imu_calibration.accel_offset_x = accel_sum_x / samples;
    imu_calibration.accel_offset_y = accel_sum_y / samples;
    imu_calibration.accel_offset_z = (accel_sum_z / samples) - 1.0; // Subtract 1G for Z-axis
    
    imu_calibration.gyro_offset_x = gyro_sum_x / samples;
    imu_calibration.gyro_offset_y = gyro_sum_y / samples;
    imu_calibration.gyro_offset_z = gyro_sum_z / samples;
    
    imu_calibration.calibrated = true;
    
    Serial.println("IMU calibration complete");
    Serial.println("Accel offsets: X=" + String(imu_calibration.accel_offset_x) + 
                   " Y=" + String(imu_calibration.accel_offset_y) + 
                   " Z=" + String(imu_calibration.accel_offset_z));
    
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(2, 2);
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.println("IMU Calibrated");
    M5.Lcd.println("Safety Monitor");
    M5.Lcd.println("Active");
    delay(2000);
}

void updateIMUData() {
    if (!imu_calibration.calibrated) {
        return;
    }
    
    float accelX, accelY, accelZ;
    float gyroX, gyroY, gyroZ;
    
    // Get raw IMU data
    M5.IMU.getAccelData(&accelX, &accelY, &accelZ);
    M5.IMU.getGyroData(&gyroX, &gyroY, &gyroZ);
    
    // Apply calibration offsets
    accelX -= imu_calibration.accel_offset_x;
    accelY -= imu_calibration.accel_offset_y;
    accelZ -= imu_calibration.accel_offset_z;
    
    gyroX -= imu_calibration.gyro_offset_x;
    gyroY -= imu_calibration.gyro_offset_y;
    gyroZ -= imu_calibration.gyro_offset_z;
    
    // Calculate roll, pitch, yaw from accelerometer
    safety_state.current_roll = atan2(accelY, accelZ) * 180.0 / PI;
    safety_state.current_pitch = atan2(-accelX, sqrt(accelY * accelY + accelZ * accelZ)) * 180.0 / PI;
    
    // For yaw, we'd need magnetometer data or integrate gyroscope
    // For now, just use gyro Z for rotational velocity
    safety_state.current_yaw += gyroZ * (SAFETY_CHECK_INTERVAL_MS / 1000.0);
    
    // Keep yaw in range [-180, 180]
    if (safety_state.current_yaw > 180.0) safety_state.current_yaw -= 360.0;
    if (safety_state.current_yaw < -180.0) safety_state.current_yaw += 360.0;
    
    // Track maximum tilt
    float current_tilt = max(abs(safety_state.current_roll), abs(safety_state.current_pitch));
    if (current_tilt > safety_state.max_tilt_detected) {
        safety_state.max_tilt_detected = current_tilt;
    }
}

void checkTiltSafety() {
    if (!safety_state.tilt_protection_active) {
        return;
    }
    
    updateIMUData();
    
    float roll_abs = abs(safety_state.current_roll);
    float pitch_abs = abs(safety_state.current_pitch);
    float max_current_tilt = max(roll_abs, pitch_abs);
    
    // Check if rover is tilted beyond safe threshold
    if (max_current_tilt > TILT_THRESHOLD_DEGREES) {
        if (!safety_state.safety_lockout) {
            Serial.println("SAFETY ALERT: Excessive tilt detected!");
            Serial.println("Roll: " + String(safety_state.current_roll) + "°");
            Serial.println("Pitch: " + String(safety_state.current_pitch) + "°");
            
            // Activate safety lockout
            safety_state.safety_lockout = true;
            safety_state.lockout_reason = "Tilt > " + String(TILT_THRESHOLD_DEGREES) + "°";
            g_system_state.safety_lockout = true;
            
            // Emergency stop all motors
            queueMovementCommand("{\"command\":\"emergency_stop\"}");
            setSafetyLockout(true);
            
            // Display safety alert
            displaySafetyAlert();
        }
        
        safety_state.consecutive_safe_readings = 0;
    } else {
        // Rover is within safe tilt range
        safety_state.last_safe_time = millis();
        safety_state.consecutive_safe_readings++;
        
        // Check if we should clear lockout (need consistent safe readings)
        if (safety_state.safety_lockout && safety_state.consecutive_safe_readings > 10) {
            if (isRoverUpright() && safety_state.recovery_button_pressed) {
                handleRecoverySequence();
            }
        }
    }
}

void checkBatterySafety() {
    float battery_voltage = M5.Axp.GetBatVoltage();
    
    if (battery_voltage < BATTERY_CRITICAL_VOLTAGE) {
        if (!safety_state.emergency_stop_active) {
            Serial.println("CRITICAL BATTERY: " + String(battery_voltage) + "V");
            
            safety_state.emergency_stop_active = true;
            safety_state.safety_lockout = true;
            safety_state.lockout_reason = "Battery critical";
            g_system_state.emergency_stop = true;
            g_system_state.safety_lockout = true;
            
            queueMovementCommand("{\"command\":\"emergency_stop\"}");
            setSafetyLockout(true);
            
            displaySafetyAlert();
        }
    } else if (battery_voltage < BATTERY_LOW_VOLTAGE) {
        if (millis() % 10000 < 100) { // Log every 10 seconds
            Serial.println("WARNING: Low battery: " + String(battery_voltage) + "V");
        }
    }
}

void handleEmergencyStop() {
    if (!safety_state.emergency_stop_active) {
        safety_state.emergency_stop_active = true;
        safety_state.emergency_start_time = millis();
        safety_state.safety_lockout = true;
        safety_state.lockout_reason = "Manual emergency stop";
        
        g_system_state.emergency_stop = true;
        g_system_state.safety_lockout = true;
        
        Serial.println("EMERGENCY STOP ACTIVATED");
        
        // Stop all motors immediately
        queueMovementCommand("{\"command\":\"emergency_stop\"}");
        setSafetyLockout(true);
        
        displaySafetyAlert();
    }
}

void handleRecoverySequence() {
    Serial.println("Initiating safety recovery sequence...");
    
    // Verify rover is upright and stable
    if (!isRoverUpright()) {
        Serial.println("Recovery failed: Rover not upright");
        return;
    }
    
    // Check battery level
    if (g_system_state.battery_voltage < BATTERY_LOW_VOLTAGE) {
        Serial.println("Recovery failed: Battery too low");
        return;
    }
    
    // Clear lockout state
    safety_state.safety_lockout = false;
    safety_state.emergency_stop_active = false;
    safety_state.recovery_button_pressed = false;
    safety_state.lockout_reason = "";
    safety_state.consecutive_safe_readings = 0;
    
    g_system_state.safety_lockout = false;
    g_system_state.emergency_stop = false;
    
    // Clear movement lockout
    setSafetyLockout(false);
    
    Serial.println("Safety recovery complete - Normal operation resumed");
    
    // Display recovery message
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(2, 2);
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.println("RECOVERY");
    M5.Lcd.println("COMPLETE");
    M5.Lcd.println("");
    M5.Lcd.println("Normal");
    M5.Lcd.println("Operation");
    M5.Lcd.println("Resumed");
    delay(3000);
}

bool isRoverUpright() {
    float roll_abs = abs(safety_state.current_roll);
    float pitch_abs = abs(safety_state.current_pitch);
    float max_tilt = max(roll_abs, pitch_abs);
    
    // Rover is considered upright if tilt is less than 50% of threshold
    return (max_tilt < (TILT_THRESHOLD_DEGREES * 0.5));
}

void displaySafetyAlert() {
    M5.Lcd.fillScreen(RED);
    M5.Lcd.setCursor(5, 5);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(1);
    
    M5.Lcd.println("SAFETY ALERT");
    M5.Lcd.println("============");
    M5.Lcd.println("");
    
    if (safety_state.lockout_reason.length() > 0) {
        M5.Lcd.println("Reason:");
        M5.Lcd.println(safety_state.lockout_reason);
    }
    
    M5.Lcd.println("");
    M5.Lcd.println("Roll: " + String(safety_state.current_roll, 1) + "°");
    M5.Lcd.println("Pitch: " + String(safety_state.current_pitch, 1) + "°");
    M5.Lcd.println("");
    M5.Lcd.println("Press A to");
    M5.Lcd.println("recover when");
    M5.Lcd.println("rover upright");
}

// API functions
void triggerEmergencyStop() {
    handleEmergencyStop();
}

void attemptRecovery() {
    safety_state.recovery_button_pressed = true;
    if (isRoverUpright() && safety_state.consecutive_safe_readings > 5) {
        handleRecoverySequence();
    }
}

String getSafetyStatus() {
    DynamicJsonDocument doc(1024);
    
    doc["tilt_protection"] = safety_state.tilt_protection_active;
    doc["emergency_stop"] = safety_state.emergency_stop_active;
    doc["safety_lockout"] = safety_state.safety_lockout;
    doc["lockout_reason"] = safety_state.lockout_reason;
    doc["rover_upright"] = isRoverUpright();
    
    // Current orientation
    doc["orientation"]["roll"] = safety_state.current_roll;
    doc["orientation"]["pitch"] = safety_state.current_pitch;
    doc["orientation"]["yaw"] = safety_state.current_yaw;
    doc["max_tilt_detected"] = safety_state.max_tilt_detected;
    
    // Safety thresholds
    doc["tilt_threshold"] = TILT_THRESHOLD_DEGREES;
    doc["battery_low_threshold"] = BATTERY_LOW_VOLTAGE;
    doc["battery_critical_threshold"] = BATTERY_CRITICAL_VOLTAGE;
    
    // Status indicators
    doc["consecutive_safe_readings"] = safety_state.consecutive_safe_readings;
    doc["time_since_last_safe"] = millis() - safety_state.last_safe_time;
    doc["imu_calibrated"] = imu_calibration.calibrated;
    
    String result;
    serializeJson(doc, result);
    return result;
}

// Safety monitoring task
void handleSafetyTask(void* parameter) {
    while (true) {
        // Check tilt safety
        checkTiltSafety();
        
        // Check battery safety  
        checkBatterySafety();
        
        // Handle button press for recovery
        M5.update();
        if (M5.BtnA.wasPressed() && safety_state.safety_lockout) {
            attemptRecovery();
        }
        
        // Task delay - critical safety checks run every 50ms
        vTaskDelay(pdMS_TO_TICKS(SAFETY_CHECK_INTERVAL_MS));
    }
}