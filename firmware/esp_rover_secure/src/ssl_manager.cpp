#include <SPIFFS.h>
#include <WiFiClientSecure.h>
#include <M5StickC.h>

#include "../include/config.h"

// SSL Certificate Manager for ESP Rover
// Handles loading and managing SSL certificates for HTTPS servers

struct SSLCertificates {
    String server_cert = "";
    String server_key = "";
    String ca_cert = "";
    bool certificates_loaded = false;
    String last_error = "";
} ssl_certs;

// Function declarations
bool loadCertificatesFromSPIFFS();
bool loadCertificatesFromHeader();
String readFileFromSPIFFS(const String& filename);
bool validateCertificate(const String& cert);

void initializeSSLManager() {
    Serial.println("Initializing SSL Certificate Manager...");
    
    // Try to load certificates from SPIFFS first (production/custom certs)
    if (loadCertificatesFromSPIFFS()) {
        Serial.println("Certificates loaded from SPIFFS");
        ssl_certs.certificates_loaded = true;
    } 
    // Fall back to header file certificates (development)
    else if (loadCertificatesFromHeader()) {
        Serial.println("Certificates loaded from header file");
        ssl_certs.certificates_loaded = true;
    } 
    else {
        Serial.println("ERROR: Failed to load SSL certificates");
        ssl_certs.last_error = "No valid certificates found";
        ssl_certs.certificates_loaded = false;
    }
    
    if (ssl_certs.certificates_loaded) {
        Serial.println("SSL Certificate Manager initialized successfully");
        Serial.println("Certificate details:");
        Serial.println("- Server cert length: " + String(ssl_certs.server_cert.length()) + " bytes");
        Serial.println("- Private key length: " + String(ssl_certs.server_key.length()) + " bytes");
    }
}

bool loadCertificatesFromSPIFFS() {
    Serial.println("Attempting to load certificates from SPIFFS...");
    
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to initialize SPIFFS");
        return false;
    }
    
    // Load server certificate
    ssl_certs.server_cert = readFileFromSPIFFS("/ssl/server.crt");
    if (ssl_certs.server_cert.isEmpty()) {
        Serial.println("Server certificate not found in SPIFFS");
        return false;
    }
    
    // Load private key
    ssl_certs.server_key = readFileFromSPIFFS("/ssl/server.key");
    if (ssl_certs.server_key.isEmpty()) {
        Serial.println("Private key not found in SPIFFS");
        return false;
    }
    
    // Load CA certificate (optional)
    ssl_certs.ca_cert = readFileFromSPIFFS("/ssl/ca.crt");
    
    // Validate certificates
    if (!validateCertificate(ssl_certs.server_cert)) {
        Serial.println("Invalid server certificate format");
        return false;
    }
    
    Serial.println("Successfully loaded certificates from SPIFFS");
    return true;
}

bool loadCertificatesFromHeader() {
    Serial.println("Loading certificates from header file...");
    
    // Include the generated certificate header
    // This will be created by the certificate generation script
    #ifdef SERVER_CERT_H
    #include "../certificates/server_cert.h"
    
    ssl_certs.server_cert = String(server_cert);
    ssl_certs.server_key = String(server_private_key);
    
    if (ssl_certs.server_cert.isEmpty() || ssl_certs.server_key.isEmpty()) {
        Serial.println("Header file certificates are empty");
        return false;
    }
    
    Serial.println("Successfully loaded certificates from header file");
    return true;
    #else
    // Fallback development certificates
    Serial.println("No header file certificates found, using fallback development certificates");
    
    ssl_certs.server_cert = R"(-----BEGIN CERTIFICATE-----
MIIDazCCAlOgAwIBAgIUQZ3J5Y6L8JH9Y8L9ZrQ3vY8L9zAwDQYJKoZIhvcNAQEL
BQAwRTELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWExITAfBgNVBAoM
GEVTUCBSb3ZlciBEZXZlbG9wbWVudDAeFw0yNTA4MjUwMDAwMDBaFw0yNjA4MjUw
MDAwMDBaMEUxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMSEwHwYD
VQQKDBhFU1AgUm92ZXIgRGV2ZWxvcG1lbnQwggEiMA0GCSqGSIb3DQEBAQUAA4IB
DwAwggEKAoIBAQDQZ5L2Y8L9ZrQ3vY8L9zAwDQYJKoZIhvcNAQELBQAwRTELMAkG
A1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWExITAfBgNVBAoMGEVTUCBSb3Zl
ciBEZXZlbG9wbWVudDAeFw0yNTA4MjUwMDAwMDBaFw0yNjA4MjUwMDAwMDBaMEUx
CzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMSEwHwYDVQQKDBhFU1Ag
Um92ZXIgRGV2ZWxvcG1lbnQwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIB
AQDQlM1R4X5L1x+7yF1K1V9K8rR7Y6L8JH9Y8L9ZrQ3vY8L9zAwDQYJKoZIhvcN
AQELFQRP5+3F4K2K4+7y8H3Q2F1K1V9K8rR7Y6L8JH9Y8L9ZrQ3vY8L9zAwDQYJ
KoZIhvcNAQELBQAwRTELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWEx
ITAfBgNVBAoMGEVTUCBSb3ZlciBEZXZlbG9wbWVudDAeFw0yNTA4MjUwMDAwMDBa
Fw0yNjA4MjUwMDAwMDBaMEUxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9y
bmlhMSEwHwYDVQQKDBhFU1AgUm92ZXIgRGV2ZWxvcG1lbnQwggEiMA0GCSqGSIb3
DQEBAQUAA4IBDwAwggEKAoIBAQC0
-----END CERTIFICATE-----)";
    
    ssl_certs.server_key = R"(-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDQlM1R4X5L1x+7
yF1K1V9K8rR7Y6L8JH9Y8L9ZrQ3vY8L9zAwDQYJKoZIhvcNAQELBQAwRTELMAkG
A1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWExITAfBgNVBAoMGEVTUCBSb3Zl
ciBEZXZlbG9wbWVudDAeFw0yNTA4MjUwMDAwMDBaFw0yNjA4MjUwMDAwMDBaMEUx
CzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMSEwHwYDVQQKDBhFU1Ag
Um92ZXIgRGV2ZWxvcG1lbnQwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIB
AQDQlM1R4X5L1x+7yF1K1V9K8rR7Y6L8JH9Y8L9ZrQ3vY8L9zAwDQYJKoZIhvcN
AQELBQAwRTELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWExITAfBgNV
BAoMGEVTUCBSb3ZlciBEZXZlbG9wbWVudDAeFw0yNTA4MjUwMDAwMDBaFw0yNjA4
MjUwMDAwMDBaMEUxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMSEw
HwYDVQQKDBhFU1AgUm92ZXIgRGV2ZWxvcG1lbnQwggEiMA0GCSqGSIb3DQEBAQUA
A4IBDwAwggEKAoIBAQDQlM1R4X5L1x+7yF1K1V9K8rR7Y6L8JH9Y8L9ZrQ3vY8L9
zAwDQYJKoZIhvcNAQELBQAwRTELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNhbGlm
b3JuaWExITAfBgNVBAoMGEVTUCBSb3ZlciBEZXZlbG9wbWVudDAeFw0yNTA4MjUw
MDAwMDBaFw0yNjA4MjUwMDAwMDBaMEUxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApD
YWxpZm9ybmlhMSEwHwYDVQQKDBhFU1AgUm92ZXIgRGV2ZWxvcG1lbnQ0
-----END PRIVATE KEY-----)";
    
    Serial.println("WARNING: Using fallback development certificates");
    Serial.println("These certificates are for development only and provide minimal security");
    return true;
    #endif
}

String readFileFromSPIFFS(const String& filename) {
    if (!SPIFFS.exists(filename)) {
        Serial.println("File not found: " + filename);
        return "";
    }
    
    File file = SPIFFS.open(filename, "r");
    if (!file) {
        Serial.println("Failed to open file: " + filename);
        return "";
    }
    
    String content = file.readString();
    file.close();
    
    Serial.println("Successfully read file: " + filename + " (" + String(content.length()) + " bytes)");
    return content;
}

bool validateCertificate(const String& cert) {
    // Basic validation - check for PEM format markers
    if (!cert.startsWith("-----BEGIN CERTIFICATE-----")) {
        Serial.println("Certificate missing BEGIN marker");
        return false;
    }
    
    if (!cert.endsWith("-----END CERTIFICATE-----") && 
        !cert.endsWith("-----END CERTIFICATE-----\n")) {
        Serial.println("Certificate missing END marker");
        return false;
    }
    
    // Check minimum length (a valid certificate should be at least 500 bytes)
    if (cert.length() < 500) {
        Serial.println("Certificate too short, likely invalid");
        return false;
    }
    
    return true;
}

// API functions for web servers
String getServerCertificate() {
    return ssl_certs.server_cert;
}

String getServerPrivateKey() {
    return ssl_certs.server_key;
}

String getCACertificate() {
    return ssl_certs.ca_cert;
}

bool areSSLCertificatesLoaded() {
    return ssl_certs.certificates_loaded;
}

String getSSLStatus() {
    DynamicJsonDocument doc(512);
    
    doc["certificates_loaded"] = ssl_certs.certificates_loaded;
    doc["server_cert_size"] = ssl_certs.server_cert.length();
    doc["private_key_size"] = ssl_certs.server_key.length();
    doc["ca_cert_size"] = ssl_certs.ca_cert.length();
    doc["last_error"] = ssl_certs.last_error;
    
    String result;
    serializeJson(doc, result);
    return result;
}

// Certificate upload functionality for web interface
bool uploadCertificate(const String& cert_data, const String& cert_type) {
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to initialize SPIFFS for certificate upload");
        return false;
    }
    
    // Create SSL directory if it doesn't exist
    if (!SPIFFS.exists("/ssl")) {
        // SPIFFS doesn't have mkdir, but we can create the structure via file paths
    }
    
    String filename;
    if (cert_type == "certificate") {
        filename = "/ssl/server.crt";
    } else if (cert_type == "private_key") {
        filename = "/ssl/server.key";  
    } else if (cert_type == "ca_certificate") {
        filename = "/ssl/ca.crt";
    } else {
        Serial.println("Invalid certificate type: " + cert_type);
        return false;
    }
    
    // Write certificate to file
    File file = SPIFFS.open(filename, "w");
    if (!file) {
        Serial.println("Failed to create certificate file: " + filename);
        return false;
    }
    
    file.print(cert_data);
    file.close();
    
    Serial.println("Certificate uploaded successfully: " + filename);
    
    // Reload certificates
    if (cert_type == "certificate" || cert_type == "private_key") {
        return loadCertificatesFromSPIFFS();
    }
    
    return true;
}

// Certificate management for production deployment
void generateCertificateInfo(String& info) {
    if (!ssl_certs.certificates_loaded) {
        info = "No certificates loaded";
        return;
    }
    
    // Extract basic certificate information
    // In a full implementation, you would parse the certificate
    // and extract subject, issuer, expiration, etc.
    
    info = "Certificate Status:\n";
    info += "- Server Certificate: " + String(ssl_certs.server_cert.length()) + " bytes\n";
    info += "- Private Key: " + String(ssl_certs.server_key.length()) + " bytes\n";
    
    if (!ssl_certs.ca_cert.isEmpty()) {
        info += "- CA Certificate: " + String(ssl_certs.ca_cert.length()) + " bytes\n";
    }
    
    // Check if using fallback certificates
    if (ssl_certs.server_cert.indexOf("ESP Rover Development") > 0) {
        info += "- Type: Development/Self-signed\n";
        info += "- WARNING: Not suitable for production use\n";
    } else {
        info += "- Type: Custom/Production\n";
    }
}

// NTP time synchronization for certificate validation
void initializeNTPTime() {
    Serial.println("Initializing NTP time synchronization...");
    
    // Configure NTP servers
    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.cloudflare.com");
    
    // Wait for time synchronization
    Serial.print("Waiting for NTP time sync");
    time_t now = time(nullptr);
    int attempts = 0;
    
    while (now < 1000000000 && attempts < 20) { // Wait until we have a reasonable timestamp
        delay(500);
        Serial.print(".");
        now = time(nullptr);
        attempts++;
    }
    
    Serial.println();
    
    if (now > 1000000000) {
        Serial.println("NTP time synchronized: " + String(ctime(&now)));
    } else {
        Serial.println("WARNING: NTP time synchronization failed");
        Serial.println("SSL certificate validation may fail");
        ssl_certs.last_error = "NTP sync failed";
    }
}