#!/bin/bash

# ESP Rover SSL Certificate Generation Script
# Creates self-signed certificates for development and testing

echo "ESP Rover - SSL Certificate Generation"
echo "======================================"

# Create certificates directory
mkdir -p certificates
cd certificates

# Generate private key
echo "1. Generating private key..."
openssl genrsa -out server.key 2048

# Generate certificate signing request
echo "2. Generating certificate signing request..."
openssl req -new -key server.key -out server.csr -config ../ssl_config.cnf

# Generate self-signed certificate
echo "3. Generating self-signed certificate..."
openssl x509 -req -days 365 -in server.csr -signkey server.key -out server.crt -extensions v3_req -extfile ../ssl_config.cnf

# Convert certificate to C header format for ESP32
echo "4. Converting certificate to C header format..."
cat > server_cert.h << 'EOF'
#ifndef SERVER_CERT_H
#define SERVER_CERT_H

const char* server_cert = R"(
EOF

cat server.crt >> server_cert.h

cat >> server_cert.h << 'EOF'
)";

const char* server_private_key = R"(
EOF

cat server.key >> server_cert.h

cat >> server_cert.h << 'EOF'
)";

#endif // SERVER_CERT_H
EOF

# Generate CA certificate for client validation (optional)
echo "5. Generating CA certificate for validation..."
openssl req -new -x509 -key server.key -out ca.crt -days 365 -config ../ssl_config.cnf -extensions v3_ca

# Display certificate information
echo "6. Certificate Information:"
echo "=========================="
openssl x509 -in server.crt -text -noout | grep -E "(Subject:|DNS:|IP Address:|Not Before:|Not After:)"

echo ""
echo "Certificate Generation Complete!"
echo "==============================="
echo "Files created:"
echo "- server.key (private key)"
echo "- server.crt (certificate)"
echo "- server.csr (certificate signing request)"
echo "- server_cert.h (C header for ESP32)"
echo "- ca.crt (CA certificate for validation)"
echo ""
echo "IMPORTANT SECURITY NOTES:"
echo "- These are self-signed certificates for DEVELOPMENT only"
echo "- Browsers will show security warnings (expected)"
echo "- For PRODUCTION, use certificates from a trusted CA"
echo "- Keep server.key file secure and never commit to version control"
echo ""
echo "Next steps:"
echo "1. Copy server_cert.h to your ESP32 project"
echo "2. Include in web_servers.cpp"
echo "3. Flash firmware to ESP32"
echo "4. Accept browser security warnings when accessing HTTPS"

cd ..