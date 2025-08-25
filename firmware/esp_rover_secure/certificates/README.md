# SSL Certificates for ESP Rover

This directory contains SSL certificates and certificate generation tools for the ESP Rover secure firmware.

## Quick Setup (Development)

### Step 1: Generate Certificates
```bash
# Make the script executable
chmod +x generate_certificates.sh

# Run certificate generation
./generate_certificates.sh
```

### Step 2: Update Firmware
```bash
# Copy the generated header file
cp certificates/server_cert.h include/

# The firmware will automatically include and use these certificates
```

### Step 3: Flash and Test
1. Compile and flash the firmware
2. Connect to rover's WiFi network
3. Navigate to `https://<rover-ip>:8443`
4. Accept the browser security warning (expected for self-signed certificates)

## Certificate Types

### 1. Self-Signed Certificates (Development)
- **Use case**: Development, testing, internal networks
- **Pros**: Quick setup, no external dependencies
- **Cons**: Browser warnings, not trusted by default
- **Security**: Encryption provided, but no identity verification

### 2. Let's Encrypt Certificates (Recommended for Production)
- **Use case**: Internet-accessible rovers with domain names
- **Pros**: Free, automatically trusted by browsers
- **Cons**: Requires domain name and internet access for renewal
- **Security**: Full trust chain validation

### 3. Private CA Certificates (Enterprise)
- **Use case**: Corporate environments with private certificate authority
- **Pros**: Full control, trusted within organization
- **Cons**: Requires CA infrastructure
- **Security**: Full validation with organizational trust

## Files in This Directory

After running the certificate generation script:

```
certificates/
├── server.key          # Private key (KEEP SECURE!)
├── server.crt          # SSL certificate
├── server.csr          # Certificate signing request
├── server_cert.h       # C header file for ESP32
├── ca.crt              # CA certificate for validation
└── README.md           # This file
```

## Security Considerations

### Development (Self-Signed)
✅ **Acceptable for**:
- Local development and testing
- Internal network access
- Proof of concept demonstrations

❌ **NOT suitable for**:
- Public internet deployment
- Production systems
- Systems handling sensitive data

### Production Requirements
For production deployment, you MUST use certificates from a trusted CA:

1. **Domain-based certificates** (recommended)
2. **IP-based certificates** (if domain not possible)
3. **Wildcard certificates** (for multiple subdomains)

## Browser Behavior

### Self-Signed Certificate Warnings
When accessing the rover with self-signed certificates, browsers will show:

**Chrome**: "Your connection is not private" - Click "Advanced" → "Proceed to [IP] (unsafe)"
**Firefox**: "Warning: Potential Security Risk" - Click "Advanced" → "Accept the Risk and Continue"
**Safari**: "This Connection Is Not Private" - Click "Show Details" → "Visit this website"

**This is expected behavior** for self-signed certificates and is safe for development.

## Advanced Configuration

### Custom Certificate Parameters
Edit `ssl_config.cnf` to customize:
- **Organization details** (O, OU, CN fields)
- **Alternative names** (DNS.x, IP.x entries)
- **Certificate validity period**
- **Key usage extensions**

### Network-Specific Setup
Update the `alt_names` section in `ssl_config.cnf` with your network's IP ranges:

```ini
[alt_names]
DNS.1 = your-rover-hostname.local
IP.1 = 192.168.1.xxx    # Your rover's likely IP
IP.2 = 192.168.4.1      # AP mode IP
```

## Production Certificate Setup

### Option A: Let's Encrypt (Free, Automated)
For rovers with internet access and domain names:

```bash
# Install certbot
sudo apt-get install certbot

# Generate certificate (replace with your domain)
certbot certonly --standalone -d your-rover.yourdomain.com

# Certificates will be in /etc/letsencrypt/live/your-rover.yourdomain.com/
```

### Option B: Commercial CA
1. Purchase SSL certificate from trusted CA (DigiCert, GlobalSign, etc.)
2. Generate CSR using the provided script
3. Submit CSR to CA for validation
4. Receive signed certificate
5. Convert to ESP32 format

### Option C: Private CA (Enterprise)
```bash
# Create your own CA (advanced users only)
openssl req -new -x509 -days 3650 -key ca.key -out ca.crt

# Sign rover certificate with your CA
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -out server.crt -days 365
```

## Troubleshooting

### Certificate Loading Errors
- **Check file format**: Ensure PEM format with proper BEGIN/END markers
- **Verify file size**: ESP32 has limited memory for certificates
- **Check line endings**: Use Unix line endings (LF, not CRLF)

### Browser Connection Issues
- **Clear browser cache**: Old certificate data may be cached
- **Check date/time**: Ensure ESP32 has correct time via NTP
- **Verify IP/hostname**: Must match certificate's subject alternative names

### ESP32 Memory Issues
- **Reduce certificate size**: Use shorter validity periods
- **Optimize other memory usage**: Reduce JSON buffer sizes if needed
- **Monitor heap usage**: Check available memory during operation

## Certificate Renewal

### Self-Signed (Manual)
Re-run the generation script annually or when needed:
```bash
./generate_certificates.sh
# Update firmware with new certificates
```

### Let's Encrypt (Automated)
Set up automatic renewal with cron:
```bash
# Add to crontab
0 12 * * * /usr/bin/certbot renew --quiet
```

### Production Planning
- **Set calendar reminders** for certificate expiration
- **Implement monitoring** for certificate validity
- **Plan for zero-downtime renewal** procedures

## Security Best Practices

### Private Key Protection
- **Never commit private keys** to version control
- **Use proper file permissions** (600 for .key files)  
- **Generate new keys** if compromised
- **Use strong entropy sources** for key generation

### Certificate Validation
- **Verify certificate details** before deployment
- **Check expiration dates** regularly
- **Monitor for certificate warnings** in logs
- **Implement proper error handling** for certificate failures

---

**Remember**: SSL certificates provide encryption and identity verification. Even self-signed certificates provide encryption, but browsers can't verify the server's identity without a trusted certificate authority signature.